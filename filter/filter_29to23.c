/*
 *  filter_29to23.c
 *
 *  Copyright (C) Tilmann Bitterberg - September 2002
 *            (C) Max Alekseyev      - July 2003
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

#define MOD_NAME    "filter_29to23.so"
#define MOD_VERSION "v0.3 (2003-07-18)"
#define MOD_CAP     "frame rate conversion filter (interpolating 29 to 23)"
#define MOD_AUTHOR  "Tilmann Bitterberg, Max Alekseyev"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/


static unsigned char *f1 = NULL;
static unsigned char *f2 = NULL;

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  static vob_t *vob=NULL;

  if (ptr->tag & TC_AUDIO) return 0;

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_GET_CONFIG && options) {

    optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "Max Alekseyev, Tilmann Bitterberg", "VRYE", "1");

    return 0;
  }

  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL) return(-1);

    // filter init ok.

    f1 = tc_malloc (SIZE_RGB_FRAME);
    f2 = tc_malloc (SIZE_RGB_FRAME);

    if (!f1 || !f2) {
	    tc_log_error(MOD_NAME, "Malloc failed in %d", __LINE__);
	    return -1;
    }

    if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

    if(verbose) tc_log_info(MOD_NAME, "options=%s", options);

    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE) {

      if (f1) free (f1);
      if (f2) free (f2);
      return(0);
  }

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------

  /* Interpolate every 5 consecutive frames with four ones
   *
   * Original frame/time scale:
   * frame #: 0       1       2       3       4
   *          *-------*-------*-------*-------*--...
   * time:    0      .2      .4      .6      .8
   *
   * Interpolated frame/time scale:
   * frame #: 0         1         2         3
   *          *-------*-x-----*---x---*-----x-*--...
   * time:    0        .25       .5        .75
   *
   * Linear interpolation suggests:
   * NewFrame[0] = OldFrame[0]
   * NewFrame[1] = ( 3*OldFrame[1] +   OldFrame[2] ) / 4
   * NewFrame[2] = (   OldFrame[2] +   OldFrame[3] ) / 2
   * NewFrame[3] = (   OldFrame[3] + 3*OldFrame[4] ) / 4
   */

  if( ptr->tag & TC_PRE_S_PROCESS &&
     (vob->im_v_codec == TC_CODEC_YUV420P || vob->im_v_codec == TC_CODEC_RGB24) ) {

      int i;
      unsigned char *f3;

      switch ( ptr->id % 5 ) {

          case 0:
              break;

          case 1:
	      ac_memcpy (f1, ptr->video_buf, ptr->video_size);
	      ptr->attributes |= TC_FRAME_IS_SKIPPED;
	      break;

          case 2:
	      ac_memcpy (f2, ptr->video_buf, ptr->video_size);
	      for (i = 0; i<ptr->video_size; i++)
		  ptr->video_buf[i] = (3*f1[i] + f2[i] + 1)/4;
	      break;

          case 3:
	      ac_memcpy (f1, ptr->video_buf, ptr->video_size);
	      for (i = 0; i<ptr->video_size; i++)
		  ptr->video_buf[i] = (f1[i] + f2[i])/2;
	      break;

          case 4:
	      //ac_memcpy (f2, ptr->video_buf, ptr->video_size);
              f3 = ptr->video_buf;
	      for (i = 0; i<ptr->video_size; i++)
		  ptr->video_buf[i] = (f1[i] + 3*f3[i] + 1)/4;
	      break;
      }
  }
  return(0);
}
