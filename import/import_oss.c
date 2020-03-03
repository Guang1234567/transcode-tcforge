/*
 *  import_oss.c
 *
 *  Copyright (C) Jacob Meuser - September 2004
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

#define MOD_NAME    "import_oss.so"
#define MOD_VERSION "v0.0.3 (2007-11-18)"
#define MOD_CODEC   "(audio) pcm"

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_PCM;

#define MOD_PRE oss
#include "import_def.h"

#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_SOUNDCARD_H
# include <sys/soundcard.h>
#else
# ifdef HAVE_SOUNDCARD_H
#  include <soundcard.h>
# endif
#endif

#include "libtcutil/optstr.h"

static int oss_fd = -1;

static int oss_init(const char *, int, int, int);
static int oss_grab(size_t, uint8_t *);
static int oss_stop(void);


static int oss_init(const char *audio_device,
                    int sample_rate, int precision, int channels)
{
    int encoding, rate = sample_rate;

    if (!strcmp(audio_device, "/dev/null")
     || !strcmp(audio_device, "/dev/zero")) {
        return TC_IMPORT_OK;
    }

    if (precision != 8 && precision != 16) {
        tc_log_warn(MOD_NAME, "bits/sample must be 8 or 16");
        return TC_IMPORT_ERROR;
    }

    encoding = (precision == 8) ? AFMT_U8 : AFMT_S16_LE;

    if ((oss_fd = open(audio_device, O_RDONLY)) < 0) {
        tc_log_perror(MOD_NAME, "open audio device");
        return TC_IMPORT_ERROR;
    }

    if (ioctl(oss_fd, SNDCTL_DSP_SETFMT, &encoding) < 0) {
        tc_log_perror(MOD_NAME, "SNDCTL_DSP_SETFMT");
        return TC_IMPORT_ERROR;
    }

    if (ioctl(oss_fd, SNDCTL_DSP_CHANNELS, &channels) < 0) {
        tc_log_perror(MOD_NAME, "SNDCTL_DSP_CHANNELS");
        return TC_IMPORT_ERROR;
    }


    if (ioctl(oss_fd, SNDCTL_DSP_SPEED, &rate) < 0) {
        tc_log_perror(MOD_NAME, "SNDCTL_DSP_SPEED");
        return TC_IMPORT_ERROR;
    }
    if (rate != sample_rate) {
        tc_log_warn(MOD_NAME, "sample rate requested=%i obtained=%i",
                              sample_rate, rate);
    }

    return TC_IMPORT_OK;
}

static int oss_grab(size_t size, uint8_t *buffer)
{
    int left;
    int offset;
    int received;

    for (left = size, offset = 0; left > 0;) {
        received = read(oss_fd, buffer + offset, left);
        if (received == 0) {
            tc_log_warn(MOD_NAME, "audio grab: received == 0");
        }
        if (received < 0) {
            if (errno == EINTR) {
                received = 0;
            } else {
                tc_log_perror(MOD_NAME, "audio grab");
                return TC_IMPORT_ERROR;
            }
        }
        if (received > left) {
            tc_log_warn(MOD_NAME,
                        "read returns more bytes than requested; "
                        "requested: %d, returned: %d",
                        left, received);
            return TC_IMPORT_ERROR;
        }
        offset += received;
        left -= received;
    }
    return TC_IMPORT_OK;
}

static int oss_stop(void)
{
    close(oss_fd);
    oss_fd = -1;

    if (verbose_flag & TC_STATS) {
        tc_log_warn(MOD_NAME, "totals: (not implemented)");
    }

    return TC_IMPORT_OK;
}


/* ------------------------------------------------------------
 * Module interface
 * ------------------------------------------------------------*/

MOD_open
{
    int ret = TC_IMPORT_OK;

    switch (param->flag) {
      case TC_VIDEO:
        tc_log_warn(MOD_NAME, "unsupported request (init video)");
        ret = TC_IMPORT_ERROR;
        break;
      case TC_AUDIO:
        if (verbose_flag & TC_DEBUG) {
            tc_log_info(MOD_NAME, "OSS audio grabbing");
        }
        if (oss_init(vob->audio_in_file,
                     vob->a_rate, vob->a_bits, vob->a_chan)) {
            ret = TC_IMPORT_ERROR;
        }
        break;
      default:
        tc_log_warn(MOD_NAME, "unsupported request (init)");
        ret = TC_IMPORT_ERROR;
        break;
    }

    return ret;
}


MOD_decode
{
    int ret = TC_IMPORT_OK;

    switch (param->flag) {
      case TC_VIDEO:
        tc_log_warn(MOD_NAME, "unsupported request (decode video)");
        ret = TC_IMPORT_ERROR;
        break;
      case TC_AUDIO:
        if (oss_grab(param->size, param->buffer)) {
            tc_log_warn(MOD_NAME, "error in grabbing audio");
            ret = TC_IMPORT_ERROR;
        }
        break;
      default:
        tc_log_warn(MOD_NAME, "unsupported request (decode)");
        ret = TC_IMPORT_ERROR;
        break;
    }

    return ret;
}


MOD_close
{
    int ret = TC_IMPORT_OK;

    switch (param->flag) {
      case TC_VIDEO:
        tc_log_warn(MOD_NAME, "unsupported request (close video)");
        ret = TC_IMPORT_ERROR;
        break;
      case TC_AUDIO:
        oss_stop();
        break;
      default:
        tc_log_warn(MOD_NAME, "unsupported request (close)");
        ret = TC_IMPORT_ERROR;
        break;
    }

    return ret;
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

