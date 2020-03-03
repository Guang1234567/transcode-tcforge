/*
 *  runcontrol.c -- asynchronous encoder runtime control - simplified.
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Updated and partially rewritten by
 *  Francesco Romani - January 2006
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include "libtc/libtc.h"
#include "tccore/runcontrol.h"


/*************************************************************************/

int tc_running(void)
{
    return 1;
}

/*************************************************************************/

static void tc_rc_pause(TCRunControl *RC)
{
    return; /* do nothing */
}

static TCRunStatus tc_rc_status(TCRunControl *RC)
{
    return TC_STATUS_RUNNING;
}

static void tc_rc_progress(TCRunControl *RC,
                           int encoding, int frame, int first, int last)
{
    fprintf(stderr, "%s frame %d\r",
            encoding ? "encoding" : "skipping",
            frame);
}

static TCRunControl RC = {
    .priv     = NULL,
    .pause    = tc_rc_pause,
    .status   = tc_rc_status,
    .progress = tc_rc_progress
};

int tc_runcontrol_init(void)
{
    return TC_OK;
}

int tc_runcontrol_fini(void)
{
    return TC_OK;
}

TCRunControl *tc_runcontrol_get_instance(void)
{
    return &RC;
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
