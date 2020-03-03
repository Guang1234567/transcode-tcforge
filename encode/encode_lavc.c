/*
 * encode_lavc.c -- encode A/V frames using libavcodec.
 * (C) 2007-2010 Francesco Romani <fromani at gmail dot com>
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
#include "src/framebuffer.h"
#include "src/filter.h"

#include "aclib/imgconvert.h"

#include "libtcutil/optstr.h"
#include "libtcutil/cfgfile.h"
#include "libtc/ratiocodes.h"
#include "libtc/tcframes.h"

#include "libtcmodule/tcmodule-plugin.h"

#include "libtcext/tc_avcodec.h"

#include <math.h>

#define MOD_NAME    "encode_lavc.so"
#define MOD_VERSION "v0.1.1 (2009-02-07)"
#define MOD_CAP     "libavcodec based encoder (" LIBAVCODEC_IDENT ")"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_ENCODE|TC_MODULE_FEATURE_VIDEO|TC_MODULE_FEATURE_AUDIO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

#define LAVC_CONFIG_FILE "lavc.cfg"
#define PSNR_LOG_FILE    "psnr.log"

static const char tc_lavc_help[] = ""
    "Overview:\n"
    "    this module uses libavcodec to encode given raw frames in\n"
    "    an huge variety of compressed formats, both audio and video.\n"
    "Options:\n"
    "    help     produce module overview and options explanations\n"
    "    list     log out a list of supported A/V codecs\n";



typedef struct tclavcconfigdata_ TCLavcConfigData;
struct tclavcconfigdata_ {
    int thread_count;

    /* 
     * following options can't be sect directly on AVCodeContext,
     * we need some buffering and translation.
     */
    int vrate_tolerance;
    int rc_min_rate;
    int rc_max_rate;
    int rc_buffer_size;
    int lmin;
    int lmax;
    int me_method;

    /* same as above for flags */
    struct {
        uint32_t mv0;
        uint32_t cbp;
        uint32_t qpel;
        uint32_t alt;
        uint32_t vdpart;
        uint32_t naq;
        uint32_t ilme;
        uint32_t ildct;
        uint32_t aic;
        uint32_t aiv;
        uint32_t umv;
        uint32_t psnr;
        uint32_t trell;
        uint32_t gray;
        uint32_t v4mv;
        uint32_t closedgop;
    } flags;

    /* 
     * special flags: flags that triggers more than a setting
     * FIXME: not yet supported
     */
    int turbo_setup;
};

typedef struct tclavcprivatedata_ TCLavcPrivateData;

/* this is to reduce if()s in out encode_video() */
typedef void (*PreEncodeVideoFn)(struct tclavcprivatedata_ *pd,
                                 TCFrameVideo *vframe);

struct tclavcprivatedata_ {
    /* shared section *****************************************************/
    TCLavcConfigData confdata;
    
    int flush_flag;

    /* video support ******************************************************/
    int vcodec_id;
    TCCodecID tc_pix_fmt;

    AVFrame ff_venc_frame;
    AVCodecContext ff_vcontext;

    AVCodec *ff_vcodec;

    struct {
        int active;
        int top_first;
    } interlacing;

    uint16_t inter_matrix[TC_MATRIX_SIZE];
    uint16_t intra_matrix[TC_MATRIX_SIZE];

    FILE *stats_file;
    FILE *psnr_file;

    TCFrameVideo *vframe_buf;
    /* for colorspace conversions in prepare functions */
    PreEncodeVideoFn pre_encode_video;

    /* audio support *****************************************************/
    int acodec_id;
    
    AVCodecContext ff_acontext;
    AVCodec *ff_acodec;

    TCFrameAudio *aframe_buf;
    
    int audio_buf_pos; /* position in the buffer (for remaining data) */
    
    int audio_bps; /* bytes per sample */
    int audio_bpf; /* bytes per frame */
};

/*************************************************************************/

static const TCCodecID tc_lavc_codecs_video_in[] = {
    TC_CODEC_YUV420P, TC_CODEC_YUV422P, TC_CODEC_RGB24,
    TC_CODEC_ERROR
};

static const TCCodecID tc_lavc_codecs_audio_in[] = {
    TC_CODEC_PCM,
    TC_CODEC_ERROR
};


#define FF_VCODEC_ID(pd) (tc_lavc_int_video_codecs[(pd)->vcodec_id])
#define TC_VCODEC_ID(pd) (tc_lavc_codecs_video_out[(pd)->vcodec_id])

#define FF_ACODEC_ID(pd) (tc_lavc_int_audio_codecs[(pd)->acodec_id])
#define TC_ACODEC_ID(pd) (tc_lavc_codecs_audio_out[(pd)->acodec_id])
/* WARNING: the arrays below MUST BE KEPT SYNCHRONIZED! */

static const TCCodecID tc_lavc_codecs_video_out[] = { 
    TC_CODEC_MPEG1VIDEO, TC_CODEC_MPEG2VIDEO, TC_CODEC_MPEG4VIDEO,
    TC_CODEC_H263I, TC_CODEC_H263P,
    TC_CODEC_H264,
    TC_CODEC_WMV1, TC_CODEC_WMV2,
    TC_CODEC_RV10,
    TC_CODEC_HUFFYUV, TC_CODEC_FFV1,
    TC_CODEC_DV,
    TC_CODEC_MJPEG, TC_CODEC_LJPEG,
    TC_CODEC_MP42, TC_CODEC_MP43,

    TC_CODEC_ERROR
};

static const TCCodecID tc_lavc_codecs_audio_out[] = { 
    TC_CODEC_MP2, TC_CODEC_AC3,

    TC_CODEC_ERROR
};

static const enum CodecID tc_lavc_int_video_codecs[] = {
    CODEC_ID_MPEG1VIDEO, CODEC_ID_MPEG2VIDEO, CODEC_ID_MPEG4,
    CODEC_ID_H263I, CODEC_ID_H263P,
    CODEC_ID_H264,
    CODEC_ID_WMV1, CODEC_ID_WMV2,
    CODEC_ID_RV10,
    CODEC_ID_HUFFYUV, CODEC_ID_FFV1,
    CODEC_ID_DVVIDEO,
    CODEC_ID_MJPEG, CODEC_ID_LJPEG,
    CODEC_ID_MSMPEG4V2, CODEC_ID_MSMPEG4V3,

    CODEC_ID_NONE
};

static const enum CodecID tc_lavc_int_audio_codecs[] = {
    CODEC_ID_MP2,
    CODEC_ID_AC3,

    CODEC_ID_NONE
};



TC_MODULE_CODEC_FORMATS(tc_lavc);


/*************************************************************************/

/* 
 * following helper private functions adapt stuff and do proper
 * colorspace conversion, if needed, preparing data for
 * later real encoding
 */

/*
 * pre_encode_video_yuv420p:
 * pre_encode_video_yuv420p_huffyuv:
 * pre_encode_video_yuv422p:
 * pre_encode_video_yuv422p_huffyuv:
 * pre_encode_video_rgb24:
 *      prepare internal structures for actual encoding, doing
 *      colorspace conversion and/or any needed adaptation.
 *
 * Parameters:
 *      pd: pointer to private module dataure
 *  vframe: pointer to *SOURCE* video data frame
 * Return Value:
 *     none
 * Preconditions:
 *     module initialized and configured;
 *     auxiliariy buffer space already allocated if needed
 *     (pix fmt != yuv420p)
 * Postconditions:
 *     video data ready to be encoded through lavc.
 */

static void pre_encode_video_yuv420p(TCLavcPrivateData *pd,
                                     TCFrameVideo *vframe)
{
    avpicture_fill((AVPicture *)&pd->ff_venc_frame, vframe->video_buf,
                    PIX_FMT_YUV420P,
                    pd->ff_vcontext.width, pd->ff_vcontext.height);
}


static void pre_encode_video_yuv420p_huffyuv(TCLavcPrivateData *pd,
                                             TCFrameVideo *vframe)
{
    uint8_t *src[3] = { NULL, NULL, NULL };

    YUV_INIT_PLANES(src, vframe->video_buf,
                    IMG_YUV_DEFAULT,
                    pd->ff_vcontext.width, pd->ff_vcontext.height);
    avpicture_fill((AVPicture *)&pd->ff_venc_frame, pd->vframe_buf->video_buf,
                   PIX_FMT_YUV422P,
                   pd->ff_vcontext.width, pd->ff_vcontext.height);
    ac_imgconvert(src, IMG_YUV_DEFAULT,
                  pd->ff_venc_frame.data, IMG_YUV422P,
                  pd->ff_vcontext.width, pd->ff_vcontext.height);
}

static void pre_encode_video_yuv422p(TCLavcPrivateData *pd,
                                     TCFrameVideo *vframe)
{
    uint8_t *src[3] = { NULL, NULL, NULL };

    YUV_INIT_PLANES(src, vframe->video_buf,
                    IMG_YUV422P,
                    pd->ff_vcontext.width, pd->ff_vcontext.height);
    avpicture_fill((AVPicture *)&pd->ff_venc_frame, pd->vframe_buf->video_buf,
                   PIX_FMT_YUV420P,
                   pd->ff_vcontext.width, pd->ff_vcontext.height);
    ac_imgconvert(src, IMG_YUV422P,
                  pd->ff_venc_frame.data, IMG_YUV420P,
                  pd->ff_vcontext.width, pd->ff_vcontext.height);
}


static void pre_encode_video_yuv422p_huffyuv(TCLavcPrivateData *pd,
                                             TCFrameVideo *vframe)
{
    avpicture_fill((AVPicture *)&pd->ff_venc_frame, vframe->video_buf,
                   PIX_FMT_YUV422P,
                   pd->ff_vcontext.width, pd->ff_vcontext.height);

}


static void pre_encode_video_rgb24(TCLavcPrivateData *pd,
                                   TCFrameVideo *vframe)
{
    avpicture_fill((AVPicture *)&pd->ff_venc_frame, pd->vframe_buf->video_buf,
                   PIX_FMT_YUV420P,
                   pd->ff_vcontext.width, pd->ff_vcontext.height);
    ac_imgconvert(&vframe->video_buf, IMG_RGB_DEFAULT,
                  pd->ff_venc_frame.data, IMG_YUV420P,
                  pd->ff_vcontext.width, pd->ff_vcontext.height);
}



/*************************************************************************/

/* more helpers */

/*
 * tc_codec_is_supported:
 *      scan the module supported output codec looking for given one.
 *
 * Parameters:
 *      codec: codec id to check against supported codec list.
 * Return Value:
 *      >= 0: index of codec, if supported, in output list
 *      TC_NULL_MATCH: given codec isn't supported.
 */
static int tc_codec_is_supported(TCCodecID codec, const TCCodecID *codec_list)
{
    int i = 0, ret = TC_NULL_MATCH;

    for (i = 0; codec_list[i] != TC_CODEC_ERROR; i++) {
        if (codec == codec_list[i]) {
            ret = i;
            break;
        }
    }
    return ret;
}

#if !defined(INFINITY) && defined(HUGE_VAL)
#define INFINITY HUGE_VAL
#endif

/*
 * psnr:
 *      compute the psnr value of given data.
 *
 * Parameters:
 *      d: value to be computed
 * Return value:
 *      psnr of `d'
 */
static double psnr(double d) {
    if (d == 0) {
        return INFINITY;
    }
    return -10.0 * log(d) / log(10);
}

static int list_codecs(const TCCodecID *codecs,
                       char *buf, size_t bufsize,
                       size_t *used)
{
    int i = 0, ret = 0;

    for (i = 0; codecs[i] != TC_CODEC_ERROR; i++) {
        char sbuf[TC_BUF_MIN] = { '\0' };
        int slen = 0;

        slen = tc_codec_description(codecs[i], sbuf, sizeof(sbuf) - 1);
        /* + 1 for the final '\n' */
        if (slen < 0) {
            tc_log_error(MOD_NAME, "codec description too long! "
                                   "This should'nt happen. "
                                   "Please file a bug report.");
            strlcpy(buf, "internal error", bufsize);
            ret = -1;
        } else if (*used + slen > bufsize) {
            tc_log_error(MOD_NAME, "too much codecs! "
                                   "This should'nt happen. "
                                   "Please file a bug report.");
            strlcpy(buf, "internal error", bufsize);
            ret = -1;
        } else {
            sbuf[slen] = '\n';
            strlcpy(buf + *used, sbuf, bufsize - *used);
            *used += slen + 1; /* for the trailing newline */
        }
    }
    return ret;
}

/*
 * tc_lavc_list_codecs:
 *      (NOT Thread safe. But do anybody cares since
 *      transcode encoder(.c) is single-threaded today
 *      and in any foreseable future?)
 *      return a buffer listing all supported codecs with
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
static const char* tc_lavc_list_codecs(void)
{
    /* XXX: I feel a bad taste */
    static char buf[TC_BUF_MAX];
    static int ready = TC_FALSE;

    if (!ready) {
        size_t used = 0;
        int err;

        err = list_codecs(tc_lavc_codecs_video_out,
                          buf, sizeof(buf), &used);
        if (!err) {
            err = list_codecs(tc_lavc_codecs_audio_out,
                              buf, sizeof(buf), &used);
        }

        buf[used] = '\0';
        ready = TC_TRUE;
    }
    return buf;
}


/*
 * tc_lavc_read_matrices:
 *      read and fill internal (as in internal module data)
 *      custom quantization matrices from data stored on
 *      disk files, using given paths, then passes them
 *      to libavcodec for usage in encoders if loading was
 *      succesfull.
 *
 * Parameters:
 *                 pd: pointer to module private data to use.
 *  intra_matrix_file: path of file containing intra matrix data.
 *  inter_matrix_file: path of file containing inter matrix data.
 * Return Value:
 *      None
 * Side Effects:
 *      0-2 files on disk are opend, read, closed
 */
static void tc_lavc_read_matrices(TCLavcPrivateData *pd,
                                  const char *intra_matrix_file,
                                  const char *inter_matrix_file)
{
    if (intra_matrix_file != NULL && strlen(intra_matrix_file) > 0) {
        /* looks like we've got something... */
        int ret = tc_read_matrix(intra_matrix_file, NULL, pd->inter_matrix);
        if (ret == 0) {
            /* ok, let's give this to lavc */
            pd->ff_vcontext.intra_matrix = pd->inter_matrix;
        } else {
            tc_log_warn(MOD_NAME, "error while reading intra matrix from"
                                  " %s", intra_matrix_file);
            pd->ff_vcontext.intra_matrix = NULL; /* paranoia */
        }
    }

    if (inter_matrix_file != NULL && strlen(inter_matrix_file) > 0) {
        /* looks like we've got something... */
        int ret = tc_read_matrix(inter_matrix_file, NULL, pd->inter_matrix);
        if (ret == 0) {
            /* ok, let's give this to lavc */
            pd->ff_vcontext.inter_matrix = pd->inter_matrix;
        } else {
            tc_log_warn(MOD_NAME, "error while reading inter matrix from"
                                  " %s", inter_matrix_file);
            pd->ff_vcontext.inter_matrix = NULL; /* paranoia */
        }
    }
}

/*
 * tc_lavc_load_filters:
 *      request to transcode core filters needed by given parameters.
 *
 * Parameters:
 *      pd: pointer to module private data.
 * Return Value:
 *      None.
 */
static void tc_lavc_load_filters(TCLavcPrivateData *pd)
{
    if (TC_VCODEC_ID(pd) == TC_CODEC_MJPEG
     || TC_VCODEC_ID(pd) == TC_CODEC_LJPEG) {
        int handle;

        tc_log_info(MOD_NAME, "output is mjpeg or ljpeg, extending range from "
                    "YUV420P to YUVJ420P (full range)");

        handle = tc_filter_add("levels", "input=16-240");
        if (!handle) {
            tc_log_warn(MOD_NAME, "cannot load levels filter");
        }
    }
}

/*************************************************************************/
/* PSNR-log stuff */

#define PSNR_REQUESTED(PD) ((PD)->confdata.flags.psnr)

/*
 * psnr_open:
 *      open psnr log file and prepare internal data to write out
 *      PSNR stats
 *
 * Parameters:
 *      pd: pointer to private module data.
 * Return Value:
 *      TC_OK: succesfull (log file open and avalaible and so on)
 *      TC_ERROR: otherwise
 */
static int psnr_open(TCLavcPrivateData *pd)
{
    pd->psnr_file = NULL;

    pd->psnr_file = fopen(PSNR_LOG_FILE, "w");
    if (pd->psnr_file != NULL) {
        /* add a little reminder */
        fprintf(pd->psnr_file, "# Num Qual Size Y U V Tot Type");
    } else {
        tc_log_warn(MOD_NAME, "can't open psnr log file '%s'",
                    PSNR_LOG_FILE);
        return TC_ERROR;
    }
    return TC_OK;
}

#define PFRAME(PD) ((PD)->ff_vcontext.coded_frame)

/*
 * psnr_write:
 *      fetch and write to log file, if avalaible, PSNR statistics
 *      for last encoded frames. Format is human-readable.
 *      If psnr log file isn't avalaible, silently doesn nothing.
 *
 * Parameters:
 *        pd: pointer to private module data.
 *      size: size (bytes) of last encoded frame.
 * Return Value:
 *      None.
 */
static void psnr_write(TCLavcPrivateData *pd, int size)
{
    if (pd->psnr_file != NULL) {
        const char pict_type[5] = { '?', 'I', 'P', 'B', 'S' };
        double f = pd->ff_vcontext.width * pd->ff_vcontext.height * 255.0 * 255.0;
        double err[3] = {
                PFRAME(pd)->error[0],
                PFRAME(pd)->error[1],
                PFRAME(pd)->error[2]
        };

        fprintf(pd->psnr_file, "%6d, %2d, %6d, %2.2f,"
                               " %2.2f, %2.2f, %2.2f %c\n",
                PFRAME(pd)->coded_picture_number, PFRAME(pd)->quality, size,
                psnr(err[0]     / f),
                psnr(err[1] * 4 / f), /* FIXME */
                psnr(err[2] * 4 / f), /* FIXME */
                psnr((err[0] + err[1] + err[2]) / (f * 1.5)),
                pict_type[PFRAME(pd)->pict_type]);
    }
}

#undef PFRAME

/*
 * psnr_close:
 *      close psnr log file, free acquired resource.
 *      It's safe to perform this call even if psnr_open()
 *      was NOT called previously.
 *
 * Parameters:
 *      pd: pointer to private module data.
 * Return Value:
 *      TC_OK: succesfull (log file closed correctly)
 *      TC_ERROR: otherwise
 */
static int psnr_close(TCLavcPrivateData *pd)
{
    if (pd->psnr_file != NULL) {
        if (fclose(pd->psnr_file) != 0) {
            return TC_ERROR;
        }
    }
    return TC_OK;
}

/*
 * psnr_print:
 *      tc_log out summary of *overall* PSNR stats.
 *
 * Parameters:
 *      pd: pointer to private module data.
 * Return Value:
 *      None.
 */
static void psnr_print(TCLavcPrivateData *pd)
{
    double f = pd->ff_vcontext.width * pd->ff_vcontext.height * 255.0 * 255.0;

    f *= pd->ff_vcontext.coded_frame->coded_picture_number;

#define ERROR pd->ff_vcontext.error
    tc_log_info(MOD_NAME, "PSNR: Y:%2.2f, Cb:%2.2f, Cr:%2.2f, All:%2.2f",
                psnr(ERROR[0] / f),
                /* FIXME: this is correct if pix_fmt != YUV420P */
                psnr(ERROR[1] * 4 / f), 
                psnr(ERROR[2] * 4 / f), 
                psnr((ERROR[0] + ERROR[1] + ERROR[2]) / (f * 1.5)));
#undef ERROR
}


/*************************************************************************/
/* 
 * configure() helpers, libavcodec allow very detailed
 * configuration step
 */

/*
 * tc_lavc_set_pix_fmt:
 *      choose the right pixel format and setup all internal module
 *      fields depending on this value.
 *      Please note that this function SHALL NOT allocate resources
 *      (i.e.: buffers) that's job of other specific functions.
 *
 * Parameters:
 *        pd: pointer to private module data.
 *       vob: pointer to vob_t structure.
 * Return Value:
 *      TC_OK: succesfull;
 *      TC_ERROR: wrong/erroneous/unsupported pixel format.
 *
 * FIXME: move to TC_CODEC_* colorspaces
 */
static int tc_lavc_set_pix_fmt(TCLavcPrivateData *pd, const vob_t *vob)
{
    switch (vob->im_v_codec) {
      case TC_CODEC_YUV420P:
        if (TC_VCODEC_ID(pd) == TC_CODEC_HUFFYUV) {
            pd->tc_pix_fmt = TC_CODEC_YUV422P;
            pd->ff_vcontext.pix_fmt = PIX_FMT_YUV422P;
            pd->pre_encode_video = pre_encode_video_yuv420p_huffyuv;
        } else {
            pd->tc_pix_fmt = TC_CODEC_YUV420P;
            pd->ff_vcontext.pix_fmt = (TC_VCODEC_ID(pd) == TC_CODEC_MJPEG) 
                                       ? PIX_FMT_YUVJ420P
                                       : PIX_FMT_YUV420P;
            pd->pre_encode_video = pre_encode_video_yuv420p;
        }
        break;
      case TC_CODEC_YUV422P:
        pd->tc_pix_fmt = TC_CODEC_YUV422P;
        pd->ff_vcontext.pix_fmt = (TC_VCODEC_ID(pd) == TC_CODEC_MJPEG) 
                                   ? PIX_FMT_YUVJ422P
                                   : PIX_FMT_YUV422P;
        if (TC_VCODEC_ID(pd) == TC_CODEC_HUFFYUV) {
            pd->pre_encode_video = pre_encode_video_yuv422p_huffyuv;
        } else {
            pd->pre_encode_video = pre_encode_video_yuv422p;
        }
        break;
      case TC_CODEC_RGB24:
        pd->tc_pix_fmt = TC_CODEC_RGB24;
        pd->ff_vcontext.pix_fmt = (TC_VCODEC_ID(pd) == TC_CODEC_HUFFYUV)
                                        ? PIX_FMT_YUV422P
                                        : (TC_VCODEC_ID(pd) == TC_CODEC_MJPEG) 
                                           ? PIX_FMT_YUVJ420P
                                           : PIX_FMT_YUV420P;
        pd->pre_encode_video = pre_encode_video_rgb24;
        break;
      default:
        tc_log_warn(MOD_NAME, "Unknown pixel format %i", vob->im_v_codec);
        return TC_ERROR;
    }

    tc_log_info(MOD_NAME, "internal pixel format: %s",
                tc_codec_to_string(pd->tc_pix_fmt));
    return TC_OK;
}


#define CAN_DO_MULTIPASS(FLAG) do { \
    if (!(FLAG)) { \
        tc_log_error(MOD_NAME, "This codec does not support multipass " \
                     "encoding."); \
        return TC_ERROR; \
    } \
} while (0)

/*
 * tc_lavc_init_multipass:
 *      setup internal (avcodec) parameters for multipass translating
 *      values from vob_t structure, and handle multipass log file data,
 *      reading it or creating it if needed.
 *
 * Parameters:
 *        pd: pointer to private module data.
 *       vob: pointer to vob_t structure.
 * Return Value:
 *      TC_OK: succesfull
 *      TC_ERROR: error (mostly I/O related; reason will tc_log*()'d out)
 * Side effects:
 *      A file on disk will be open'd, and possibly read.
 *      Seeks are possible as well.
 */
static int tc_lavc_init_multipass(TCLavcPrivateData *pd, const vob_t *vob)
{
    int multipass_flag = tc_codec_is_multipass(TC_VCODEC_ID(pd));
    pd->stats_file = NULL;
    size_t fsize = 0;

    switch (vob->divxmultipass) {
      case 1:
        CAN_DO_MULTIPASS(multipass_flag);
        pd->ff_vcontext.flags |= CODEC_FLAG_PASS1;
        pd->stats_file = fopen(vob->divxlogfile, "w");
        if (pd->stats_file == NULL) {
            tc_log_error(MOD_NAME, "could not create 2pass log file"
                         " \"%s\".", vob->divxlogfile);
            return TC_ERROR;
        }
        break;
      case 2:
        CAN_DO_MULTIPASS(multipass_flag);
        pd->ff_vcontext.flags |= CODEC_FLAG_PASS2;
        pd->stats_file = fopen(vob->divxlogfile, "r");
        if (pd->stats_file == NULL){
            tc_log_error(MOD_NAME, "could not open 2pass log file \"%s\""
                         " for reading.", vob->divxlogfile);
            return TC_ERROR;
        }
        /* FIXME: we're optimistic here, don't we? */
        fseek(pd->stats_file, 0, SEEK_END);
        fsize = ftell(pd->stats_file);
        fseek(pd->stats_file, 0, SEEK_SET);

        pd->ff_vcontext.stats_in = tc_malloc(fsize + 1);
        if (pd->ff_vcontext.stats_in == NULL) {
            tc_log_error(MOD_NAME, "can't get memory for multipass log");
            fclose(pd->stats_file);
            return TC_ERROR;
        }

        if (fread(pd->ff_vcontext.stats_in, fsize, 1, pd->stats_file) < 1) {
            tc_log_error(MOD_NAME, "Could not read the complete 2pass log"
                         " file \"%s\".", vob->divxlogfile);
            return TC_ERROR;
        }
        pd->ff_vcontext.stats_in[fsize] = 0; /* paranoia */
        fclose(pd->stats_file);
        break;
      case 3:
        /* fixed qscale :p */
        pd->ff_vcontext.flags |= CODEC_FLAG_QSCALE;
        pd->ff_venc_frame.quality = vob->divxbitrate;
        break;
    }
    return TC_OK;
}

#undef CAN_DO_MULTIPASS

/*
 * tc_lavc_fini_multipass:
 *      release multipass resources, most notably but NOT exclusively
 *      close log file open'd on disk.
 *
 * Parameters:
 *        pd: pointer to private module data.
 * Return Value:
 *      None.
 */
static void tc_lavc_fini_multipass(TCLavcPrivateData *pd)
{
    if (pd->ff_vcontext.stats_in != NULL) {
        tc_free(pd->ff_vcontext.stats_in);
        pd->ff_vcontext.stats_in = NULL;
    }
    if (pd->stats_file != NULL) {
        fclose(pd->stats_file); /* XXX */
        pd->stats_file = NULL;
    }
}

/*
 * tc_lavc_init_rc_override:
 *      parse Rate Control override string given in format understood
 *      by libavcodec and store result in internal avcodec context.
 *
 * Parameters:
 *        pd: pointer to private module data.
 *       str: RC override string to parse.
 * Return Value:
 *      None.
 * Side Effects:
 *      some memory will be allocated.
 */
static void tc_lavc_init_rc_override(TCLavcPrivateData *pd, const char *str)
{
    int i = 0;

    if (str != NULL && strlen(str) > 0) {
        const char *p = str;

        for (i = 0; p != NULL; i++) {
            int start, end, q;
            int e = sscanf(p, "%i,%i,%i", &start, &end, &q);

            if (e != 3) {
                tc_log_warn(MOD_NAME, "Error parsing rc_override (ignored)");
                return;
            }
            pd->ff_vcontext.rc_override = 
                tc_realloc(pd->ff_vcontext.rc_override,
                           sizeof(RcOverride) * (i + 1)); /* XXX */
            pd->ff_vcontext.rc_override[i].start_frame = start;
            pd->ff_vcontext.rc_override[i].end_frame   = end;
            if (q > 0) {
                pd->ff_vcontext.rc_override[i].qscale         = q;
                pd->ff_vcontext.rc_override[i].quality_factor = 1.0;
            } else {
                pd->ff_vcontext.rc_override[i].qscale         = 0;
                pd->ff_vcontext.rc_override[i].quality_factor = -q / 100.0;
            }
            p = strchr(p, '/');
            if (p != NULL) {
                p++;
            }
        }
    }
    pd->ff_vcontext.rc_override_count = i;
}

/*
 * tc_lavc_fini_rc_override:
 *      free Rate Control override resources acquired by
 *      former call of tc_lavc_init_rc_override.
 *      It's safe to call this function even if
 *      tc_lavc_init_rc_override was NOT called previously.
 *
 * Parameters:
 *        pd: pointer to private module data.
 * Return Value:
 *      None.
 */
static void tc_lavc_fini_rc_override(TCLavcPrivateData *pd)
{
    if (pd->ff_vcontext.rc_override != NULL) {
        tc_free(pd->ff_vcontext.rc_override);
        pd->ff_vcontext.rc_override = NULL;
    }
}

/*
 * tc_lavc_init_buf:
 *      allocate internal colorspace conversion buffer, if needed
 *      (depending by internal pixel format),
 *
 * Parameters:
 *        pd: pointer to private module data.
 *       vob: pointer to vob_t structure.
 * Return Value:
 *      TC_OK: succesfull
 *      TC_ERROR: error (can't allocate buffers)
 * Preconditions:
 *      INTERNAL pixel format already determined using
 *      tc_lavc_set_pix_fmt().
 */

static int tc_lavc_init_buf(TCLavcPrivateData *pd, const vob_t *vob)
{
    if (pd->tc_pix_fmt != TC_CODEC_YUV420P) { /*yuv420p it's our default */
        pd->vframe_buf = tc_new_video_frame(vob->im_v_width, vob->im_v_height,
                                            pd->tc_pix_fmt, TC_TRUE);
        if (pd->vframe_buf == NULL) {
            tc_log_warn(MOD_NAME, "unable to allocate internal vframe buffer");
            return TC_ERROR;
        }
    }
    return TC_OK;
}

/* release internal colorspace conversion buffers. */
#define tc_lavc_fini_buf(PD) do { \
    if ((PD) != NULL && (PD)->vframe_buf != NULL) { \
        tc_del_video_frame((PD)->vframe_buf); \
    } \
} while (0)


/*
 * tc_lavc_video_settings_from_vob:
 *      translate vob settings and store them in module
 *      private data and in avcodec context, in correct format.
 *
 * Parameters:
 *        pd: pointer to private module data.
 *       vob: pointer to vob_t structure.
 * Return Value:
 *      TC_OK: succesfull
 *      TC_ERROR: error (various reasons, all will be tc_log*()'d out)
 * Side Effects:
 *      various helper subroutines will be called.
 */
static int tc_lavc_video_settings_from_vob(TCLavcPrivateData *pd, const vob_t *vob)
{
    int ret = 0;

    pd->ff_vcontext.codec_type = CODEC_TYPE_VIDEO;
    pd->ff_vcontext.bit_rate   = vob->divxbitrate * 1000;
    pd->ff_vcontext.width      = vob->ex_v_width;
    pd->ff_vcontext.height     = vob->ex_v_height;
    pd->ff_vcontext.qmin       = vob->min_quantizer;
    pd->ff_vcontext.qmax       = vob->max_quantizer;

    if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_GOP) {
        pd->ff_vcontext.gop_size = vob->divxkeyframes;
    } else {
        if (TC_VCODEC_ID(pd) == TC_CODEC_MPEG1VIDEO
         || TC_VCODEC_ID(pd) == TC_CODEC_MPEG2VIDEO) {
            pd->ff_vcontext.gop_size = 15; /* conservative default for mpeg1/2 svcd/dvd */
        } else {
            pd->ff_vcontext.gop_size = 250; /* reasonable default for mpeg4 (and others) */
        }
    }

    ret = tc_find_best_aspect_ratio(vob,
                                    &pd->ff_vcontext.sample_aspect_ratio.num,
                                    &pd->ff_vcontext.sample_aspect_ratio.den,
                                    MOD_NAME);
    if (ret != TC_OK) {
        tc_log_error(MOD_NAME, "unable to find sane value for SAR");
        return TC_ERROR;
    }
    ret = tc_frc_code_to_ratio(vob->ex_frc,
                               &pd->ff_vcontext.time_base.den,
                               &pd->ff_vcontext.time_base.num);
                               /* watch out here */
    if (ret == TC_NULL_MATCH) {
        /* legacy */
        if ((vob->ex_fps > 29) && (vob->ex_fps < 30)) {
            pd->ff_vcontext.time_base.den = 30000;
            pd->ff_vcontext.time_base.num = 1001;
        } else {
            pd->ff_vcontext.time_base.den = (int)(vob->ex_fps * 1000.0);
            pd->ff_vcontext.time_base.num = 1000;
        }
    }

    switch(vob->encode_fields) {
      case TC_ENCODE_FIELDS_TOP_FIRST:
        pd->interlacing.active    = 1;
        pd->interlacing.top_first = 1;
        break;
      case TC_ENCODE_FIELDS_BOTTOM_FIRST:
        pd->interlacing.active    = 1;
        pd->interlacing.top_first = 0;
        break;
      default: /* progressive / unknown */
        pd->interlacing.active    = 0;
        pd->interlacing.top_first = 0;
        break;
    }

    ret = tc_lavc_set_pix_fmt(pd, vob);
    if (ret != TC_OK) {
        return ret;
    }
    return tc_lavc_init_multipass(pd, vob);
}


static int tc_lavc_audio_settings_from_vob(TCLavcPrivateData *pd, const vob_t *vob)
{
    pd->ff_vcontext.codec_type  = CODEC_TYPE_AUDIO;
    pd->ff_acontext.bit_rate    = vob->mp3bitrate * 1000;  // bitrate dest.
    pd->ff_acontext.channels    = vob->dm_chan;            // channels
    pd->ff_acontext.sample_rate = vob->a_rate;
    pd->audio_bps               = vob->dm_chan * vob->dm_bits/8;
    pd->audio_bpf               = pd->ff_acontext.frame_size * pd->audio_bps;
    // FIXME
    pd->audio_buf_pos           = 0;
    return TC_OK;
}

#define PCTX(field) &(pd->ff_vcontext.field)
#define PAUX(field) &(pd->confdata.field)

/*
 * tc_lavc_config_defaults_video:
 *      setup sane values for auxiliary config, and setup *transcode's*
 *      AVCodecContext default settings for video.
 *
 * Parameters:
 *        pd: pointer to private module data.
 * Return Value:
 *      None
 */
static void tc_lavc_config_defaults_video(TCLavcPrivateData *pd)
{
    /* first of all reinitialize lavc data */
    avcodec_get_context_defaults(&pd->ff_vcontext);

    pd->confdata.thread_count    = 1;

    pd->confdata.vrate_tolerance = 8 * 1000;
    pd->confdata.rc_min_rate     = 0;
    pd->confdata.rc_max_rate     = 0;
    pd->confdata.rc_buffer_size  = 0;
    pd->confdata.lmin            = 2;
    pd->confdata.lmax            = 31;
    pd->confdata.me_method       = ME_EPZS;

    memset(&pd->confdata.flags, 0, sizeof(pd->confdata.flags));
    pd->confdata.turbo_setup = 0;

    /* 
     * context *transcode* (not libavcodec) defaults
     */
    pd->ff_vcontext.mb_qmin                 = 2;
    pd->ff_vcontext.mb_qmax                 = 31;
    pd->ff_vcontext.max_qdiff               = 3;
    pd->ff_vcontext.max_b_frames            = 0;
    pd->ff_vcontext.me_range                = 0;
    pd->ff_vcontext.mb_decision             = 0;
    pd->ff_vcontext.scenechange_threshold   = 0;
    pd->ff_vcontext.scenechange_factor      = 1;
    pd->ff_vcontext.b_frame_strategy        = 0;
    pd->ff_vcontext.b_sensitivity           = 40;
    pd->ff_vcontext.brd_scale               = 0;
    pd->ff_vcontext.bidir_refine            = 0;
    pd->ff_vcontext.rc_strategy             = 2;
    pd->ff_vcontext.b_quant_factor          = 1.25;
    pd->ff_vcontext.i_quant_factor          = 0.8;
    pd->ff_vcontext.b_quant_offset          = 1.25;
    pd->ff_vcontext.i_quant_offset          = 0.0;
    pd->ff_vcontext.qblur                   = 0.5;
    pd->ff_vcontext.qcompress               = 0.5;
    pd->ff_vcontext.mpeg_quant              = 0;
    pd->ff_vcontext.rc_initial_cplx         = 0.0;
    pd->ff_vcontext.rc_qsquish              = 1.0;
    pd->ff_vcontext.luma_elim_threshold     = 0;
    pd->ff_vcontext.chroma_elim_threshold   = 0;
    pd->ff_vcontext.strict_std_compliance   = 0;
    pd->ff_vcontext.dct_algo                = FF_DCT_AUTO;
    pd->ff_vcontext.idct_algo               = FF_IDCT_AUTO;
    pd->ff_vcontext.lumi_masking            = 0.0;
    pd->ff_vcontext.dark_masking            = 0.0;
    pd->ff_vcontext.temporal_cplx_masking   = 0.0;
    pd->ff_vcontext.spatial_cplx_masking    = 0.0;
    pd->ff_vcontext.p_masking               = 0.0;
    pd->ff_vcontext.border_masking          = 0.0;
    pd->ff_vcontext.me_pre_cmp              = 0;
    pd->ff_vcontext.me_cmp                  = 0;
    pd->ff_vcontext.me_sub_cmp              = 0;
    pd->ff_vcontext.ildct_cmp               = FF_CMP_SAD;
    pd->ff_vcontext.pre_dia_size            = 0;
    pd->ff_vcontext.dia_size                = 0;
    pd->ff_vcontext.mv0_threshold           = 256;
    pd->ff_vcontext.last_predictor_count    = 0;
    pd->ff_vcontext.pre_me                  = 1;
    pd->ff_vcontext.me_subpel_quality       = 8;
    pd->ff_vcontext.refs                    = 1;
    pd->ff_vcontext.intra_quant_bias        = FF_DEFAULT_QUANT_BIAS;
    pd->ff_vcontext.inter_quant_bias        = FF_DEFAULT_QUANT_BIAS;
    pd->ff_vcontext.noise_reduction         = 0;
    pd->ff_vcontext.quantizer_noise_shaping = 0;
    pd->ff_vcontext.flags                   = 0;
}


/* FIXME: it is too nasty? */
#define SET_FLAG(pd, field) (pd)->ff_vcontext.flags |= (pd)->confdata.flags.field

/*
 * tc_lavc_dispatch_settings:
 *      translate auxiliary configuration into context values;
 *      also does some consistency verifications.
 *
 * Parameters:
 *        pd: pointer to private module data.
 *       vob: pointer to vob_t structure.
 * Return Value:
 *      None.
 */
static void tc_lavc_dispatch_settings(TCLavcPrivateData *pd)
{
    /* some translation... */
    pd->ff_vcontext.bit_rate_tolerance = pd->confdata.vrate_tolerance * 1000;
    pd->ff_vcontext.rc_min_rate        = pd->confdata.rc_min_rate * 1000;
    pd->ff_vcontext.rc_max_rate        = pd->confdata.rc_max_rate * 1000;
    pd->ff_vcontext.rc_buffer_size     = pd->confdata.rc_buffer_size * 1024;
    pd->ff_vcontext.lmin               = (int)(FF_QP2LAMBDA * pd->confdata.lmin + 0.5);
    pd->ff_vcontext.lmax               = (int)(FF_QP2LAMBDA * pd->confdata.lmax + 0.5);
    pd->ff_vcontext.me_method          = ME_ZERO + pd->confdata.me_method;

    pd->ff_vcontext.flags = 0;
    SET_FLAG(pd, mv0);
    SET_FLAG(pd, cbp);
    SET_FLAG(pd, qpel);
    SET_FLAG(pd, alt);
    SET_FLAG(pd, vdpart);
    SET_FLAG(pd, naq);
    SET_FLAG(pd, ilme);
    SET_FLAG(pd, ildct);
    SET_FLAG(pd, aic);
    SET_FLAG(pd, aiv);
    SET_FLAG(pd, umv);
    SET_FLAG(pd, psnr);
    SET_FLAG(pd, trell);
    SET_FLAG(pd, gray);
    SET_FLAG(pd, v4mv);
    SET_FLAG(pd, closedgop);

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
    /* FIXME: coherency check */
    if (pd->ff_vcontext.rtp_payload_size > 0) {
        pd->ff_vcontext.rtp_mode = 1;
    }
#endif
    if (pd->confdata.flags.closedgop) {
        pd->ff_vcontext.scenechange_threshold = 1000000;
    }
    if (pd->interlacing.active) {
        /* enforce interlacing */
        pd->ff_vcontext.flags |= CODEC_FLAG_INTERLACED_DCT;
        pd->ff_vcontext.flags |= CODEC_FLAG_INTERLACED_ME;
    }
}

#undef SET_FLAG


/*
 * tc_lavc_read_config:
 *      read configuration values from
 *      1) configuration file (if found)
 *      2) command line (overrides configuration file in case
 *         of conflicts).
 *      Also read related informations like RC override string
 *      and custom quantization matrices; translate all settings
 *      in libavcodec-friendly values (if needed), then finally
 *      perform some coherency checks and feed avcodec context
 *      with gathered data.
 *
 * Parameters:
 *           pd: pointer to private module data.
 *      options: command line options of *THIS MODULE*.
 * Return Value:
 *      TC_OK: succesfull
 *      TC_ERROR: error. Mostly I/O related or badly broken
 *                (meaningless) value. Exact reason will tc_log*()'d out.
 * Side Effects:
 *      Quite a lot, since various (and quite complex) subroutines
 *      are involved. Most notably, various files can be opened/read/closed
 *      on disk, and some memory could be allocated.
 */
static int tc_lavc_read_config(TCLavcPrivateData *pd,
                               const char *options, const vob_t *vob)
{
    char *intra_matrix_file = NULL;
    char *inter_matrix_file = NULL;
    char *rc_override_buf = NULL;
    /* 
     * Please note that option names are INTENTIONALLY identical/similar
     * to mplayer/mencoder ones
     */
    TCConfigEntry lavc_conf[] = {
        { "threads", PAUX(thread_count), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 7 },
        //  need special handling
        //  { "keyint", PCTX(gop_size), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 1000 },
        //  handled by transcode core
        //  { "vbitrate", PCTX(bit_rate), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, INT_MAX },
        //  handled by transcode core
        //  { "vqmin", PCTX(qmin), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 60 },
        //  handled by transcode core
        //  { "vqmax", PCTX(qmax), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 60 },
        //  handled by transcode core
        { "mbqmin", PCTX(mb_qmin), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 60 },
        { "mbqmax", PCTX(mb_qmax), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 60 },
        { "lmin", PAUX(lmin), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.01, 255.0 },
        { "lmax", PAUX(lmax), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.01, 255.0 },
        { "vqdiff", PCTX(max_qdiff), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31 },
        { "vmax_b_frames", PCTX(max_b_frames), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, FF_MAX_B_FRAMES },
        { "vme", PAUX(me_method), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 16, },
        { "me_range", PCTX(me_range), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 16000 },
        { "mbd", PCTX(mb_decision), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 3 },
        { "sc_threshold", PCTX(scenechange_threshold), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -1000000, 1000000 },
        { "sc_factor", PCTX(scenechange_factor), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 16 },
        { "vb_strategy", PCTX(b_frame_strategy), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 10 },
        { "b_sensitivity", PCTX(b_sensitivity), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 100 },
        { "brd_scale", PCTX(brd_scale), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 10 },
        { "bidir_refine", PCTX(bidir_refine), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 4 },
        //  { "aspect",     },
        //  handled by transcode core
        { "vratetol", PAUX(vrate_tolerance), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 4, 24000000 },
        { "vrc_maxrate", PAUX(rc_max_rate), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 24000000 },
        { "vrc_minrate", PAUX(rc_min_rate), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 24000000 },
        { "vrc_buf_size", PAUX(rc_buffer_size), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 4, 24000000 },
        { "vrc_strategy", PCTX(rc_strategy), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2 },
        { "vb_qfactor", PCTX(b_quant_factor), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, -31.0, 31.0 },
        { "vi_qfactor", PCTX(i_quant_factor), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, -31.0, 31.0 },
        { "vb_qoffset", PCTX(b_quant_offset), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 31.0 },
        { "vi_qoffset", PCTX(i_quant_offset), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 31.0 },
        { "vqblur", PCTX(qblur), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 1.0 },
        { "vqcomp", PCTX(qcompress), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 1.0 },
        { "mpeg_quant", PCTX(mpeg_quant), TCCONF_TYPE_FLAG, 0, 0, 1 },
        //  { "vrc_eq",     }, // not yet supported
        { "vrc_override", rc_override_buf, TCCONF_TYPE_STRING, 0, 0, 0 },
        { "vrc_init_cplx", PCTX(rc_initial_cplx), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 9999999.0 },
        //  { "vrc_init_occupancy",   }, // not yet supported
        { "vqsquish", PCTX(rc_qsquish), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 99.0 },
        { "vlelim", PCTX(luma_elim_threshold), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -99, 99 },
        { "vcelim", PCTX(chroma_elim_threshold), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -99, 99 },
        { "vstrict", PCTX(strict_std_compliance), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -99, 99 },
        { "vpsize", PCTX(rtp_payload_size), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100000000 },
        { "dct", PCTX(dct_algo), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 10 },
        { "idct", PCTX(idct_algo), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 20 },
        { "lumi_mask", PCTX(lumi_masking), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 1.0 },
        { "dark_mask", PCTX(dark_masking), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 1.0 },
        { "tcplx_mask", PCTX(temporal_cplx_masking), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 1.0 },
        { "scplx_mask", PCTX(spatial_cplx_masking), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 1.0 },
        { "p_mask", PCTX(p_masking), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 1.0 },
        { "border_mask", PCTX(border_masking), TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 1.0 },
        { "pred", PCTX(prediction_method), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 4 },
        { "precmp", PCTX(me_pre_cmp), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000 },
        { "cmp", PCTX(me_cmp), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000 },
        { "subcmp", PCTX(me_sub_cmp), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000 },
        { "ildctcmp", PCTX(ildct_cmp), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000 },
        { "predia", PCTX(pre_dia_size), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -2000, 2000 },
        { "dia", PCTX(dia_size), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -2000, 2000 },
        { "mv0_threshold", PCTX(mv0_threshold), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 1000 },
        { "last_pred", PCTX(last_predictor_count), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000 },
        { "pre_me", PCTX(pre_me), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000},
        { "subq", PCTX(me_subpel_quality), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 8 },
        { "refs", PCTX(refs), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 8 },
        { "ibias", PCTX(intra_quant_bias), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -512, 512 },
        { "pbias", PCTX(inter_quant_bias), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -512, 512 },
        { "nr", PCTX(noise_reduction), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 1000000},
        { "qns", PCTX(quantizer_noise_shaping), TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 3 },
        { "inter_matrix_file", inter_matrix_file, TCCONF_TYPE_STRING, 0, 0, 0 },
        { "intra_matrix_file", intra_matrix_file, TCCONF_TYPE_STRING, 0, 0, 0 },
    
        { "mv0", PAUX(flags.mv0), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_MV0 },
        { "cbp", PAUX(flags.cbp), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_CBP_RD },
        { "qpel", PAUX(flags.qpel), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_QPEL },
        { "alt", PAUX(flags.alt), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_ALT_SCAN },
        { "ilme", PAUX(flags.ilme), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_INTERLACED_ME },
        { "ildct", PAUX(flags.ildct), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_INTERLACED_DCT },
        { "naq", PAUX(flags.naq), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_NORMALIZE_AQP },
        { "vdpart", PAUX(flags.vdpart), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_PART },
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
        { "aic", PAUX(flags.aic), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_H263P_AIC },
#else        
        { "aic", PAUX(flags.aic), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_AC_PRED },
#endif
        { "aiv", PAUX(flags.aiv), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_H263P_AIV },
        { "umv", PAUX(flags.umv), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_H263P_UMV },
        { "psnr", PAUX(flags.psnr), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_PSNR },
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
        { "trell", PAUX(flags.trell), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_TRELLIS_QUANT },
#else
        { "trell", PCTX(trellis), TCCONF_TYPE_FLAG, 0, 0, 1 },
#endif
        { "gray", PAUX(flags.gray), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_GRAY },
        { "v4mv", PAUX(flags.v4mv), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_4MV },
        { "closedgop", PAUX(flags.closedgop), TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_CLOSED_GOP },
    
        //  { "turbo", PAUX(turbo_setup), TCCONF_TYPE_FLAG, 0, 0, 1 }, // not yet  supported
        /* End of the config file */
        { NULL, 0, 0, 0, 0, 0 }
    };
    const char *dirs[] = { ".", NULL };

    tc_config_read_file(dirs, LAVC_CONFIG_FILE,
                        tc_codec_to_string(vob->ex_v_codec),
                        lavc_conf, MOD_NAME);

    if (options && strlen(options) > 0) {
        size_t i = 0, n = 0;
        char **opts = tc_strsplit(options, ':', &n);

        if (opts == NULL) {
            tc_log_error(MOD_NAME, "can't split option string");
            return TC_ERROR;
        }
        for (i = 0; i < n; i++) {
            if (!tc_config_read_line(opts[i], lavc_conf, MOD_NAME)) {
                tc_log_error(MOD_NAME, "error parsing module options (%s)",
                             opts[i]);
                tc_strfreev(opts);
                return TC_ERROR;
            }
        }
        tc_strfreev(opts);
    }

    /* gracefully go ahead if no matrices are given */
    tc_lavc_read_matrices(pd, intra_matrix_file, inter_matrix_file);
    /* gracefully go ahead if no rc override is given */
    tc_lavc_init_rc_override(pd, rc_override_buf);

    if (verbose >= TC_DEBUG) {
        tc_config_print(lavc_conf, MOD_NAME);
    }
    /* only now we can do this safely */
    tc_lavc_dispatch_settings(pd);

    return TC_OK;
}

#undef PCTX
#undef PAUX

/*
 * tc_lavc_write_logs:
 *      write on disk file encoding logs. That means encoder
 *      multipass log file, but that can also include PSNR
 *      statistics, if requested.
 *
 * Parameters:
 *           pd: pointer to private module data.
 *         size: size of last encoded frame.
 * Return Value:
 *      TC_OK: succesfull
 *      TC_ERROR: I/O error. Exact reason will tc_log*()'d out.
 */
static int tc_lavc_write_logs(TCLavcPrivateData *pd, int size)
{
    /* store stats if there are any */
    if (pd->ff_vcontext.stats_out != NULL && pd->stats_file != NULL) {
        int ret = fprintf(pd->stats_file, "%s",
                          pd->ff_vcontext.stats_out);
        if (ret < 0) {
            tc_log_warn(MOD_NAME, "error while writing multipass log file");
            return TC_ERROR;
        }
    }

    if (PSNR_REQUESTED(pd)) {
        /* errors not fatal, they can be ignored */
        psnr_write(pd, size);
    }
        
    return TC_OK;
}

/*************************************************************************/
/* see libtc/tcmodule-data.h for functions meaning and purposes          */


static int tc_lavc_init(TCModuleInstance *self, uint32_t features)
{
    TCLavcPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    /* FIXME: move into core? */
    TC_INIT_LIBAVCODEC;

    pd = tc_malloc(sizeof(TCLavcPrivateData));
    if (pd == NULL) {
        tc_log_error(MOD_NAME, "unable to allocate private data");
        return TC_ERROR;
    }

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    self->userdata = pd;

    return TC_OK;
}

TC_MODULE_GENERIC_FINI(tc_lavc)

#define ABORT_IF_NOT_OK(RET) do { \
    if ((RET) != TC_OK) { \
        goto failed; \
    } \
} while (0)

static int tc_lavc_stop_video(TCLavcPrivateData *pd)
{
    tc_lavc_fini_buf(pd);

    if (PSNR_REQUESTED(pd)) {
        psnr_print(pd);
        psnr_close(pd);
    }

    tc_lavc_fini_rc_override(pd);
    /* ok, now really start the real teardown */
    tc_lavc_fini_multipass(pd);

    if (pd->ff_vcodec != NULL) {
        avcodec_close(&pd->ff_vcontext);
        pd->ff_vcodec = NULL;
    }

    return TC_OK;
}

static int tc_lavc_stop_audio(TCLavcPrivateData *pd)
{
    if (pd->ff_acodec != NULL) {
        avcodec_close(&pd->ff_acontext);
        pd->ff_acodec = NULL;
    }
    return TC_OK;
}

static int tc_lavc_stop(TCModuleInstance *self)
{
    TCLavcPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    if (self->features & TC_MODULE_FEATURE_VIDEO) {
        tc_lavc_stop_video(pd);
    }
    if (self->features & TC_MODULE_FEATURE_AUDIO) {
        tc_lavc_stop_audio(pd);
    }
    return TC_OK;
}

static int tc_lavc_configure_video(TCModuleInstance *self,
                                   const char *options,
                                   TCJob *vob,
                                   TCModuleExtraData *xdata)
{
    const char *vcodec_name = tc_codec_to_string(vob->ex_v_codec);
    TCLavcPrivateData *pd = NULL;
    int ret = TC_OK;

    TC_MODULE_SELF_CHECK(options, "configure"); /* paranoia */

    pd = self->userdata;

    pd->flush_flag = vob->encoder_flush;
    
    avcodec_get_frame_defaults(&pd->ff_venc_frame);
    /*
     * auxiliary config data needs to be blanked too
     * before any other operation
     */
    tc_lavc_config_defaults_video(pd);

    /* 
     * we must do first since we NEED valid vcodec_name
     * ASAP to read right section of configuration file.
     */
    pd->vcodec_id = tc_codec_is_supported(vob->ex_v_codec,
                                         tc_lavc_codecs_video_out);
    if (pd->vcodec_id == TC_NULL_MATCH) {
        tc_log_error(MOD_NAME, "unsupported codec `%s'", vcodec_name);
        return TC_ERROR;
    }
    if (verbose) {
        tc_log_info(MOD_NAME, "using video codec '%s'", vcodec_name);
    }

    ret = tc_lavc_video_settings_from_vob(pd, vob);
    ABORT_IF_NOT_OK(ret);

    /* calling WARNING: order matters here */
    ret = tc_lavc_init_buf(pd, vob);
    ABORT_IF_NOT_OK(ret);

    ret = tc_lavc_read_config(pd, options, vob);
    ABORT_IF_NOT_OK(ret);

    tc_lavc_load_filters(pd);

    if (verbose) {
        tc_log_info(MOD_NAME, "using %i thread%s",
                    pd->confdata.thread_count,
                    (pd->confdata.thread_count > 1) ?"s" :"");
    }
    avcodec_thread_init(&pd->ff_vcontext, pd->confdata.thread_count);

    pd->ff_vcodec = avcodec_find_encoder(FF_VCODEC_ID(pd));
    if (pd->ff_vcodec == NULL) {
        tc_log_error(MOD_NAME, "unable to find a libavcodec encoder for `%s'",
                     tc_codec_to_string(TC_VCODEC_ID(pd)));
        goto failed;
    }

    TC_LOCK_LIBAVCODEC;
    ret = avcodec_open(&pd->ff_vcontext, pd->ff_vcodec);
    TC_UNLOCK_LIBAVCODEC;

    if (ret < 0) {
        tc_log_error(MOD_NAME, "avcodec_open() failed");
        goto failed;
    }
    /* finally, pass up the extradata, if any */
    xdata->stream_id  = 0; /* FIXME */
    xdata->codec      = pd->vcodec_id;
    xdata->extra.data = pd->ff_vcontext.extradata;
    xdata->extra.size = pd->ff_vcontext.extradata_size;

    if (PSNR_REQUESTED(pd)) {
        /* errors already logged, and they can be ignored */
        psnr_open(pd);
        pd->confdata.flags.psnr = 0; /* no longer requested :^) */
    }
    return TC_OK;

failed:
    tc_lavc_fini_buf(pd);
    return TC_ERROR;
}

static int tc_lavc_configure_audio(TCModuleInstance *self,
                                   const char *options,
                                   TCJob *vob,
                                   TCModuleExtraData *xdata)
{
    const char *acodec_name = tc_codec_to_string(vob->ex_a_codec);
    TCLavcPrivateData *pd = NULL;
    int ret = TC_OK;

    TC_MODULE_SELF_CHECK(options, "configure"); /* paranoia */

    pd = self->userdata;

    /* 
     * we must do first since we NEED valid vcodec_name
     * ASAP to read right section of configuration file.
     */
    pd->acodec_id = tc_codec_is_supported(vob->ex_a_codec,
                                         tc_lavc_codecs_audio_out);
    if (pd->acodec_id == TC_NULL_MATCH) {
        tc_log_error(MOD_NAME, "unsupported codec `%s'", acodec_name);
        return TC_ERROR;
    }
    if (verbose) {
        tc_log_info(MOD_NAME, "using video codec '%s'", acodec_name);
    }

    pd->ff_acodec = avcodec_find_encoder(FF_ACODEC_ID(pd));
    if (pd->ff_acodec == NULL) {
        tc_log_error(MOD_NAME, "unable to find a libavcodec encoder for `%s'",
                     tc_codec_to_string(TC_ACODEC_ID(pd)));
        goto failed;
    }

    TC_LOCK_LIBAVCODEC;
    ret = avcodec_open(&pd->ff_acontext, pd->ff_acodec);
    TC_UNLOCK_LIBAVCODEC;

    if (ret < 0) {
        tc_log_error(MOD_NAME, "avcodec_open() failed");
        goto failed;
    }
 
    avcodec_get_context_defaults(&pd->ff_acontext);
    return tc_lavc_audio_settings_from_vob(pd, vob);

failed:
    return TC_ERROR;
}

#undef ABORT_IF_NOT_OK

static int tc_lavc_configure(TCModuleInstance *self,
                             const char *options,
                             TCJob *vob, TCModuleExtraData *xdata[])
{
    int ret = TC_OK;

    TC_MODULE_SELF_CHECK(self, "configure");


    if (self->features & TC_MODULE_FEATURE_VIDEO) {
        ret = tc_lavc_configure_video(self, options, vob, xdata[0]);
        if (ret != TC_OK) {
            goto failure;
        }
    }
    if (self->features & TC_MODULE_FEATURE_AUDIO) {
        ret = tc_lavc_configure_audio(self, options, vob, xdata[1]);
        if (ret != TC_OK) {
            goto failure;
        }
    }
    return TC_OK;

failure:
    tc_lavc_stop(self);
    return TC_ERROR;
}

static int tc_lavc_inspect(TCModuleInstance *self,
                           const char *param, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    if (optstr_lookup(param, "help")) {
        *value = tc_lavc_help;
    }

    if (optstr_lookup(param, "list")) {
        *value = tc_lavc_list_codecs();
    }
    return TC_OK;
}

static int tc_lavc_flush_video(TCModuleInstance *self,
                               TCFrameVideo *outframe,
                               int *frame_returned)
{
    *frame_returned = 0;
    return TC_OK;
}


static int tc_lavc_encode_video(TCModuleInstance *self,
                                TCFrameVideo *inframe,
                                TCFrameVideo *outframe)
{
    TCLavcPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "encode_video");

    pd = self->userdata;

    pd->ff_venc_frame.interlaced_frame = pd->interlacing.active;
    pd->ff_venc_frame.top_field_first  = pd->interlacing.top_first;

    pd->pre_encode_video(pd, inframe); 

    TC_LOCK_LIBAVCODEC;
    outframe->video_len = avcodec_encode_video(&pd->ff_vcontext,
                                               outframe->video_buf,
                                               inframe->video_size,
                                               &pd->ff_venc_frame);
    TC_UNLOCK_LIBAVCODEC;

    if (outframe->video_len < 0) {
        tc_log_warn(MOD_NAME, "encoder error: size (%i)",
                    outframe->video_len);
        return TC_ERROR;
    }
    
    if (pd->ff_vcontext.coded_frame->key_frame) {
        outframe->attributes |= TC_FRAME_IS_KEYFRAME;
    }

    return tc_lavc_write_logs(pd, outframe->video_len);
}

static int tc_lavc_flush_audio(TCModuleInstance *self,
                               TCFrameAudio *outframe,
                               int *frame_returned)
{
    *frame_returned = 0;
    return TC_OK; /* FIXME */
}

#define INT_BUF_GET(PD)    ((PD)->aframe_buf->audio_buf)
#define INT_BUF_POS(PD)    (INT_BUF_GET(PD) + ((PD)->audio_buf_pos))
#define INT_BUF_FWD(PD, N) ((PD)->audio_buf_pos += (N))

static int tc_lavc_encode_audio(TCModuleInstance *self,
                                TCFrameAudio *inframe,
                                TCFrameAudio *outframe)
{
    int in_size      = inframe->audio_len;
    int out_size     = 0;
    uint8_t *in_buf  = inframe->audio_buf;
    uint8_t *out_buf = outframe->audio_buf;
    TCLavcPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "encode_audio");

    pd = self->userdata;

    //-- any byte in mpa-buffer left from past call ? --
    if (pd->audio_buf_pos > 0) {
        int bytes_needed = pd->audio_bps - pd->audio_buf_pos;

        //-- complete frame -> encode --
        if (in_size >= bytes_needed) {
            ac_memcpy(INT_BUF_POS(pd), in_buf, bytes_needed);
            TC_LOCK_LIBAVCODEC;
            out_size = avcodec_encode_audio(&pd->ff_acontext, out_buf,
                                            SIZE_PCM_FRAME, // FIXME
                                            (int16_t*)INT_BUF_GET(pd));
                                            /* yes, at the start */
            TC_UNLOCK_LIBAVCODEC;

            out_buf += out_size;
            in_size -= bytes_needed;
            in_buf  += bytes_needed;

            pd->audio_buf_pos = 0;
        } else {
            //-- incomplete frame -> append bytes to mpa-buffer and return --
            ac_memcpy(INT_BUF_POS(pd), in_buf, in_size);
            pd->audio_buf_pos += in_size;
            return TC_OK;
        }
    }
    //-- encode only as much "full" frames as available --
    while (in_size >= pd->audio_bpf) {
        TC_LOCK_LIBAVCODEC;
        out_size = avcodec_encode_audio(&pd->ff_acontext, out_buf,
                                        SIZE_PCM_FRAME, // FIXME
                                        (int16_t*)in_buf);
        TC_UNLOCK_LIBAVCODEC;

        out_buf += out_size;
        in_size -= pd->audio_bpf;
        in_buf  += pd->audio_bpf;
    }

    //-- hold rest of bytes in mpa-buffer --
    if (in_size > 0) {
        pd->audio_buf_pos = in_size;
        ac_memcpy(INT_BUF_GET(pd), in_buf, in_size);
    }

    return TC_OK;
}

/*************************************************************************/

TC_MODULE_INFO(tc_lavc);

static const TCModuleClass tc_lavc_class = {
    TC_MODULE_CLASS_HEAD(tc_lavc),

    .init         = tc_lavc_init,
    .fini         = tc_lavc_fini,
    .configure    = tc_lavc_configure,
    .stop         = tc_lavc_stop,
    .inspect      = tc_lavc_inspect,

    .encode_video = tc_lavc_encode_video,
    .encode_audio = tc_lavc_encode_audio,

    .flush_video  = tc_lavc_flush_video,
    .flush_audio  = tc_lavc_flush_audio,
};

TC_MODULE_ENTRY_POINT(tc_lavc);

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

