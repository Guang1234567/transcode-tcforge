/*
 *  filter_facemask.c -- mask people faces in video interviews
 *
 *  Copyright (C) Julien Tierny <julien.tierny@wanadoo.fr> - October 2004
 *  Copyright (C) Thomas Oestreich - June 2001
 *      modified 2007 by Branko Kokanovic <branko.kokanovic at gmail dot com> to use NMS
 *  This file is part of transcode, a video stream processing tool
 *
 *  transcode is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  transcode is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA].
 *
 */

#define MOD_NAME    "filter_facemask.so"
#define MOD_VERSION "v0.2.1 (2007-07-29)"
#define MOD_CAP     "Mask people faces in video interviews."
#define MOD_AUTHOR  "Julien Tierny"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

/* -------------------------------------------------
 *
 * mandatory include files
 *
 *-------------------------------------------------*/

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"
/* For RGB->YUV conversion */
#include "libtcvideo/tcvideo.h"

static const char facemask_help[]=""
    "Overview:\n"
    "   This filter can mask people faces in video interviews.\n"
    "   Both YUV and RGB formats are supported, in multithreaded mode.\n"
    "\n"
    "   Warning:\n"
    "   You have to calibrate by your own the mask dimensions and positions so as it fits to your video sample.\n"
    "   You also have to choose a resolution that is multiple of the mask dimensions.\n"
    "\n"
    "Options:\n"
    "   'xpos':        Position of the upper left corner of the mask (x)\n"
    "   'ypos':        Position of the upper left corner of the mask (y)\n"
    "   'xresolution': Resolution of the mask (width)\n"
    "   'yresolution': Resolution of the mask (height)\n"
    "   'xdim':        Width of the mask (= n*xresolution)\n"
    "   'ydim':        Height of the mask (= m*yresolution)\n";

/*************************************************************************/

typedef struct {
    int       xpos;
    int       ypos;
    int       xresolution;
    int       yresolution;
    int       xdim;
    int       ydim;
    TCVHandle tcvhandle;
    int       codec;
    char      conf_str[TC_BUF_MIN];
} FacemaskPrivateData;


static int check_parameters(int x, int y, int w, int h,
                            int W, int H, vob_t *vob)
{

    /* First, we check if the face-zone is contained in the picture */
    if ((x+W) > vob->im_v_width){
        tc_log_error(MOD_NAME, "Face zone is larger than the picture !");
        return TC_ERROR;
    }
    if ((y+H) > vob->im_v_height){
        tc_log_error(MOD_NAME, "Face zone is taller than the picture !");
        return TC_ERROR;
    }

    /* Then, we check the resolution */
    if ((H%h) != 0) {
        tc_log_error(MOD_NAME, "Uncorrect Y resolution !");
        return TC_ERROR;
    }
    if ((W%w) != 0) {
        tc_log_error(MOD_NAME, "Uncorrect X resolution !");
        return TC_ERROR;
    }
    return TC_OK;
}

static int average_neighbourhood(int x, int y, int w, int h,
                                 uint8_t *buffer, int width)
{
    uint32_t red=0, green=0, blue=0;
    int      i=0,j=0;

    for (j=y; j<=y+h; j++){
        for (i=3*(x + width*(j-1)); i<3*(x + w + (j-1)*width); i+=3){
            red     += (int) buffer[i];
            green   += (int) buffer[i+1];
            blue    += (int) buffer[i+2];
        }
    }

    red     /= ((w+1)*h);
    green   /= ((w+1)*h);
    blue    /= ((w+1)*h);

    /* Now let's print values in buffer */
    for (j=y; j<y+h; j++) {
        for (i=3*(x + width*(j-1)); i<3*(x + w + (j-1)*width); i+=3) { 
            buffer[i]       = (char)red;
            buffer[i+1]     = (char)green;
            buffer[i+2]     = (char)blue;
        }
    }
    return 0;
}

static int print_mask(int x, int y, int w, int h, int W, int H, vframe_list_t *ptr)
{
    int i = 0,j = 0;
    for (j=y; j<=y+H; j+=h)
        for (i=x; i<=x+W; i+=w)
            average_neighbourhood(i, j, w, h, ptr->video_buf, ptr->v_width);
    return 0;
}

/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * facemask_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(facemask, FacemaskPrivateData)

/*************************************************************************/

/**
 * facemask_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(facemask)

/*************************************************************************/

/**
 * facemask_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int facemask_configure(TCModuleInstance *self,
                              const char *options,
                              TCJob *vob,
                              TCModuleExtraData *xdata[])
{
    FacemaskPrivateData *fpd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");
    TC_MODULE_SELF_CHECK(vob, "configure");

    fpd = self->userdata;

    /* Filter default options */
    if (verbose & TC_DEBUG)
        tc_log_info(MOD_NAME, "Preparing default options.");
    fpd->codec          = vob->im_v_codec;
    fpd->xpos           = 0;
    fpd->ypos           = 0;
    fpd->xresolution    = 1;
    fpd->yresolution    = 1;
    fpd->xdim           = 1;
    fpd->ydim           = 1;
    fpd->tcvhandle      = 0;

    if (options) {
        optstr_get(options, "xpos",        "%d", &fpd->xpos);
        optstr_get(options, "ypos",        "%d", &fpd->ypos);
        optstr_get(options, "xresolution", "%d", &fpd->xresolution);
        optstr_get(options, "yresolution", "%d", &fpd->yresolution);
        optstr_get(options, "xdim",        "%d", &fpd->xdim);
        optstr_get(options, "ydim",        "%d", &fpd->ydim);
    }

    if (vob->im_v_codec == TC_CODEC_YUV420P){
        fpd->tcvhandle = tcv_init();
        if (!fpd->tcvhandle) {
            tc_log_error(MOD_NAME, "Error at image conversion initialization.");
            return TC_ERROR;
        }
    }

    return check_parameters(fpd->xpos, fpd->ypos,
                            fpd->xresolution, fpd->yresolution,
                            fpd->xdim, fpd->ydim, vob);
}

/*************************************************************************/

/**
 * facemask_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int facemask_stop(TCModuleInstance *self)
{
    FacemaskPrivateData *fpd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    fpd = self->userdata;

    if (fpd->tcvhandle){
        tcv_free(fpd->tcvhandle);
    }


    return TC_OK;
}

/*************************************************************************/

/**
 * facemask_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int facemask_inspect(TCModuleInstance *self,
                            const char *param, const char **value)
{
    FacemaskPrivateData *fpd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    fpd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = facemask_help; 
    }
    if (optstr_lookup(param, "xpos")) {
        tc_snprintf(fpd->conf_str, sizeof(fpd->conf_str),
                    "xpos=%d", fpd->xpos);
        *value = fpd->conf_str;
    }
    if (optstr_lookup(param, "ypos")) {
        tc_snprintf(fpd->conf_str, sizeof(fpd->conf_str),
                    "ypos=%d", fpd->xpos);
        *value = fpd->conf_str;
    }
    if (optstr_lookup(param, "xresolution")) {
        tc_snprintf(fpd->conf_str, sizeof(fpd->conf_str),
                    "xresolution=%d", fpd->xpos);
        *value = fpd->conf_str;
    }
    if (optstr_lookup(param, "yresolution")) {
        tc_snprintf(fpd->conf_str, sizeof(fpd->conf_str),
                    "yresolution=%d", fpd->xpos);
        *value = fpd->conf_str;
    }
    if (optstr_lookup(param, "xdim")) {
        tc_snprintf(fpd->conf_str, sizeof(fpd->conf_str),
                    "xdim=%d", fpd->xpos);
        *value = fpd->conf_str;
    }
    if (optstr_lookup(param, "ydim")) {
        tc_snprintf(fpd->conf_str, sizeof(fpd->conf_str),
                    "ydim=%d", fpd->xpos);
        *value = fpd->conf_str;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * facemask_filter_video:  show something on given frame of the video
 * stream.  See tcmodule-data.h for function details.
 */

static int facemask_filter_video(TCModuleInstance *self, vframe_list_t *frame)
{
    FacemaskPrivateData *fpd = NULL;

    TC_MODULE_SELF_CHECK(self, "filer_video");
    TC_MODULE_SELF_CHECK(frame, "filer_video");

    fpd = self->userdata;

    if (!(frame->attributes & TC_FRAME_IS_SKIPPED)) {
        switch(fpd->codec){
            case TC_CODEC_RGB24:
                return print_mask(fpd->xpos, fpd->ypos,
                                  fpd->xresolution, fpd->yresolution,
                                  fpd->xdim, fpd->ydim, frame);
                break;
            case TC_CODEC_YUV420P:
                if (!tcv_convert(fpd->tcvhandle, frame->video_buf, frame->video_buf,
                                 frame->v_width, frame->v_height,
                                 IMG_YUV_DEFAULT, IMG_RGB24)){
                    tc_log_error(MOD_NAME,
                                 "cannot convert YUV stream to RGB format !");
                    return TC_ERROR;
                }

                if ((print_mask(fpd->xpos, fpd->ypos,
                                fpd->xresolution, fpd->yresolution,
                                fpd->xdim, fpd->ydim, frame)) < 0) {
                    return TC_ERROR;
                }

                if (!tcv_convert(fpd->tcvhandle, frame->video_buf, frame->video_buf,
                                 frame->v_width, frame->v_height,
                                 IMG_RGB24, IMG_YUV_DEFAULT)){
                    tc_log_error(MOD_NAME,
                                 "cannot convert RGB stream to YUV format !");
                    return TC_ERROR;
                }
                break;
            default:
                tc_log_error(MOD_NAME, "Internal video codec is not supported.");
                return TC_ERROR;
        }
    }
    return TC_OK;
}


/*************************************************************************/

static const TCCodecID facemask_codecs_video_in[] = { 
    TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_ERROR
};
static const TCCodecID facemask_codecs_video_out[] = {
    TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(facemask);
TC_MODULE_FILTER_FORMATS(facemask);

TC_MODULE_INFO(facemask);

static const TCModuleClass facemask_class = {
    TC_MODULE_CLASS_HEAD(facemask),

    .init         = facemask_init,
    .fini         = facemask_fini,
    .configure    = facemask_configure,
    .stop         = facemask_stop,
    .inspect      = facemask_inspect,

    .filter_video = facemask_filter_video
};

TC_MODULE_ENTRY_POINT(facemask)

/*************************************************************************/

static int facemask_get_config(TCModuleInstance *self, char *options)
{
    FacemaskPrivateData *fpd = NULL;

    TC_MODULE_SELF_CHECK(self, "get_config");

    fpd = self->userdata;

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       MOD_AUTHOR, "VRYMEO", "1");

    optstr_param(options, "help", "Mask people faces in video interviews",
                 "", "0");
    optstr_param(options, "xpos",
                 "Position of the upper left corner of the mask (x)",
                 "%d", "0", "0", "oo");
    optstr_param(options, "ypos",
                 "Position of the upper left corner of the mask (y)",
                 "%d", "0", "0", "oo");
    optstr_param(options, "xresolution",
                 "Resolution of the mask (width)",
                 "%d", "0", "1", "oo");
    optstr_param(options, "yresolution",
                 "Resolution of the mask (height)",
                 "%d", "0", "1", "oo");
    optstr_param(options, "xdim",
                 "Width of the mask (= n*xresolution)",
                 "%d", "0", "1", "oo");
    optstr_param(options, "ydim",
                 "Height of the mask (= m*yresolution)",
                 "%d", "0", "1", "oo");

    return TC_OK;
}

static int facemask_process(TCModuleInstance *self, 
                            frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    if ((frame->tag & TC_VIDEO) && (frame->tag & TC_POST_M_PROCESS)) {
        return facemask_filter_video(self, (vframe_list_t*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE(facemask)

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
