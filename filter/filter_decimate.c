/*
 *  filter_decimate.c
 *
 *  Copyright (C) Thanassis Tsiodras - August 2002
 *
 *  This file is part of transcode, a video stream processing tool
 *  Based on the excellent work of Donald Graft in Decomb.
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

#define MOD_NAME    "filter_decimate.so"
#define MOD_VERSION "v0.4 (2003-04-22)"
#define MOD_CAP     "NTSC decimation plugin"
#define MOD_AUTHOR  "Thanassis Tsiodras"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtcutil/optstr.h"


static int show_results=0;

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

#define FRBUFSIZ 6

int tc_filter(frame_list_t *ptr_, char *options)
{
    vframe_list_t *ptr = (vframe_list_t *)ptr_;
    static vob_t *vob = NULL;
    static char *lastFrames[FRBUFSIZ];
    static int frameIn = 0, frameOut = 0;
    static int frameCount = -1, lastFramesOK[FRBUFSIZ];

    //----------------------------------
    //
    // filter init
    //
    //----------------------------------

    if (ptr->tag & TC_FILTER_GET_CONFIG) {
	if (options) {
	    optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "Thanassis Tsiodras", "VYO", "1");
	    optstr_param (options, "verbose", "print verbose information", "", "0");
	}
    }

    if (ptr->tag & TC_FILTER_INIT) {

	int i;

	if ((vob = tc_get_vob()) == NULL)
	    return (-1);

	if (vob->im_v_codec != TC_CODEC_YUV420P) {
		tc_log_error(MOD_NAME, "Sorry, only YUV input allowed for now");
		return (-1);
	}

	// filter init ok.
	if (options != NULL) {

	    if (optstr_lookup (options, "verbose") != NULL) {
		show_results=1;
	    }

	}

	if (verbose)
	    tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

	for(i=0; i<FRBUFSIZ; i++) {
	    lastFrames[i] = tc_malloc(SIZE_RGB_FRAME);
	    lastFramesOK[i] = 1;
    	}

	return (0);
    }
    //----------------------------------
    //
    // filter close
    //
    //----------------------------------


    if (ptr->tag & TC_FILTER_CLOSE) {
	int i;

	for(i=0; i<FRBUFSIZ; i++)
	    free(lastFrames[i]);
	return (0);
    }
    //----------------------------------
    //
    // filter frame routine
    //
    //----------------------------------


    // tag variable indicates, if we are called before
    // transcodes internal video/audio frame processing routines
    // or after and determines video/audio context

    if ((ptr->tag & TC_POST_S_PROCESS) && (ptr->tag & TC_VIDEO)) {

	// After frame processing, the frames must be deinterlaced.
	// For inverse telecine, this has been done by the ivtc filter
	//     (you must use filter_ivtc before this filter)
	// for example :
	//     -J ivtc,decimate
	// or (better) :
	//     -J ivtc,32detect=force_mode=3,decimate
	ac_memcpy( lastFrames[frameIn],
		ptr->video_buf,
		ptr->v_width*ptr->v_height*3);
	if (show_results)
	    tc_log_info(MOD_NAME, "Inserted frame %d into slot %d ",
		    frameCount, frameIn);
	lastFramesOK[frameIn] = 1;
	frameIn = (frameIn+1) % FRBUFSIZ;
	frameCount++;

	// The first 5 frames are not processed; they are only buffered.
	if (frameCount <= 4) {
	    ptr->attributes |= TC_FRAME_IS_SKIPPED;
	} else {
	    // Having 6 frames in the buffer, we will now output one of them.
	    // From now on, for each group of 5 frames we will drop 1
	    // (FPS: 29.97->23.976). In fact, we will drop the frame
	    // that looks almost exactly like its successor.
	    if ((frameCount % 5) == 0) {

		// First, find which one of the first 5 frames in the group
		// looks almost exactly like the frame that follows.
		int i, j, diffMin=INT_MAX, indexMin = -1;

		for(j=0; j<5; j++) {
		    int diff = 0;
		    for(i=0; i<ptr->v_height*ptr->v_width; i+=16)
			diff += abs(
			    lastFrames[(frameOut+j+1)%FRBUFSIZ][i] -
			    lastFrames[(frameOut+j)%FRBUFSIZ][i]);
		    if (diff<diffMin) {
			diffMin = diff;
			indexMin = j;
		    }
		}
		// ...and mark it as junk
		lastFramesOK[(frameOut+indexMin)%FRBUFSIZ] = 0;
	    }
	    if (lastFramesOK[frameOut]) {
		ac_memcpy(	ptr->video_buf,
			lastFrames[frameOut],
			ptr->v_width*ptr->v_height*3);
		if (show_results)
		    tc_log_info(MOD_NAME, "giving slot %d", frameOut);
	    }
	    else {
		ptr->attributes |= TC_FRAME_IS_SKIPPED;
		if (show_results)
		    tc_log_info(MOD_NAME, "droping slot %d", frameOut);
	    }
	    // Regardless of the job we periodically do (for each group
	    // of 5 frames) we must also advance the two indexes.
	    // The frameIn index is increased at frame insertion.
	    // Now it is time for the frameOut index.
	    // Note that both indexes are moving at the same speed,
	    // so no code for circular queue index-clashing is required.
	    frameOut = (frameOut+1) % FRBUFSIZ;
	}
    }

    return (0);
}
