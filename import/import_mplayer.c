/*
 *  import_mplayer.c
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

#define MOD_NAME    "import_mplayer.so"
#define MOD_VERSION "v0.1.2 (2007-11-01)"
#define MOD_CODEC   "(video) rendered by mplayer | (audio) rendered by mplayer"

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_YUV|TC_CAP_RGB|TC_CAP_VID|TC_CAP_PCM;

#define MOD_PRE mplayer
#include "import_def.h"

#include <sys/types.h>



#define VIDEOPIPE_TEMPLATE "/tmp/mplayer2transcode-video.XXXXXX"
#define AUDIOPIPE_TEMPLATE "/tmp/mplayer2transcode-audio.XXXXXX"
static char videopipe[40];
static char audiopipe[40];
static FILE *videopipefd = NULL;
static FILE *audiopipefd = NULL;

/* ------------------------------------------------------------
 * private helper macros/functions.
 * ------------------------------------------------------------*/

#define RETURN_IF_BAD_SRET(SRET, FIFO) do { \
    if ((SRET) < 0) { \
        unlink((FIFO)); \
        return TC_IMPORT_ERROR; \
    } \
} while (0)

#define RETURN_IF_OPEN_FAILED(FP, MSG) do { \
    if ((FP) == NULL) { \
        tc_log_perror(MOD_NAME, (MSG)); \
        unlink(videopipe); \
        return TC_IMPORT_ERROR; \
    } \
} while (0)


static int tc_mplayer_open_video(vob_t *vob, transfer_t *param)
{
    char import_cmd_buf[TC_BUF_MAX];
    long sret = 0;

    tc_snprintf(videopipe, sizeof(videopipe), VIDEOPIPE_TEMPLATE);
    if (!mktemp(videopipe)) {
        tc_log_perror(MOD_NAME, "mktemp videopipe failed");
        return(TC_IMPORT_ERROR);
    }
    if (mkfifo(videopipe, 00660) == -1) {
        tc_log_perror(MOD_NAME, "mkfifo video failed");
        return(TC_IMPORT_ERROR);
    }

    sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
                       "mplayer -slave -benchmark -noframedrop -nosound"
                       " -vo yuv4mpeg:file=%s %s \"%s\" -osdlevel 0"
                       " > /dev/null 2>&1",
                       videopipe,
                       ((vob->im_v_string) ? vob->im_v_string : ""),
                       vob->video_in_file);
    RETURN_IF_BAD_SRET(sret, videopipe);

    if (verbose_flag)
        tc_log_info(MOD_NAME, "%s", import_cmd_buf);

    videopipefd = popen(import_cmd_buf, "w");
    RETURN_IF_OPEN_FAILED(videopipefd, "popen videopipe failed");

    if (vob->im_v_codec == TC_CODEC_YUV420P) {
        sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
                           "%s -i %s -x yuv420p -t yuv4mpeg",
                           TCEXTRACT_EXE, videopipe);
        RETURN_IF_BAD_SRET(sret, videopipe);
    } else {
        sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
                           "%s -i %s -x yuv420p -t yuv4mpeg |"
                           " %s -x yuv420p -g %dx%d",
                           TCEXTRACT_EXE, videopipe,
                           TCDECODE_EXE, vob->im_v_width, vob->im_v_height);
        RETURN_IF_BAD_SRET(sret, videopipe);
    }

    // print out
    if (verbose_flag)
        tc_log_info(MOD_NAME, "%s", import_cmd_buf);

    param->fd = popen(import_cmd_buf, "r");
    RETURN_IF_OPEN_FAILED(videopipefd, "popen YUV stream");

    return TC_IMPORT_OK;
}

static int tc_mplayer_open_audio(vob_t *vob, transfer_t *param)
{
    char import_cmd_buf[TC_BUF_MAX];
    long sret = 0;

    tc_snprintf(audiopipe, sizeof(audiopipe), AUDIOPIPE_TEMPLATE);
    if (!mktemp(audiopipe)) {
        tc_log_perror(MOD_NAME, "mktemp audiopipe failed");
        return(TC_IMPORT_ERROR);
    }
    if (mkfifo(audiopipe, 00660) == -1) {
        tc_log_perror(MOD_NAME, "mkfifo audio failed");
        unlink(audiopipe);
        return(TC_IMPORT_ERROR);
    }

    sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
                       "mplayer -slave -hardframedrop -vo null -ao pcm:nowaveheader"
                       ":file=\"%s\" %s \"%s\" > /dev/null 2>&1",
                       audiopipe, (vob->im_a_string ? vob->im_a_string : ""),
                       vob->audio_in_file);
    RETURN_IF_BAD_SRET(sret, audiopipe);

    if (verbose_flag)
        tc_log_info(MOD_NAME, "%s", import_cmd_buf);

    audiopipefd = popen(import_cmd_buf, "w");
    RETURN_IF_OPEN_FAILED(audiopipefd, "popen audiopipe failed");

    /* 
     * XXX
     * ok, this is really an ugly *temporary* hack that make things work.
     * I'm not proud nor satisfied of this, but there isn't much that
     * better this moment.
     */
    sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
                       "%s -i %s -x pcm -t raw",
                       TCEXTRACT_EXE, audiopipe);
    RETURN_IF_BAD_SRET(sret, audiopipe);

    // print out
    if (verbose_flag)
        tc_log_info(MOD_NAME, "%s", import_cmd_buf);

    param->fd = popen(import_cmd_buf, "r");
    RETURN_IF_OPEN_FAILED(audiopipefd, "popen PCM stream");

    return TC_IMPORT_OK;
}

/*
 * OK, there is a nasty deadlocks when dealing with (audio) pipe
 * hidden and buried in this module internals.
 * short history:
 * - mplayer keeps writing data on the FIFO but
 * - transcode stops reading from FIFO, so
 * - FIFO buffer eventually become full and
 * - mplayer blocks, so cannot terminate, but
 * - transcode waits for mplayer termination:
 * - DEADLOCK!
 *
 * possible workaround:
 * static void tc_mplayer_send_quit(FILE *fd)
 * {
 *     fprintf(fd, "quit\n");
 *     fflush(fd);
 * }
 *
 * and invoke it in close* functions
 */

static int tc_mplayer_close_video(transfer_t *param)
{
    if (param->fd != NULL) {
        pclose(param->fd);
    }
    if (videopipefd != NULL) {
        pclose(videopipefd);
        videopipefd = NULL;
    }
    unlink(videopipe);
    return TC_IMPORT_OK; 
}

static int tc_mplayer_close_audio(transfer_t *param)
{
    if (param->fd != NULL)
        pclose(param->fd);
    if (audiopipefd != NULL) {
        pclose(audiopipefd);
        audiopipefd = NULL;
    }
    unlink(audiopipe);
    return TC_IMPORT_OK;
}

/* ------------------------------------------------------------
 * main external API.
 * ------------------------------------------------------------*/

MOD_open
{
    /* check for mplayer */
    if (tc_test_program("mplayer") != 0) {
        return TC_IMPORT_ERROR;
    }
    if (param->flag == TC_VIDEO) {
        return tc_mplayer_open_video(vob, param);
    }
    if (param->flag == TC_AUDIO) {
        return tc_mplayer_open_audio(vob, param);
    }
    return TC_IMPORT_ERROR;
}


MOD_decode
{
    return TC_IMPORT_OK;
}

MOD_close
{
    if (param->flag == TC_VIDEO) {
        return tc_mplayer_close_video(param);
    }
    if (param->flag == TC_AUDIO) {
        return tc_mplayer_close_audio(param);
    }
    return TC_IMPORT_ERROR;
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
