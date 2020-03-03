/*
 *  import_v4l.c
 *
 *  Copyright (C) Thomas Oestreich - February 2002
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

#include "src/transcode.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "aclib/imgconvert.h"

#include <sys/ioctl.h>
#include <sys/mman.h>

#include "videodev.h"

#define MOD_NAME    "import_v4l.so"
#define MOD_VERSION "v0.2.0 (2008-10-26)"
#define MOD_CODEC   "(video) v4l"

static int verbose_flag    = TC_QUIET;
static int capability_flag = TC_CAP_RGB|TC_CAP_YUV;

#define MOD_PRE v4l
#include "import_def.h"

/*************************************************************************/

/*#define MMAP_DEBUG  1*/

#define MAX_BUFFER 32

typedef struct v4lsource V4LSource;
struct v4lsource {
    int video_dev;

    int width;
    int height;
    int input;
    int format;

    struct video_mmap vid_mmap[MAX_BUFFER];
    int grab_buf_idx;
    int grab_buf_max;
    struct video_mbuf vid_mbuf;
    uint8_t *video_mem;
    int grabbing_active;
    int have_new_frame;
    int totalframecount;
    int image_size;
    int image_pixels;
    int framecount;
    int fps_update_interval;
    double fps;
    double lasttime;

    int (*grab)(V4LSource *vs, uint8_t *buf, size_t bufsize);
    int (*close)(V4LSource *vs);
};

/*************************************************************************/

static struct v4lsource VS;

/*************************************************************************/

static int v4lsource_mmap_init(V4LSource *vs);
static int v4lsource_mmap_grab(V4LSource *vs, uint8_t *buffer, size_t bufsize);
static int v4lsource_mmap_close(V4LSource *vs);

static int v4lsource_read_init(V4LSource *vs);
static int v4lsource_read_grab(V4LSource *vs, uint8_t *buffer, size_t bufsize);
static int v4lsource_read_close(V4LSource *vs);

static int v4lsource_generic_close(V4LSource *vs);

/*************************************************************************/

#define RETURN_IF_ERROR(RET, MSG) do { \
     if (-1 == (RET)) { \
        tc_log_perror(MOD_NAME, (MSG)); \
        return TC_ERROR; \
    } \
} while (0)

/*************************************************************************/

static int v4lsource_generic_close(V4LSource *vs)
{
    int err = close(vs->video_dev);
    if (err) {
        return TC_ERROR;
    }
    return TC_OK;
}

/*************************************************************************/

static int v4lsource_read_init(V4LSource *vs)
{
    int ret, flag = 1;

    if (verbose_flag)
        tc_log_info(MOD_NAME, "capture method: read");

    ret = ioctl(vs->video_dev, VIDIOCCAPTURE, &flag);
    RETURN_IF_ERROR(ret, "error starting the capture");

    vs->grab  = v4lsource_read_grab;
    vs->close = v4lsource_read_close;
    return TC_OK;
}

static int v4lsource_read_grab(V4LSource *vs, uint8_t *buffer, size_t bufsize)
{
    ssize_t r = read(vs->video_dev, buffer, bufsize);
    if (r != bufsize) {
        return TC_ERROR;
    }
    return TC_OK;
}

static int v4lsource_read_close(V4LSource *vs)
{
    int ret, flag = 0;

    ret = ioctl(vs->video_dev, VIDIOCCAPTURE, &flag);
    RETURN_IF_ERROR(ret, "error stopping the capture");

    return v4lsource_generic_close(vs);
}

/*************************************************************************/

static int v4lsource_mmap_init(V4LSource *vs)
{
    int i, ret;

    if (verbose_flag)
        tc_log_info(MOD_NAME, "capture method: mmap");

    // retrieve buffer size and offsets
    ret = ioctl(vs->video_dev, VIDIOCGMBUF, &vs->vid_mbuf);
    RETURN_IF_ERROR(ret, "error requesting capture buffers");

    if (verbose_flag)
        tc_log_info(MOD_NAME, "%d frame buffer%s available", 
                    vs->vid_mbuf.frames, (vs->vid_mbuf.frames > 1) ?"s" :"");
    vs->grab_buf_max = vs->vid_mbuf.frames;

    if (!vs->grab_buf_max) {
        tc_log_error(MOD_NAME, "no frame capture buffer(s) available");
        return TC_ERROR;
    }

    vs->video_mem = mmap(0, vs->vid_mbuf.size, PROT_READ|PROT_WRITE, MAP_SHARED, vs->video_dev, 0);
    if ((uint8_t *) -1 == vs->video_mem) {
        tc_log_perror(MOD_NAME, "error mapping capture buffers in memory");
        return TC_ERROR;
    }
#ifdef MMAP_DEBUG
    tc_log_msg(MOD_NAME,
               "(mmap_init) video_mem base address=%p size=%li",
               vs->video_mem, (long)vs->vid_mbuf.size);
#endif

    for (i = 0; i < vs->grab_buf_max; i++) {
        vs->vid_mmap[i].frame  = i;
        vs->vid_mmap[i].format = vs->format;
        vs->vid_mmap[i].width  = vs->width;
        vs->vid_mmap[i].height = vs->height;
#ifdef MMAP_DEBUG
        tc_log_msg(MOD_NAME,
                   "(mmap_init) setup: mmap buf #%i: %ix%i@0x%x",
                   vs->vid_mmap[i].frame,
                   vs->vid_mmap[i].width, vs->vid_mmap[i].height,
                   vs->vid_mmap[i].format);
#endif
    }

    for (i = 1; i < vs->grab_buf_max + 1; i++) {
        ret = ioctl(vs->video_dev, VIDIOCMCAPTURE, &vs->vid_mmap[i % vs->grab_buf_max]);
        RETURN_IF_ERROR(ret, "error setting up a capture buffer");
#ifdef MMAP_DEBUG
        tc_log_msg(MOD_NAME,
                   "(mmap_init) enqueue: mmap buf #%i",
                   i % vs->grab_buf_max);
#endif
    }

    vs->grab  = v4lsource_mmap_grab;
    vs->close = v4lsource_mmap_close;
    return TC_OK;
}


static int v4lsource_mmap_close(V4LSource *vs)
{
    // video device
    munmap(vs->video_mem, vs->vid_mbuf.size);
    return v4lsource_generic_close(vs);
}

static int v4lsource_mmap_grab(V4LSource *vs, uint8_t *buffer, size_t bufsize)
{
    uint8_t *p, *planes[3] = { NULL, NULL, NULL };
    int ret = 0, offset = 0;

    vs->grab_buf_idx = ((vs->grab_buf_idx + 1) % vs->grab_buf_max);
#ifdef MMAP_DEBUG
    tc_log_msg(MOD_NAME,
               "(mmap_grab) querying for buffer #%i",
               vs->grab_buf_idx);
#endif

    // wait for next image in the sequence to complete grabbing
    ret = ioctl(vs->video_dev, VIDIOCSYNC, &vs->vid_mmap[vs->grab_buf_idx]);
    RETURN_IF_ERROR(ret, "error waiting for new video frame (VIDIOCSYNC)");

    //copy frame
    offset = vs->vid_mbuf.offsets[vs->grab_buf_idx];
    p = vs->video_mem + offset;
#ifdef MMAP_DEBUG
    tc_log_msg(MOD_NAME,
               "(mmap_grab) got offset=%i frame pointer=%p",
               offset, p);
#endif

    switch (vs->format) {
      case VIDEO_PALETTE_RGB24: /* fallback */
      case VIDEO_PALETTE_YUV420P:
        ac_memcpy(buffer, p, vs->image_size);
        break;
      case VIDEO_PALETTE_YUV422:
        YUV_INIT_PLANES(planes, buffer, IMG_YUV_DEFAULT, vs->width, vs->height);
        ac_imgconvert(&p, IMG_YUY2, planes, IMG_YUV_DEFAULT, vs->width, vs->height);
        break;
    }

    vs->totalframecount++;

    // issue new grab command for this buffer
    ret = ioctl(vs->video_dev, VIDIOCMCAPTURE, &vs->vid_mmap[vs->grab_buf_idx]);
    RETURN_IF_ERROR(ret, "error acquiring new video frame (VIDIOCMCAPTURE)");
    
    return TC_OK;
}

/*************************************************************************/

static int v4lsource_setup_capture(V4LSource *vs, int w, int h, int fmt)
{
    struct video_picture pict;
    struct video_window win;
    int ret;

    // picture parameter
    ret = ioctl(VS.video_dev, VIDIOCGPICT, &pict);
    RETURN_IF_ERROR(ret, "error getting picture parameters (VIDIOCGPICT)");

    // store variables
    VS.width           = w;
    VS.height          = h;
    VS.format          = fmt;
    // reset grab counter variables
    VS.grab_buf_idx    = 0;
    VS.totalframecount = 0;
    // calculate framebuffer size
    VS.image_pixels    = w * h;
    switch (VS.format) {
      case VIDEO_PALETTE_RGB24:
        VS.image_size = VS.image_pixels * 3;
        break;
      case VIDEO_PALETTE_YUV420P:
        VS.image_size = (VS.image_pixels * 3) / 2;
        break;
      case VIDEO_PALETTE_YUV422:
        VS.image_size = VS.image_pixels * 2; // XXX
        break;
    }

    if (fmt == VIDEO_PALETTE_RGB24) {
        pict.depth = 24;
    }
    pict.palette = fmt;

    ret = ioctl(VS.video_dev, VIDIOCSPICT, &pict);
    RETURN_IF_ERROR(ret, "error setting picture parameters (VIDIOCSPICT)");

    ret = ioctl(vs->video_dev, VIDIOCGWIN, &win);
    RETURN_IF_ERROR(ret, "error getting capture window properties");

    win.width  = vs->width;
    win.height = vs->height;
    win.flags  = 0; /* no flags */
    
    ret = ioctl(vs->video_dev, VIDIOCSWIN, &win);
    RETURN_IF_ERROR(ret, "error getting capture window properties");

    return TC_OK;
}

/*************************************************************************/

static int v4lsource_init(const char *device, const char *options,
                          int w, int h, int fmt)
{
    struct video_capability capability;
    int ret, use_read = TC_FALSE;

    VS.video_dev = open(device, O_RDWR);
    if (VS.video_dev == -1) {
        tc_log_perror(MOD_NAME, "error opening grab device");
        return TC_ERROR;
    }

    ret = ioctl(VS.video_dev, VIDIOCGCAP, &capability);
    RETURN_IF_ERROR(ret, "error quering capabilities (VIDIOCGCAP)");
   
    if (verbose_flag)
        tc_log_info(MOD_NAME, "capture device: %s", capability.name);
    if (!(capability.type & VID_TYPE_CAPTURE)) {
        tc_log_error(MOD_NAME, "device does NOT support capturing!");
        return TC_ERROR;
    }

    ret = v4lsource_setup_capture(&VS, w, h, fmt);

    if (options) {
        if (optstr_lookup(options, "capture_read")) {
            use_read = TC_TRUE;
        }
    }

    if (use_read) {
        return v4lsource_read_init(&VS);
    }
    return v4lsource_mmap_init(&VS);
}

/*************************************************************************/

MOD_open
{
    int fmt = 0;

    if (verbose_flag)
        tc_log_warn(MOD_NAME, "this module is deprecated: "
                              "please use import_v4l2 instead");
    if (param->flag == TC_VIDEO) {
        // print out
        param->fd = NULL;

        switch (vob->im_v_codec) {
          case TC_CODEC_RGB24:
            fmt = VIDEO_PALETTE_RGB24;
            break;
          case TC_CODEC_YUV422P:
             fmt = VIDEO_PALETTE_YUV422;
            break;
          case TC_CODEC_YUV420P:
            fmt = VIDEO_PALETTE_YUV420P;
            break;
        }
        
        if (v4lsource_init(vob->video_in_file, vob->im_v_string,
                            vob->im_v_width, vob->im_v_height, fmt) < 0) {
            tc_log_error(MOD_NAME, "error grab init");
            return TC_ERROR;
        }
        return TC_OK;
    }
    return TC_ERROR;
}

MOD_decode
{
    if (param->flag == TC_VIDEO) {
        return VS.grab(&VS, param->buffer, param->size);
        return TC_OK;
    }
    return TC_ERROR;
}

MOD_close
{
    if (param->flag == TC_VIDEO) {
        VS.close(&VS);
        return TC_OK;
    }
    return TC_ERROR;
}

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
