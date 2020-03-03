/*
 * static_tclist.h - static linkage helper for tclist.
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef STATIC_TCLIST_H
#define STATIC_TCLIST_H

#include "libtcutil/tclist.h"
void dummy_tclist(void);
void dummy_tclist(void) 
{
    tc_list_init(NULL, 0);
    tc_list_size(NULL);
    tc_list_fini(NULL);
}

#endif /* STATIC_TCLIST_H */
