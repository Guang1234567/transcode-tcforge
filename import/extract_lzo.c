/*
 *  extract_lzo.c
 *
 *  Copyright (C) Tilmann Bitterberg - 2003
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

#include "tccore/tcinfo.h"
#include "src/transcode.h"
#include "libtc/libtc.h"

#include "ioaux.h"
#include "avilib/avilib.h"
#include "tc.h"

#ifdef HAVE_LZO

#include <lzo/lzo1x.h>
#include <lzo/lzoutil.h>

#define BUFFER_SIZE SIZE_RGB_FRAME<<1

inline static void long2str(long a, unsigned char *b)
{
      b[0] = (a&0xff000000)>>24;
      b[1] = (a&0x00ff0000)>>16;
      b[2] = (a&0x0000ff00)>>8;
      b[3] = (a&0x000000ff);
}

void extract_lzo(info_t *ipipe)
{

  avi_t *avifile=NULL;
  char *video;

  int key, error=0;

  long frames, bytes, n;

  switch(ipipe->magic) {

  case TC_MAGIC_AVI:

    // scan file
    if (ipipe->nav_seek_file) {
      if(NULL == (avifile = AVI_open_indexfd(ipipe->fd_in,0,ipipe->nav_seek_file))) {
	AVI_print_error("AVI open");
	import_exit(1);
      }
    } else {
      if(NULL == (avifile = AVI_open_fd(ipipe->fd_in,1))) {
	AVI_print_error("AVI open");
	import_exit(1);
      }
    }

    // read video info;

    frames =  AVI_video_frames(avifile);
    if (ipipe->frame_limit[1] < frames)
      {
	frames=ipipe->frame_limit[1];
      }


    if(ipipe->verbose & TC_STATS)
      tc_log_msg(__FILE__, "%ld video frames", frames);

    // allocate space, assume max buffer size
    if((video = tc_zalloc(SIZE_RGB_FRAME))==NULL) {
      tc_log_msg(__FILE__, "out of memory");
      error=1;
      break;
    }

    (int)AVI_set_video_position(avifile,ipipe->frame_limit[0]);
    for (n=ipipe->frame_limit[0]; n<=frames; ++n) {
      // video
      if((bytes = AVI_read_frame(avifile, video, &key))<0) {
	error=1;
	break;
      }
      if(tc_pwrite(ipipe->fd_out, video, bytes)!=bytes) {
	error=1;
	break;
      }
    }

    free(video);

    break;

  case TC_MAGIC_RAW:
  default:

    if(ipipe->magic == TC_MAGIC_UNKNOWN)
      tc_log_warn(__FILE__, "no file type specified, assuming %s",
		  filetype(TC_MAGIC_RAW));

    error = tc_preadwrite(ipipe->fd_in, ipipe->fd_out);
    if (error < 0)
        error = 1;

    break;
  }

  import_exit(error);
}

#else
void extract_lzo(info_t *ipipe)
{
    tc_log_error(__FILE__, "No support for LZO configured -- exiting");
    import_exit(1);
}
#endif

