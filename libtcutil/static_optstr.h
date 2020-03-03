/*
 * static_optstr.h - static linkage helper for optstr.
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef STATIC_OPTSTR_H
#define STATIC_OPTSTR_H

#include "libtcutil/optstr.h"
void dummy_optstr(void);
void dummy_optstr(void)
{
  optstr_lookup(NULL, NULL);
  optstr_get(NULL, NULL, NULL);
  optstr_filter_desc(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  optstr_frames_needed(NULL, NULL);
  optstr_param(NULL, NULL, NULL, NULL, NULL);
}

#endif /* STATIC_OPTSTR_H */
