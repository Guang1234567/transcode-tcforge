/*
 *  runcontrol.c -- asynchronous encoder runtime control.
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
#include "tccore/tc_defaults.h" /* TC_DELAY_MIN */
#include "counter.h"


/* volatile: for threadness paranoia */
static int pause_flag = 0;


void tc_pause_request(void)
{
    pause_flag = !pause_flag;
}

void tc_pause(void)
{
    while (pause_flag) {
    	usleep(TC_DELAY_MIN);
    }
}


/*************************************************************************/

static TCMutex run_status_lock;
static volatile int tc_run_status = TC_STATUS_RUNNING;
/* `volatile' is for threading paranoia */

static TCRunStatus tc_get_run_status(void)
{
    TCRunStatus rs;
    tc_mutex_lock(&run_status_lock);
    rs = tc_run_status;
    tc_mutex_unlock(&run_status_lock);
    return rs;
}

int tc_interrupted(void)
{
    return (TC_STATUS_INTERRUPTED == tc_get_run_status());
}

int tc_stopped(void)
{
    return (TC_STATUS_STOPPED == tc_get_run_status());
}

int tc_running(void)
{
    return (TC_STATUS_RUNNING == tc_get_run_status());
}

void tc_start(void)
{
    tc_mutex_lock(&run_status_lock);
    tc_run_status = TC_STATUS_RUNNING;
    tc_mutex_unlock(&run_status_lock);
}

void tc_stop(void)
{
    tc_mutex_lock(&run_status_lock);
    /* no preemption, be polite */
    if (tc_run_status == TC_STATUS_RUNNING) {
        tc_run_status = TC_STATUS_STOPPED;
    }
    tc_mutex_unlock(&run_status_lock);
}

void tc_interrupt(void)
{
    tc_mutex_lock(&run_status_lock);
    /* preempt and don't care of politeness. */
    if (tc_run_status != TC_STATUS_INTERRUPTED) {
        tc_run_status = TC_STATUS_INTERRUPTED;
    }
    tc_mutex_unlock(&run_status_lock);
}

/*************************************************************************/

static void tc_rc_pause(TCRunControl *RC)
{
    tc_pause();
}

static TCRunStatus tc_rc_status(TCRunControl *RC)
{
    return tc_get_run_status();
}

static void tc_rc_progress(TCRunControl *RC,
                           int encoding, int frame, int first, int last)
{
    counter_print(encoding, frame, first, last);
}

static TCRunControl RC = {
    .priv     = NULL,
    .pause    = tc_rc_pause,
    .status   = tc_rc_status,
    .progress = tc_rc_progress
};

int tc_runcontrol_init(void)
{
    tc_mutex_init(&run_status_lock);
    tc_run_status = TC_STATUS_RUNNING;

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
