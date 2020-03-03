/*
 * static_tctimer.h - static linkage helper for tctimer.
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef STATIC_TIMER_H
#define STATIC_TIMER_H

#include "libtcutil/tctimer.h"
void dummy_tctimer(void);
void dummy_tctimer(void) 
{
    TCTimer t;

    tc_timer_init_soft(&t, 0);
    tc_timer_fini(&t);
}

#endif /* STATIC_TIMER_H */
