/*
 *  pv.c
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


#include "pv.h"
#include "src/socket.h"

#include <sys/ipc.h>
#include <sys/shm.h>
#include <math.h>


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

static int xv_display_Xv_init (xv_display_t *dv_dpy, char *w_name,
			       char *i_name, int flags, int size);


xv_player_t *xv_player_new(void)
{
  xv_player_t *result;

  if(!(result = calloc(1,sizeof(xv_player_t)))) goto no_mem;
  if(!(result->display = xv_display_new())) goto no_display;

  return(result);

 no_display:
  free(result);
  result = NULL;
 no_mem:
  return(result);
} // xv_player_new


xv_display_t *xv_display_new(void)
{
  xv_display_t *result;

  result = (xv_display_t *)calloc(1,sizeof(xv_display_t));
  if(!result) goto no_mem;

 no_mem:
  return(result);
} // xv_display_new


void xv_display_show(xv_display_t *dv_dpy) {

  xv_display_event(dv_dpy);

  if (!dv_dpy->dontdraw) {
    XvShmPutImage(dv_dpy->dpy, dv_dpy->port,
		  dv_dpy->win, dv_dpy->gc,
		  dv_dpy->xv_image,
		  0, 0,			                /* sx, sy */
		  dv_dpy->swidth, dv_dpy->sheight,	/* sw, sh */
		  dv_dpy->lxoff,  dv_dpy->lyoff,        /* dx, dy */
		  dv_dpy->lwidth, dv_dpy->lheight,	/* dw, dh */
		  True);
    XFlush(dv_dpy->dpy);
  }

} // xv_display_show


void xv_display_exit(xv_display_t *dv_dpy) {

  if(!dv_dpy) return;

  XvStopVideo(dv_dpy->dpy, dv_dpy->port, dv_dpy->win);

  if(dv_dpy->shminfo.shmaddr)
    shmdt(dv_dpy->shminfo.shmaddr);

  if(dv_dpy->shminfo.shmid > 0)
    shmctl(dv_dpy->shminfo.shmid, IPC_RMID, 0);

  if(dv_dpy->xv_image) free(dv_dpy->xv_image);
  dv_dpy->xv_image = NULL;

  free(dv_dpy);
  dv_dpy = NULL;
} // xv_display_exit

static int xv_pause=0;

static void xv_window_close(xv_display_t *dv_dpy)
{
    dv_dpy->dontdraw = 1;

    XvStopVideo(dv_dpy->dpy, dv_dpy->port, dv_dpy->win);
    XDestroyWindow(dv_dpy->dpy, dv_dpy->win);

    tc_socket_submit ("[filter_pv]: preview window close\n");
    //reset
    xv_pause=0;
}


void xv_display_event (xv_display_t *dv_dpy)
{
  int	old_pic_format;
  KeySym keysym;
  char buf[16];
  XButtonEvent *but_event;
  static int x1, y1, x2, y2;
  TCSockPVCmd pvcmd;

  tc_socket_get_pv_cmd(&pvcmd);

  while ( pvcmd.cmd || XPending(dv_dpy->dpy) )  {

    // tibit: Poll for a socket message
    if (pvcmd.cmd) {
	//tc_log_msg(__FILE__, "Got char (%c)", pvcmd.cmd);
	//tc_log_msg(__FILE__, "FILTER: (%d)", arg);
	switch (pvcmd.cmd) {
	    case TC_SOCK_PV_DRAW:
		preview_filter_buffer(pvcmd.arg?pvcmd.arg:1);
		break;
	    case TC_SOCK_PV_UNDO:
		preview_cache_undo();
		break;
	    case TC_SOCK_PV_SLOW_FW:
		preview_cache_draw(cache_short_skip);
		break;
	    case TC_SOCK_PV_SLOW_BW:
		preview_cache_draw(-cache_short_skip);
		break;
	    case TC_SOCK_PV_FAST_FW:
		preview_cache_draw(cache_long_skip);
		break;
	    case TC_SOCK_PV_FAST_BW:
		preview_cache_draw(-cache_long_skip);
		break;
	    case TC_SOCK_PV_ROTATE:
		//tc_outstream_rotate_request(); // FIXME
		break;
	    case TC_SOCK_PV_FASTER:
		dec_preview_delay();
		break;
	    case TC_SOCK_PV_SLOWER:
		inc_preview_delay();
		break;
	    case TC_SOCK_PV_TOGGLE:
		preview_toggle_skip();
		break;
	    case TC_SOCK_PV_SAVE_JPG:
		preview_grab_jpeg();
		break;
	    case TC_SOCK_PV_DISPLAY:
		xv_pause=0;
		dv_dpy->dontdraw = (dv_dpy->dontdraw) ? 0:1;
		break;
	    case TC_SOCK_PV_PAUSE:
		xv_pause = (xv_pause)?0:1;
		while(xv_pause) {
		    xv_display_event(dv_dpy);
		    usleep(10000);
		}
		break;
	    default:
		break;
	} // switch msg
	pvcmd.cmd = TC_SOCK_PV_NONE;
    } else {

    // remove Event
    XNextEvent(dv_dpy->dpy, &dv_dpy->event);


    switch (dv_dpy->event.type) {

	case ClientMessage:
	    // stolen from xmms opengl_plugin  -- tibit
	    if ((Atom)(dv_dpy->event.xclient.data.l[0]) == dv_dpy->wm_delete_window_atom)
	    {
		xv_window_close (dv_dpy);
	    }
	    break;

    case ConfigureNotify:
      dv_dpy->dwidth = dv_dpy->event.xconfigure.width;
      dv_dpy->dheight = dv_dpy->event.xconfigure.height;
      /* --------------------------------------------------------------------
       * set current picture format to unknown, so that .._check_format
       * does some work.
       */
      old_pic_format = dv_dpy->pic_format;
      dv_dpy->pic_format = DV_FORMAT_UNKNOWN;
      xv_display_check_format(dv_dpy, old_pic_format);
      break;

    case ButtonPress:

      but_event = (XButtonEvent *) &dv_dpy->event;
      if (DoSelection(but_event, &x1, &y1, &x2, &y2)) {
	      // min <-> max
	      int xanf = (x1<x2)?x1:x2;
	      int yanf = (y1<y2)?y1:y2;
	      int xend = (x1>x2)?x1:x2;
	      int yend = (y1>y2)?y1:y2;
	      char buf[255];

	      tc_snprintf(buf, sizeof(buf), "[%s]: pos1=%dx%d pos2=%dx%d pos=%dx%d:size=%dx%d -Y %d,%d,%d,%d\n",
			      "filter_pv", xanf, yanf, xend, yend, xanf, yanf, xend-xanf, yend-yanf,
			      yanf, xanf,  dv_dpy->height-yend, dv_dpy->width - xend);

	      //print to socket
	      tc_socket_submit (buf);
	      //print elsewhere
	      tc_log_msg(__FILE__, "%s", buf);
	      // white
	      XSetForeground(dv_dpy->dpy,dv_dpy->gc, 0xFFFFFFFFUL);
	      XDrawRectangle(dv_dpy->dpy,dv_dpy->win,dv_dpy->gc,
			      xanf, yanf, (unsigned int)xend-xanf, (unsigned int)yend-yanf);

      }

      break;

    case KeyPress:

      XLookupString (&dv_dpy->event.xkey, buf, 16, &keysym, NULL);

      switch(keysym) {

      case XK_Escape:
	xv_window_close (dv_dpy);
	break;

      case XK_u:
      case XK_U:
	preview_cache_undo();
	break;

      case XK_Q:
      case XK_q:
	xv_pause=0;
	dv_dpy->dontdraw = (dv_dpy->dontdraw) ? 0:1;
	break;

      case XK_Up:
	preview_cache_draw(cache_long_skip);
	break;

      case XK_Down:
	preview_cache_draw(-cache_long_skip);
	break;

      case XK_Left:
	preview_cache_draw(-cache_short_skip);
	break;

      case XK_Right:
	preview_cache_draw(cache_short_skip);
	break;

      case XK_R:
      case XK_r:
//	tc_outstream_rotate_request(); // FIXME
	break;

      case XK_s:
      case XK_S:
	inc_preview_delay();
	break;

      case XK_f:
      case XK_F:
	dec_preview_delay();
	break;

      case XK_y:
      case XK_Y:
	preview_toggle_skip();
	break;

      case XK_j:
      case XK_J:
	preview_grab_jpeg();
	break;

#if 0
      case XK_a:
      case XK_A:
	preview_filter();
	break;
#endif

      case XK_Return:
	xv_display_show(dv_dpy);
	break;

      case XK_space:
	xv_pause = (xv_pause)?0:1;
	while(xv_pause) {
	  xv_display_event(dv_dpy);
	  //xv_display_show(dv_dpy);
	  usleep(10000);
	}
      default:
	break;
      }
    default:
      break;
    } /* switch */
  } /* else */
  } /* while */
} // xv_display_event


void xv_display_check_format(xv_display_t *dv_dpy, int pic_format)
{
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
} // xv_display_check_format


static int xv_display_Xv_init(xv_display_t *dv_dpy, char *w_name, char *i_name,
			      int flags, int size)
{

  int scn_id, ad_cnt, fmt_cnt, got_port, got_fmt;
  int i, k;

  XGCValues	values;
  XSizeHints	hints;
  XWMHints	wmhints;
  XTextProperty	x_wname, x_iname;

  Atom wm_protocols[1];

  XvAdaptorInfo	*ad_info;
  XvImageFormatValues *fmt_info;

  if(!(dv_dpy->dpy = XOpenDisplay(NULL))) return 0;

  dv_dpy->rwin = DefaultRootWindow(dv_dpy->dpy);
  scn_id = DefaultScreen(dv_dpy->dpy);

  /*
   * So let's first check for an available adaptor and port
   */
  /* Note: this is identical to the similar section in display.c  --AC */

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
   * default settings which allow arbitrary resizing of the window
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
   * default settings: source, destination and logical width/height
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

    if(dv_dpy->full_screen) {
      int  screen = XDefaultScreen(dv_dpy->dpy);
      dv_dpy->lwidth = dv_dpy->dwidth =DisplayWidth(dv_dpy->dpy, screen);
      dv_dpy->lheight = dv_dpy->dheight = DisplayHeight(dv_dpy->dpy, screen);
    }

    dv_dpy->win = XCreateSimpleWindow(dv_dpy->dpy,
				       dv_dpy->rwin,
				       0, 0,
				       dv_dpy->dwidth, dv_dpy->dheight,
				       0,
				       XWhitePixel(dv_dpy->dpy, scn_id),
				       XBlackPixel(dv_dpy->dpy, scn_id));

    if(dv_dpy->full_screen) {
      static Atom           XA_WIN_STATE = None;
      long                  propvalue[2];

      XA_WIN_STATE = XInternAtom (dv_dpy->dpy, "_NET_WM_STATE", False);
      propvalue[0] = XInternAtom (dv_dpy->dpy, "_NET_WM_STATE_FULLSCREEN", False);
      propvalue[1] = 0;

      XChangeProperty (dv_dpy->dpy, dv_dpy->win, XA_WIN_STATE, XA_ATOM,
                       32, PropModeReplace, (unsigned char *)propvalue, 1);
    }
  } else {
  }

  XSetWMProperties(dv_dpy->dpy, dv_dpy->win,
		    &x_wname, &x_iname,
		    NULL, 0,
		    &hints, &wmhints, NULL);


  XSelectInput(dv_dpy->dpy, dv_dpy->win, ExposureMask | StructureNotifyMask |
		                         KeyPressMask | ButtonPressMask);

  dv_dpy->wm_delete_window_atom = wm_protocols[0] =
      XInternAtom(dv_dpy->dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(dv_dpy->dpy, dv_dpy->win, wm_protocols, 1);

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
} // xv_display_Xv_init

int xv_display_init(xv_display_t *dv_dpy, int *argc, char ***argv, int width, int height, char *w_name, char *i_name, int yuv422) {

  dv_dpy->width = width;
  dv_dpy->height = height;

  dv_dpy->dontdraw = 0;

  dv_dpy->format = yuv422 ? DV_FOURCC_YUY2 : DV_FOURCC_I420;
  dv_dpy->len = (dv_dpy->width * dv_dpy->height * 3) / 2;
  if (yuv422) dv_dpy->len = dv_dpy->width * dv_dpy->height * 2;

  /* Xv */
  if(xv_display_Xv_init(dv_dpy, w_name, i_name,
			dv_dpy->arg_aspect_val,
			dv_dpy->arg_size_val)) {
    goto Xv_ok;
  } else {
    tc_log_error(__FILE__, "Attempt to display via Xv failed");
    goto fail;
  }

 Xv_ok:
  tc_log_info(__FILE__, "Using Xv for display");
  dv_dpy->lib = e_dv_dpy_Xv;
  dv_dpy->color_space = e_dv_color_yuv;


  switch(dv_dpy->format) {

  case DV_FOURCC_YUY2:
    dv_dpy->pitches[0] = width * 2;
    break;

  case DV_FOURCC_I420:
    dv_dpy->pixels[1] = dv_dpy->pixels[0] + (width * height);
    dv_dpy->pixels[2] = dv_dpy->pixels[1] + (width * height / 4);

    dv_dpy->pitches[0] = width;
    dv_dpy->pitches[1] = width / 2;
    dv_dpy->pitches[2] = width / 2;
    break;
  }

  return(0);

 fail:
  tc_log_error(__FILE__, "Unable to establish a display method");
  return(-1);
} // xv_display_init


// returns 1 if a selection is complete (2nd click)

int DoSelection(XButtonEvent *ev, int *xanf, int *yanf, int *xend, int *yend)
{
  int          rv;
  static Time  lastClickTime   = 0;
  static int   lastClickButton = Button3;


  /* make sure it's even vaguely relevant */
  if (ev->type   != ButtonPress) return 0;

  rv = 0;

  if (ev->button == Button1) {
    /* double clicked B1 ? */
      // save
    if (lastClickButton!=Button1) {
      *xanf = ev->x;
      *yanf = ev->y;
      lastClickButton=Button1;
      rv = 0;
    } else  {
      *xend = ev->x;
      *yend = ev->y;
      lastClickButton=Button3;

      //tc_log_msg(__FILE__, "** x (%d) y (%d) h (%d) w (%d)", *xanf, *yanf, *xend-*xanf, *yend-*yanf);
      rv = 1;
    }

  } else if (ev->button == Button2) {      /* do a drag & drop operation */
    tc_log_msg(__FILE__, "** Button2");
  }

  lastClickTime   = ev->time;
  return rv;
}

