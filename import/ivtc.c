/*
 *  ivtc.c
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

#include "src/transcode.h"
#include "ivtc.h"
#include "libtc/libtc.h"

// basic parameter
static int color_diff_threshold1=50;
static int color_diff_threshold2=100;
static double critical_threshold=0.00001;

static void merge_yuv_fields(unsigned char *src1, unsigned char *src2, int width, int height)
{

    char *in, *out;

    int i, block;

    block = width;

    in  = src2+block;
    out = src1+block;

    //move every second row
    //Y
    for (i=0; i<height; i=i+2) {

	ac_memcpy(out, in, block);
	in  += 2*block;
	out += 2*block;
    }


    block = width/2;

    //Cb
    in  = src2 + width*height + block;
    out = src1 + width*height + block;

    //move every second row
    for (i=0; i<height/2; i=i+2) {

	ac_memcpy(out, in, block);
	in  += 2*block;
	out += 2*block;
    }


    //Cr
    in  = src2 + width*height*5/4 + block;
    out = src1 + width*height*5/4 + block;

    //move every second row
    for (i=0; i<height/2; i=i+2) {

	ac_memcpy(out, in, block);
	in  += 2*block;
	out += 2*block;
    }
}

static void merge_rgb_fields(unsigned char *src1, unsigned char *src2, int width, int height)
{

    char *in, *out;

    int i, block;

    block = 3*width;

    in  = src2;
    out = src1;

    //move every second row
    for (i=0; i<height; i=i+2) {

	ac_memcpy(out, in, block);
	in  += 2*block;
	out += 2*block;
    }
}

int interlace_test(char *video_buf, int width, int height)
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

static inline void rgb_average(char *row1, char *row2, char *out, int bytes)
{

  // calculate the average of each color entry in two arrays and return
  // result in char *out

    unsigned int y;
    unsigned short tmp;

    for (y = 0; y<bytes; ++y) {
      tmp = ((unsigned char) row2[y] + (unsigned char) row1[y])>>1;
      out[y] = tmp & 0xff;
    }


    return;
}


static void yuv_deinterlace(char *image, int width, int height)
{
    char *in, *out;

    unsigned int y, block;

    block = width;

    in  = image;
    out = image;

    // convert half frame to full frame by simple interpolation

    out +=block;

    for (y = 0; y < (height>>1)-1; y++) {

      rgb_average(in, in+(block<<1), out, block);

      in  += block<<1;
      out += block<<1;
    }

    // clone last row

    ac_memcpy(out, in, block);

    return;
}


static void rgb_deinterlace(char *image, int width, int height)
{
    char *in, *out;

    unsigned int y, block;

    block = width * 3;

    in  = image;
    out = image;

    // convert half frame to full frame by simple interpolation

    out +=block;

    for (y = 0; y < (height>>1)-1; y++) {

      rgb_average(in, in+(block<<1), out, block);

      in  += block<<1;
      out += block<<1;
    }

    // clone last row

    ac_memcpy(out, in, block);

    return;
}

  // ----------------------------------------------------------------
  //
  // reverse 3:2 pulldown (inverse telecine)
  // currently, only a few number of hardcoded sequences
  // indicated by pulldown flag, are supported, s. below:

static int pulldown_drop_ctr=0, pulldown_frame_ctr=0, pulldown_buffer_flag=0;

int ivtc(int *flag, int pflag, char *buffer, char *pulldown_buffer, int width, int height, int size, int vcodec, int verbose)
{

    int interlace_flag = 0;
    int merge_flag = 0, flush_flag=0;
    int last_frame = 0, must_drop=0;
    static int merge_ctr=0, interlace_ctr=0, flush_ctr=0, post_interlace_ctr=0;
    //Ok, this sequence has 3:2 pulldown applied, says demuxer
    //ignore demuxer drop suggestions

    int clone_flag=*flag;

    ++pulldown_frame_ctr;

    // check if frame is interlaced
    if(vcodec==TC_CODEC_RGB24) {
	interlace_flag=interlace_test(buffer, 3*width, height);
    } else {
	interlace_flag=interlace_test(buffer, width, height);
    }

    //case 1:
    if(pulldown_buffer_flag==0 && interlace_flag==1) {

      //copy first frame of pair to tmp buffer

      if(verbose & TC_STATS)
	  tc_log_msg(__FILE__, "COPY: (%2d)", pulldown_frame_ctr);

      ac_memcpy(pulldown_buffer, buffer, size);
      pulldown_buffer_flag=1;
      clone_flag=0; //drop frame
      ++pulldown_drop_ctr;
      goto resume;
    }

    //case 2:
    if(pulldown_buffer_flag==1 && interlace_flag==1) {

      //this means, we need to contruct a new frame from the current
      //and the previous, stored in pulldown_buffer

      if(verbose & TC_STATS)
	  tc_log_msg(__FILE__, "MERGE (%2d)", pulldown_frame_ctr);

      if(vcodec==TC_CODEC_RGB24) {
	merge_rgb_fields(buffer, pulldown_buffer, width, height);
      } else {
	merge_yuv_fields(buffer, pulldown_buffer, width, height);
      }
      clone_flag=1; //use this frame
      merge_flag=1;
      pulldown_buffer_flag=0;
      goto resume;
    }

    //case 3:
    if(pulldown_buffer_flag==1 && interlace_flag==0) {

	if(verbose & TC_STATS)
	    tc_log_msg(__FILE__, "FLUSH: (%2d)", pulldown_frame_ctr);

	//failed to detect the second interlaced frame of the pair
	pulldown_buffer_flag=0;  //clear buffer
	flush_flag=1;
	clone_flag=1;

	goto resume;
    }

    //case 4:
    if(pulldown_buffer_flag==0 && interlace_flag==0) {

      if(verbose & TC_STATS)
	  tc_log_msg(__FILE__, "PASS: (%2d)", pulldown_frame_ctr);
      clone_flag=1;
      goto resume;
    }

  resume:

    if(interlace_flag) ++interlace_ctr;
    if(merge_flag)     ++merge_ctr;
    if(flush_flag)     ++flush_ctr;

    switch(pflag) {

    case 1:   //15 frames, 3 to drop

	last_frame = 15;
	must_drop = 3;

	//force frame drop - no interlaced frames detected at checkpoint 1
	if(pulldown_frame_ctr==5 && pulldown_drop_ctr == 0) {
	    if(verbose & TC_STATS)
		tc_log_msg(__FILE__, "ADJUST");
	    clone_flag=0;
	    ++pulldown_drop_ctr;
	}

	//force frame drop - no interlaced frames detected at checkpoint 2
	if(pulldown_frame_ctr==10 && pulldown_drop_ctr < 2) {
	    if(verbose & TC_STATS)
		tc_log_msg(__FILE__, "ADJUST");
	    clone_flag=0;
	    ++pulldown_drop_ctr;
	}

	//force frame drop - no interlaced frames detected at checkpoint 3
	if(pulldown_frame_ctr==15 && pulldown_drop_ctr < 3) {
	    if(verbose & TC_STATS)
		tc_log_msg(__FILE__, "ADJUST");
	    clone_flag=0;
	    ++pulldown_drop_ctr;
	}

	break;


    case 2:    //15 frames, 4 to drop

	last_frame = 15;
	must_drop=4;

	//force frame drop - no interlaced frames detected at checkpoint 1
	if(pulldown_frame_ctr==4 && pulldown_drop_ctr == 0) {
	    if(verbose & TC_STATS)
		tc_log_msg(__FILE__, "ADJUST");
	    clone_flag=0;
	    ++pulldown_drop_ctr;
	}

	//force frame drop - no interlaced frames detected at checkpoint 2
	if(pulldown_frame_ctr==8 && pulldown_drop_ctr < 2) {
	    if(verbose & TC_STATS)
		tc_log_msg(__FILE__, "ADJUST");
	    clone_flag=0;
	    ++pulldown_drop_ctr;
	}

	//force frame drop - no interlaced frames detected at checkpoint 3
	if(pulldown_frame_ctr==12 && pulldown_drop_ctr < 3) {
	    if(verbose & TC_STATS)
		tc_log_msg(__FILE__, "ADJUST");
	    clone_flag=0;
	    ++pulldown_drop_ctr;
	}

	//force frame drop - no interlaced frames detected at checkpoint 4
	if(pulldown_frame_ctr==15 && pulldown_drop_ctr < 4) {
	    if(verbose & TC_STATS)
		tc_log_msg(__FILE__, "ADJUST");
	    clone_flag=0;
	    ++pulldown_drop_ctr;
	}

	break;

    case 3:   //4 frames, 2 to drop

	last_frame=4;
	must_drop=2;

	//force frame drop - no interlaced frames detected at checkpoint 1
	if(pulldown_frame_ctr==2 && pulldown_drop_ctr == 0) {
	    if(verbose & TC_STATS)
		tc_log_msg(__FILE__, "ADJUST");
	    clone_flag=0;
	    ++pulldown_drop_ctr;
	}

	//force frame drop - no interlaced frames detected at checkpoint 2
	if(pulldown_frame_ctr==4 && pulldown_drop_ctr < 2) {
	    if(verbose & TC_STATS)
		tc_log_msg(__FILE__, "ADJUST");
	    clone_flag=0;
	    ++pulldown_drop_ctr;
	}

	break;

    case 4:   //11 frames, 1 to drop

	last_frame = 11;
	must_drop=1;

	//force frame drop - no interlaced frames detected at checkpoint 1
	if(pulldown_frame_ctr==11 && pulldown_drop_ctr == 0) {
	    if(verbose & TC_STATS)
		tc_log_msg(__FILE__, "ADJUST");
	    clone_flag=0;
	    ++pulldown_drop_ctr;
	}

	break;
    }

    //check for over drop
    if(pulldown_drop_ctr>must_drop) {
      clone_flag=1;
      --pulldown_drop_ctr;
    }

    //still deinterlaced?
    if(interlace_flag==1 && merge_flag==0 && clone_flag==1) {

      //FIXME: move this to main frame processing
      if(vcodec==TC_CODEC_RGB24) {
	rgb_deinterlace(buffer, width, height);
      } else {
	yuv_deinterlace(buffer, width, height);
      }
      ++post_interlace_ctr;
    }

    //reset
    if(pulldown_frame_ctr==last_frame) {

      if(verbose >= TC_STATS)
	  tc_log_msg(__FILE__, "DROP: (%2d)", pulldown_drop_ctr);
      //summary
      if(verbose >= TC_STATS)
	  tc_log_msg(__FILE__, "frames=(%2d|%d), interlaced=%2d, merged=%2d, flushed=%2d, post=%2d",
		     last_frame, must_drop, interlace_ctr, merge_ctr,
		     flush_ctr, post_interlace_ctr);

      pulldown_frame_ctr = 0;
      pulldown_drop_ctr = 0;

      flush_ctr=0;
      merge_ctr=0;
      interlace_ctr=0;
      post_interlace_ctr=0;

      //do not reset buffer flag, important for next sequence
    }

    *flag=clone_flag;
    return(0);
}

