/*
 * x11source.c - X11/transcode bridge code, allowing screen capture.
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

#include "src/transcode.h"
#include "libtc/ratiocodes.h"
#include "libtc/tccodecs.h"
#include "libtc/tcframes.h"
#include "libtcvideo/tcvideo.h"

#include <string.h>

#include "magic.h"
#include "x11source.h"

/*
 * TODO:
 * - internal docs
 * - internla refactoring to properly grab cursor
 */

#ifdef HAVE_X11

/*************************************************************************/
/* cursor grabbing support. */

#ifdef HAVE_X11_FIXES

#include <X11/extensions/Xfixes.h>


static void tc_x11source_acquire_cursor_fixes(TCX11Source *handle,
                                              uint8_t *data, int maxdata)
{
    XFixesCursorImage *cursor = XFixesGetCursorImage(handle->dpy);

    if (cursor == NULL) {
        /* this MUST be noisy! */
        tc_log_warn(__FILE__, "failed to get cursor image");
    } else {
        /* FIXME: this has to be rewritten and need significant
         * internal refactoring :( */
    }
}

#endif /* HAVE_X11_FIXES */

/* FIXME: explain why don't use funcpointers in here */
static void tc_x11source_acquire_cursor_plain(TCX11Source *handle,
                                              uint8_t *data, int maxdata)
{
    static int warn = 0;

    if (!warn) {
        tc_log_warn(__FILE__, "cursor grabbing not supported!");
        warn = 1;
    }
}

static void tc_x11source_init_cursor(TCX11Source *handle)
{
    /* sane default if we don't have any better */
    handle->acquire_cursor = tc_x11source_acquire_cursor_plain;
#ifdef HAVE_X11_FIXES
    handle->acquire_cursor = tc_x11source_acquire_cursor_fixes;
#endif
}

/*************************************************************************/

int tc_x11source_is_display_name(const char *name)
{
    if (name != NULL && strlen(name) != 0) {
        uint32_t disp, screen;
        int ret = sscanf(name, ":%u.%u", &disp, &screen);
        if (ret == 2) {
            /* looks like a display specifier */
            return TC_TRUE;
        }
    }
    return TC_FALSE;
}

int tc_x11source_probe(TCX11Source *handle, ProbeInfo *info)
{
    if (handle != NULL && info != NULL) {
        info->width = handle->width;
        info->height = handle->height;
        info->codec = handle->out_fmt;
        info->magic = TC_MAGIC_X11; /* enforce */
        info->asr = 1; /* force 1:1 ASR (XXX) */
        /* FPS/FRC MUST BE choosed by user; that's only a kind suggestion */
        info->fps = 10.0;
        tc_frc_code_from_value(&info->frc, info->fps);
        
        info->num_tracks = 0; /* no audio, here */
        return 0;
    }

    return 1;
}

/*************************************************************************/

static int tc_x11source_acquire_image_plain(TCX11Source *handle,
                                           uint8_t *data, int maxdata)
{
    int size = -1;

    /* but draw such areas if windows are opaque */
    /* FIXME: switch to XCreateImage? */
    handle->image = XGetImage(handle->dpy, handle->pix, 0, 0, 
                              handle->width, handle->height,
                              AllPlanes, ZPixmap);

    if (handle->image == NULL || handle->image->data == NULL) {
        tc_log_error(__FILE__, "cannot get X image");
    } else {
        size = (int)tc_video_frame_size(handle->image->width,
                                        handle->image->height,
                                        handle->out_fmt);
        if (size <= maxdata) {
            /* to make gcc happy */
            tcv_convert(handle->tcvhandle,
                        (uint8_t*)handle->image->data, data,
                        handle->image->width, handle->image->height,
                        IMG_BGRA32, handle->conv_fmt);
        } else {
            size = 0;
        }
        XDestroyImage(handle->image);
    }
    return size;
}

static int tc_x11source_fini_plain(TCX11Source *handle)
{
    return 0;
}

static int tc_x11source_init_plain(TCX11Source *handle)
{
    handle->acquire_image = tc_x11source_acquire_image_plain;
    handle->fini = tc_x11source_fini_plain;
    return 0;
}


/*************************************************************************/

#ifdef HAVE_X11_SHM

static int tc_x11source_acquire_image_shm(TCX11Source *handle,
                                          uint8_t *data, int maxdata)
{
    int size = -1;
    Status ret;

    /* but draw such areas if windows are opaque */
    ret = XShmGetImage(handle->dpy, handle->pix, handle->image,
                       0, 0, AllPlanes);

    if (!ret || handle->image == NULL || handle->image->data == NULL) {
        tc_log_error(__FILE__, "cannot get X image (using SHM)");
    } else {
        size = (int)tc_video_frame_size(handle->image->width,
                                        handle->image->height,
                                        handle->out_fmt);
        if (size <= maxdata) {
            tcv_convert(handle->tcvhandle, handle->image->data, data,
                        handle->image->width, handle->image->height,
                        IMG_BGRA32, handle->conv_fmt);
        } else {
            size = 0;
        }
    }
    return size;
}

static int tc_x11source_fini_shm(TCX11Source *handle)
{
    Status ret = XShmDetach(handle->dpy, &handle->shm_info);
    if (!ret) { /* XXX */
        tc_log_error(__FILE__, "failed to attach SHM from Xserver");
        return -1;
    }
    XDestroyImage(handle->image);
    handle->image = NULL;

    XSync(handle->dpy, False); /* XXX */
    if (shmdt(handle->shm_info.shmaddr) != 0) {
        tc_log_error(__FILE__, "failed to destroy shared memory segment");
        return -1;
    }
    return 0;
}

static int tc_x11source_init_shm(TCX11Source *handle)
{
    Status ret;

    ret = XMatchVisualInfo(handle->dpy, handle->screen, handle->depth,
                           DirectColor, &handle->vis_info);
    if (!ret) {
        tc_log_error(__FILE__, "Can't match visual information");
        goto xshm_failed;
    }
    handle->image = XShmCreateImage(handle->dpy, handle->vis_info.visual,
                                    handle->depth, ZPixmap,
                                    NULL, &handle->shm_info,
                                    handle->width, handle->height);
    if (handle->image == NULL) {
        tc_log_error(__FILE__, "XShmCreateImage failed.");
        goto xshm_failed_image;
    }
    handle->shm_info.shmid = shmget(IPC_PRIVATE,
                                    handle->image->bytes_per_line * handle->image->height,
                                    IPC_CREAT | 0777);
    if (handle->shm_info.shmid < 0) {
        tc_log_error(__FILE__, "failed to create shared memory segment");
        goto xshm_failed_image;
    }
    handle->shm_info.shmaddr = shmat(handle->shm_info.shmid, NULL, 0);
    if (handle->shm_info.shmaddr == (void*)-1) {
        tc_log_error(__FILE__, "failed to attach shared memory segment");
        goto xshm_failed_image;
    }
    
    shmctl(handle->shm_info.shmid, IPC_RMID, 0); /* XXX */

    handle->image->data = handle->shm_info.shmaddr;
    handle->shm_info.readOnly = False;

    ret = XShmAttach(handle->dpy, &handle->shm_info);
    if (!ret) {
        tc_log_error(__FILE__, "failed to attach SHM to Xserver");
        goto xshm_failed_image;
    }

    XSync(handle->dpy, False);
    handle->mode = TC_X11_MODE_SHM;
    handle->acquire_image = tc_x11source_acquire_image_shm;
    handle->fini = tc_x11source_fini_shm;

    return 0;

xshm_failed_image:
    XDestroyImage(handle->image);
    handle->image = NULL;
xshm_failed:
    return -1;
}

/*************************************************************************/

#endif /* X11_SHM */

static int tc_x11source_map_format(TCX11Source *handle, uint32_t format)
{
    int ret = -1;

    if (handle != NULL) {
        ret = 0;
        switch (format) {
          case TC_CODEC_RGB24:
            handle->out_fmt = TC_CODEC_RGB24;
            handle->conv_fmt = IMG_RGB24;
            if (verbose >= TC_DEBUG) {
                tc_log_info(__FILE__, "output colorspace: RGB24");
            }
            break;
          case TC_CODEC_YUV420P:
            handle->out_fmt = TC_CODEC_YUV420P;
            handle->conv_fmt = IMG_YUV420P;
            if (verbose >= TC_DEBUG) {
                tc_log_info(__FILE__, "output colorspace: YUV420P");
            }
            break;
          case TC_CODEC_YUV422P:
            handle->out_fmt = TC_CODEC_YUV422P;
            handle->conv_fmt = IMG_YUV422P;
            if (verbose >= TC_DEBUG) {
                tc_log_info(__FILE__, "output colorspace: YUV4222");
            }
            break;
          default:
            tc_log_error(__FILE__, "unknown colorspace requested: 0x%x",
                         format);
            ret = -1;
        }
    }
    return ret;
}

int tc_x11source_acquire(TCX11Source *handle, uint8_t *data, int maxdata)
{
    int size = -1;

    if (handle == NULL || data == NULL || maxdata <= 0) {
        tc_log_error(__FILE__, "x11source_acquire: wrong (NULL) parameters");
        return size;
    }

    XLockDisplay(handle->dpy);
    /* OK, let's hack a bit our GraphicContext */
    XSetSubwindowMode(handle->dpy, handle->gc, IncludeInferiors);
    /* don't catch areas of windows covered by children windows */
    XCopyArea(handle->dpy, handle->root, handle->pix, handle->gc,
              0, 0, handle->width, handle->height, 0, 0);

    XSetSubwindowMode(handle->dpy, handle->gc, ClipByChildren);
    /* but draw such areas if windows are opaque */
    
    size = handle->acquire_image(handle, data, maxdata);
    if (size > 0) {
        handle->acquire_cursor(handle, data, maxdata); /* cannot fail */
    }
    XUnlockDisplay(handle->dpy);
    return size;
}

int tc_x11source_close(TCX11Source *handle)
{
    if (handle != NULL) {
        if (handle->dpy != NULL) {
            int ret = handle->fini(handle);
            if (ret != 0) {
                return ret;
            }

            tcv_free(handle->tcvhandle);

            XFreePixmap(handle->dpy, handle->pix); /* XXX */
            XFreeGC(handle->dpy, handle->gc); /* XXX */

            ret = XCloseDisplay(handle->dpy);
            if (ret == 0) {
                handle->dpy = NULL;
            } else {
                tc_log_error(__FILE__, "XCloseDisplay() failed: %i", ret);
                return -1;
            }
        }
    }
    return 0;
}

int tc_x11source_open(TCX11Source *handle, const char *display,
                      int mode, uint32_t format)
{
    XWindowAttributes winfo;
    Status ret;
    int err;

    if (handle == NULL) {
        return 1;
    }

    XInitThreads();

    err = tc_x11source_map_format(handle, format);
    if (err != 0) {
        return err;
    }

    handle->mode = mode;
    handle->dpy = XOpenDisplay(display);
    if (handle->dpy == NULL) {
        tc_log_error(__FILE__, "failed to open display %s",
                     (display != NULL) ?display :"default");
        goto open_failed;
    }

    handle->screen = DefaultScreen(handle->dpy);
    handle->root = RootWindow(handle->dpy, handle->screen);
    /* Get the parameters of the root winfow */
    ret = XGetWindowAttributes(handle->dpy, handle->root, &winfo);
    if (!ret) {
        tc_log_error(__FILE__, "failed to get root window attributes");
        goto link_failed;
    }

    handle->width = winfo.width;
    handle->height = winfo.height;
    handle->depth = winfo.depth;

    if (handle->depth != 24) { /* XXX */
        tc_log_error(__FILE__, "Non-truecolor display depth"
                               " not supported. Yet.");
        goto link_failed;
    }

    if (verbose >= TC_STATS) {
        tc_log_info(__FILE__, "display properties: %ix%i@%i", 
                    handle->width, handle->height, handle->depth);
    }

    handle->pix = XCreatePixmap(handle->dpy, handle->root,
                                handle->width, handle->height,
                                handle->depth); /* XXX */
    if (!handle->pix) {
        tc_log_error(__FILE__, "Can't allocate Pixmap");
        goto pix_failed;
    }
 
    handle->gc = XCreateGC(handle->dpy, handle->root, 0, 0);
    /* FIXME: what about failures? */

    handle->tcvhandle = tcv_init();
    if (!handle->tcvhandle)
        goto tcv_failed;

    tc_x11source_init_cursor(handle); /* cannot fail */

#ifdef HAVE_X11_SHM
    if (XShmQueryExtension(handle->dpy) != 0
      && (mode & TC_X11_MODE_SHM)) {
        if (tc_x11source_init_shm(handle) < 0)
            goto init_failed;
    } else
#endif /* X11_SHM */
    if (tc_x11source_init_plain(handle) < 0)
        goto init_failed;
    return 0;

  init_failed:
    tcv_free(handle->tcvhandle);
  tcv_failed:
    XFreeGC(handle->dpy, handle->gc);
    XFreePixmap(handle->dpy, handle->pix);
  pix_failed:
  link_failed:
    XCloseDisplay(handle->dpy);
  open_failed:
    return -1;
}


#else /* HAVE_X11 */


int tc_x11source_open(TCX11Source *handle, const char *display,
                      int mode, uint32_t format)
{
    tc_log_error(__FILE__, "X11 support unavalaible");
    return -1;
}

int tc_x11source_close(TCX11Source *handle)
{
    tc_log_error(__FILE__, "X11 support unavalaible");
    return 0;
}

int tc_x11source_probe(TCX11Source *handle, ProbeInfo *info)
{
    tc_log_error(__FILE__, "X11 support unavalaible");
    return -1;
}

int tc_x11source_acquire(TCX11Source *handle, uint8_t *data, int maxdata)
{
    tc_log_error(__FILE__, "X11 support unavalaible");
    return -1;
}

int tc_x11source_is_display_name(const char *name)
{
    return TC_FALSE;
}

#endif /* HAVE_X11 */

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
