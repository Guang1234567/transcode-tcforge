/*
 *  import_imlist.c
 *
 *  Copyright (C) Thomas Oestreich - February 2002
 *  port to MagickWand API:
 *  Copyright (C) Francesco Romani - July 2007
 *
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
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define MOD_NAME    "import_imlist.so"
#define MOD_VERSION "v0.2.0 (2009-03-07)"
#define MOD_CODEC   "(video) RGB"

#include "src/transcode.h"
#include "libtcext/tc_magick.h"
#include "libtcvideo/tcvideo.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB|TC_CAP_YUV|TC_CAP_VID;

#define MOD_PRE imlist
#include "import_def.h"


typedef struct tcimprivatedata_ TCIMPrivateData;
struct tcimprivatedata_ {
    TCMagickContext magick;
    TCVHandle       tcvhandle;  // For colorspace conversion

    int             width;
    int             height;
    FILE            *fd;
};

static TCIMPrivateData IM;


/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
    if (param->flag == TC_AUDIO) {
        return TC_OK;
    }

    if (param->flag == TC_VIDEO) {
        int ret = TC_ERROR;

        param->fd = NULL;

        if (vob->im_v_codec == TC_CODEC_YUV420P
         && (vob->im_v_width % 2 != 0 || vob->im_v_height % 2 != 0)
        ) {
            tc_log_error(MOD_NAME, "Width and height must be even for YUV420P");
            return TC_ERROR;
        }
        if (vob->im_v_codec == TC_CODEC_YUV422P && vob->im_v_width % 2 != 0) {
            tc_log_error(MOD_NAME, "Width must be even for YUV422P");
            return TC_ERROR;
        }

        IM.width  = vob->im_v_width;
        IM.height = vob->im_v_height;

        tc_log_warn(MOD_NAME,
                    "This module is DEPRECATED.");
        tc_log_warn(MOD_NAME,
                    "Please consider to use the multi input mode"
                    " (--multi_input) with import_im module.");
        tc_log_warn(MOD_NAME,
                    "(e.g.) transcode --multi_input -x im ...");

        IM.tcvhandle = tcv_init();
        if (!IM.tcvhandle) {
            return TC_ERROR;
        }

        IM.fd = fopen(vob->video_in_file, "r");
        if (IM.fd == NULL) {
            tcv_free(IM.tcvhandle);
            IM.tcvhandle = 0;
            return TC_ERROR;
        }

        ret = tc_magick_init(&IM.magick, TC_MAGICK_QUALITY_DEFAULT);
        if (ret != TC_OK) {
            fclose(IM.fd);
            IM.fd = NULL;
            tcv_free(IM.tcvhandle);
            IM.tcvhandle = 0;
            tc_log_error(MOD_NAME, "cannot create magick context");
            return ret;
        }

        return TC_OK;
    }

    return TC_ERROR;
}


/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

MOD_decode
{
    char filename[PATH_MAX+1];
    int ret = TC_ERROR;

    if (param->flag == TC_AUDIO) {
        return TC_OK;
    }

    if (param->flag == TC_VIDEO) {
        // read a filename from the list
        if (fgets(filename, PATH_MAX, IM.fd) == NULL) {
            return TC_ERROR;
        }
        filename[PATH_MAX] = '\0'; /* enforce */
        tc_strstrip(filename);

        ret = tc_magick_filein(&IM.magick, filename);
        if (ret != TC_OK) {
            return ret;
        }

        ret = tc_magick_RGBout(&IM.magick,
                               IM.width, IM.height, param->buffer);
        if (ret != TC_OK) {
            return ret;
        }

        if (vob->im_v_codec == TC_CODEC_YUV420P) {
            tcv_convert(IM.tcvhandle, param->buffer, param->buffer,
                        vob->im_v_width, vob->im_v_height,
                        IMG_RGB24, IMG_YUV420P);
            param->size = vob->im_v_width * vob->im_v_height
                        + 2 * (vob->im_v_width/2) * (vob->im_v_height/2);
        } else if (vob->im_v_codec == TC_CODEC_YUV422P) {
            tcv_convert(IM.tcvhandle, param->buffer, param->buffer,
                        vob->im_v_width, vob->im_v_height,
                        IMG_RGB24, IMG_YUV422P);
            param->size = vob->im_v_width * vob->im_v_height
                        + 2 * (vob->im_v_width/2) * vob->im_v_height;
        }

        param->attributes |= TC_FRAME_IS_KEYFRAME;

        return TC_OK;
    }
    return TC_ERROR;
}

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
    if (param->flag == TC_AUDIO) {
        return TC_OK;
    }

    if (param->flag == TC_VIDEO) {
        if (IM.fd != NULL) {
            fclose(IM.fd);
            IM.fd = NULL;
        }

        tcv_free(IM.tcvhandle);
        IM.tcvhandle = 0;

        return tc_magick_fini(&IM.magick);
    }

    return TC_ERROR;
}

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

