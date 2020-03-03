/*
 *  filter_testframe.c
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

#define MOD_NAME    "filter_testframe.so"
#define MOD_VERSION "v0.1.3 (2003-09-04)"
#define MOD_CAP     "generate stream of testframes"
#define MOD_AUTHOR  "Thomas Oestreich"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtcutil/optstr.h"

static int mode=0;
static vob_t *vob=NULL;

static void generate_rgb_frame(char *buffer, int width, int height)
{
  int n, j, row_bytes;

  row_bytes = width*3;

  memset(buffer, 0, width*height*3);

  switch(mode) {

  case 0:

      for(n=0; n<height; ++n) {

	  if(n & 1) {
	      for(j=0; j<row_bytes; ++j) buffer[n*row_bytes+j] = 255;
	  } else {
	      for(j=0; j<row_bytes; ++j) buffer[n*row_bytes+j] = 0;
	  }
      }

      break;

  case 1:

      for(n=0; n<height*width; n=n+2) {
	  buffer[n*3]   = 255;
	  buffer[n*3+1] = 255;
	  buffer[n*3+2] = 255;
      }

      break;

  case 2:  //red picture

    for(n=0; n<height*width; ++n) {
      buffer[n*3]   = 255;
      buffer[n*3+1] = 0;
      buffer[n*3+2] = 0;
    }
    break;

  case 3:  //green picture

    for(n=0; n<height*width; ++n) {
      buffer[n*3]   = 0;
      buffer[n*3+1] = 255;
      buffer[n*3+2] = 0;
    }
    break;
  case 4:  //blue

    for(n=0; n<height*width; ++n) {
      buffer[n*3]   = 0;
      buffer[n*3+1] = 0;
      buffer[n*3+2] = 255;
    }
    break;
  }
}

static void generate_yuv_frame(char *buffer, int width, int height)
{
  int n, j, row_bytes;

  row_bytes = width;

  memset(buffer, 0x80, width*height*3/2);

  switch(mode) {

  case 0:

      for(n=0; n<height; ++n) {

	  if(n & 1) {
	      for(j=0; j<row_bytes; ++j) buffer[n*row_bytes+j]   = 255;
	  } else {
	      for(j=0; j<row_bytes; ++j) buffer[n*row_bytes+j]   = 0;
	  }
      }

      break;

  case 1:

      for(n=0; n<height*width; ++n) buffer[n]=(n&1)?255:0;

      break;

  case 5: // from libavformat
      {
	  static int indx = 0;
	  int x, y;
	  unsigned char
	      *Y = buffer,
	      *U = Y + width*height,
	      *V = U + (width/2)*(height/2);

	  for(y=0;y<height;y++) {
	      for(x=0;x<width;x++) {
		  Y[y * width + x] = x + y + indx * 3;
	      }
	  }

	  /* Cb and Cr */
	  for(y=0;y<height/2;y++) {
	      for(x=0;x<width/2;x++) {
		  U[y * width/2 + x] = 128 + y + indx * 2;
		  V[y * width/2 + x] = 64 + x + indx * 5;
	      }
	  }
	  indx++;
      }
      break;
  }
}


/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/


static int is_optstr(char *options)
{
    if (strchr(options, 'm')) return 1;
    if (strchr(options, 'h')) return 1;
    if (strchr(options, '=')) return 1;
    return 0;
}

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;

  if(ptr->tag & TC_FILTER_GET_CONFIG) {

      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "Thomas Oestreich", "VRYE", "1");
      optstr_param (options, "mode",   "Choose the test pattern (0-4 interlaced, 5 colorfull)", "%d", "0", "0", "5");
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

    if (options) {
	if (is_optstr(options)) {
	    optstr_get(options, "mode", "%d", &mode);
	} else
	    sscanf(options, "%d", &mode);
    }

    if(mode <0) { tc_log_error(MOD_NAME, "Invalid mode"); return(-1); }

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

  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  if(ptr->tag & TC_PRE_M_PROCESS && ptr->tag & TC_VIDEO && !(ptr->attributes & TC_FRAME_IS_SKIPPED)) {

    if(vob->im_v_codec==TC_CODEC_RGB24) {
      generate_rgb_frame(ptr->video_buf, ptr->v_width, ptr->v_height);
    } else {
      generate_yuv_frame(ptr->video_buf, ptr->v_width, ptr->v_height);
    }
  }
  return(0);
}
