/*
 *  filter_detectclipping
 *
 *  Copyright (C) Tilmann Bitterberg - June 2002
 *    Based on Code from mplayers cropdetect by A'rpi
 *    Updated by Antonio Beamud Montero (Microgenesis S.A.) - Jan 2009
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

#define MOD_NAME    "filter_detectclipping.so"
#define MOD_VERSION "v0.2.0 (2009-01-30)"
#define MOD_CAP     "detect clipping parameters (-j or -Y)"
#define MOD_AUTHOR  "Tilmann Bitterberg, A'rpi, A. Beamud"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"


// basic parameter

typedef struct MyFilterData {
    /* configurable */
        unsigned int start;
        unsigned int end;
        unsigned int step;
        int post;
	int limit;
        FILE *log;
        int frames;
        int x1, y1, x2, y2;

    /* internal */
	int stride, bpp;
	int fno;
	int boolstep;
} MyFilterData;

static MyFilterData *mfd[16];

/* should probably honor the other flags too */

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

static void help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help\n"
"* Overview\n"
"    Detect black regions on top, bottom, left and right of an image\n"
"    It is suggested that the filter is run for around 100 frames.\n"
"    It will print its detected parameters every frame. If you\n"
"    don't notice any change in the printout for a while, the filter\n"
"    probably won't find any other values.\n"
"    The filter converges, meaning it will learn.\n"
"* Options\n"
"    'range' apply filter to [start-end]/step frames [0-oo/1]\n"
"    'limit' the sum of a line must be below this limit to be considered black\n"
"    'post' run as a POST filter (calc -Y instead of the default -j)\n"
"    'log' file to save a detailed values.\n"
		, MOD_CAP);
}

static int checkline(unsigned char* src,int stride,int len,int bpp){
    int total=0;
    int div=len;
    switch(bpp){
    case 1:
	while(--len>=0){
	    total+=src[0]; src+=stride;
	}
	break;
    case 3:
    case 4:
	while(--len>=0){
	    total+=src[0]+src[1]+src[2]; src+=stride;
	}
	div*=3;
	break;
    }
    total/=div;
    return total;
}

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  static vob_t *vob=NULL;

  if (ptr->tag & TC_AUDIO)
    return 0;

  if(ptr->tag & TC_FILTER_GET_CONFIG) {
      char buf[128];
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYEOM", "1");

      tc_snprintf(buf, 128, "%u-%u/%d", mfd[ptr->filter_id]->start, mfd[ptr->filter_id]->end, mfd[ptr->filter_id]->step);
      optstr_param (options, "range", "apply filter to [start-end]/step frames",
	      "%u-%u/%d", buf, "0", "oo", "0", "oo", "1", "oo");
      optstr_param (options, "limit", "the sum of a line must be below this limit to be considered as black", "%d", "24", "0", "255");
      optstr_param (options, "post", "run as a POST filter (calc -Y instead of the default -j)", "", "0");
      optstr_param(options, "log", "file to save a detailed values", "", "");

      return 0;
  }

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL) return(-1);

    mfd[ptr->filter_id] = tc_malloc (sizeof(MyFilterData));
    if(mfd[ptr->filter_id] == NULL)
        return (-1);

    char log_name[PATH_MAX];
    memset(log_name, 0, PATH_MAX);    

    mfd[ptr->filter_id]->start=0;
    mfd[ptr->filter_id]->end=(unsigned int)-1;
    mfd[ptr->filter_id]->step=1;
    mfd[ptr->filter_id]->limit=24;
    mfd[ptr->filter_id]->post = 0;
    mfd[ptr->filter_id]->log = NULL;
    mfd[ptr->filter_id]->frames = 0;

    if (options != NULL) {

	if(verbose) tc_log_info (MOD_NAME, "options=%s", options);

	optstr_get (options, "range",  "%u-%u/%d",    &mfd[ptr->filter_id]->start, &mfd[ptr->filter_id]->end, &mfd[ptr->filter_id]->step);
	optstr_get (options, "limit",  "%d",    &mfd[ptr->filter_id]->limit);
	if (optstr_lookup (options, "post")!=NULL) mfd[ptr->filter_id]->post = 1;
        optstr_get (options, "log", "%[^:]", log_name);
    }


    if (verbose > 1) {
	tc_log_info (MOD_NAME, " detectclipping#%d Settings:", ptr->filter_id);
	tc_log_info (MOD_NAME, "              range = %u-%u", mfd[ptr->filter_id]->start, mfd[ptr->filter_id]->end);
	tc_log_info (MOD_NAME, "               step = %u", mfd[ptr->filter_id]->step);
	tc_log_info (MOD_NAME, "              limit = %u", mfd[ptr->filter_id]->limit);
        tc_log_info (MOD_NAME, "                log = %s", log_name);
	tc_log_info (MOD_NAME, "    run POST filter = %s", mfd[ptr->filter_id]->post?"yes":"no");
    }

    if (options)
	if (optstr_lookup (options, "help")) {
	    help_optstr();
	}

    if (mfd[ptr->filter_id]->start % mfd[ptr->filter_id]->step == 0)
      mfd[ptr->filter_id]->boolstep = 0;
    else
      mfd[ptr->filter_id]->boolstep = 1;

    if (!mfd[ptr->filter_id]->post) {
	mfd[ptr->filter_id]->x1 = vob->im_v_width;
	mfd[ptr->filter_id]->y1 = vob->im_v_height;
    } else {
	mfd[ptr->filter_id]->x1 = vob->ex_v_width;
	mfd[ptr->filter_id]->y1 = vob->ex_v_height;
    }
    mfd[ptr->filter_id]->x2 = 0;
    mfd[ptr->filter_id]->y2 = 0;
    mfd[ptr->filter_id]->fno = 0;
    if (strlen(log_name) != 0)
        if (!(mfd[ptr->filter_id]->log = fopen(log_name, "w")))
	     perror("could not open file for writing");
		


    if (vob->im_v_codec == TC_CODEC_YUV420P) {
	mfd[ptr->filter_id]->stride = mfd[ptr->filter_id]->post?vob->ex_v_width:vob->im_v_width;
	mfd[ptr->filter_id]->bpp = 1;
    } else if (vob->im_v_codec == TC_CODEC_RGB24) {
	mfd[ptr->filter_id]->stride = mfd[ptr->filter_id]->post?(vob->ex_v_width*3):(vob->im_v_width*3);
	mfd[ptr->filter_id]->bpp = 3;
    } else {
	tc_log_error (MOD_NAME, "unsupported colorspace");
	return -1;
    }

    // filter init ok.
    if (verbose) tc_log_info(MOD_NAME, "%s %s #%d", MOD_VERSION, MOD_CAP, ptr->filter_id);
    if(mfd[ptr->filter_id]->log)
        fprintf(mfd[ptr->filter_id]->log,"#fps:%f\n",vob->fps);

    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_CLOSE) {

    if (mfd[ptr->filter_id]) {
        fprintf(mfd[ptr->filter_id]->log,"#total: %d",mfd[ptr->filter_id]->frames);
        fclose(mfd[ptr->filter_id]->log);
	free(mfd[ptr->filter_id]);
    }
    mfd[ptr->filter_id]=NULL;

    return(0);

  } /* filter close */

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------

  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  if(((ptr->tag & TC_PRE_M_PROCESS && !mfd[ptr->filter_id]->post) ||
      (ptr->tag & TC_POST_M_PROCESS && mfd[ptr->filter_id]->post)) &&
     !(ptr->attributes & TC_FRAME_IS_SKIPPED))  {

    int y;
    char *p = ptr->video_buf;
    int l,r,t,b;

    if (mfd[ptr->filter_id]->fno++ < 3)
	return 0;

    if (mfd[ptr->filter_id]->start <= ptr->id && ptr->id <= mfd[ptr->filter_id]->end && ptr->id%mfd[ptr->filter_id]->step == mfd[ptr->filter_id]->boolstep) {

    for (y = 0; y < mfd[ptr->filter_id]->y1; y++) {
	if(checkline(p+mfd[ptr->filter_id]->stride*y, mfd[ptr->filter_id]->bpp, ptr->v_width, mfd[ptr->filter_id]->bpp) > mfd[ptr->filter_id]->limit) {
	    mfd[ptr->filter_id]->y1 = y;
	    break;
	}
    }

    for (y=ptr->v_height-1; y>mfd[ptr->filter_id]->y2; y--) {
	if (checkline(p+mfd[ptr->filter_id]->stride*y, mfd[ptr->filter_id]->bpp, ptr->v_width, mfd[ptr->filter_id]->bpp) > mfd[ptr->filter_id]->limit) {
	    mfd[ptr->filter_id]->y2 = y;
	    break;
	}
    }

    for (y = 0; y < mfd[ptr->filter_id]->x1; y++) {
	if(checkline(p+mfd[ptr->filter_id]->bpp*y, mfd[ptr->filter_id]->stride, ptr->v_height, mfd[ptr->filter_id]->bpp) > mfd[ptr->filter_id]->limit) {
	    mfd[ptr->filter_id]->x1 = y;
	    break;
	}
    }

    for (y = ptr->v_width-1; y > mfd[ptr->filter_id]->x2; y--) {
	if(checkline(p+mfd[ptr->filter_id]->bpp*y, mfd[ptr->filter_id]->stride, ptr->v_height, mfd[ptr->filter_id]->bpp) > mfd[ptr->filter_id]->limit) {
	    mfd[ptr->filter_id]->x2 = y;
	    break;
	}
    }

    t = (mfd[ptr->filter_id]->y1+1)&(~1);
    l = (mfd[ptr->filter_id]->x1+1)&(~1);
    b = ptr->v_height - ((mfd[ptr->filter_id]->y2+1)&(~1));
    r = ptr->v_width - ((mfd[ptr->filter_id]->x2+1)&(~1));

    tc_log_info(MOD_NAME, "[detectclipping#%d] valid area: X: %d..%d Y: %d..%d  -> %s %d,%d,%d,%d",
	ptr->filter_id,
	mfd[ptr->filter_id]->x1,mfd[ptr->filter_id]->x2,
	mfd[ptr->filter_id]->y1,mfd[ptr->filter_id]->y2,
	mfd[ptr->filter_id]->post?"-Y":"-j",
	t, l, b, r
	  );
    if(mfd[ptr->filter_id]->log)
        fprintf(mfd[ptr->filter_id]->log, "%d %d %d %d %d\n",
                mfd[ptr->filter_id]->frames,
                t, l , b, r);
    


    }

  }
  if((ptr->tag & TC_PRE_S_PROCESS) && (ptr->tag & TC_VIDEO)){
	  /* ever count the frames, and only analize the non skipped frames */
	  mfd[ptr->filter_id]->frames++;
  }
  
  return(0);
}

