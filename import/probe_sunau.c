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

#ifdef HAVE_SUNAU

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>


void
probe_sunau(info_t * ipipe)
{
    audio_info_t audio_if;

    close(ipipe->fd_in);
    ipipe->fd_in = open(ipipe->name, O_RDONLY, 0);
    if (ipipe->fd_in < 0) {
	tc_log_error(__FILE__, "cannot (re)open device: %s", strerror(errno));
	goto error;
    }

    AUDIO_INITINFO(&audio_if);

    /* try tc's defaults, we probably don't want 8000kHz 8 bit mono */
    audio_if.record.precision = 16;
    audio_if.record.channels = 2;
    audio_if.record.sample_rate = 48000;
    audio_if.record.encoding = AUDIO_ENCODING_SLINEAR_LE;
    audio_if.mode = AUMODE_RECORD;

    if (ipipe->verbose & TC_DEBUG)
	tc_log_msg(__FILE__, "checking for valid samplerate...");
    if (ioctl(ipipe->fd_in, AUDIO_SETINFO, &audio_if) < 0) {
        audio_if.record.sample_rate = 44100;
        if (ioctl(ipipe->fd_in, AUDIO_SETINFO, &audio_if) < 0) {
            audio_if.record.sample_rate = 32000;
            if (ioctl(ipipe->fd_in, AUDIO_SETINFO, &audio_if) < 0) {
                audio_if.record.sample_rate = 22050;
                if (ioctl(ipipe->fd_in, AUDIO_SETINFO, &audio_if) < 0) {
                    audio_if.record.sample_rate = 24000;
                    if (ioctl(ipipe->fd_in, AUDIO_SETINFO, &audio_if) < 0) {
                        audio_if.record.sample_rate = 16000;
                        if (ioctl(ipipe->fd_in, AUDIO_SETINFO, &audio_if) < 0) {
                            audio_if.record.sample_rate = 11025;
                            if (ioctl(ipipe->fd_in, AUDIO_SETINFO, &audio_if) < 0) {
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
        tc_log_msg(__FILE__, "... found %d", audio_if.record.sample_rate);

    if (ioctl(ipipe->fd_in, AUDIO_GETINFO, &audio_if) < 0) {
        tc_log_perror(__FILE__, "AUDIO_GETINFO");
	goto error;
    }

    ipipe->probe_info->track[0].bits = audio_if.record.precision;
    ipipe->probe_info->track[0].chan = audio_if.record.channels;
    ipipe->probe_info->track[0].samplerate = audio_if.record.sample_rate;
    ipipe->probe_info->track[0].format = 0x1;

    if (ipipe->probe_info->track[0].chan > 0)
        ipipe->probe_info->num_tracks = 1;

    ipipe->probe_info->magic = TC_MAGIC_SUNAU_AUDIO;
    ipipe->probe_info->codec = TC_CODEC_PCM;

    return;

error:
    ipipe->error = 1;
    ipipe->probe_info->codec = TC_CODEC_UNKNOWN;
    ipipe->probe_info->magic = TC_MAGIC_UNKNOWN;

    return;

}

#else			/* HAVE_SUNAU */

void
probe_sunau(info_t * ipipe)
{
    tc_log_error(__FILE__, "No support for sunau compiled in");
    ipipe->probe_info->codec = TC_CODEC_UNKNOWN;
    ipipe->probe_info->magic = TC_MAGIC_UNKNOWN;
}

#endif
