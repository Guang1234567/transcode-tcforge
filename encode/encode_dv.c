/*
 *  encode_dv.c - encode a DV video stream using libdv
 *  (C) 2005-2010 Francesco Romani <fromani at gmail dot com>
 *  Based on code
 *  Copyright (C) Thomas Oestreich et Al. - June 2001
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "src/transcode.h"
#include "aclib/imgconvert.h"
#include "libtcutil/optstr.h"

#include "libtcmodule/tcmodule-plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <libdv/dv.h>

#define MOD_NAME    "encode_dv.so"
#define MOD_VERSION "v0.0.5 (2009-02-03)"
#define MOD_CAP     "Digital Video encoder"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_ENCODE|TC_MODULE_FEATURE_VIDEO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

 
/* tc_dv_ prefix was used to avoid any possible name clash with libdv */

static const char tc_dv_help[] = ""
    "Overview:\n"
    "\tthis module encodes raw RGB/YUV video frames in DV, using libdv.\n"
    "Options:\n"
    "\thelp\tproduce module overview and options explanations\n";


typedef struct {
    int frame_size;
    int is_yuv;

    int dv_yuy2_mode;

    dv_encoder_t *dvenc;
    uint8_t *conv_buf;
} DVPrivateData;

/*************************************************************************/


static int tc_dv_configure(TCModuleInstance *self,
                           const char *options,
                           TCJob *vob,
                           TCModuleExtraData *xdata[])
{
    DVPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    if((vob->ex_v_width != PAL_W && vob->ex_v_height != PAL_H) &&
       (vob->ex_v_width != NTSC_W && vob->ex_v_height != NTSC_H)) {
        tc_log_error(MOD_NAME, "configure: illegal frame dimensions");
        return TC_ERROR;
    }

    switch (vob->im_v_codec) {
      case TC_CODEC_RGB24:
        pd->is_yuv = 0;
        break;
      case TC_CODEC_YUV420P:
        pd->is_yuv = 1;
        break;
      default:
        tc_log_error(MOD_NAME, "video format not supported:"
                               " not RGB or YUV420P");
        return TC_ERROR;
    }

    // for reading
    pd->frame_size               = (vob->ex_v_height == PAL_H)
                                      ?TC_FRAME_DV_PAL :TC_FRAME_DV_NTSC;
    pd->dvenc->isPAL             = (vob->ex_v_height == PAL_H) ?1 :0;
    pd->dvenc->is16x9            = FALSE;
    pd->dvenc->vlc_encode_passes = 3;
    pd->dvenc->static_qno        = 0;
    pd->dvenc->force_dct         = DV_DCT_AUTO;

    if (verbose) {
        tc_log_info(MOD_NAME, "dv mode: %s",
                              (pd->dv_yuy2_mode) ?"yuy2" :"yv12"); // XXX
        tc_log_info(MOD_NAME, "source type: %s/%s",
                    (pd->dvenc->isPAL) ?"PAL" :"NTSC",
                    (pd->is_yuv) ?"YUV420P" :"RGB24");
        tc_log_info(MOD_NAME, "source frame size: %i", pd->frame_size);
    }
    return TC_OK;
}


static int tc_dv_init(TCModuleInstance *self, uint32_t features)
{
    DVPrivateData *pd = NULL;
    vob_t *vob = tc_get_vob();

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    pd = tc_malloc(sizeof(DVPrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: can't allocate private data");
        return TC_ERROR;
    }

    pd->dvenc = dv_encoder_new(FALSE, FALSE, FALSE);
    if (!pd->dvenc) {
        tc_log_error(MOD_NAME, "init: can't allocate encoder data");
        goto failed_alloc_dvenc;
    }

    if(vob->dv_yuy2_mode) {
        pd->conv_buf = tc_bufalloc(PAL_W * PAL_H * 2); /* max framne size */
        if (!pd->conv_buf) {
            tc_log_error(MOD_NAME, "init: can't allocate private buffer");
            goto failed_alloc_privbuf;
        }
        pd->dv_yuy2_mode = 1;
    } else {
        pd->conv_buf = NULL;
        pd->dv_yuy2_mode = 0;
    }

    pd->frame_size = 0;
    pd->is_yuv = -1; /* invalid value */

    self->userdata = pd;
    
    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return TC_OK;

failed_alloc_privbuf:
    dv_encoder_free(pd->dvenc);
failed_alloc_dvenc:
    tc_free(pd);
    return TC_ERROR;
}

static int tc_dv_fini(TCModuleInstance *self)
{
    DVPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "fini");

    pd = self->userdata;

    if (pd->conv_buf != NULL) {
        tc_free(pd->conv_buf);
    }
    dv_encoder_free(pd->dvenc);
    tc_free(pd);

    self->userdata = NULL;
    return TC_OK;
}


static int tc_dv_inspect(TCModuleInstance *self,
                         const char *param, const char **value)
{
    DVPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");

    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = tc_dv_help;
    }

    return TC_OK;
}

static int tc_dv_stop(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "stop");

    /* we don't need to do anything here */

    return TC_OK;
}


/* ------------------------------------------------------------
 *
 * encode and export
 *
 * ------------------------------------------------------------*/

/* FIXME: switch to libtc::tcframes::tc_video_planes_size */
#define DV_INIT_PLANES(pixels, buf, w, h) do {\
    pixels[0] = (buf); \
    pixels[1] = pixels[0] + ((w) * (h)); \
    pixels[2] = pixels[1] + (((w) / 2) * ((h) / 2)); \
} while (0)

static int tc_dv_encode_video(TCModuleInstance *self,
                              TCFrameVideo *inframe, TCFrameVideo *outframe)
{
    DVPrivateData *pd = NULL;
    uint8_t *pixels[3] = { NULL, NULL, NULL };
    time_t now = time(NULL);
    int w, h;

    TC_MODULE_SELF_CHECK(self, "encode_video");

    pd = self->userdata;
    w = (pd->dvenc->isPAL) ?PAL_W :NTSC_W;
    h = (pd->dvenc->isPAL) ?PAL_H :NTSC_H;
	

    if (!pd->dv_yuy2_mode) {
        DV_INIT_PLANES(pixels, inframe->video_buf, w, h);
    } else {
        /* 
         * tcv_convert is handy, but since operates in place
         * it requires an extra ac_memcpy that I should be able
         * to avoid doing as follows:
         */
        uint8_t *conv_pixels[3] = { NULL, NULL, NULL };
        DV_INIT_PLANES(conv_pixels, pd->conv_buf, w, h);

        ac_imgconvert(pixels, IMG_YUV420P, conv_pixels, IMG_YUY2,
		              PAL_W, (pd->dvenc->isPAL) ?PAL_H :NTSC_H);

        /* adjust main pointers */
        DV_INIT_PLANES(pixels, pd->conv_buf, w, h);
    }

    dv_encode_full_frame(pd->dvenc, pixels,
                         (pd->is_yuv) ?e_dv_color_yuv :e_dv_color_rgb,
                         outframe->video_buf);
    outframe->video_len = pd->frame_size;

    dv_encode_metadata(outframe->video_buf, pd->dvenc->isPAL,
                       pd->dvenc->is16x9, &now, 0);
    dv_encode_timecode(outframe->video_buf, pd->dvenc->isPAL, 0);

    /* only keyframes */
    outframe->attributes |= TC_FRAME_IS_KEYFRAME;

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID tc_dv_codecs_video_in[] = {
    TC_CODEC_YUY2, TC_CODEC_RGB24, TC_CODEC_YUV420P,
    TC_CODEC_ERROR
};

static const TCCodecID tc_dv_codecs_video_out[] = {
    TC_CODEC_DV,
    TC_CODEC_ERROR
};

TC_MODULE_AUDIO_UNSUPPORTED(tc_dv);
TC_MODULE_CODEC_FORMATS(tc_dv);

TC_MODULE_INFO(tc_dv);

static const TCModuleClass tc_dv_class = {
    TC_MODULE_CLASS_HEAD(tc_dv),

    .init         = tc_dv_init,
    .fini         = tc_dv_fini,
    .configure    = tc_dv_configure,
    .stop         = tc_dv_stop,
    .inspect      = tc_dv_inspect,

    .encode_video = tc_dv_encode_video,
};

TC_MODULE_ENTRY_POINT(tc_dv);

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
