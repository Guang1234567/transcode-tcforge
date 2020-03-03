/*
 *  pv.h
 *
 *  Copyright (C) Thomas Oestreich - October 2002
 *
 *  based on "display.c
 *     Copyright (C) Charles 'Buck' Krasic - April 2000
 *     Copyright (C) Erik Walthinsen - April 2000
 *  part of libdv, a free DV (IEC 61834/SMPTE 314M) codec.
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

#ifndef PV_H
#define PV_H

#include "src/transcode.h"
#include "libtc/libtc.h"

#include <sys/types.h>
#include <sys/mman.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xvlib.h>
#include <X11/Xatom.h>

#define DV_FOURCC_I420  0x30323449	/* 4:2:0 Planar mode: Y + U + V  (3 planes) */
#define DV_FOURCC_YV12  0x32315659	/* 4:2:0 Planar mode: Y + V + U  (3 planes) */
#define DV_FOURCC_YUY2  0x32595559	/* 4:2:2 Packed mode: Y0+U0+Y1+V0 (1 plane) */
#define DV_FOURCC_UYVY  0x59565955	/* 4:2:2 Packed mode: U0+Y0+V0+Y1 (1 plane) */

#define DV_DISPLAY_OPT_METHOD   0
#define DV_DISPLAY_OPT_ASPECT	1
#define DV_DISPLAY_OPT_SIZE	2
#define DV_DISPLAY_OPT_CALLBACK 3
#define DV_DISPLAY_OPT_XV_PORT  4
#define DV_DISPLAY_NUM_OPTS     5

typedef enum dv_dpy_lib_e {
  e_dv_dpy_Xv,
  e_dv_dpy_SDL,
  e_dv_dpy_gtk,
  e_dv_dpy_XShm,
} dv_dpy_lib_t;


typedef enum color_space_e {
  e_dv_color_yuv,
  e_dv_color_rgb,
  e_dv_color_bgr0,
} dv_color_space_t;


typedef struct {
  dv_color_space_t color_space;
  int		    width, height;
  char             *pixels[3];
  int              pitches[3];
  int              dontdraw;

  /* Begin Private */
  dv_dpy_lib_t       lib;
  uint32_t           len;
  uint32_t           format;   /* fourcc code for YUV modes */

  /* -----------------------------------------------------------
   * Xv specific members
   */
  Display          *dpy;
  Screen           *scn;
  Window            rwin, win;
  int		    dwidth, dheight,
		    swidth, sheight,
		    lwidth, lheight,
		    lxoff, lyoff,
		    flags,
		    pic_format;
  GC                gc;
  Atom              wm_delete_window_atom;
  XEvent            event;
  XvPortID	    port;
  XShmSegmentInfo   shminfo;
  XvImage          *xv_image;

  int 			arg_display,
			arg_aspect_val,
			arg_size_val,
			arg_xv_port;
  char			*arg_aspect_string;
  char                  full_screen;
} xv_display_t;

/* Book-keeping for mmap */
typedef struct dv_mmap_region_s {
  void   *map_start;  /* Start of mapped region (page aligned) */
  size_t  map_length; /* Size of mapped region */
  uint8_t *data_start; /* Data we asked for */
} xv_mmap_region_t;

typedef struct {

  xv_display_t    *display;
  xv_mmap_region_t mmap_region;

  struct stat     statbuf;
  struct timeval  tv[3];
  int             arg_disable_audio;
  int             arg_disable_video;
  int             arg_num_frames;
  int             arg_dump_frames;
} xv_player_t;

#ifdef __cplusplus
extern "C" {
#endif

  extern xv_player_t *xv_player_new(void);
  extern xv_display_t *xv_display_new(void);
  extern int xv_display_init(xv_display_t *dpy,
			     int *argc, char ***argv,
			     int width, int height,
			     char *w_name, char *i_name, int yuv422);

  extern void xv_display_show(xv_display_t *dv_dpy);
  extern void xv_display_event(xv_display_t *dv_dpy);
  extern void xv_display_exit(xv_display_t *dv_dpy);
  extern void xv_display_check_format(xv_display_t *dv_dpy, int pic_format);

  void preview_filter(void);
  int preview_filter_buffer(int frames_needed);
  void dec_preview_delay(void);
  void inc_preview_delay(void);
  void preview_cache_undo(void);
  void preview_toggle_skip(void);
  void preview_cache_draw(int d);
  void preview_cache_submit(char *buf, int n, int flag);
  int preview_cache_init(void);
  int preview_grab_jpeg(void);

  char **char2bmp(char c);
  void bmp2img(char *img, char **c, int width, int height,
	       int char_width, int char_height, int posx, int posy, int codec);
  void str2img(char *img, char *c, int width,  int height,
	       int char_width, int char_height, int posx, int posy, int codec);

 extern int cache_long_skip;
 extern int cache_short_skip;

 int DoSelection(XButtonEvent *ev, int *xanf, int *yanf, int *xend, int *yend);

#ifdef __cplusplus
}
#endif

#endif // PV_H

