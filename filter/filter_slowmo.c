/*
 *  filter_slowmo.c
 *
 *  Copyright (C) Thomas Oestreich - August 2002
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

#define MOD_NAME    "filter_slowmo.so"
#define MOD_VERSION "v0.3.1 (2006-09-10)"
#define MOD_CAP     "very cheap slow-motion effect"
#define MOD_AUTHOR  "Tilmann Bitterberg"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

static void help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help", MOD_CAP);
    tc_log_info(MOD_NAME,
"\n* Overview\n"
"   This filter produces a simple slow-motion effect by\n"
"   duplicating certain frames. I have seen this effect\n"
"   on TV and despite its the simple algorithm it works\n"
"   quite well. The filter has no options.\n");
}

static int do_clone(int id)
{
    static int last = 0;

    if ((id) % 3 == 0) {
        last = 0;
        return 1;
    }
    if (last > 0) {
        last--;
        return 0;
    }
    if (last == 0) {
        last = -1;
        return 1;
    }
    return 0;
}

static inline int slowmo_init(const char *options)
{
    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    if (options != NULL) {
        if (verbose) {
            tc_log_info(MOD_NAME, "options=%s", options);
        }
        if (optstr_lookup(options, "help") != NULL) {
            help_optstr();
        }
    }
    return 0;
}
 
static inline int slowmo_exec(vframe_list_t *ptr)
{
    /*
     * tag variable indicates, if we are called before
     * transcodes internal video/audo frame processing routines
     * or after and determines video/audio context
     *
     *  1 <-
     *  2 <-
     *  3 = 2
     *  4 <-
     *  5 = 4
     *  6 <-
     *  7 <-
     *  8 = 7
     *  9 <-
     * 10 = 9
     * 11 <-
     * 12 <-
     * 13 = 12
     * 14 <-
     * 15 = 14
     */

    if (ptr->tag & TC_PRE_S_PROCESS && ptr->tag & TC_VIDEO) {
        if (!(ptr->tag & TC_FRAME_WAS_CLONED) && do_clone(ptr->id))  {
	        ptr->attributes |= TC_FRAME_IS_CLONED;
        }
    }
    return 0;
}

int tc_filter(frame_list_t *ptr_, char *options)
{
    vframe_list_t *ptr = (vframe_list_t*)ptr_;

    if (ptr->tag & TC_FILTER_INIT) {
        return slowmo_init(options);
    }

    if (ptr->tag & TC_FILTER_CLOSE) {
        return 0;
    }

    if (ptr->tag & TC_FILTER_GET_CONFIG) {
        optstr_filter_desc(options, MOD_NAME, MOD_CAP,
                           MOD_VERSION, MOD_AUTHOR, "VRYE", "1");
        return 0;
    }

    return slowmo_exec(ptr);
}

