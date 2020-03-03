/*
 *  filter_32detect.c
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

#define MOD_NAME    "filter_32drop.so"
#define MOD_VERSION "v0.4 (2003-02-01)"
#define MOD_CAP     "3:2 inverse telecine removal plugin"
#define MOD_AUTHOR  "Chad Page"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include <stdint.h>

// basic parameter
static int color_diff_threshold1=50;
static int color_diff_threshold2=100;
static double critical_threshold=0.00005;

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

// static int sq( int d ) { return d * d; } // unused, EMS

static int interlace_test(char *video_buf, int width, int height, int id, int verbose)
{

    int j, n, off, block, cc_1, cc_2, cc;

    uint16_t s1, s2, s3, s4;

    cc_1 = 0;
    cc_2 = 0;

    block = width;

    for(j=0; j<block; ++j) {

	off=0;

	for(n=0; n<(height-4); n=n+2) {


	    s1 = (video_buf[off+j        ] & 0xff);
	    s2 = (video_buf[off+j+  block] & 0xff);
	    s3 = (video_buf[off+j+2*block] & 0xff);
	    s4 = (video_buf[off+j+3*block] & 0xff);

	    if((abs(s1 - s3) < color_diff_threshold1) &&
	       (abs(s1 - s2) > color_diff_threshold2)) ++cc_1;

	    if((abs(s2 - s4) < color_diff_threshold1) &&
	       (abs(s2 - s3) > color_diff_threshold2)) ++cc_2;

	    off +=2*block;
	}
    }

    // compare results

    cc = (((double) (cc_1 + cc_2))/(width*height) > critical_threshold) ? 1:0;

    return(cc);
}

static void merge_frames(unsigned char *f1, unsigned char *f2, int width, int height, int pw)
{
	int i;
	char *cbuf1, *cbuf2;

	/* In YUV, only merge the Y plane, since CrCb planes can't be discerned
	 * due to the merger.  This lets us also reuse the code for RGB */
	for (i = 0; i < height; i += 2) {
		ac_memcpy(&f2[i * width * pw], &f1[i * width * pw], width * pw);
	}

	/* If we're in YUV mode, the previous frame has the correct color data */
	if (pw == 1) {
		cbuf1 = &f1[height * width];
		cbuf2 = &f2[height * width];
		ac_memcpy(cbuf2, cbuf1, (height * width / 2));
	}
}

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  static vob_t *vob=NULL;
  static char *lastframe, *lastiframe;
  static int linum = -1, lfnum = -1, fnum = 0, isint = 0, dcnt = 0, dfnum = 0;

  if(ptr->tag & TC_FILTER_GET_CONFIG) {

    optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "Thomas Oestreich", "VRYE", "1");

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

    lastframe = tc_malloc(SIZE_RGB_FRAME);
    lastiframe = tc_malloc(SIZE_RGB_FRAME);

    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE) {
     free(lastframe);
     free(lastiframe);
     return(0);
  }

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------


  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  if((ptr->tag & TC_PRE_M_PROCESS) && (ptr->tag & TC_VIDEO))  {

    if(vob->im_v_codec==TC_CODEC_RGB24) {
      isint = interlace_test(ptr->video_buf, 3*ptr->v_width, ptr->v_height, ptr->id, 1);
    } else {
      isint = interlace_test(ptr->video_buf, ptr->v_width, ptr->v_height, ptr->id, 1);
    }

    //tc_log_msg(MOD_NAME, "%d %d", fnum, dcnt);

    if (isint) {
 	linum = fnum;

	/* If this is the second interlaced frame in a row, do a copy of the
	 * bottom field of the previous frame.
	 */

	if ((fnum - lfnum) == 2) {
	    merge_frames(lastiframe, ptr->video_buf, ptr->v_width, ptr->v_height, ((vob->im_v_codec == TC_CODEC_RGB24) ? 3:1) );
	} else {
          ac_memcpy(lastiframe, ptr->video_buf, ptr->video_size);
	  /* The use of the drop counter ensures syncronization even with
	   * video-based sources.  */
	  if (dcnt < 8) {
	      ptr->attributes |= TC_FRAME_IS_SKIPPED;
	      dcnt += 5;
	      dfnum++;
	   } else {
	       /* If we'd lose sync by dropping, copy the last frame in.
	        * If there are more than 3 interlaced frames in a row, it's
	        * probably video and we don't want to copy the last frame over */
	       if (((fnum - lfnum) < 3) && fnum)
		   ac_memcpy(ptr->video_buf, lastframe, ptr->video_size);
	   }
	}
    } else {
        ac_memcpy(lastframe, ptr->video_buf, ptr->video_size);
        lfnum = fnum;
    }
    /* If we're dealing with a non-interlaced source, or close to it, it won't
     * drop enough interlaced frames, so drop here to keep sync */
    if (dcnt <= -5) {
	 ptr->attributes |= TC_FRAME_IS_SKIPPED;
	 dcnt += 5;
	dfnum++;
    }
    fnum++;
    dcnt--;
  }

  return(0);
}

