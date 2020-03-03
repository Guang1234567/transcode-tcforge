/*
 *  filter_aclip.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
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

#define MOD_NAME    "filter_aclip.so"
#define MOD_VERSION "v0.1.1 (2003-09-04)"
#define MOD_CAP     "generate audio clips from source"
#define MOD_AUTHOR  "Thomas Oestreich"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtcutil/optstr.h"

#include <math.h>


static uint64_t total=0;

static int level=10, range=25, range_ctr=0, skip_mode=0;

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

static int is_optstr(char *a)
{
    if (strchr(a, '=')) return 1;
    if (strchr(a, 'h')) return 1;
    return 0;
}

int tc_filter(frame_list_t *ptr_, char *options)
{
  aframe_list_t *ptr = (aframe_list_t *)ptr_;
  int n;
  double sum;

  short *s;

  vob_t *vob=NULL;

  if(ptr->tag & TC_FILTER_GET_CONFIG) {
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "Thomas Oestreich", "AE", "1");
      optstr_param (options, "level", "The audio must be under this level to be skipped", "%d", "10", "0", "255");
      optstr_param (options, "range", "Number of samples over level will be keyframes", "%d", "25", "0", "255");
      return 0;
  }

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL) return(-1);

    // filter init ok.

    if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

    if(verbose) tc_log_info(MOD_NAME, "options=%s", options);

    if(options!=NULL) {
	if (!is_optstr(options)) {
	    n=sscanf(options,"%d:%d", &level, &range);
	} else {
	    optstr_get (options, "level", "%d", &level);
	    optstr_get (options, "range", "%d", &range);
	}
    }

    range_ctr=range;

    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_CLOSE) {

    return(0);
  }

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------

  if(verbose & TC_STATS)
    tc_log_info(MOD_NAME, "%s/%s %s %s",
                vob->mod_path, MOD_NAME, MOD_VERSION, MOD_CAP);

  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  if(ptr->tag & TC_PRE_S_PROCESS && ptr->tag & TC_AUDIO && !(ptr->attributes & TC_FRAME_IS_SKIPPED)) {

    total += (uint64_t) ptr->audio_size;

    s=(short *) ptr->audio_buf;

    sum=0;

    for(n=0; n<ptr->audio_size>>1; ++n) {
      sum+=(double) ((int)(*s) * (int)(*s));
      s++;
    }

    if(ptr->audio_size>0) sum = sqrt(sum)/(ptr->audio_size>>1);

    sum *= 1000;

    if(verbose & TC_DEBUG) tc_log_info(MOD_NAME, "frame=%d sum=%f", ptr->id, sum);

    if(sum<level) {

      if(range_ctr == range) {

	ptr->attributes |= TC_FRAME_IS_SKIPPED;
	skip_mode=1;
      } else ++range_ctr;

    } else {

      if(skip_mode) ptr->attributes |= TC_FRAME_IS_KEYFRAME;
      skip_mode = 0;
      range_ctr = 0;
    }
  }
  return(0);
}
