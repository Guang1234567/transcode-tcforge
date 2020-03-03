/*
 *  import_im.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  port to GraphicsMagick API:
 *  Copyright (C) Francesco Romani - 2009-2010
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

#define MOD_NAME    "import_im.so"
#define MOD_VERSION "v0.2.0 (2009-03-07)"
#define MOD_CODEC   "(video) RGB"

#include "src/transcode.h"
#include "libtcutil/optstr.h"

#include "libtcext/tc_magick.h"
#include "libtcvideo/tcvideo.h"

/*%*
 *%* DESCRIPTION 
 *%*   This module reads single images from disk using ImageMagick;
 *%*   a stream of correlated images can be automatically read if
 *%*   their filenames contains a common prefix and a serial number.
 *%*   All formats supported by ImageMagick are supported as well.
 *%*
 *%* BUILD-DEPENDS
 *%*   libGraphicsMagick >= 1.0.11
 *%*
 *%* DEPENDS
 *%*   libGraphicsMagick >= 1.0.11
 *%*
 *%* PROCESSING
 *%*   import/demuxer
 *%*
 *%* MEDIA
 *%*   video
 *%*
 *%* #INPUT
 *%*
 *%* OUTPUT
 *%*   RGB24
 *%*
 *%* OPTION
 *%*   noseq (flag)
 *%*     disable internal auto loading of images with similar names.
 *%*/

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB|TC_CAP_YUV|TC_CAP_VID;

#define MOD_PRE im
#include "import_def.h"

#include <time.h>
#include <sys/types.h>
#include <regex.h>


typedef struct tcimprivatedata_ TCIMPrivateData;
struct tcimprivatedata_ {
    TCMagickContext magick;
    TCVHandle       tcvhandle;  // For colorspace conversion

    int             width;
    int             height;

    char            *head;
    char            *tail;

    int             first_frame;
    int             current_frame;
    int             decoded_frame;
    int             total_frame;

    int             pad;
    /* 
     * automagically read further images with filename like the first one 
     * enabled by default for backward compatibility, but obsoleted
     * by core option --multi_input
     */
    int             auto_seq_read;
};

static TCIMPrivateData IM;


/*************************************************************************/

static void tc_im_defaults(TCIMPrivateData *pd)
{
    pd->head            = NULL;
    pd->tail            = NULL;
    pd->first_frame     = 0;
    pd->current_frame   = 0;
    pd->decoded_frame   = 0;
    pd->total_frame     = 0;
    pd->width           = 0;
    pd->height          = 0;
    pd->pad             = 0;
    pd->auto_seq_read   = TC_TRUE; 
}

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/


/* I suspect we have a lot of potential memleaks in here -- FRomani */
MOD_open
{
    int err = 0, slen = 0;
    char *regex = NULL, *frame = NULL;
    regex_t preg;
    regmatch_t pmatch[4];

    if (param->flag == TC_AUDIO) {
        return TC_OK;
    }

    if (param->flag == TC_VIDEO) {
        tc_im_defaults(&IM);

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

        IM.tcvhandle = tcv_init();
        if (!IM.tcvhandle) {
            return TC_ERROR;
        }

        param->fd = NULL;

        // get the frame name and range
        regex = "\\([^0-9]\\+[-._]\\?\\)\\?\\([0-9]\\+\\)\\([-._].\\+\\)\\?";
        err = regcomp(&preg, regex, 0);
        if (err) {
            tc_log_perror(MOD_NAME, "ERROR:  Regex compile failed.\n");
            tcv_free(IM.tcvhandle);
            IM.tcvhandle = 0;
            return TC_ERROR;
        }

        err = regexec(&preg, vob->video_in_file, 4, pmatch, 0);
        if (err) {
            tc_log_warn(MOD_NAME, "Regex match failed: no image sequence");
            slen = strlen(vob->video_in_file) + 1;
            IM.head = tc_malloc(slen);
            if (IM.head == NULL) {
                tc_log_perror(MOD_NAME, "filename head");
                tcv_free(IM.tcvhandle);
                IM.tcvhandle = 0;
                return TC_ERROR;
            }
            strlcpy(IM.head, vob->video_in_file, slen);
            IM.tail = tc_malloc(1); /* URGH -- FRomani */
            IM.tail[0] = 0;
            IM.first_frame = -1;
        } else {
            // split the name into head, frame number, and tail
            slen = pmatch[1].rm_eo - pmatch[1].rm_so + 1;
            IM.head = tc_malloc(slen);
            if (IM.head == NULL) {
                tc_log_perror(MOD_NAME, "filename head");
                tcv_free(IM.tcvhandle);
                IM.tcvhandle = 0;
                return TC_ERROR;
            }
            strlcpy(IM.head, vob->video_in_file, slen);

            slen = pmatch[2].rm_eo - pmatch[2].rm_so + 1;
            frame = tc_malloc(slen);
            if (frame == NULL) {
                tc_log_perror(MOD_NAME, "filename frame");
                tcv_free(IM.tcvhandle);
                IM.tcvhandle = 0;
                return TC_ERROR;
            }
            strlcpy(frame, vob->video_in_file + pmatch[2].rm_so, slen);

            // If the frame number is padded with zeros, record how many digits
            // are actually being used.
            if (frame[0] == '0') {
                IM.pad = pmatch[2].rm_eo - pmatch[2].rm_so;
            }
            IM.first_frame = atoi(frame);

            slen = pmatch[3].rm_eo - pmatch[3].rm_so + 1;
            IM.tail = tc_malloc(slen);
            if (IM.tail == NULL) {
                tc_log_perror(MOD_NAME, "filename tail");
                tcv_free(IM.tcvhandle);
                IM.tcvhandle = 0;
                return TC_ERROR;
            }
            strlcpy(IM.tail, vob->video_in_file + pmatch[3].rm_so, slen);

            tc_free(frame);
        }

        if (vob->im_v_string != NULL) {
            if (optstr_lookup(vob->im_v_string, "noseq")) {
                IM.auto_seq_read = TC_FALSE;
                if (verbose > TC_INFO) {
                    tc_log_info(MOD_NAME, "automagic image sequential read disabled");
                }
            }
        }
 
        IM.current_frame = IM.first_frame;
        IM.decoded_frame = 0;
        IM.width         = vob->im_v_width;
        IM.height        = vob->im_v_height;

        if (IM.total_frame == 0) {
            /* only the very first time */
            int ret = tc_magick_init(&IM.magick, TC_MAGICK_QUALITY_DEFAULT);
            if (ret != TC_OK) {
                tc_log_error(MOD_NAME, "cannot create magick context");
                tcv_free(IM.tcvhandle);
                IM.tcvhandle = 0;
                return ret;
            }
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
    char *frame = NULL, *filename = NULL;
    int slen, ret;

    if (param->flag == TC_AUDIO) {
        return TC_OK;
    }

    if (param->flag == TC_VIDEO) {
        if (!IM.auto_seq_read) {
            if (IM.decoded_frame > 0) {
                return TC_ERROR;
            }
            filename = tc_strdup(vob->video_in_file);
        } else {
            // build the filename for the current frame
            slen = strlen(IM.head) + IM.pad + strlen(IM.tail) + 1;
            filename = tc_malloc(slen);
            if (IM.pad) {
                char framespec[10] = { '\0' };
                frame = tc_malloc(IM.pad+1);
                tc_snprintf(framespec, 10, "%%0%dd", IM.pad);
                tc_snprintf(frame, IM.pad+1, framespec, IM.current_frame);
                frame[IM.pad] = '\0';
            } else if (IM.first_frame >= 0) {
                frame = tc_malloc(10);
                tc_snprintf(frame, 10, "%d", IM.current_frame);
            }
            strlcpy(filename, IM.head, slen);
            if (frame != NULL) {
                strlcat(filename, frame, slen);
                tc_free(frame);
                frame = NULL;
            }
            strlcat(filename, IM.tail, slen);
        }

        ret = tc_magick_filein(&IM.magick, filename);
        if (ret != TC_OK) {
            return ret;
        }

        ret = tc_magick_RGBout(&IM.magick, 
                               IM.width, IM.height, param->buffer); 
        /* param->size already set correctly by caller */
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

        IM.total_frame++;
        IM.current_frame++;
        IM.decoded_frame++;
    
        tc_free(filename);

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
        TCSession *session = tc_get_session(); /* bandaid */
        int ret = TC_OK;

        if (param->fd != NULL) {
            pclose(param->fd);
            param->fd = NULL;
        }
        tcv_free(IM.tcvhandle);
        IM.tcvhandle = 0;
        tc_free(IM.head);
        IM.head = NULL;
        tc_free(IM.tail);
        IM.tail = NULL;

        if (!tc_has_more_video_in_file(session)) {
            /* FIXME FIXME FIXME: outrageous layering violation */
            /* Can you hear this? It's the sound of the uglinesssssss */
            ret = tc_magick_fini(&IM.magick);
        }
        return ret;
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

