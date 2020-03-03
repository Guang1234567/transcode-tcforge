/*
 *  display.c
 *
 *     Copyright (C) Charles 'Buck' Krasic - April 2000
 *     Copyright (C) Erik Walthinsen - April 2000
 *
 *  This file is part of libdv, a free DV (IEC 61834/SMPTE 314M)
 *  codec.
 *
 *  libdv is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your
 *  option) any later version.
 *
 *  libdv is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  The libdv homepage is http://libdv.sourceforge.net/.
 */

/* Most of this file is derived from patches 101018 and 101136 submitted by
 * Stefan Lucke <lucke@berlin.snafu.de> */


#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include <libdv/dv_types.h>
#include "display.h"

#include "libtc/libtc.h"

#if HAVE_LIBXV
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

// FIXME: Use autoconf for this!!
// MacOSX SDL uses native GUI frameworks, but this is an X11
// application setup.
#ifdef __APPLE__
#undef HAVE_SDL
#endif

static int      dv_display_SDL_init(dv_display_t *dv_dpy, char *w_name, char   *i_name           );
static int      dv_display_gdk_init(dv_display_t *dv_dpy, int  *argc,   char ***argv             );

#if HAVE_SDL
static void dv_center_window(SDL_Surface *screen);
#endif

#if HAVE_LIBXV

#define XV_FORMAT_MASK		0x03
#define XV_FORMAT_ASIS		0x00
#define XV_FORMAT_NORMAL	0x01
#define XV_FORMAT_WIDE		0x02

#define XV_SIZE_MASK		0x0c
#define XV_SIZE_NORMAL		0x04
#define XV_SIZE_QUARTER		0x08

#define XV_NOSAWINDOW		0x10	/* not use at the moment	*/

#define DV_FORMAT_UNKNOWN	-1
#define DV_FORMAT_NORMAL	0
#define DV_FORMAT_WIDE		1

static void dv_display_event (dv_display_t *dv_dpy);
static int dv_display_Xv_init (dv_display_t *dv_dpy, char *w_name,
				char   *i_name, int flags, int size);
#endif

dv_display_t *
dv_display_new(void)
{
  dv_display_t *result;

  result = (dv_display_t *)calloc(1,sizeof(dv_display_t));
  if(!result) goto no_mem;

 no_mem:
  return(result);
} /* dv_display_new */

void
dv_display_show(dv_display_t *dv_dpy) {
  switch(dv_dpy->lib) {
  case e_dv_dpy_Xv:
#if HAVE_LIBXV
    dv_display_event(dv_dpy);
    if (!dv_dpy->dontdraw) {
      XvShmPutImage(dv_dpy->dpy, dv_dpy->port,
		  dv_dpy->win, dv_dpy->gc,
		  dv_dpy->xv_image,
		  0, 0,					        /* sx, sy */
		  dv_dpy->swidth, dv_dpy->sheight,	        /* sw, sh */
		  dv_dpy->lxoff,  dv_dpy->lyoff,                /* dx, dy */
		  dv_dpy->lwidth, dv_dpy->lheight,	        /* dw, dh */
		  True);
      XFlush(dv_dpy->dpy);
    }
#endif /* HAVE_LIBXV */
    break;
  case e_dv_dpy_XShm:
    break;
  case e_dv_dpy_gtk:
    break;
  case e_dv_dpy_SDL:
#if HAVE_SDL
    {
      SDL_Event event;
      if (SDL_PollEvent(&event)) {
	if ( event.type == SDL_KEYDOWN ) {
	  switch(event.key.keysym.sym) {
	    case SDLK_ESCAPE:
	    case SDLK_q:
	      dv_dpy->dontdraw = 1;
	      break;
	    default:
	      break;
	  }
	}
      }

      if (!dv_dpy->dontdraw) {
	SDL_UnlockYUVOverlay(dv_dpy->overlay);
	SDL_DisplayYUVOverlay(dv_dpy->overlay, &dv_dpy->rect);
	SDL_LockYUVOverlay(dv_dpy->overlay);
      } else {
	SDL_Quit();
      }
    }
#endif
    break;
  default:
    break;
  } /* switch */
} /* dv_display_show */

void
dv_display_exit(dv_display_t *dv_dpy) {
  if(!dv_dpy)
    return;

  switch(dv_dpy->lib) {
  case e_dv_dpy_Xv:
#if HAVE_LIBXV

    XvStopVideo(dv_dpy->dpy, dv_dpy->port, dv_dpy->win);

    if(dv_dpy->shminfo.shmaddr)
      shmdt(dv_dpy->shminfo.shmaddr);

    if(dv_dpy->shminfo.shmid > 0)
      shmctl(dv_dpy->shminfo.shmid, IPC_RMID, 0);

    if(dv_dpy->xv_image)
      free(dv_dpy->xv_image);
      dv_dpy->xv_image = NULL;

#endif /* HAVE_LIBXV */
    break;
  case e_dv_dpy_gtk:
    break;
  case e_dv_dpy_XShm:
    break;
  case e_dv_dpy_SDL:
#if HAVE_SDL
    SDL_Quit();
#endif /* HAVE_SDL */
    break;
  } /* switch */

  free(dv_dpy);
  dv_dpy = NULL;
} /* dv_display_exit */

static int
dv_display_gdk_init(dv_display_t *dv_dpy, int *argc, char ***argv) {

  return FALSE;
} /* dv_display_gdk_init */

#if HAVE_LIBXV

static int xv_pause=0;

/* ----------------------------------------------------------------------------
 */
static void
dv_display_event (dv_display_t *dv_dpy)
{
    int	old_pic_format;
    KeySym keysym;
    char buf[16];


  while (XCheckTypedWindowEvent (dv_dpy->dpy, dv_dpy->win,
				 ConfigureNotify, &dv_dpy->event) ||
  XCheckTypedWindowEvent (dv_dpy->dpy, dv_dpy->win,
				 KeyPress, &dv_dpy->event)) {
    switch (dv_dpy->event.type) {
      case ConfigureNotify:
	dv_dpy->dwidth = dv_dpy->event.xconfigure.width;
	dv_dpy->dheight = dv_dpy->event.xconfigure.height;
        /* --------------------------------------------------------------------
         * set current picture format to unknown, so that .._check_format
         * does some work.
         */
        old_pic_format = dv_dpy->pic_format;
        dv_dpy->pic_format = DV_FORMAT_UNKNOWN;
        dv_display_check_format (dv_dpy, old_pic_format);
	break;
      case KeyPress:

	XLookupString (&dv_dpy->event.xkey, buf, 16, &keysym, NULL);

	switch(keysym) {

	case XK_Escape:
	  dv_dpy->dontdraw = 1;
	  xv_pause=0;
	  XvStopVideo(dv_dpy->dpy, dv_dpy->port, dv_dpy->win);
	  XDestroyWindow(dv_dpy->dpy, dv_dpy->win);
	  break;

	case XK_Q:
	case XK_q:
	  xv_pause=0;
	  dv_dpy->dontdraw = (dv_dpy->dontdraw) ? 0:1;
	  break;

	case XK_space:
	  xv_pause = (xv_pause)?0:1;
	  while(xv_pause) {
	    dv_display_event(dv_dpy);
	    usleep(10000);
	  }

	default:
		break;
	}
	break;
      default:
	break;
    } /* switch */
  } /* while */
} /* dv_display_event */

#endif /* HAVE_LIBXV */

/* ----------------------------------------------------------------------------
 */
void
dv_display_set_norm (dv_display_t *dv_dpy, dv_system_t norm)
{
#if HAVE_LIBXV
  dv_dpy->sheight = (norm == e_dv_system_625_50) ? 576: 480;
#endif /* HAVE_LIBXV */
} /* dv_display_set_norm */

/* ----------------------------------------------------------------------------
 */
void
dv_display_check_format(dv_display_t *dv_dpy, int pic_format)
{
#if HAVE_LIBXV
  /*  return immediate if ther is no format change or no format
   * specific flag was set upon initialisation
   */
  if (pic_format == dv_dpy->pic_format ||
      !(dv_dpy->flags & XV_FORMAT_MASK))
    return;

  /* --------------------------------------------------------------------
   * check if there are some aspect ratio constraints
   */
  if (dv_dpy->flags & XV_FORMAT_NORMAL) {
    if (pic_format == DV_FORMAT_NORMAL) {
      dv_dpy->lxoff = dv_dpy->lyoff = 0;
      dv_dpy->lwidth = dv_dpy->dwidth;
      dv_dpy->lheight = dv_dpy->dheight;
    } else if (pic_format == DV_FORMAT_WIDE) {
      dv_dpy->lxoff = 0;
      dv_dpy->lyoff = dv_dpy->dheight / 8;
      dv_dpy->lwidth = dv_dpy->dwidth;
      dv_dpy->lheight = (dv_dpy->dheight * 3) / 4;
    }
  } else if (dv_dpy->flags & XV_FORMAT_WIDE) {
    if (pic_format == DV_FORMAT_NORMAL) {
      dv_dpy->lxoff = dv_dpy->dwidth / 8;
      dv_dpy->lyoff = 0;
      dv_dpy->lwidth = (dv_dpy->dwidth * 3) / 4;
      dv_dpy->lheight = dv_dpy->dheight;
    } else if (pic_format == DV_FORMAT_WIDE) {
      dv_dpy->lxoff = dv_dpy->lyoff = 0;
      dv_dpy->lwidth = dv_dpy->dwidth;
      dv_dpy->lheight = dv_dpy->dheight;
    }
  } else {
    dv_dpy->lwidth = dv_dpy->dwidth;
    dv_dpy->lheight = dv_dpy->dheight;
  }
  dv_dpy->pic_format = pic_format;
#endif /* HAVE_LIBXV */
} /* dv_display_check_format */

#if HAVE_LIBXV
/* ----------------------------------------------------------------------------
 */
static int
dv_display_Xv_init(dv_display_t *dv_dpy, char *w_name, char *i_name,
                   int flags, int size) {
  int		scn_id,
                ad_cnt, fmt_cnt,
                got_port, got_fmt,
                i, k;
  XGCValues	values;
  XSizeHints	hints;
  XWMHints	wmhints;
  XTextProperty	x_wname, x_iname;

  XvAdaptorInfo	*ad_info;
  XvImageFormatValues	*fmt_info;

  if(!(dv_dpy->dpy = XOpenDisplay(NULL))) {
    return 0;
  } /* if */

  dv_dpy->rwin = DefaultRootWindow(dv_dpy->dpy);
  scn_id = DefaultScreen(dv_dpy->dpy);

  /*
   * So let's first check for an available adaptor and port
   */
  if(Success == XvQueryAdaptors(dv_dpy->dpy, dv_dpy->rwin, &ad_cnt, &ad_info)) {

    for(i = 0, got_port = False; i < ad_cnt; ++i) {
      tc_log_msg(__FILE__,
		 "Xv: %s: ports %ld - %ld",
		 ad_info[i].name,
		 ad_info[i].base_id,
		 ad_info[i].base_id +
		 ad_info[i].num_ports - 1);

      if (dv_dpy->arg_xv_port != 0 &&
	      (dv_dpy->arg_xv_port < ad_info[i].base_id ||
	       dv_dpy->arg_xv_port >= ad_info[i].base_id+ad_info[i].num_ports)) {
	  tc_log_msg(__FILE__,
		    "Xv: %s: skipping (looking for port %i)",
		    ad_info[i].name,
		    dv_dpy->arg_xv_port);
	  continue;
      }

      if (!(ad_info[i].type & XvImageMask)) {
	tc_log_warn(__FILE__,
		    "Xv: %s: XvImage NOT in capabilty list (%s%s%s%s%s )",
		    ad_info[i].name,
		    (ad_info[i].type & XvInputMask) ? " XvInput"  : "",
		    (ad_info[i]. type & XvOutputMask) ? " XvOutput" : "",
		    (ad_info[i]. type & XvVideoMask)  ?  " XvVideo"  : "",
		    (ad_info[i]. type & XvStillMask)  ?  " XvStill"  : "",
		    (ad_info[i]. type & XvImageMask)  ?  " XvImage"  : "");
	continue;
      } /* if */
      fmt_info = XvListImageFormats(dv_dpy->dpy, ad_info[i].base_id,&fmt_cnt);
      if (!fmt_info || fmt_cnt == 0) {
	tc_log_warn(__FILE__, "Xv: %s: NO supported formats", ad_info[i].name);
	continue;
      } /* if */
      for(got_fmt = False, k = 0; k < fmt_cnt; ++k) {
	if (dv_dpy->format == fmt_info[k].id) {
	  got_fmt = True;
	  break;
	} /* if */
      } /* for */
      if (!got_fmt) {
	char tmpbuf[1000];
	*tmpbuf = 0;
	for (k = 0; k < fmt_cnt; ++k) {
	  tc_snprintf(tmpbuf+strlen(tmpbuf), sizeof(tmpbuf)-strlen(tmpbuf),
		      "%s%#08x[%s]", k>0 ? " " : "", fmt_info[k].id,
		      fmt_info[k].guid);
	}
	tc_log_warn(__FILE__,
		    "Xv: %s: format %#08x is NOT in format list (%s)",
		    ad_info[i].name,
		    dv_dpy->format,
		    tmpbuf);
	continue;
      } /* if */

      for(dv_dpy->port = ad_info[i].base_id, k = 0;
	  k < ad_info[i].num_ports;
	  ++k, ++(dv_dpy->port)) {
	if (dv_dpy->arg_xv_port != 0 && dv_dpy->arg_xv_port != dv_dpy->port) continue;
	if(!XvGrabPort(dv_dpy->dpy, dv_dpy->port, CurrentTime)) {
	  tc_log_msg(__FILE__, "Xv: grabbed port %ld",
		     dv_dpy->port);
	  got_port = True;
	  break;
	} /* if */
      } /* for */
      if(got_port)
	break;
    } /* for */

  } else {
    /* Xv extension probably not present */
    return 0;
  } /* else */

  if(!ad_cnt) {
    tc_log_warn(__FILE__, "Xv: (ERROR) no adaptor found!");
    return 0;
  }
  if(!got_port) {
    tc_log_warn(__FILE__, "Xv: (ERROR) could not grab any port!");
    return 0;
  }

  /* --------------------------------------------------------------------------
   * default settings which allow arbitraray resizing of the window
   */
  hints.flags = PSize | PMaxSize | PMinSize;
  hints.min_width = dv_dpy->width / 16;
  hints.min_height = dv_dpy->height / 16;

  /* --------------------------------------------------------------------------
   * maximum dimensions for Xv support are about 2048x2048
   */
  hints.max_width = 2048;
  hints.max_height = 2048;

  wmhints.input = True;
  wmhints.flags = InputHint;

  XStringListToTextProperty(&w_name, 1 ,&x_wname);
  XStringListToTextProperty(&i_name, 1 ,&x_iname);

  /*
   * default settings: source, destination and logical widht/height
   * are set to our well known dimensions.
   */
  dv_dpy->lwidth = dv_dpy->dwidth = dv_dpy->swidth = dv_dpy->width;
  dv_dpy->lheight = dv_dpy->dheight = dv_dpy->sheight = dv_dpy->height;
  dv_dpy->lxoff = dv_dpy->lyoff = 0;
  dv_dpy-> flags = flags;

  if (flags & XV_FORMAT_MASK) {
    dv_dpy->lwidth = dv_dpy->dwidth = 768;
    dv_dpy->lheight = dv_dpy->dheight = 576;
    dv_dpy->pic_format = DV_FORMAT_UNKNOWN;
    if (flags & XV_FORMAT_WIDE) {
      dv_dpy->lwidth = dv_dpy->dwidth = 1024;
    }
  }
  if (size) {
    dv_dpy->lwidth  = (int)(((double)dv_dpy->lwidth  * (double)size)/100.0);
    dv_dpy->lheight = (int)(((double)dv_dpy->lheight * (double)size)/100.0);
    dv_dpy->dwidth  = (int)(((double)dv_dpy->dwidth  * (double)size)/100.0);
    dv_dpy->dheight = (int)(((double)dv_dpy->dheight * (double)size)/100.0);
  }
  if (flags & XV_FORMAT_MASK) {
    hints.flags |= PAspect;
    if (flags & XV_FORMAT_WIDE) {
      hints.min_aspect.x = hints.max_aspect.x = 1024;
    } else {
      hints.min_aspect.x = hints.max_aspect.x = 768;
    }
    hints.min_aspect.y = hints.max_aspect.y = 576;
  }

  if (!(flags & XV_NOSAWINDOW)) {
    dv_dpy->win = XCreateSimpleWindow(dv_dpy->dpy,
				       dv_dpy->rwin,
				       0, 0,
				       dv_dpy->dwidth, dv_dpy->dheight,
				       0,
				       XWhitePixel(dv_dpy->dpy, scn_id),
				       XBlackPixel(dv_dpy->dpy, scn_id));
  } else {
  }
  XSetWMProperties(dv_dpy->dpy, dv_dpy->win,
		    &x_wname, &x_iname,
		    NULL, 0,
		    &hints, &wmhints, NULL);

  XSelectInput(dv_dpy->dpy, dv_dpy->win, ExposureMask | StructureNotifyMask | KeyPressMask);
  XMapRaised(dv_dpy->dpy, dv_dpy->win);
  XNextEvent(dv_dpy->dpy, &dv_dpy->event);

  dv_dpy->gc = XCreateGC(dv_dpy->dpy, dv_dpy->win, 0, &values);

  /*
   * Now we do shared memory allocation etc..
   */
  dv_dpy->xv_image = XvShmCreateImage(dv_dpy->dpy, dv_dpy->port,
					 dv_dpy->format, dv_dpy->pixels[0],
				      	 dv_dpy->width, dv_dpy->height,
					 &dv_dpy->shminfo);

  dv_dpy->shminfo.shmid = shmget(IPC_PRIVATE,
				     dv_dpy->len,
				     IPC_CREAT | 0777);

  dv_dpy->xv_image->data = dv_dpy->pixels[0] = dv_dpy->shminfo.shmaddr =
    shmat(dv_dpy->shminfo.shmid, 0, 0);

  XShmAttach(dv_dpy->dpy, &dv_dpy->shminfo);
  XSync(dv_dpy->dpy, False);

  return 1;
} /* dv_display_Xv_init */
#endif /* HAVE_LIBXV */


#if HAVE_SDL

static void
dv_center_window(SDL_Surface *screen)
{
    SDL_SysWMinfo info;

    SDL_VERSION(&info.version);
    if ( SDL_GetWMInfo(&info) > 0 ) {
        int x, y;
        int w, h;
        if ( info.subsystem == SDL_SYSWM_X11 ) {
            info.info.x11.lock_func();
            w = DisplayWidth(info.info.x11.display,
                             DefaultScreen(info.info.x11.display));
            h = DisplayHeight(info.info.x11.display,
                             DefaultScreen(info.info.x11.display));
            x = (w - screen->w)/2;
            y = (h - screen->h)/2;
            XMoveWindow(info.info.x11.display, info.info.x11.wmwindow, x, y);
            info.info.x11.unlock_func();
        } /* if */
    } /* if  */
} /* dv_center_window */

static int
dv_display_SDL_init(dv_display_t *dv_dpy, char *w_name, char *i_name) {
  const SDL_VideoInfo *video_info;
  int video_bpp;

  if(SDL_Init(SDL_INIT_VIDEO) < 0) goto no_sdl;
  /* Get the "native" video mode */
  video_info = SDL_GetVideoInfo();
  switch (video_info->vfmt->BitsPerPixel) {
  case 16:
  case 32:
    video_bpp = video_info->vfmt->BitsPerPixel;
    break;
  default:
    video_bpp = 16;
    break;
  } /* switch  */
  dv_dpy->sdl_screen = SDL_SetVideoMode(dv_dpy->width,dv_dpy->height,
					video_bpp,SDL_HWSURFACE);
  SDL_WM_SetCaption(w_name, i_name);
  dv_dpy->overlay = SDL_CreateYUVOverlay(dv_dpy->width, dv_dpy->height, dv_dpy->format,
					 dv_dpy->sdl_screen);
  if((!dv_dpy->overlay || (!dv_dpy->overlay->hw_overlay) ||  /* we only want HW overlays */
      SDL_LockYUVOverlay(dv_dpy->overlay)<0)) {
    goto no_overlay;
  } /* if */
  dv_center_window(dv_dpy->sdl_screen);
  dv_dpy->rect.x = 0;
  dv_dpy->rect.y = 0;
  dv_dpy->rect.w = dv_dpy->overlay->w;
  dv_dpy->rect.h = dv_dpy->overlay->h;
  dv_dpy->pixels[0] = dv_dpy->overlay->pixels[0];
  dv_dpy->pixels[1] = dv_dpy->overlay->pixels[1];
  dv_dpy->pixels[2] = dv_dpy->overlay->pixels[2];
  dv_dpy->pitches[0] = dv_dpy->overlay->pitches[0];
  dv_dpy->pitches[1] = dv_dpy->overlay->pitches[1];
  dv_dpy->pitches[2] = dv_dpy->overlay->pitches[2];
  return(True);

 no_overlay:
  if(dv_dpy->overlay)
    SDL_FreeYUVOverlay(dv_dpy->overlay);
  SDL_Quit();
 no_sdl:
  return(False);

} /* dv_display_SDL_init */

#else

static int
dv_display_SDL_init(dv_display_t *dv_dpy, char *w_name, char *i_name) {
  tc_log_warn(__FILE__,"playdv was compiled without SDL support");
  return(FALSE);
} /* dv_display_SDL_init */

#endif /* HAVE_SDL */

int
dv_display_init(dv_display_t *dv_dpy, int *argc, char ***argv, int width, int height,
		dv_sample_t sampling, char *w_name, char *i_name) {

  dv_dpy->width = width;
  dv_dpy->height = height;

  dv_dpy->dontdraw = 0;

  switch(sampling) {

  case e_dv_sample_420:
    dv_dpy->format = DV_FOURCC_YV12;
    dv_dpy->len = (dv_dpy->width * dv_dpy->height * 3) / 2;
    break;

  default:
    /* Not possible */
    break;
  } /* switch */

  switch(dv_dpy->arg_display) {
  case 0:
    /* Autoselect */
#if HAVE_LIBXV
    /* Try to use Xv first, then SDL */
    if(dv_display_Xv_init(dv_dpy, w_name, i_name,
			  dv_dpy->arg_aspect_val,
			  dv_dpy->arg_size_val)) {
      goto Xv_ok;
    } else
#endif /* HAVE_LIBXV */
    if(dv_display_SDL_init(dv_dpy, w_name, i_name)) {
      goto SDL_ok;
    } else {
      goto use_gtk;
    } /* else */
    break;
  case 1:
    /* Gtk */
    goto use_gtk;
    break;
  case 2:
#if HAVE_LIBXV
    /* Xv */
    if(dv_display_Xv_init(dv_dpy, w_name, i_name,
			  dv_dpy->arg_aspect_val,
			  dv_dpy->arg_size_val)) {
      goto Xv_ok;
    } else {
      tc_log_error(__FILE__, "Attempt to display via Xv failed");
      goto fail;
    }
#else /* HAVE_LIBXV */
    tc_log_error(__FILE__, "Attempt to display via Xv failed");
    goto fail;
#endif /* HAVE_LIBXV */
    break;
  case 3:
    /* SDL */
    if(dv_display_SDL_init(dv_dpy, w_name, i_name)) {
      goto SDL_ok;
    } else {
      tc_log_error(__FILE__, "Attempt to display via SDL failed");
      goto fail;
    }
    break;
  default:
    break;
  } /* switch */

#if HAVE_LIBXV
 Xv_ok:
  tc_log_info(__FILE__, " Using Xv for display");
  dv_dpy->lib = e_dv_dpy_Xv;
  goto yuv_ok;
#endif /* HAVE_LIBXV */

 SDL_ok:
  tc_log_info(__FILE__, " Using SDL for display");
  dv_dpy->lib = e_dv_dpy_SDL;
  goto yuv_ok;

 yuv_ok:

  dv_dpy->color_space = e_dv_color_yuv;

  switch(dv_dpy->format) {
  case DV_FOURCC_YUY2:
    dv_dpy->pitches[0] = width * 2;
    break;
  case DV_FOURCC_YV12:
    dv_dpy->pixels[1] = dv_dpy->pixels[0] + (width * height);
    dv_dpy->pixels[2] = dv_dpy->pixels[1] + (width * height / 4);
    dv_dpy->pitches[0] = width;
    dv_dpy->pitches[1] = width / 2;
    dv_dpy->pitches[2] = width / 2;
    break;
  } /* switch */

  goto ok;

 use_gtk:

  /* Try to use GDK since we couldn't get a HW YUV surface */
  dv_dpy->color_space = e_dv_color_rgb;
  dv_dpy->lib = e_dv_dpy_gtk;
  dv_dpy->len = dv_dpy->width * dv_dpy->height * 3;
  if(!dv_display_gdk_init(dv_dpy, argc, argv)) {
    tc_log_error(__FILE__, "Attempt to use gtk for display failed");
    goto fail;
  } /* if  */
  dv_dpy->pitches[0] = width * 3;
  tc_log_info(__FILE__, " Using gtk for display");

 ok:
  return(TRUE);

 fail:
  tc_log_error(__FILE__, " Unable to establish a display method");
  return(FALSE);
} /* dv_display_init */
