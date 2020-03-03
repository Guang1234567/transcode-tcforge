/*
 *  probe_bktr.c
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

#ifdef HAVE_BKTR

#include <sys/ioctl.h>

#ifdef HAVE_DEV_IC_BT8XX_H
#include <dev/ic/bt8xx.h>
#endif
#ifdef HAVE_DEV_BKTR_IOCTL_BT848_H
#include <dev/bktr/ioctl_meteor.h>
#include <dev/bktr/ioctl_bt848.h>
#endif
#ifdef HAVE_MACHINE_IOCTL_BT848_H
#include <machine/ioctl_meteor.h>
#include <machine/ioctl_bt848.h>
#endif


void
probe_bktr(info_t * ipipe)
{
    struct bktr_capture_area caparea;
    unsigned short status, fps;

    close(ipipe->fd_in);
    ipipe->fd_in = open(ipipe->name, O_RDONLY, 0);
    if (ipipe->fd_in < 0) {
	tc_log_error(__FILE__, "cannot open device: %s", strerror(errno));
	goto error;
    }

    /* try a bktr ioctl */
    if (ipipe->verbose & TC_DEBUG)
	tc_log_msg(__FILE__, "checking if bktr ioctls are supported...");
    if (ioctl(ipipe->fd_in, METEORSTATUS, &status) < 0) {
	if (ipipe->verbose & TC_DEBUG)
	    tc_log_msg(__FILE__, "... no");
	goto error;
    } else {
        if (ipipe->verbose & TC_DEBUG)
            tc_log_msg(__FILE__, "... yes");
    }

    if (ioctl(ipipe->fd_in, BT848_GCAPAREA, &caparea) < 0) {
	tc_log_perror(__FILE__, "BT848_GCAPAREA");
        goto error;
    }
    ipipe->probe_info->width = caparea.x_size;
    ipipe->probe_info->height = caparea.y_size;

    if (ioctl(ipipe->fd_in, METEORGFPS, &fps) < 0) {
	tc_log_perror(__FILE__, "METEORGFPS");
        goto error;
    }
    ipipe->probe_info->fps = fps;
    switch(fps) {
        case 30:
            ipipe->probe_info->frc = 4;
            break;
        case 25:
            ipipe->probe_info->frc = 3;
            break;
        default:
            break;
    }

    ipipe->probe_info->magic = TC_MAGIC_BKTR_VIDEO;

    return;

error:
    ipipe->error = 1;
    ipipe->probe_info->codec = TC_CODEC_UNKNOWN;
    ipipe->probe_info->magic = TC_MAGIC_UNKNOWN;

    return;

}

#else			/* HAVE_BKTR */

void
probe_bktr(info_t * ipipe)
{
    tc_log_error(__FILE__, "No support for bktr compiled in");
    ipipe->probe_info->codec = TC_CODEC_UNKNOWN;
    ipipe->probe_info->magic = TC_MAGIC_UNKNOWN;
}

#endif
