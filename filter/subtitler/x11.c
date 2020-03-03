#include "subtitler.h"

#include <stdio.h>
#include <stdlib.h>

#include <X11/Intrinsic.h>
#include <X11/Xaw/Simple.h>

#include "x11.h"

extern int debug_flag;

XtAppContext app_context;
Widget app_shell, tv;
Display *dpy;
static XImage *grab_ximage = NULL;
static GC grab_gc;
static unsigned int display_bits = 0;


void putimage(int xsize, int ysize)
{
if (grab_ximage)
	{
	XPutImage(dpy, XtWindow(tv), grab_gc, grab_ximage, 0, 0, 0, 0,\
	xsize, ysize);

	XFlush(dpy);
	}
}/* end function putimage */


int openwin(int argc, char *argv[], int xsize, int ysize)
{
static Window root;
XVisualInfo	*info, template;
int found;

app_shell =\
XtAppInitialize(\
&app_context, "subtitler by Panteltje (c)",\
NULL, 0, &argc, argv, NULL, NULL, 0);

XtMakeResizeRequest(app_shell, xsize, ysize, NULL, NULL);

dpy = XtDisplay(app_shell);

root = DefaultRootWindow(dpy);

template.screen = XDefaultScreen(dpy);

template.visualid =\
XVisualIDFromVisual(DefaultVisualOfScreen(DefaultScreenOfDisplay(dpy)));

info =\
XGetVisualInfo(dpy, VisualIDMask | VisualScreenMask, &template,&found);
if (!info)
	{
	tc_log_warn(MOD_NAME, "XGetVisualInfo failed");
	return -1;
	}

display_bits = info -> depth;

//UNCOMMENT ONE OF THESE LINES IF YOU WANT FIXED COLOR DEPTH.
//display_bits = 16;
//display_bits = 24;
//display_bits = 32;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME,
	"x11: color depth: %u bits", display_bits);

	tc_log_msg(MOD_NAME,
	"x11: color masks: red=0x%08lx green=0x%08lx blue=0x%08lx",
	info->red_mask, info->green_mask, info->blue_mask);
	}

XFree(info);

tv = XtVaCreateManagedWidget("tv", simpleWidgetClass, app_shell,NULL);

XtRegisterDrawable(dpy, root, tv);

XtRealizeWidget(app_shell);

grab_gc = XCreateGC(dpy, XtWindow(tv), 0, NULL);

grab_ximage =\
XCreateImage(dpy, DefaultVisualOfScreen(DefaultScreenOfDisplay(dpy)),\
	DefaultDepthOfScreen(DefaultScreenOfDisplay(dpy)),\
	ZPixmap, 0, malloc(xsize * ysize * 4), // depth / 8  (24 / 8)? just 2 be on the safe side
	xsize, ysize, 8, 0);

XClearArea(XtDisplay(tv), XtWindow(tv), 0, 0, 0, 0, True);

return 0;
}/* end function openwin */


unsigned char *getbuf(void)
{
if (!grab_ximage)
	{
	tc_log_error(MOD_NAME, "grab_ximage == NULL shouldn't be!\n");
	}
return grab_ximage -> data;
}/* end function getbuf */


void closewin(void)
{
if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "closewin(): arg none\n");
	}

XtDestroyWidget(app_shell);
return;

XtUnrealizeWidget(app_shell);

if(tv) XtDestroyWidget(tv);

if(grab_ximage) XDestroyImage(grab_ximage);

}/* end function closewin */


int get_x11_bpp()
{
return display_bits;
}/* end function get_x11_bpp */


int resize_window(int xsize, int ysize)
{
XtMakeResizeRequest(app_shell, xsize, ysize, NULL, NULL);

return 1;
}/* end function resize_window */

