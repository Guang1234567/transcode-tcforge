/*
 *  filter_preview.c
 *
 *  Copyright (C) Thomas Oestreich - December 2001
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

#define MOD_NAME    "filter_preview.so"
#define MOD_VERSION "v0.1.4 (2002-10-08)"
#define MOD_CAP     "xv/sdl/gtk preview plugin"
#define MOD_AUTHOR  "Thomas Oestreich"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtcutil/optstr.h"

#include "filter_preview.h"

static char buffer[128];
static int size=0;
static int use_secondary_buffer=0;
static char *undo_buffer = NULL;

static int preview_delay=0;

/* global variables */

static dv_player_t *dv_player = NULL;

static dv_player_t *dv_player_new(void)
{
    dv_player_t *result;

    if(!(result = calloc(1,sizeof(dv_player_t)))) goto no_mem;
    if(!(result->display = dv_display_new())) goto no_display;

    return(result);

 no_display:
    free(result);
    result = NULL;
 no_mem:
    return(result);
} // dv_player_new


/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  vob_t *vob=NULL;

  int pre=0, vid=0;

  if(ptr->tag & TC_FILTER_GET_CONFIG) {
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYO", "1");
  }

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_INIT) {

    int w, h;

    if((vob = tc_get_vob())==NULL) return(-1);

    // filter init ok.

    if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

    if(verbose) tc_log_info(MOD_NAME, "options=%s", options);

    tc_snprintf(buffer, sizeof(buffer), "%s-%s", PACKAGE, VERSION);

    if(dv_player != NULL) return(-1);
    if(!(dv_player = dv_player_new())) return(-1);

    //init filter

    dv_player->display->arg_display=0;

    if(options!=NULL) {
      if(strcasecmp(options,"help")==0) return -1;
      if(strcasecmp(options,"gtk")==0) dv_player->display->arg_display=1;
      if(strcasecmp(options,"sdl")==0) dv_player->display->arg_display=3;
      if(strcasecmp(options,"xv")==0)  dv_player->display->arg_display=2;
    }

    w = vob->ex_v_width;
    h = vob->ex_v_height;

    if(verbose) tc_log_info(MOD_NAME, "preview window %dx%d", w, h);

    switch(vob->im_v_codec) {

    case TC_CODEC_RGB24:

      if(!dv_display_init(dv_player->display, 0, NULL,
			  w, h, e_dv_color_rgb,
			  buffer, buffer)) return(-1);

      size = w * h * 3;
      break;

    case TC_CODEC_YUV420P:

      if(!dv_display_init(dv_player->display, 0, NULL,
			  w, h, e_dv_sample_420,
			  buffer, buffer)) return(-1);

      size = w*h* 3/2;
      break;

    case TC_CODEC_RAW:

      if(!dv_display_init(dv_player->display, 0, NULL,
			  w, h, e_dv_sample_420,
			  buffer, buffer)) return(-1);
      size = w*h* 3/2;

      use_secondary_buffer=1;

      break;

    default:
      tc_log_error(MOD_NAME, "codec not supported for preview");
      return(-1);
    }

    if ((undo_buffer = (char *) malloc (size)) == NULL) {
      tc_log_error(MOD_NAME, "codec not supported for preview");
      return (-1);
    }

    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_CLOSE) {

    if(size) dv_display_exit(dv_player->display);

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

  if(verbose & TC_STATS) tc_log_info(MOD_NAME, "%s/%s %s %s", vob->mod_path, MOD_NAME, MOD_VERSION, MOD_CAP);

  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  pre = (ptr->tag & TC_PREVIEW)? 1:0;
  vid = (ptr->tag & TC_VIDEO)? 1:0;

  if(pre && vid) {

    //0.6.2 (secondaray buffer for pass-through mode)
    (use_secondary_buffer) ? ac_memcpy(dv_player->display->pixels[0], (char*) ptr->video_buf2, size) : ac_memcpy(dv_player->display->pixels[0], (char*) ptr->video_buf, size);

    //display video frame
    dv_display_show(dv_player->display);

    //0.6.2
    usleep(preview_delay);
  }

  return(0);
}
