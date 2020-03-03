/*
 * x11source.h - X11/transcode bridge code, allowing screen capture.
 * (C) 2006-2010 - Francesco Romani <fromani -at- gmail -dot- com>
 *
 * This file is part of transcode, a video stream processing tool.
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TC_X11SOURCE_H
#define TC_X11SOURCE_H

#include "config.h"

#include <stdint.h>

#include "probe.h"
#include "framebuffer.h"

typedef struct tcX11source_ TCX11Source;

/*
 * Quick summary:
 *
 * this code acts as a bridge to a running X11 server, allowing
 * client code to query about picture attributes, from transcode
 * point of view, so frame size, frame depth and so on, and allow
 * to grab images when requested (ok, that's still needs some work
 * to avoid async responses and other X11 quirks).
 *
 * PLEASE NOTE: Only *LOCAL* X11 connections (NOT NETWORKED)
 * are supported.
 *
 * Quick TODO (approx. priorty sorted):
 * - internal refactoring
 * - support 15/16 bits color depth.
 * - grab pointer too.
 * - support useful extensions like Damage/etc. etc.
 * - docs for sources
 * - user docs
 */

#ifdef HAVE_X11

#include <X11/Xlib.h>
#include <X11/Xutil.h>

# ifdef HAVE_X11_SHM
# include <sys/ipc.h>
# include <sys/shm.h>   
# include <X11/extensions/XShm.h>
# endif /* X11_SHM */

#endif /* X11 */

struct tcX11source_ {
#ifdef HAVE_X11
    Display *dpy;
    int screen;
    Window root;
    Pixmap pix;
    GC gc;
    XImage *image;

# ifdef HAVE_X11_SHM
    XVisualInfo vis_info;
    XShmSegmentInfo shm_info;
# endif /* X11_SHM */
#endif /* X11 */

    int width;
    int height;
    int depth;

    int mode;
    uint32_t out_fmt; /* TC internal identifier */
    int conv_fmt; /* precomputed tcv_convert identifier */
    TCVHandle tcvhandle;

    int (*acquire_image)(TCX11Source *handle, uint8_t *data, int maxdata);
    void (*acquire_cursor)(TCX11Source *handle, uint8_t *data, int maxdata);
    int (*fini)(TCX11Source *handle);
};

typedef enum tcx11sourcemode_ TCX11SourceMode;
enum tcx11sourcemode_ {
    TC_X11_MODE_PLAIN = 0,
    TC_X11_MODE_SHM,

    TC_X11_MODE_BEST = 255, /* this MUST be the last one */
};


/*
 * tc_x11source_is_display_name:
 *     check if given name looks like an X11 display ID.
 *
 *     PLEASE NOTE: only LOCAL display are supported (^:[0-9]+\.[0-9]+$)
 *
 * Parameters:
 *     name: ID to be verified
 * Return Value:
 *     TC_TRUE: given name looks like an X11 display ID (so it can
 *              be used as argument for _open, see below).
 *     TC_FALSE: otherwise.
 */
int tc_x11source_is_display_name(const char *name);

/*
 * tc_x11source_probe:
 *      fetch image parameters through given connection and
 *      store them into given info structure.
 *
 * Parameters:
 *      handle: connection handle to be used for probing.
 *        info: pointer to a ProbeInfo strucutre which will
 *              hold probed informations.
 * Return Value:
 *      -1: error on connection, reason will be tc_log_*()'d out.
 *       0: succesfull
 *       1: wrong (NULL) parameters.
 */
int tc_x11source_probe(TCX11Source *handle, ProbeInfo *info);

/*
 * tc_x11source_open:
 *      connext to given LOCAL X11 display, and prepare ofr later
 *      probing and/or image acquisition.
 *
 * Parameters:
 *       handle: connection handle to be used. (Allocation must
 *               be handled by caller).
 *      display: LOCAL X11 display identifier to connect on.
 *         mode: select X extensions to use, if avalaible.
 *       format: image (colorspace) format to be used in
 *               tc_x11source_acquire. Currently only following
 *               formats are supported:
 *               TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_YUV422P
 * Return Value:
 *      -1: error on connection, reason will be tc_log_*()'d out.
 *       0: succesfull
 *       1: wrong (NULL) parameters.
 */
int tc_x11source_open(TCX11Source *handle, const char *display,
                      int mode, uint32_t format);

/*
 * tc_x11source_close:
 *      close an X11 connection represented by given handle, and
 *      releases all acquired resources.
 *
 * Parameters:
 *      handle: connection handle to be closed.
 * Return Value:
 *      -1: error on connection, reason will be tc_log_*()'d out.
 *       0: succesfull
 *       1: wrong (NULL) parameters.
 */
int tc_x11source_close(TCX11Source *handle);

/*
 * tc_x11source_acquire:
 *     grab a screenshot from given X11 source connection, convert
 *     it in RGB24 format and store it in given buffer, if this buffer
 *     is large enough to store the full picture.
 *
 * Parameters:
 *      handle: connection handle to be used for picture acquisition.
 *        data: picture buffer
 *     maxdata: size of picture buffer
 * Return Value:
 *     -1: can't get image data from X11 connection
 *      0: image buffer too small, so buffer was left untouched
 *     >0: size of acquire dimage.
 */
int tc_x11source_acquire(TCX11Source *handle, uint8_t *data, int maxdata);


#endif /* TC_X11SOURCE_H */
