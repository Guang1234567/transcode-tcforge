/*
 * tctimer.h -- simple timer code for transcode.
 * (C) 2006-2010 - Francesco Romani <fromani -at- gmail -dot- com>
 *
 * This file is part of transcode, a video stream processing tool.
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef TCTIMER_H
#define TCTIMER_H

#include "config.h"

#include <stdint.h>


/*
 * Quick Summary:
 *
 * At time of writing, {import,demultiplex}_x11 is the only
 * piece of transcode that uses timers.
 * I've chosen to factorize such code and put here on libtc
 * in order to make it more visible and to promote reviews.
 * 
 * This code may look overengineered, I'd like to make it generic
 * in order to easily introduce further, platform-specific, timing
 * support (i.e. Linux RTC). They aren't yet ready since the overall
 * X11 source support is still on work. More will follow soon.
 *
 */

/*
 * Time unit used: microseconds (1e-6)
 * It's EXPECTED that client code requires a timing resolution at least
 * one order of magnitude LESS precise than internal resolution, I.e.
 * milliseconds.
 */


/*
 * tc_gettime:
 *     return the current time using the best avalaible time source.
 *
 * Parameters:
 *     None.
 * Return Value:
 *     time units elapsed since EPOCH.
 */
uint64_t tc_gettime(void);

/*************************************************************************/


typedef struct tctimer_ TCTimer;
struct tctimer_ {
    uint64_t    last_time;
    /* timestamp of last timer reading */

    int         (*fini)(TCTimer *timer);
    uint64_t    (*elapsed)(TCTimer *timer);
    int         (*sleep)(TCTimer *timer, uint64_t amount);
};

int tc_timer_init_soft(TCTimer *timer, uint16_t frequency);

/*
 * tc_timer_fini:
 *     finalize given timer by freeing all resources acquired.
 *
 * Parameters:
 *     timer: timer to finalize.
 * Return Value:
 *     0 : succesfull.
 *     -1: error.
 */
int tc_timer_fini(TCTimer *timer);

/*
 * tc_timer_elapsed:
 *     read timer status and get the amount of time units
 *     elapsed *SINCE LAST READ*.
 *     First read automagically delivers right results,
 *     so client code hasn't to worry about this.
 *
 * Parameters:
 *     timer: timer to read.
 * Return Value:
 *     time units elapsed since last reading.
 * Side Effects:
 *     Update internal timestamp.
 */
uint64_t tc_timer_elapsed(TCTimer *timer);

/*
 * tc_timer_sleep:
 *     blocks caller (thread) for given amount of time units.
 *
 *     *PLEASE NOTE*
 *     that this function CAN'T guarantee STRICT observancy of
 *     sleeping time. It is very likely that blocking time is 
 *     different (usually greater) than wanted.
 *     Providing more guarantees involve deeper interaction with
 *     host OS that is out of the scope of this code, yet.
 *
 * Parameters:
 *      timer: timer to use.
 *     amount: (try to) block caller for this amount of time units.
 * Return Value:
 *      0: succesfully. Blocked for given amount of time units
 *         (see note above)
 *     -1: failed: an error has caused premature return.
 */
int tc_timer_sleep(TCTimer *timer, uint64_t amount);

#endif /* TCTIMER_H */
