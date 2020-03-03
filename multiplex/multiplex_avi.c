/*
 *  multiplex_avi.c -- multiplex frames in an AVI file using avilib.
 *  (C) 2005-2010 Francesco Romani <fromani at gmail dot com>
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

#include "libtcmodule/tcmodule-plugin.h"

#include "avilib/avilib.h"

#define MOD_NAME    "multiplex_avi.so"
#define MOD_VERSION "v0.1.0 (2009-02-07)"
#define MOD_CAP     "create an AVI stream using avilib"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_MULTIPLEX|TC_MODULE_FEATURE_VIDEO|TC_MODULE_FEATURE_AUDIO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE


/* default FourCC to use if given one isn't known or if it's just absent */
#define DEFAULT_FOURCC "RGB"

static const char avi_help[] = ""
    "Overview:\n"
    "    this module create an AVI stream using avilib.\n"
    "    AVI streams produced by this module can have a\n"
    "    maximum of one audio and video track.\n"
    "    You can add more tracks with further processing.\n"
    "Options:\n"
    "    help    produce module overview and options explanations\n";

typedef struct {
    avi_t *avifile;
    int force_kf; /* boolean flag */
    vob_t *vob;
    int arate;
    int abitrate;
    const char *fcc;
} AVIPrivateData;

static int avi_inspect(TCModuleInstance *self,
                       const char *param, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");

    if (optstr_lookup(param, "help")) {
        *value = avi_help;
    }

    return TC_OK;
}

static int avi_configure(TCModuleInstance *self,
                          const char *options,
                          TCJob *vob,
                          TCModuleExtraData *xdata[])
{
    AVIPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");
    TC_MODULE_SELF_CHECK(vob,  "configure"); /* hackish? */

    pd = self->userdata;
    pd->vob      = vob;
    pd->arate    = (vob->mp3frequency != 0)
                    ?vob->mp3frequency :vob->a_rate;
    pd->abitrate = (vob->ex_a_codec == TC_CODEC_PCM)
                    ?(vob->a_rate*4)/1000*8 :vob->mp3bitrate;
    pd->fcc      = tc_codec_fourcc(vob->ex_v_codec);
    if (pd->fcc == NULL) {
        pd->fcc = DEFAULT_FOURCC;
    }
    if (verbose >= TC_DEBUG) {
        tc_log_info(MOD_NAME, "AVI FourCC: '%s'", pd->fcc);
    }

    if (vob->ex_v_codec == TC_CODEC_RGB24
     || vob->ex_v_codec == TC_CODEC_YUV420P
     || vob->ex_v_codec == TC_CODEC_YUV422P) {
        pd->force_kf = TC_TRUE;
    } else {
        pd->force_kf = TC_FALSE;
    }
    return TC_OK;
}

static int avi_open(TCModuleInstance *self,
                    const char *filename,
                    TCModuleExtraData *xdata[])
{
    AVIPrivateData *pd = NULL;
    vob_t *vob = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;
    vob = pd->vob;

    pd->avifile = AVI_open_output_file(vob->video_out_file);
    if(!pd->avifile) {
        tc_log_error(MOD_NAME, "avilib error: %s", AVI_strerror());
        return TC_ERROR;
    }

	AVI_set_video(pd->avifile, vob->ex_v_width, vob->ex_v_height,
	              vob->ex_fps, pd->fcc);

    AVI_set_audio_track(pd->avifile, vob->a_track);
    AVI_set_audio(pd->avifile, vob->dm_chan, pd->arate, vob->dm_bits,
                  vob->ex_a_codec, pd->abitrate);
    AVI_set_audio_vbr(pd->avifile, vob->a_vbr);

    return TC_OK;
}

static int avi_stop(TCModuleInstance *self)
{
    /* do nothing succesfully */
    TC_MODULE_SELF_CHECK(self, "stop");
    return TC_OK;
}


static int avi_close(TCModuleInstance *self)
{
    AVIPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "close");

    pd = self->userdata;

    if (pd->avifile != NULL) {
        AVI_close(pd->avifile);
        pd->avifile = NULL;
    }

    return TC_OK;
}

static int avi_write_video(TCModuleInstance *self,  TCFrameVideo *frame)
{
    uint32_t size_before, size_after;
    int ret, key = 0;

    AVIPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "write_video");

    pd = self->userdata;
    size_before = AVI_bytes_written(pd->avifile);

    key = ((frame->attributes & TC_FRAME_IS_KEYFRAME)
           || pd->force_kf) ?1 :0;

    ret = AVI_write_frame(pd->avifile, (const char*)frame->video_buf,
                          frame->video_len, key);

    if(ret < 0) {
        tc_log_error(MOD_NAME, "avilib error writing video: %s",
                     AVI_strerror());
        return TC_ERROR;
    }

    size_after = AVI_bytes_written(pd->avifile);
    return (size_after - size_before);
}


static int avi_write_audio(TCModuleInstance *self,  TCFrameAudio *frame)
{
    uint32_t size_before, size_after;
    int ret;

    AVIPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "write_audio");

    pd = self->userdata;
    size_before = AVI_bytes_written(pd->avifile);

 	ret = AVI_write_audio(pd->avifile, (const char*)frame->audio_buf,
                          frame->audio_len);
    if (ret < 0) {
        tc_log_error(MOD_NAME, "avilib error writing audio: %s",
                     AVI_strerror());
	    return TC_ERROR;
	}

    size_after = AVI_bytes_written(pd->avifile);
    return (size_after - size_before);
}

static int avi_init(TCModuleInstance *self, uint32_t features)
{
    AVIPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    pd = tc_malloc(sizeof(AVIPrivateData));
    if (!pd) {
        return TC_ERROR;
    }

    pd->avifile = NULL;
    pd->force_kf = TC_FALSE;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
        if (verbose >= TC_DEBUG) {
            tc_log_info(MOD_NAME, "max AVI-file size limit = %lu bytes",
                                  (unsigned long)AVI_max_size());
        }
    }

    self->userdata = pd;
    return TC_OK;
}

static int avi_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    tc_free(self->userdata);
    self->userdata = NULL;

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID avi_codecs_audio_in[] = {
    TC_CODEC_PCM, TC_CODEC_AC3, TC_CODEC_MP2, TC_CODEC_MP3,
    TC_CODEC_AAC, /* FIXME: that means asking for troubles */
    TC_CODEC_ERROR
};
static const TCCodecID avi_codecs_video_in[] = {
    TC_CODEC_YUV420P, TC_CODEC_DV,
    TC_CODEC_DIVX3, TC_CODEC_DIVX4, TC_CODEC_DIVX5, TC_CODEC_XVID,
    TC_CODEC_H264, /* FIXME: that means asking for troubles */
    TC_CODEC_MPEG4VIDEO, TC_CODEC_MPEG1VIDEO, TC_CODEC_MJPEG,
    TC_CODEC_LZO1, TC_CODEC_LZO2, TC_CODEC_RGB24,
    TC_CODEC_ERROR
};
static const TCFormatID avi_formats_out[] = { TC_FORMAT_AVI, TC_FORMAT_ERROR };
/* a multiplexor is at the end of pipeline */
TC_MODULE_MPLEX_FORMATS_CODECS(avi);

TC_MODULE_INFO(avi);

static const TCModuleClass avi_class = {
    TC_MODULE_CLASS_HEAD(avi),

    .init         = avi_init,
    .fini         = avi_fini,
    .configure    = avi_configure,
    .stop         = avi_stop,
    .inspect      = avi_inspect,

    .open         = avi_open,
    .close        = avi_close,
    .write_audio  = avi_write_audio,
    .write_video  = avi_write_video,
};

TC_MODULE_ENTRY_POINT(avi)

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

