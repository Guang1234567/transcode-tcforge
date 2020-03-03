/*
 *  probe_sunau.c
 *
 *  Copyright (C) Jacob Meuser - December 2004
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

#include "tccore/tcinfo.h"
#include "src/transcode.h"
#include "ioaux.h"
#include "tc.h"
#include "libtc/libtc.h"

#ifdef HAVE_OSS

#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_SOUNDCARD_H
# include <sys/soundcard.h>
#else
# ifdef HAVE_SOUNDCARD_H
#  include <soundcard.h>
# endif
#endif


void
probe_oss(info_t * ipipe)
{
int encodings;
int encoding;
int precision;
int channels;
int sample_rate;

    close(ipipe->fd_in);
    ipipe->fd_in = open(ipipe->name, O_RDONLY, 0);
    if (ipipe->fd_in < 0) {
	tc_log_error(__FILE__, "cannot (re)open device: %s", strerror(errno));
	goto error;
    }

    /* try tc's defaults */
    encoding = AFMT_S16_LE;
    precision = 16;
    channels = 2;
    sample_rate = 48000;

    if (ioctl(ipipe->fd_in, SNDCTL_DSP_GETFMTS, &encodings) < 0) {
        tc_log_perror(__FILE__, "SNDCTL_DSP_SETFMT");
        goto error;
    }
    if (encodings & AFMT_S16_LE) {
        if (ioctl(ipipe->fd_in, SNDCTL_DSP_SETFMT, &encoding) < 0) {
            if (encodings & AFMT_U8) {
                encoding = AFMT_U8;
                precision = 8;
                if (ioctl(ipipe->fd_in, SNDCTL_DSP_SETFMT, &encoding) < 0) {
                    tc_log_perror(__FILE__, "SNDCTL_DSP_SETFMT");
                    goto error;
                }
            }
        }
    }

    if (ioctl(ipipe->fd_in, SNDCTL_DSP_CHANNELS, &channels) < 0) {
        tc_log_perror(__FILE__, "SNDCTL_DSP_CHANNELS");
        goto error;
    }

    if (ipipe->verbose & TC_DEBUG)
	tc_log_msg(__FILE__, "checking for valid samplerate...");
    if (ioctl(ipipe->fd_in, SNDCTL_DSP_SPEED, &sample_rate) < 0) {
        sample_rate = 44100;
        if (ioctl(ipipe->fd_in, SNDCTL_DSP_SPEED, &sample_rate) < 0) {
            sample_rate = 32000;
            if (ioctl(ipipe->fd_in, SNDCTL_DSP_SPEED, &sample_rate) < 0) {
                sample_rate = 22050;
                if (ioctl(ipipe->fd_in, SNDCTL_DSP_SPEED, &sample_rate) < 0) {
                    sample_rate = 24000;
                    if (ioctl(ipipe->fd_in, SNDCTL_DSP_SPEED, &sample_rate) < 0) {
                        sample_rate = 16000;
                        if (ioctl(ipipe->fd_in, SNDCTL_DSP_SPEED, &sample_rate) < 0) {
                            sample_rate = 11025;
                            if (ioctl(ipipe->fd_in, SNDCTL_DSP_SPEED, &sample_rate) < 0) {
                                if (ipipe->verbose & TC_DEBUG)
                                    tc_log_msg(__FILE__, "... not found");
                                goto error;
                            }
                        }
                    }
                }
            }
        }
    }
    if (ipipe->verbose & TC_DEBUG)
        tc_log_msg(__FILE__, "... found %d", sample_rate);

    ipipe->probe_info->track[0].bits = precision;
    ipipe->probe_info->track[0].chan = channels;
    ipipe->probe_info->track[0].samplerate = sample_rate;
    ipipe->probe_info->track[0].format = 0x1;

    if (ipipe->probe_info->track[0].chan > 0)
        ipipe->probe_info->num_tracks = 1;

    ipipe->probe_info->magic = TC_MAGIC_OSS_AUDIO;
    ipipe->probe_info->codec = TC_CODEC_PCM;

    return;

error:
    ipipe->error = 1;
    ipipe->probe_info->codec = TC_CODEC_UNKNOWN;
    ipipe->probe_info->magic = TC_MAGIC_UNKNOWN;

    return;

}

#else			/* HAVE_OSS */

void
probe_oss(info_t * ipipe)
{
    tc_log_error(__FILE__, "No support for oss compiled in");
    ipipe->probe_info->codec = TC_CODEC_UNKNOWN;
    ipipe->probe_info->magic = TC_MAGIC_UNKNOWN;
}

#endif
