/*
 *  import_sunau.c
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

#define MOD_NAME	"import_sunau.so"
#define MOD_VERSION	"v0.0.2 (2004-10-02)"
#define MOD_CODEC	"(audio) pcm"

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_PCM;

#define MOD_PRE sunau
#include "import_def.h"

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>

#include "libtcutil/optstr.h"


static int sunau_fd = -1;

int sunau_init(const char *, int, int, int);
int sunau_grab(size_t, char *);
int sunau_stop(void);


int sunau_init(const char *audio_device,
                    int sample_rate, int precision, int channels)
{
    audio_info_t audio_if;
    int encoding;

    if(!strcmp(audio_device, "/dev/null") || !strcmp(audio_device, "/dev/zero"))
        return(0);

    if(precision != 8 && precision != 16) {
        tc_log_warn(MOD_NAME,
            "bits/sample must be 8 or 16");
        return(1);
    }

    encoding = (precision == 8) ?
        AUDIO_ENCODING_ULINEAR : AUDIO_ENCODING_SLINEAR_LE;

    AUDIO_INITINFO(&audio_if);

    audio_if.record.precision = precision;
    audio_if.record.channels = channels;
    audio_if.record.sample_rate = sample_rate;
    audio_if.record.encoding = encoding;

    audio_if.mode = AUMODE_RECORD;

    if ((sunau_fd = open(audio_device, O_RDONLY)) < 0) {
        tc_log_perror(MOD_NAME, MOD_NAME "open audio device");
        return(1);
    }

    if (ioctl(sunau_fd, AUDIO_SETINFO, &audio_if) < 0) {
        tc_log_perror(MOD_NAME, "AUDIO_SETINFO");
        return(1);
    }

    if (ioctl(sunau_fd, AUDIO_GETINFO, &audio_if) < 0) {
        tc_log_perror(MOD_NAME, "AUDIO_GETINFO");
        return(1);
    }

    if (audio_if.record.precision != precision) {
        tc_log_warn(MOD_NAME,
            "unable to initialize sample size for %s; "
            "tried %d, got %d",
            audio_device, precision, audio_if.record.precision);
        return(1);
    }
    if (audio_if.record.channels != channels) {
        tc_log_warn(MOD_NAME,
            "unable to initialize number of channels for %s; "
            "tried %d, got %d",
            audio_device, channels, audio_if.record.channels);
        return(1);
    }
    if (audio_if.record.sample_rate != sample_rate) {
        tc_log_warn(MOD_NAME,
            "unable to initialize rate for %s; "
            "tried %d, got %d\n",
            audio_device, sample_rate, audio_if.record.sample_rate);
        return(1);
    }
    if (audio_if.record.encoding != encoding) {
        tc_log_warn(MOD_NAME,
            "unable to initialize encoding for %s; "
            "tried %d, got %d",
            audio_device, encoding, audio_if.record.encoding);
        return(1);
    }

    if (ioctl(sunau_fd, AUDIO_FLUSH) < 0) {
        tc_log_perror(MOD_NAME, "AUDIO_FLUSH");
        return(1);
    }

    return(0);
}

int sunau_grab(size_t size, char *buffer)
{
    int left;
    int offset;
    int received;

    for (left = size, offset = 0; left > 0;) {
        received = read(sunau_fd, buffer + offset, left);
        if (received == 0) {
            tc_log_warn(MOD_NAME,
                "audio grab: received == 0");
        }
        if (received < 0) {
            if(errno == EINTR) {
                received = 0;
            } else {
                tc_log_perror(MOD_NAME, MOD_NAME "audio grab");
                return(1);
            }
        }
        if (received > left) {
            tc_log_warn(MOD_NAME,
                "read returns more bytes than requested; "
                "requested: %d, returned: %d",
                left, received);
            return(1);
        }
        offset += received;
        left -= received;
    }
    return(0);
}

int sunau_stop(void)
{
    close(sunau_fd);
    sunau_fd = -1;

    if (verbose_flag & TC_STATS) {
        tc_log_warn(MOD_NAME,
            "totals: (not implemented)");
    }

    return(0);
}


/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
    int ret = TC_IMPORT_OK;

    switch (param->flag) {
      case TC_VIDEO:
        tc_log_warn(MOD_NAME,
            "unsupported request (init video)\n");
        ret = TC_IMPORT_ERROR;
        break;
      case TC_AUDIO:
        if (verbose_flag & TC_DEBUG) {
            tc_log_info(MOD_NAME,
                "sunau audio grabbing\n");
        }
        if (sunau_init(vob->audio_in_file,
                      vob->a_rate, vob->a_bits, vob->a_chan)) {
            ret = TC_IMPORT_ERROR;
        }
        break;
      default:
        tc_log_warn(MOD_NAME,
            "unsupported request (init)");
        ret = TC_IMPORT_ERROR;
        break;
    }

    return(ret);
}


/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

MOD_decode
{
    int ret = TC_IMPORT_OK;

    switch (param->flag) {
      case TC_VIDEO:
        tc_log_warn(MOD_NAME,
            "unsupported request (decode video)");
        ret = TC_IMPORT_ERROR;
        break;
      case TC_AUDIO:
        if (sunau_grab(param->size, param->buffer)) {
            tc_log_warn(MOD_NAME,
                "error in grabbing audio");
            ret = TC_IMPORT_ERROR;
        }
        break;
      default:
        tc_log_warn(MOD_NAME,
            "unsupported request (decode)");
        ret = TC_IMPORT_ERROR;
        break;
    }

    return(ret);
}

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
    int ret = TC_IMPORT_OK;

    switch (param->flag) {
      case TC_VIDEO:
        tc_log_warn(MOD_NAME,
            "unsupported request (close video)");
        ret = TC_IMPORT_ERROR;
        break;
      case TC_AUDIO:
        sunau_stop();
        break;
      default:
        tc_log_warn(MOD_NAME,
            "unsupported request (close)");
        ret = TC_IMPORT_ERROR;
        break;
    }

    return(ret);
}
