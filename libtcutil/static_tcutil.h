/*
 * static_tcutils.h - static linkage helper for tcutils
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef STATIC_TCUTIL_H
#define STATIC_TCUTIL_H

#include "libtcutil/optstr.h"
#include "libtcutil/ioutils.h"
#include "libtcutil/memutils.h"
#include "libtcutil/strutils.h"
#include "libtcutil/tclist.h"

void dummy_tcutil(void);
void dummy_tcutil(void)
{
    optstr_param(NULL, NULL, NULL, NULL, NULL);
    tc_test_string(__FILE__, __LINE__, 0, 0, 0);
    tc_test_program(NULL); 
    tc_free(NULL);

    strlcat(NULL, NULL, 0);
    strlcpy(NULL, NULL, 0);

    tc_list_init(NULL, 0);
}

#endif /* STATIC_TCUTIL_H */
