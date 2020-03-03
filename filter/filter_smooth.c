/*
 *  filter_smooth.c
 *
 *  Copyright (C) Chad Page - October 2002
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

#define MOD_NAME    "filter_smooth.so"
#define MOD_VERSION "v0.2.3 (2003-03-27)"
#define MOD_CAP     "(single-frame) smoothing plugin"
#define MOD_AUTHOR  "Chad Page"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtcutil/optstr.h"

/* FIXME: this uses the filter ID as an index--the ID can grow
 * arbitrarily large, so this needs to be fixed */
static unsigned char *tbuf[100];

static void smooth_yuv(unsigned char *buf, int width, int height, int maxdiff,
		       int maxldiff, int maxdist, float level, int instance)
{
	int x, y, pu, cpu, cdiff;
	int xa, ya, oval, ldiff;
	unsigned char *bufcr, *bufcb;
	unsigned char *tbufcr, *tbufcb, *ltbuf;
	float dist, ratio, nval;

	ltbuf = tbuf[instance];
	tbufcb = &ltbuf[width * height];
	tbufcr = &tbufcb[(width/2) * (height/2)];

	ac_memcpy(ltbuf, buf, (width * height) * 3 / 2);

	bufcb = &buf[width * height];
	bufcr = &bufcb[(width/2) * (height/2)];


	/* First pass - horizontal */

	for (y = 0; y < (height); y++) {
		for (x = 0; x < width; x++) {
			pu = ((y * width) / 2) + (x / 2);
			nval = ((float)buf[x + (y * width)]);
			oval = buf[x + (y * width)];
			for (xa = x - maxdist; (xa <= (x + maxdist)) && (xa < width); xa++) {
				if (xa < 0) xa = 0;
				if (xa == x) xa++;
				cpu = ((y * width) / 2) + (xa / 2);
				cdiff = abs(tbufcr[pu] - tbufcr[cpu]);
				cdiff += abs(tbufcb[pu] - tbufcb[cpu]);

				/* If color difference not too great, average the pixel according to distance */
				ldiff = abs(ltbuf[xa + (y * width)] - oval);
				if ((cdiff < maxdiff) && (ldiff < maxldiff)) {
					dist = abs(xa - x);
					ratio = level / dist;
					nval = nval * (1 - ratio);
					nval += ((float)ltbuf[xa + (y * width)]) * ratio;
				}
			}
			buf[x + (y * width)] = (unsigned char)(nval + 0.5);
		}
	}

	/* Second pass - vertical lines */

	ac_memcpy(ltbuf, buf, (width * height) * 3 / 2);

	for (y = 0; y < (height); y++) {
		for (x = 0; x < width; x++) {
			pu = ((y * width) / 2) + (x / 2);
			nval = ((float)buf[x + (y * width)]);
			oval = buf[x + (y * width)];
			for (ya = y - maxdist; (ya <= (y + maxdist)) && (ya < height); ya++) {
				if (ya < 0) ya = 0;
				if (ya == y) ya++;
				cpu = ((ya * width) / 2) + (x / 2);
				cdiff = abs(tbufcr[pu] - tbufcr[cpu]);
				cdiff += abs(tbufcb[pu] - tbufcb[cpu]);

				/* If color difference not too great, average the pixel according to distance */
				ldiff = abs(ltbuf[x + (ya * width)] - oval);
				if ((cdiff < maxdiff) && (ldiff < maxldiff)) {
					dist = abs(ya - y);
					ratio = level / dist;
					nval = nval * (1 - ratio);
					nval += ((float)ltbuf[x + (ya * width)]) * ratio;
				}
			}
			buf[x + (y * width)] = (unsigned char)(nval + 0.5);
		}
	}
}

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  static vob_t *vob=NULL;
  /* FIXME: these use the filter ID as an index--the ID can grow
   * arbitrarily large, so this needs to be fixed */
  static int cdiff[100], ldiff[100], range[100];
  static float strength[100];
  int instance = ptr->filter_id;


  //----------------------------------
  //
  // filter print configure
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_GET_CONFIG) {

      char buf[32];
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VYEM", "1");

      // buf, name, comment, format, val, from, to
      tc_snprintf (buf, 32, "%.2f", strength[instance]);
      optstr_param (options, "strength", "Blending factor",                 "%f", buf, "0.0", "0.9");

      tc_snprintf (buf, 32, "%d", cdiff[instance]);
      optstr_param (options, "cdiff",    "Max difference in chroma values", "%d", buf, "0", "16");

      tc_snprintf (buf, 32, "%d", ldiff[instance]);
      optstr_param (options, "ldiff",    "Max difference in luma value",    "%d", buf, "0", "16");

      tc_snprintf (buf, 32, "%d", range[instance]);
      optstr_param (options, "range",    "Search Range",                    "%d", buf, "0", "16");

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

    // set defaults

    strength[instance] = 0.25;	/* Blending factor.  Do not exceed 2 ever */
    cdiff[instance] = 6;		/* Max difference in UV values */
    ldiff[instance] = 8;		/* Max difference in Y value */
    range[instance] = 4;		/* Search range */

    if (options != NULL) {
    	if(verbose) tc_log_info(MOD_NAME, "options=%s", options);

	optstr_get (options, "strength",  "%f", &strength[instance]);
	optstr_get (options, "cdiff",  "%d", &cdiff[instance]);
	optstr_get (options, "ldiff",  "%d", &ldiff[instance]);
	optstr_get (options, "range",  "%d", &range[instance]);
    }

    tbuf[instance] = tc_malloc(SIZE_RGB_FRAME);
    if (strength[instance]> 0.9) strength[instance] = 0.9;
    memset(tbuf[instance], 0, SIZE_RGB_FRAME);

    if (vob->im_v_codec == TC_CODEC_RGB24) {
	if (verbose) tc_log_error(MOD_NAME, "only capable of YUV mode");
	return -1;
    }

    if(verbose) tc_log_info(MOD_NAME, "%s %s #%d", MOD_VERSION, MOD_CAP, ptr->filter_id);

    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_CLOSE) {
    if (tbuf[instance])
      free(tbuf[instance]);
    tbuf[instance] = NULL;

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

	if (vob->im_v_codec == TC_CODEC_YUV420P)
		smooth_yuv(ptr->video_buf, ptr->v_width, ptr->v_height, cdiff[instance],
		    ldiff[instance], range[instance], strength[instance], instance);

  }

  return(0);
}
