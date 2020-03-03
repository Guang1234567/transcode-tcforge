/*
 *  filter_ivtc.c
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

#define MOD_NAME    "filter_ivtc.so"
#define MOD_VERSION "v0.4.1 (2004-06-01)"
#define MOD_CAP     "NTSC inverse telecine plugin"
#define MOD_AUTHOR  "Thanassis Tsiodras"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtcutil/optstr.h"


static int show_results=0;

static void ivtc_copy_field (unsigned char *dest, unsigned char *src, vframe_list_t * ptr, int field) {
    int y;

    if (field == 1) {
	src  += ptr->v_width;
	dest += ptr->v_width;
    }

    for (y = 0; y < (ptr->v_height+1)/2; y++) {
	ac_memcpy(dest, src, ptr->v_width);
	src  += ptr->v_width*2;
	dest += ptr->v_width*2;
    }

    if (field == 1) {
	src  -= (ptr->v_width+1) / 2;
	dest -= (ptr->v_width+1) / 2;
    }

    for (y = 0; y < (ptr->v_height+1)/2; y++) {
	ac_memcpy(dest, src, ptr->v_width/2);
	src  += ptr->v_width;
	dest += ptr->v_width;
    }
}


/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

#define FRBUFSIZ 3

int tc_filter(frame_list_t *ptr_, char *options)
{
    vframe_list_t *ptr = (vframe_list_t *)ptr_;
    static vob_t *vob = NULL;
    static char *lastFrames[FRBUFSIZ];
    static int frameIn = 0;
    static int frameCount = 0;
    static int field = 0;
    static int magic = 0;

    //----------------------------------
    //
    // filter init
    //
    //----------------------------------


    if (ptr->tag & TC_FILTER_GET_CONFIG) {
	if (options) {
	    optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "Thanassis Tsiodras", "VYE", "1");
	    optstr_param (options, "verbose", "print verbose information", "", "0");
	    optstr_param (options, "field", "which field to replace (0=top 1=bottom)",  "%d", "0",  "0", "1");
	    optstr_param (options, "magic", "perform magic? (0=no 1=yes)",  "%d", "0",  "0", "1");
	}
    }

    if (ptr->tag & TC_FILTER_INIT) {

	int i;

	if ((vob = tc_get_vob()) == NULL)
	    return (-1);

	if (vob->im_v_codec != TC_CODEC_YUV420P) {
		tc_log_error(MOD_NAME, "Sorry, only YUV 420 input allowed for now");
		return (-1);
	}

	// filter init ok.
	if (options != NULL) {

	    if (optstr_lookup (options, "verbose") != NULL) {
		show_results=1;
	    }

	    optstr_get(options, "field", "%d", &field);
	    optstr_get(options, "magic", "%d", &magic);

	}

	if (verbose)
	    tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

	for(i=0; i<FRBUFSIZ; i++) {
	    lastFrames[i] = tc_malloc(SIZE_RGB_FRAME);
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

    if ((ptr->tag & TC_PRE_S_PROCESS) && (ptr->tag & TC_VIDEO)) {

	ac_memcpy(	lastFrames[frameIn],
		ptr->video_buf,
		ptr->v_width*ptr->v_height*3);
	if (show_results)
	    tc_log_info(MOD_NAME, "Inserted frame %d into slot %d",
		    frameCount, frameIn);
	frameIn = (frameIn+1) % FRBUFSIZ;
	frameCount++;

	// The first 2 frames are not output - they are only buffered
	if (frameCount <= 2) {
	    ptr->attributes |= TC_FRAME_IS_SKIPPED;
	} else {
	    // We have the last 3 frames in the buffer...
	    //
	    //		Previous Current Next
	    //
	    // OK, time to work...

	    unsigned char *curr,
		*pprev, *pnext, *cprev, *cnext, *nprev, *nnext, *dstp;
	    int idxp, idxc, idxn;
	    int p, c, n, lowest, chosen;
	    int C, x, y;
	    int comb;

	    idxn = frameIn-1; while(idxn<0) idxn+=FRBUFSIZ;
	    idxc = frameIn-2; while(idxc<0) idxc+=FRBUFSIZ;
	    idxp = frameIn-3; while(idxp<0) idxp+=FRBUFSIZ;

            y = (field ? 2 : 1) * ptr->v_width;

	    // bottom field of current
	    curr =  &lastFrames[idxc][y];
	    // top field of previous
	    pprev = &lastFrames[idxp][y - ptr->v_width];
	    // top field of previous - 2nd scanline
	    pnext = &lastFrames[idxp][y + ptr->v_width];
	    // top field of current
	    cprev = &lastFrames[idxc][y - ptr->v_width];
	    // top field of current - 2nd scanline
	    cnext = &lastFrames[idxc][y + ptr->v_width];
	    // top field of next
	    nprev = &lastFrames[idxn][y - ptr->v_width];
	    // top field of next - 2nd scanline
	    nnext = &lastFrames[idxn][y + ptr->v_width];

	    // Blatant copy begins...

	    p = c = n = 0;
	    /* Try to match the top field of the current frame to the
	       bottom fields of the previous, current, and next frames.
	       Output the assembled frame that matches up best. For
	       matching, subsample the frames in the x dimension
	       for speed. */
	    for (y = 0; y < ptr->v_height-2; y+=4)
	    {
		for (x = 0; x < ptr->v_width;)
		{
		    C = curr[x];
#define T 100
		    /* This combing metric is based on
		       an original idea of Gunnar Thalin. */
		    comb = ((long)pprev[x] - C) * ((long)pnext[x] - C);
		    if (comb > T) p++;

		    comb = ((long)cprev[x] - C) * ((long)cnext[x] - C);
		    if (comb > T) c++;

		    comb = ((long)nprev[x] - C) * ((long)nnext[x] - C);
		    if (comb > T) n++;

		    if (!(++x&3)) x += 12;
		}
		curr  += ptr->v_width*4;
		pprev += ptr->v_width*4;
		pnext += ptr->v_width*4;
		cprev += ptr->v_width*4;
		cnext += ptr->v_width*4;
		nprev += ptr->v_width*4;
		nnext += ptr->v_width*4;
	    }

	    lowest = c;
	    chosen = 1;
	    if (p < lowest)
	    {
		    lowest = p;
		    chosen = 0;
	    }
	    if (n < lowest)
	    {
		    lowest = n;
		    chosen = 2;
	    }

	    if (magic && c < 50 && abs(lowest - c) < 10 && (p+c+n) > 1000) {
		lowest = c;
		chosen = 1;
            }

	    // Blatant copy ends... :)

	    if (show_results)
		tc_log_info(MOD_NAME,
		    "Telecide => frame %d: p=%u  c=%u  n=%u [using %d]",
		    frameCount, p, c, n, chosen);

	    // Set up the pointers in preparation to output final frame.

	    // First, the Y plane
	    if (chosen == 0)
		curr = lastFrames[idxp];
	    else if (chosen == 1)
		curr = lastFrames[idxc];
	    else
		curr = lastFrames[idxn];

	    dstp = ptr->video_buf;

	    // First output the top field selected
	    // from the set of three stored frames.
	    ivtc_copy_field(dstp, curr, ptr, field);

	    // The bottom field of the current frame unchanged
	    ivtc_copy_field(dstp, lastFrames[idxc], ptr, 1-field);

	}
    }

    return (0);
}
