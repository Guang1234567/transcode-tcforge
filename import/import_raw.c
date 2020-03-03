/*
 *  import_raw.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *
 *  This file is part of transcode, a video stream  processing tool
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

#define MOD_NAME    "import_raw.so"
#define MOD_VERSION "v0.3.3 (2007-08-26)"
#define MOD_CODEC   "(video) RGB/YUV | (audio) PCM"

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB|TC_CAP_YUV|TC_CAP_PCM|TC_CAP_YUV422;

#define MOD_PRE raw
#include "import_def.h"

char import_cmd_buf[TC_BUF_MAX];
static int codec;

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
    char cat_buf[TC_BUF_MAX];
    char *co = NULL;

    if (param->flag == TC_AUDIO) {
        co = (vob->a_codec_flag == TC_CODEC_ULAW) ?"ulaw" :"pcm"; // XXX

        /* multiple inputs? */
        if (tc_file_check(vob->audio_in_file) == 1) {
            tc_snprintf(cat_buf, sizeof(cat_buf), "%s -a", TCCAT_EXE);
        } else {
            tc_snprintf(cat_buf, sizeof(cat_buf),
                        "%s -x %s %s", TCEXTRACT_EXE, co,
                        (vob->im_a_string) ?vob->im_v_string :"");
        }
        if (tc_snprintf(import_cmd_buf, TC_BUF_MAX, 
                        "%s -i \"%s\" -d %d | %s -a %d -x %s -d %d -t raw",
                        cat_buf, vob->audio_in_file, vob->verbose,
                        TCEXTRACT_EXE, vob->a_track, co, vob->verbose) < 0) {
            tc_log_perror(MOD_NAME, "cmd buffer overflow");
            return TC_IMPORT_ERROR;
        }

	    if (verbose_flag)
            tc_log_info(MOD_NAME, "%s", import_cmd_buf);

        param->fd = popen(import_cmd_buf, "r");
        if (param->fd == NULL) {
            tc_log_perror(MOD_NAME, "popen audio stream");
            return TC_IMPORT_ERROR;
        }

        return TC_IMPORT_OK;
    }

    if (param->flag == TC_VIDEO) {
        codec = vob->im_v_codec;

        switch (codec) {
          case TC_CODEC_RGB24:
            co = "rgb";
            break;
          case TC_CODEC_YUV422P:
            co = "yuv422p";
            break;
          case TC_CODEC_YUV420P: /* fallthrough */
          default:
            co = "yuv420p";
            break;
        }

        /* multiple inputs? */
        if (tc_file_check(vob->video_in_file) == 1) {
            tc_snprintf(cat_buf, sizeof(cat_buf), "%s", TCCAT_EXE);
        } else {
            tc_snprintf(cat_buf, sizeof(cat_buf),
                        "%s %s",
                        TCEXTRACT_EXE,
                        (vob->im_v_string) ?vob->im_v_string :"");
        }

	    if (tc_snprintf(import_cmd_buf, TC_BUF_MAX,
                        "%s -i \"%s\" -d %d -x %s | %s -a %d -x %s -d %d",
                        cat_buf, vob->video_in_file, vob->verbose, co,
                        TCEXTRACT_EXE, vob->v_track, co, vob->verbose) < 0) {
            tc_log_perror(MOD_NAME, "cmd buffer overflow");
            return TC_IMPORT_ERROR;
        }

        if (verbose_flag)
            tc_log_info(MOD_NAME, "%s", import_cmd_buf);

        param->fd = popen(import_cmd_buf, "r");
        if (param->fd == NULL) {
            tc_log_perror(MOD_NAME, "popen video stream");
            return TC_IMPORT_ERROR;
        }

        return TC_IMPORT_OK;
    }

    return TC_IMPORT_ERROR;
}


/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

MOD_decode
{
    return TC_IMPORT_OK;
}

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
    if (param->fd != NULL) {
        pclose(param->fd);
        param->fd = NULL;
    }
    return TC_IMPORT_OK;
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

