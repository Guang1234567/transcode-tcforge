/*
 *  probe_bsdav.c
 *
 *  Copyright (C) Jacob Meuser <jakemsr@jakemsr.com> - May 2005
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
#include "libtc/ratiocodes.h"

#ifdef HAVE_BSDAV

#include <bsdav.h>

void probe_bsdav(info_t *ipipe)
{
    struct bsdav_stream_header strhdr;
    FILE *file;

    if ((file = fdopen(ipipe->fd_in, "r")) == NULL) {
        tc_log_error(__FILE__, "failed to fdopen bsdav stream");
        ipipe->error = 1;
        return;
    }

    /* read stream header */
    if (bsdav_read_stream_header(file, &strhdr) != 0) {
        tc_log_error(__FILE__, "failed to read bsdav stream header");
        ipipe->error = 1;
        return;
    }

    ipipe->probe_info->width = strhdr.vidwth;
    ipipe->probe_info->height = strhdr.vidhgt;
    ipipe->probe_info->track[0].samplerate = strhdr.audsrt;
    ipipe->probe_info->track[0].chan = strhdr.audchn;
    ipipe->probe_info->track[0].bits = bsdav_aud_fmts[strhdr.audfmt].bps;
    ipipe->probe_info->track[0].format = 0x1;

    ipipe->probe_info->magic = TC_MAGIC_BSDAV;

    switch (strhdr.vidfmt) {
    case BSDAV_VIDFMT_I420:
        ipipe->probe_info->codec = TC_CODEC_YUV420P;
        break;
    case BSDAV_VIDFMT_YUY2:
        ipipe->probe_info->codec = TC_CODEC_YUY2;
        break;
    case BSDAV_VIDFMT_UYVY:
        ipipe->probe_info->codec = TC_CODEC_UYVY;
        break;
    default:
        ipipe->probe_info->codec = TC_CODEC_UNKNOWN;
        break;
    }

    if (ipipe->probe_info->track[0].chan > 0)
        ipipe->probe_info->num_tracks = 1;

    if (fseek(file, 0, SEEK_SET) != 0) {
        tc_log_error(__FILE__, "failed to fseek bsdav stream");
        ipipe->error = 1;
        return;
    }

    ipipe->probe_info->fps = bsdav_probe_frame_rate(file,
      ipipe->factor * 1024 * 1024);

    tc_frc_code_from_value(&(ipipe->probe_info->frc),
                           ipipe->probe_info->fps);

    return;
}

#else	/* HAVE_BSDAV */

void
probe_bsdav(info_t * ipipe)
{
    tc_log_error(__FILE__, "No support for bsdav compiled in");
    ipipe->probe_info->codec = TC_CODEC_UNKNOWN;
    ipipe->probe_info->magic = TC_MAGIC_UNKNOWN;
}


#endif
