/*
 *  multiplex_lavf.c -- multiplex A/V frames in a custom container
 *                      using libavformat.
 *  (C) 2008-2010 Francesco Romani <fromani at gmail dot com>
 *
 * This file is part of transcode, a video stream processing tool.
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "src/transcode.h"
#include "libtcutil/optstr.h"
#include "libtc/ratiocodes.h"

#include "libtcmodule/tcmodule-plugin.h"

#include "libtcext/tc_avcodec.h"

#define MOD_NAME    "multiplex_lavf.so"
#define MOD_VERSION "v0.1.0 (2009-02-09)"
#define MOD_CAP     "libavformat based multiplexor (" LIBAVFORMAT_IDENT ")"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_MULTIPLEX|TC_MODULE_FEATURE_VIDEO|TC_MODULE_FEATURE_AUDIO


#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

static const char tc_lavf_help[] = ""
    "Overview:\n"
    "Options:\n"
    "    help    produce module overview and options explanations\n";

/* until 0.1.x at least */
#define LAVF_TEST 1
#define DEBUG     1

/*************************************************************************/

static const TCCodecID tc_lavf_codecs_audio_in[] = {
    TC_CODEC_PCM, TC_CODEC_LPCM,
    TC_CODEC_AC3, TC_CODEC_DTS, TC_CODEC_MP2,
    TC_CODEC_AAC,
#ifdef LAVF_TEST    
    TC_CODEC_MP3,
#endif
    TC_CODEC_ERROR
};
static const TCCodecID tc_lavf_codecs_video_in[] = {
    TC_CODEC_MPEG2VIDEO, TC_CODEC_MPEG4VIDEO,
    TC_CODEC_H264, TC_CODEC_SVQ1, TC_CODEC_SVQ3,
    TC_CODEC_ERROR
};
static const TCFormatID tc_lavf_formats_out[] = {
    TC_FORMAT_MPEG_PS,
    TC_FORMAT_MPEG_TS,
    TC_FORMAT_MPEG_MP4,
    TC_FORMAT_MOV,
#ifdef LAVF_TEST
    TC_FORMAT_AVI,
#endif
    TC_FORMAT_ERROR
};

/*
 * second stage compatibility check
 * FIXME: doc me up
 */
#define MAX_FMT_CODECS      12
struct fmt_desc {
    TCFormatID  format;
    const char  *lavf_name;
    int         lavf_flags;
    TCCodecID   codecs_vid[MAX_FMT_CODECS];
    TCCodecID   codecs_aud[MAX_FMT_CODECS];
};
static const struct fmt_desc fmt_descs[] = {
    {
        .format     = TC_FORMAT_MPEG_PS,
        .lavf_name  = "vob",
        .lavf_flags = 0,
        .codecs_aud = {
            TC_CODEC_PCM, TC_CODEC_LPCM, TC_CODEC_AC3, TC_CODEC_DTS,
            TC_CODEC_MP2,
            TC_CODEC_ERROR
         },
        .codecs_vid = {
            TC_CODEC_MPEG2VIDEO,
            TC_CODEC_ERROR
        }
    },
    {
        .format     = TC_FORMAT_MPEG_TS,
        .lavf_name  = "mpegts",
        .lavf_flags = 0,
        .codecs_aud = {
            TC_CODEC_PCM, TC_CODEC_LPCM, TC_CODEC_AC3, TC_CODEC_DTS,
            TC_CODEC_ERROR
        },
        .codecs_vid = {
            TC_CODEC_MP2, TC_CODEC_MPEG2VIDEO,
            TC_CODEC_ERROR
        }
    },
    {
        .format     = TC_FORMAT_MOV,
        .lavf_name  = "mov",
        .lavf_flags = CODEC_FLAG_GLOBAL_HEADER,
        .codecs_aud = {
            TC_CODEC_AAC,
            TC_CODEC_ERROR
        },
        .codecs_vid = {
            TC_CODEC_MPEG4VIDEO, TC_CODEC_H264, TC_CODEC_SVQ1, TC_CODEC_SVQ3,
            TC_CODEC_ERROR
        }
    },
    {
        .format     = TC_FORMAT_MPEG_MP4,
        .lavf_name  = "mp4",
        .lavf_flags = CODEC_FLAG_GLOBAL_HEADER,
        .codecs_aud = {
            TC_CODEC_AAC,
            TC_CODEC_ERROR
        },
        .codecs_vid = {
            TC_CODEC_MPEG4VIDEO,
            TC_CODEC_ERROR
        }
    },
#ifdef LAVF_TEST
    {
        .format     = TC_FORMAT_AVI,
        .lavf_name  = "avi",
        .lavf_flags = 0,
        .codecs_aud = {
            TC_CODEC_MP3,
            TC_CODEC_ERROR,
        },
        .codecs_vid = {
            TC_CODEC_MPEG4VIDEO,
            TC_CODEC_ERROR,
        }
    },
#endif
    {
        .format     = TC_FORMAT_ERROR,
        .lavf_name  = NULL,
        .lavf_flags = 0,
        .codecs_aud = {
            TC_CODEC_ERROR
        },
        .codecs_aud = {
            TC_CODEC_ERROR
        }
    }
};


/*************************************************************************/

typedef struct tclavfprivatedata_ TCLavfPrivateData;
struct tclavfprivatedata_ {
    TCFormatID      fmt_id;
    int             nstreams;

    AVOutputFormat  *mux_format;
    AVFormatContext *mux_context;

    AVStream        *astream;
    AVStream        *vstream;

    uint32_t        aframes;
    uint32_t        vframes;

    double          audio_pts;
    double          video_pts;

    char            fmt_name[TC_BUF_MIN];
};

/*************************************************************************/

typedef int (*cmpfn)(const struct fmt_desc *des, const void *data);

static int by_name(const struct fmt_desc *des, const void *data)
{
    return (strcmp(des->lavf_name, data) == 0);
}

static int by_id(const struct fmt_desc *des, const void *data)
{
    TCFormatID id = (*(TCFormatID*)data);
    return (des->format == id);
}

static const struct fmt_desc *find_fmt_desc(cmpfn cmp, const void *data)
{
    int i = 0;
    const struct fmt_desc *cur = NULL;
    for (i = 0; fmt_descs[i].format != TC_FORMAT_ERROR; i++) {
        int match = cmp(&(fmt_descs[i]), data);
        if (match) {
            cur = &(fmt_descs[i]);
            break;
        }
    }
    return cur;
}
/*
 * second stage compatibility check
 * FIXME: doc me up
 */
static int tc_lavf_is_codec_compatible(TCFormatID format, TCCodecID codec,
                                       int is_video) /* FIXME */
{
    int j = 0, ret = 0;
    const struct fmt_desc *des = find_fmt_desc(by_id, &format);
    if (des) {
        const TCCodecID *codecs = (is_video) ?(des->codecs_vid)
                                             :(des->codecs_aud);
        for (j = 0; !ret && codecs[j] != TC_CODEC_ERROR; j++) {
            if (codecs[j] == codec) {
                ret = 1;
            }
        }
    }
    return ret;
}

static const char *tc_format_to_lavf(TCFormatID format)
{
    const struct fmt_desc *des = find_fmt_desc(by_id, &format);
    return (des) ?(des->lavf_name) :NULL;
}

static TCFormatID tc_format_from_lavf(const char *name)
{
    const struct fmt_desc *des = find_fmt_desc(by_name, name);
    return (des) ?(des->format) :TC_FORMAT_ERROR;
}


/*
 * tc_lavf_list_formats:
 *      (NOT Thread safe. But do anybody cares since
 *      transcode encoder(.c) is single-threaded today
 *      and in any foreseable future?)
 *      return a buffer listing all supported formats with
 *      respective name and short description.
 *
 * Parameters:
 *      None
 * Return Value:
 *      Read-only pointer to a char buffer holding the
 *      description data. Buffer is guaranted valid
 *      at least until next call of this function.
 *      You NEVER need to tc_free() the pointer.
 */
static const char* tc_lavf_list_formats(void)
{
    /* XXX: I feel a bad taste */
    static char buf[TC_BUF_MAX];
    static int ready = TC_FALSE;

    if (!ready) {
        size_t used = 0;
        int i = 0;

        for (i = 0; tc_lavf_formats_out[i] != TC_FORMAT_ERROR; i++) {
            char sbuf[TC_BUF_MIN];
            int slen = 0;

            slen = tc_format_description(tc_lavf_formats_out[i],
                                         sbuf, sizeof(sbuf) - 1);
            if (slen < 0) {
                tc_log_error(MOD_NAME, "format description too long! "
                                       "This should'nt happen. "
                                       "Please file a bug report.");
                strlcpy(buf, "internal error", sizeof(buf));
            } else if (used + slen + 1 > sizeof(buf)) {
                tc_log_error(MOD_NAME, "too much formats! "
                                       "This should'nt happen. "
                                       "Please file a bug report.");
                strlcpy(buf, "internal error", sizeof(buf));
            } else {
                sbuf[slen] = '\n';
                strlcpy(buf + used, sbuf, sizeof(buf) - used);
                used += slen + 1; /* for the trailing newline */
            }
        }
        buf[used] = '\0';
        ready = TC_TRUE;
    }
    return buf;
}

static int tc_lavf_are_codec_compatible(TCLavfPrivateData *pd, vob_t *vob)
{
    int ret;

    ret = tc_lavf_is_codec_compatible(pd->fmt_id, vob->ex_v_codec, 1);
    if (!ret) {
        tc_log_error(MOD_NAME, "requested video codec %s is incompatible"
                               " with format %s",
                               tc_codec_to_string(vob->ex_v_codec),
                               tc_format_to_string(pd->fmt_id));
        return TC_ERROR;
    }
    ret = tc_lavf_is_codec_compatible(pd->fmt_id, vob->ex_a_codec, 0);
    if (!ret) {
        tc_log_error(MOD_NAME, "requested audio codec %s is incompatible"
                               " with format %s",
                               tc_codec_to_string(vob->ex_a_codec),
                               tc_format_to_string(pd->fmt_id));
        return TC_ERROR;
    }
    return TC_OK;
}

static int tc_lavf_init_fmt_from_user(TCLavfPrivateData *pd,
                                      const char *options)
{
    const char *fmt_tag = NULL;
    char fmt_name[16] = { '\0' }; /* FIXME */
    int has_fmt = optstr_get(options, "format", "%15s", fmt_name);
    if (has_fmt == 1) {
        pd->fmt_id = tc_format_from_string(fmt_name);

        if (pd->fmt_id == TC_FORMAT_ERROR) {
            tc_log_error(MOD_NAME, "unknown format: %s", fmt_name);
            return TC_ERROR;
        }

        fmt_tag = tc_format_to_lavf(pd->fmt_id);
        if (!fmt_tag) {
            tc_log_error(MOD_NAME, "unsupported format: %s", fmt_name);
            return TC_ERROR;
        }

        pd->mux_format = guess_format(fmt_tag, NULL, NULL);
        if (!pd->mux_format) {
            tc_log_error(MOD_NAME, "format unsupported by libavformat: %s", fmt_name);
            return TC_ERROR;
        }
    }
    return TC_OK;
}

static int tc_lavf_init_fmt_from_filename(TCLavfPrivateData *pd,
                                          const char *filename)
{
    const char *fname = strrchr(filename, '/'); /* strip path component */
    AVOutputFormat *fmt = NULL;
    if (verbose) {
        tc_log_info(MOD_NAME, "no format specified,"
                              " detecting from filename...");
    }

    fname = (fname) ?fname :filename; /* fallback to given name */
    fmt = guess_format(NULL, fname, NULL);
    if (!fmt) {
        tc_log_error(MOD_NAME, "unable to detect format");
        return TC_ERROR;
    }

    pd->fmt_id = tc_format_from_lavf(fmt->name);
    if (pd->fmt_id == TC_FORMAT_ERROR) {
        tc_log_error(MOD_NAME, "detected an unsupported format: %s", fmt->name);
        return TC_ERROR;
    }
    pd->mux_format = fmt;

    if (verbose) {
        tc_log_info(MOD_NAME, "using container format '%s'", fmt->name);
    }
    return TC_OK;
}

static int tc_lavf_init_audio_stream(TCLavfPrivateData *pd,
                                     vob_t *vob,
                                     int flags)
{
    int ret = TC_ERROR;

    pd->astream = av_new_stream(pd->mux_context, pd->nstreams);
    if (pd->astream) {
        AVCodecContext *c = pd->astream->codec;

        c->codec_id     = pd->mux_format->audio_codec;
        c->codec_type   = CODEC_TYPE_AUDIO;
        c->bit_rate     = vob->mp3bitrate * 1000;
        c->sample_rate  = vob->mp3frequency ?vob->mp3frequency :vob->a_rate;
        c->channels     = vob->dm_chan;
        c->frame_size   = vob->ex_a_size; // XXX XXX XXX
        c->block_align  = 0; // XXX XXX XXX

        pd->nstreams++;
        ret = TC_OK;
    }
    return ret;
}

/* add a video output stream */
static int tc_lavf_init_video_stream(TCLavfPrivateData *pd,
                                     vob_t *vob,
                                     int flags)
{
    int ret = TC_ERROR;
    int n, d;

    ret = tc_frc_code_to_ratio(vob->ex_frc, &d, &n); /* watch out here */
    if (ret == TC_NULL_MATCH || n != 1000) {
        tc_log_error(MOD_NAME, "unrecognized/unsupported"
                               " output frame rate!");
        return TC_ERROR;
    }

    pd->vstream = av_new_stream(pd->mux_context, pd->nstreams);
    if (pd->vstream) {
        AVCodecContext *c = pd->vstream->codec;
        c->codec_id      = pd->mux_format->video_codec;
        c->codec_type    = CODEC_TYPE_VIDEO;
        c->width         = vob->ex_v_width;
        c->height        = vob->ex_v_height;
        c->bit_rate      = vob->divxbitrate * 1000;
        /*
         * XXX:
         * YES, that's ugly, but libavformat wants
         * them like that. Apparently.
         */
        c->time_base.den = d / 1000;
        c->time_base.num = n / 1000;
        /* time base: this is the fundamental unit of time (in seconds) in terms
           of which frame timestamps are represented. for fixed-fps content,
           timebase should be 1/framerate and timestamp increments should be
           identically 1. */
        c->pix_fmt       = PIX_FMT_YUV420P; // XXX XXX XXX
        c->max_b_frames  = 1;               // XXX XXX XXX

        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_GOP) {
            c->gop_size = vob->divxkeyframes;
        } else {
            if (vob->ex_v_codec == TC_CODEC_MPEG1VIDEO
             || vob->ex_v_codec == TC_CODEC_MPEG2VIDEO) {
                c->gop_size = 15; /* conservative default for mpeg1/2 svcd/dvd */
            } else {
                c->gop_size = 250; /* reasonable default for mpeg4 (and others) */
            }
        }

        c->flags        |= flags;

        pd->nstreams++;
        ret = TC_OK;
    }
    return ret;
}

static int tc_lavf_open_file(TCLavfPrivateData *pd, const char *filename)
{
    int ret = TC_OK;

    strlcpy(pd->mux_context->filename, filename, sizeof(pd->mux_context->filename));
    /* open the output file, if needed */
    if (!(pd->mux_format->flags & AVFMT_NOFILE)) {
        ret = url_fopen(&(pd->mux_context->pb), filename, URL_WRONLY);
        if (ret < 0) {
            tc_log_error(MOD_NAME, "unable to open output file '%s'",
                                   filename);
            ret = TC_ERROR;
        } else {
            ret = TC_OK;
        }
    }
    return ret;
}

static int tc_lavf_write(AVFormatContext *ctx, AVPacket *pkt,
                         uint32_t *counter, const char *tag)
{
    int ret = TC_OK;
    int err = av_write_frame(ctx, pkt);
    if (!err) {
        (*counter)++;
    } else {
        tc_log_error(MOD_NAME,
                     "error while writing %s frame (err=%i)",
                     tag, err);
        ret = TC_ERROR;
    }
    return ret;
}

/*************************************************************************/

static int tc_lavf_write_video(TCModuleInstance *self,
                               TCFrameVideo *frame)
{
//    AVCodecContext *c = pd->vstream->codec;
    AVPacket pkt;
    TCLavfPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "write_video");

    pd = self->userdata;
 
    av_init_packet(&pkt);

    pkt.stream_index = pd->vstream->index;
    pkt.data         = frame->video_buf;
    pkt.size         = frame->video_len;
    pkt.pts          = pd->video_pts;
//    pkt.pts          = av_rescale_q(c->coded_frame->pts,
//                                     c->time_base, st->time_base);
    if (frame->attributes & TC_FRAME_IS_KEYFRAME) {
        pkt.flags |= PKT_FLAG_KEY;
    }

    return tc_lavf_write(pd->mux_context, &pkt, &(pd->vframes), "video");
}

static int tc_lavf_write_audio(TCModuleInstance *self,
                                   TCFrameAudio *frame)
{
//    AVCodecContext *c = pd->astream->codec;
    AVPacket pkt;
    TCLavfPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "write_audio");

    pd = self->userdata;

    av_init_packet(&pkt);

    pkt.stream_index = pd->astream->index;
    pkt.data         = frame->audio_buf;
    pkt.size         = frame->audio_size;
    pkt.pts          = pd->audio_pts;
//    pkt.pts= av_rescale_q(c->coded_frame->pts,
//                          c->time_base, st->time_base);
    pkt.flags       |= PKT_FLAG_KEY; /* always */

    return tc_lavf_write(pd->mux_context, &pkt, &(pd->aframes), "audio");
}

static void tc_lavf_init_defaults(TCLavfPrivateData *pd)
{

    /* some sane defaults */
    pd->fmt_id      = TC_FORMAT_UNKNOWN;
    pd->mux_format  = NULL;
    pd->mux_context = NULL;
    pd->vstream     = NULL;
    pd->astream     = NULL;
    pd->nstreams    = 0;
    pd->aframes     = 0;
    pd->vframes     = 0;
    pd->audio_pts   = 0.0;
    pd->video_pts   = 0.0;
}

/*************************************************************************/

static int tc_lavf_init(TCModuleInstance *self, uint32_t features)
{
    TCLavfPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    /* FIXME: move into core? */
    TC_INIT_LIBAVFORMAT;

    pd = tc_malloc(sizeof(TCLavfPrivateData));
    if (pd == NULL) {
        tc_log_error(MOD_NAME, "unable to allocate private data");
        return TC_ERROR;
    }

    /* enforce NULL-ness of dangerous (segfault-friendly) stuff */

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    self->userdata = pd;

    return TC_OK;
}

TC_MODULE_GENERIC_FINI(tc_lavf)

static int tc_lavf_inspect(TCModuleInstance *self,
                           const char *param, const char **value)
{
    TCLavfPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");

    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = tc_lavf_help;
    }

    if (optstr_lookup(param, "format")) {
        *value = tc_format_to_string(pd->fmt_id);
    }

    if (optstr_lookup(param, "list")) {
        *value = tc_lavf_list_formats();
    }

    return TC_OK;
}

static int tc_lavf_close(TCModuleInstance *self)
{
    TCLavfPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "close");

    pd = self->userdata;

    if (pd->mux_context) {
        int i = 0;

        if (pd->vstream || pd->astream) {
            if (pd->aframes > 0 || pd->vframes > 0) {
                av_write_trailer(pd->mux_context);
            }

            /* free the streams */
            for(i = 0; i < pd->mux_context->nb_streams; i++) {
                av_freep(&pd->mux_context->streams[i]->codec);
                av_freep(&pd->mux_context->streams[i]);
            }
        }

        if (pd->mux_context->pb && !(pd->mux_format->flags & AVFMT_NOFILE)) {
            /* close the output file */
            url_fclose(pd->mux_context->pb);
        }
    }

    return TC_OK;
}

static int tc_lavf_stop(TCModuleInstance *self)
{
    TCLavfPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    if (pd->mux_context) {
        av_free(pd->mux_context);
        pd->mux_context = NULL;
    }

    return TC_OK;
}

#define ABORT_IF_FAILED(RET, SELF) do { \
    if ((RET) != TC_OK) {               \
        tc_lavf_stop((SELF));           \
        return (RET);                   \
    }                                   \
} while (0)

static int tc_lavf_configure(TCModuleInstance *self,
                             const char *options,
                             TCJob *vob,
                             TCModuleExtraData *xdata[])
{
    int ret = 0;
    TCLavfPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    /* some sane defaults */
    tc_lavf_init_defaults(pd);

    pd->mux_context = av_alloc_format_context();
    if (!pd->mux_context) {
        tc_log_error(MOD_NAME, "unable to allocate muxer context");
        return TC_ERROR;
    }

    if (options) {
        ret = tc_lavf_init_fmt_from_user(pd, options);
        ABORT_IF_FAILED(ret, self);
    }
    /* format no explicitely selected (no options or no option gived) */
    if (pd->fmt_id == TC_FORMAT_UNKNOWN) {
        ret = tc_lavf_init_fmt_from_filename(pd, vob->video_out_file);
        ABORT_IF_FAILED(ret, self);
    }

    ret = tc_lavf_are_codec_compatible(pd, vob);
    ABORT_IF_FAILED(ret, self);
    return TC_OK;
}

static int tc_lavf_open(TCModuleInstance *self, const char *filename,
                        TCModuleExtraData *xdata[])
{
    int ret = 0;
    const struct fmt_desc *des = NULL;
    TCLavfPrivateData *pd = NULL;
    vob_t *vob = tc_get_vob();

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;
 
    /* now we have a valid format */
    des = find_fmt_desc(by_id, &pd->fmt_id); // ugly;
    pd->mux_context->oformat = pd->mux_format;

    ret = tc_lavf_open_file(pd, filename);
    ABORT_IF_FAILED(ret, self);
    ret = tc_lavf_init_video_stream(pd, vob, des->lavf_flags);
    ABORT_IF_FAILED(ret, self);
    ret = tc_lavf_init_audio_stream(pd, vob, des->lavf_flags);
    ABORT_IF_FAILED(ret, self);

    // FIXME: we need this?
    ret = av_set_parameters(pd->mux_context, NULL);
    if (ret < 0) {
        tc_log_error(MOD_NAME, "unable to set output format parameters");
        tc_lavf_stop(self);
        return TC_ERROR;
    }

    if (verbose >= TC_DEBUG) {
        dump_format(pd->mux_context, 0, vob->video_out_file, 1);
    }

    /* write the stream header, if any */
    av_write_header(pd->mux_context);

    return TC_OK;
}

#if 0
static double tc_lavf_get_stream_pts(const AVStream *st)
{
    if (!st) {
        return 0.0;
    }
    return (double)st->pts.val * st->time_base.num / st->time_base.den;
}
#endif

/*************************************************************************/

TC_MODULE_MPLEX_FORMATS_CODECS(tc_lavf);

TC_MODULE_INFO(tc_lavf);

static const TCModuleClass tc_lavf_class = {
    TC_MODULE_CLASS_HEAD(tc_lavf),

    .init         = tc_lavf_init,
    .fini         = tc_lavf_fini,
    .configure    = tc_lavf_configure,
    .stop         = tc_lavf_stop,
    .inspect      = tc_lavf_inspect,

    .open         = tc_lavf_open,
    .close        = tc_lavf_close,
    .write_audio  = tc_lavf_write_audio,
    .write_video  = tc_lavf_write_video,
};

TC_MODULE_ENTRY_POINT(tc_lavf);

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */

