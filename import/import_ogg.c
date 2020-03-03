/*
 *  import_ogg.c
 *
 *  Copyright (C) Thomas Oestreich - July 2002
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

#define MOD_NAME    "import_ogg.so"
#define MOD_VERSION "v0.1.0 (2007-12-15)"
#define MOD_CODEC   "(video) * | (audio) *"

#include "src/transcode.h"
#include "libtc/libtc.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB|TC_CAP_YUV|TC_CAP_AUD|TC_CAP_PCM|TC_CAP_VID;

#define MOD_PRE ogg
#include "import_def.h"

#include "magic.h"


/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
    char import_cmd_buf[TC_BUF_MAX];

    char *codec = "";
    param->fd = NULL;

    if (param->flag != TC_VIDEO || param->flag != TC_AUDIO) {
        return TC_ERROR;
    }
    
    if (param->flag == TC_VIDEO) {
        char *color = NULL;
        char *magic = NULL;

        switch (vob->im_v_codec) {
          case TC_CODEC_RGB24:
            color = "rgb";
            break;
          
          case TC_CODEC_YUV420P:
            color = "yuv420p";
            break;
          
          default:
            color = "";
            break;
        }

        switch (vob->v_codec_flag) {
          case TC_CODEC_DIVX5:
          case TC_CODEC_DIVX4:
          case TC_CODEC_DIVX3:
          case TC_CODEC_XVID:
            codec = "divx4";
            magic = "-t lavc";
            break;

          case TC_CODEC_DV:
            codec = "dv";
            magic = "";
            break;

          case TC_CODEC_RGB24:
          case TC_CODEC_YUV420P:
          default:
            codec = "raw";
            magic = "";
            break;
        }

        if (tc_snprintf(import_cmd_buf, sizeof(import_cmd_buf),
                        "%s -i \"%s\" -x raw -d %d |"
                        " %s %s -g %dx%d -x %s -y %s -d %d",
                        TCEXTRACT_EXE, vob->video_in_file, vob->verbose,
                        TCDECODE_EXE, magic, vob->im_v_width, vob->im_v_height,
                        codec, color, vob->verbose) < 0
           ) {
            tc_log_perror(MOD_NAME, "command buffer overflow");
            return TC_ERROR;
        }
    }

    if (param->flag == TC_AUDIO) {
        switch (vob->a_codec_flag) {
          case TC_CODEC_MP3:
          case TC_CODEC_MP2:
            codec = "mp3";
            break;

          case TC_CODEC_VORBIS:
            codec = "ogg";
            break;

          case TC_CODEC_PCM:
            codec = "pcm";
            break;

          default:
            tc_log_warn(MOD_NAME, "Unkown codec");
            break;
        }

        if (tc_snprintf(import_cmd_buf, sizeof(import_cmd_buf),
                        "%s -i \"%s\" -x %s -a %d -d %d",
                        TCEXTRACT_EXE, vob->audio_in_file, codec,
                        vob->a_track, vob->verbose) < 0) {
            tc_log_perror(MOD_NAME, "command buffer overflow");
            return TC_ERROR;

            if (vob->a_codec_flag != TC_CODEC_PCM) {
                char buf[TC_BUF_MAX];
                if (tc_snprintf(buf, sizeof(buf), " | %s -x %s -d %i",
                                TCDECODE_EXE, codec, vob->verbose) < 0) {
                    tc_log_perror(MOD_NAME, "command buffer overflow");
                    return TC_ERROR;
                }
                strlcpy(import_cmd_buf, buf, sizeof(import_cmd_buf)); // XXX
            }
        }
    }

    if (verbose_flag)
        tc_log_info(MOD_NAME, "%s", import_cmd_buf);

    param->fd = popen(import_cmd_buf, "r");
    if (param->fd == NULL) {
        tc_log_perror(MOD_NAME, "popen video stream");
        return TC_ERROR;
    }
    return TC_OK;
}


MOD_decode
{
    /* nothing to do */
    return TC_OK;
}


MOD_close
{
    if (param->fd != NULL) {
        pclose(param->fd);
    }
    return TC_OK;
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
