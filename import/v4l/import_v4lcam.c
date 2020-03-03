/*
 * import_v4lcam.c -- imports video frames from v4l2 using libv4l*
 *                    with special focus on webcams.
 * (C) 2009-2010 Francesco Romani <fromani at gmail dot com>
 * based on import_v4l2.c code, which is
 * (C) Erik Slagter <erik@slagter.name> Sept 2003
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

#define MOD_NAME        "import_v4lcam.so"
#define MOD_VERSION     "v0.1.0 (2009-08-30)"
#define MOD_CODEC       "(video) v4l2"

#include "src/transcode.h"


static int verbose_flag     = TC_QUIET;
static int capability_flag  = TC_CAP_RGB|TC_CAP_YUV;

/* 
 * Briefing
 *
 * Q: why a new module?
 * Q: why don't just enhance import_v4l2?
 * A: because I want take this chance to do a fresh start with a v4l import
 *    module, so we can get rid of some old code, try to redesign/rewrite
 *    it in a better way, experimenting new designes and so on. I want the
 *    freedom to add special code and special design decisions useful for
 *    webcams only (or just mostly). import_v4l2 will stay and the experiments
 *    which time proven to be good will be backported.
 *    Eventually, v4lcam can be merged into the main v4l module.
 *
 * Q: there is some duplicate code with import_v4l2.c. Why?
 * A: because I'm taking advantage of being a separate module, and because
 *    I'm experimenting new stuff. After a while, the remaining duplicated
 *    parts will be merged in a common source.
 *
 * Q: why libv4lconvert? We can just extend aclib.
 * A: no objections of course (but no time either!). However, libv4lconvert
 *    has IMHO a slightly different focus wrt aclib and I think it's just
 *    fine to use both of them. As example, the MJPG->I420 conversion
 *    should NOT enter into aclib (eventually v4lcam can emit MJPG frames
 *    too, when the module pipeline get enhanced enough).

 */

/*%*
 *%* DESCRIPTION 
 *%*   This module allow to capture video frames through a V4L2 (V4L api version 2)
 *%*   device. This module is specialized for webcam devices.
 *%*
 *%* #BUILD-DEPENDS
 *%*
 *%* #DEPENDS
 *%*
 *%* PROCESSING
 *%*   import/demuxer
 *%*
 *%* MEDIA
 *%*   video
 *%*
 *%* #INPUT
 *%*
 *%* OUTPUT
 *%*   YUV420P, RGB24
 *%*/

#define MOD_PRE         tc_v4lcam
#include "import_def.h"

#define _ISOC9X_SOURCE 1

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/types.h>

// The v4l2_buffer struct check is because some distributions protect that
// struct in videodev2 with a #ifdef __KERNEL__ (SuSE 9.0)

#if defined(HAVE_LINUX_VIDEODEV2_H) && defined(HAVE_STRUCT_V4L2_BUFFER)
#define _LINUX_TIME_H
#include <linux/videodev2.h>
#else
#include "videodev2.h"
#endif

#include "libv4l2.h"
#include "libv4lconvert.h"

#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#define TC_V4L2_BUFFERS_NUM             (32)

/* TODO: memset() verify and sanitization */

typedef struct tcv4lbuffer TCV4LBuffer;
struct tcv4lbuffer {
    void    *start;
    size_t  length;
};

typedef struct v4l2source_ V4L2Source;

/* FIXME: naming */
typedef int (*TCV4LFetchDataFn)(V4L2Source *vs,
                                uint8_t *src, int src_len,
                                uint8_t *dst, int dst_len);

struct v4l2source_ {
    int                     video_fd;
    int                     video_sequence;

    int                     v4l_dst_csp;
    struct v4l2_format      v4l_dst_fmt;
    struct v4l2_format      v4l_src_fmt;
    struct v4lconvert_data  *v4l_convert;
    int                     buffers_count;

    int                     width;
    int                     height;

    TCV4LFetchDataFn        fetch_data;
    TCV4LBuffer             buffers[TC_V4L2_BUFFERS_NUM];
};

static int tc_v4l2_fetch_data_memcpy(V4L2Source *vs,
                                     uint8_t *src, int src_len,
                                     uint8_t *dst, int dst_len)
{
    int ret = TC_ERROR;
    if (dst_len >= src_len) {
        ac_memcpy(dst, src, src_len);
        ret = TC_OK;
    }
    return ret;
}

static int tc_v4l2_fetch_data_v4lconv(V4L2Source *vs,
                                      uint8_t *src, int src_len,
                                      uint8_t *dst, int dst_len)
{
    int err = v4lconvert_convert(vs->v4l_convert,
                                 &(vs->v4l_src_fmt),
                                 &(vs->v4l_dst_fmt),
                                 src, src_len, dst, dst_len);

    return (err == -1) ?TC_ERROR :TC_OK; /* FIXME */
}

/* FIXME: reorganize the layout */
static int tc_v4l2_video_grab_frame(V4L2Source *vs, uint8_t *dest, size_t length)
{
    static struct v4l2_buffer buffer; /* FIXME */
    int ix, err = 0, eio = 0, ret = TC_ERROR;

    // get buffer
    buffer.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    err = v4l2_ioctl(vs->video_fd, VIDIOC_DQBUF, &buffer);
    if (err < 0) {
        tc_log_perror(MOD_NAME,
                      "error in setup grab buffer (ioctl(VIDIOC_DQBUF) failed)");

        if (errno != EIO) {
            return TC_OK;
        } else {
            eio = 1;

            for (ix = 0; ix < vs->buffers_count; ix++) {
                buffer.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buffer.memory = V4L2_MEMORY_MMAP;
                buffer.index  = ix;
                buffer.flags  = 0;

                err = v4l2_ioctl(vs->video_fd, VIDIOC_DQBUF, &buffer);
                if (err < 0)
                    tc_log_perror(MOD_NAME,
                                  "error in recovering grab buffer (ioctl(DQBUF) failed)");
            }

            for (ix = 0; ix < vs->buffers_count; ix++) {
                buffer.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buffer.memory = V4L2_MEMORY_MMAP;
                buffer.index  = ix;
                buffer.flags  = 0;

                err = v4l2_ioctl(vs->video_fd, VIDIOC_QBUF, &buffer);
                if (err < 0)
                    tc_log_perror(MOD_NAME,
                                  "error in recovering grab buffer (ioctl(QBUF) failed)");
            }
        }
    }

    ix  = buffer.index;

    ret = vs->fetch_data(vs,
                         vs->buffers[ix].start, buffer.bytesused,
                         dest, length);

    // enqueue buffer again
    if (!eio) {
        buffer.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory   = V4L2_MEMORY_MMAP;
        buffer.flags    = 0;

        err = v4l2_ioctl(vs->video_fd, VIDIOC_QBUF, &buffer);
        if (err < 0) {
            tc_log_perror(MOD_NAME, "error in enqueuing buffer (ioctl(VIDIOC_QBUF) failed)");
            return TC_OK;
        }
    }

    return ret;
}

static int tc_v4l2_video_count_buffers(V4L2Source *vs)
{
    struct v4l2_buffer buffer;
    int ix, ret, buffers_filled = 0;

    for (ix = 0; ix < vs->buffers_count; ix++) {
        buffer.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index  = ix;

        ret = v4l2_ioctl(vs->video_fd, VIDIOC_QUERYBUF, &buffer);
        if (ret < 0) {
            tc_log_perror(MOD_NAME, 
                          "error in querying buffers"
                          " (ioctl(VIDIOC_QUERYBUF) failed)");
            return -1;
        }

        if (buffer.flags & V4L2_BUF_FLAG_DONE)
            buffers_filled++;
    }
    return buffers_filled;
}

static int tc_v4l2_video_check_capabilities(V4L2Source *vs)
{
    struct v4l2_capability caps;
    int err = 0;

    err = v4l2_ioctl(vs->video_fd, VIDIOC_QUERYCAP, &caps);
    if (err < 0) {
        tc_log_error(MOD_NAME, "driver does not support querying capabilities");
        return TC_ERROR;
    }

    if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        tc_log_error(MOD_NAME, "driver does not support video capture");
        return TC_ERROR;
    }

    if (!(caps.capabilities & V4L2_CAP_STREAMING)) {
        tc_log_error(MOD_NAME, "driver does not support streaming (mmap) video capture");
        return TC_ERROR;
    }

    if (verbose_flag > TC_INFO) {
        tc_log_info(MOD_NAME, "v4l2 video grabbing, driver = %s, device = %s",
                    caps.driver, caps.card);
    }

    return TC_OK;
}

#define pixfmt_to_fourcc(pixfmt, fcc) do { \
    fcc[0] = (pixfmt >> 0 ) & 0xFF; \
    fcc[1] = (pixfmt >> 8 ) & 0xFF; \
    fcc[2] = (pixfmt >> 16) & 0xFF; \
    fcc[3] = (pixfmt >> 24) & 0xFF; \
} while (0)

static int tc_v4l2_video_setup_image_format(V4L2Source *vs, int width, int height)
{
    int err = 0;

    vs->width  = width;
    vs->height = height;

    vs->v4l_convert = v4lconvert_create(vs->video_fd);
    if (!vs->v4l_convert) {
        return TC_ERROR;
    }

    memset(&(vs->v4l_dst_fmt), 0, sizeof(vs->v4l_dst_fmt));
    vs->v4l_dst_fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vs->v4l_dst_fmt.fmt.pix.width       = width;
    vs->v4l_dst_fmt.fmt.pix.height      = height;
    vs->v4l_dst_fmt.fmt.pix.pixelformat = vs->v4l_dst_csp;
	
    err = v4lconvert_try_format(vs->v4l_convert,
                                &(vs->v4l_dst_fmt), &(vs->v4l_src_fmt));
    if (err) {
        tc_log_error(MOD_NAME, "unable to match formats: %s",
                     v4lconvert_get_error_message(vs->v4l_convert));
        return TC_ERROR;
    }

    err = v4l2_ioctl(vs->video_fd, VIDIOC_S_FMT, &(vs->v4l_src_fmt));
    if (err < 0) {
        tc_log_error(MOD_NAME, "error while setting the cam image format");
        return TC_ERROR;            
    }

    if (!v4lconvert_needs_conversion(vs->v4l_convert,
                                    &(vs->v4l_src_fmt),
                                    &(vs->v4l_dst_fmt))) {
        tc_log_info(MOD_NAME, "fetch frames directly");
        vs->fetch_data = tc_v4l2_fetch_data_memcpy;
        /* Into the near future we should aim for zero-copy. -- FR */
    } else {
        char src_fcc[5] = { '\0' };
        char dst_fcc[5] = { '\0' };

        pixfmt_to_fourcc(vs->v4l_src_fmt.fmt.pix.pixelformat, src_fcc);
        pixfmt_to_fourcc(vs->v4l_dst_fmt.fmt.pix.pixelformat, dst_fcc);

        tc_log_info(MOD_NAME, "fetch frames using libv4lconvert "
                              "[%s] -> [%s]",
                              src_fcc, dst_fcc);
        vs->fetch_data = tc_v4l2_fetch_data_v4lconv;
    }

    return TC_OK;
}

static void tc_v4l2_teardown_image_format(V4L2Source *vs)
{
    if (vs->v4l_convert) {
        v4lconvert_destroy(vs->v4l_convert);
        vs->v4l_convert = NULL;
    }
}

static int tc_v4l2_video_setup_stream_parameters(V4L2Source *vs, int fps)
{
    struct v4l2_streamparm streamparm;
    int err = 0;

    memset(&streamparm, 0, sizeof(streamparm));
    streamparm.type                                  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparm.parm.capture.capturemode              = 0;
    streamparm.parm.capture.timeperframe.numerator   = 1e7;
    streamparm.parm.capture.timeperframe.denominator = fps;

    err = v4l2_ioctl(vs->video_fd, VIDIOC_S_PARM, &streamparm);
    if (err < 0) {
        tc_log_warn(MOD_NAME, "driver does not support setting parameters"
                              " (ioctl(VIDIOC_S_PARM) returns \"%s\")",
                    errno <= sys_nerr ? sys_errlist[errno] : "unknown");
    }
    return TC_OK;
}

static int tc_v4l2_video_get_capture_buffer_count(V4L2Source *vs)
{
    struct v4l2_requestbuffers reqbuf;
    int err = 0;

    reqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count  = TC_V4L2_BUFFERS_NUM;

    err = v4l2_ioctl(vs->video_fd, VIDIOC_REQBUFS, &reqbuf);
    if (err < 0) {
        tc_log_perror(MOD_NAME, "VIDIOC_REQBUFS");
        return TC_ERROR;
    }

    vs->buffers_count = TC_MIN(reqbuf.count, TC_V4L2_BUFFERS_NUM);

    if (vs->buffers_count < 2) {
        tc_log_error(MOD_NAME, "not enough buffers for capture");
        return TC_ERROR;
    }

    if (verbose_flag > TC_INFO) {
        tc_log_info(MOD_NAME, "%i buffers available (maximum supported: %i)",
                    vs->buffers_count, TC_V4L2_BUFFERS_NUM);
    }
    return TC_OK;
}


static int tc_v4l2_video_setup_capture_buffers(V4L2Source *vs)
{
    struct v4l2_buffer buffer;
    int ix, err = 0;

    /* map the buffers */
    for (ix = 0; ix < vs->buffers_count; ix++) {
        buffer.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index  = ix;

        err = v4l2_ioctl(vs->video_fd, VIDIOC_QUERYBUF, &buffer);
        if (err < 0) {
            tc_log_perror(MOD_NAME, "VIDIOC_QUERYBUF");
            return TC_ERROR;
        }

        vs->buffers[ix].length = buffer.length;
        vs->buffers[ix].start  = v4l2_mmap(0, buffer.length,
                                           PROT_READ|PROT_WRITE, MAP_SHARED,
                                           vs->video_fd, buffer.m.offset);

        if (vs->buffers[ix].start == MAP_FAILED) {
            tc_log_perror(MOD_NAME, "mmap");
            return TC_ERROR;
        }
    }

    /* then enqueue them all */
    for (ix = 0; ix < vs->buffers_count; ix++) {
        buffer.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index  = ix;

        err = v4l2_ioctl(vs->video_fd, VIDIOC_QBUF, &buffer);
        if (err < 0) {
            tc_log_perror(MOD_NAME, "VIDIOC_QBUF");
            return TC_ERROR;
        }
    }

    return TC_OK;
}

static int tc_v4l2_capture_start(V4L2Source *vs)
{
    int err = 0, arg = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    err = v4l2_ioctl(vs->video_fd, VIDIOC_STREAMON, &arg);
    if (err < 0) {
        /* ugh, needs VIDEO_CAPTURE */
        tc_log_perror(MOD_NAME, "VIDIOC_STREAMON");
        return TC_ERROR;
    }

    return TC_OK;
}

static int tc_v4l2_capture_stop(V4L2Source *vs)
{
    int err = 0, arg = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    err = v4l2_ioctl(vs->video_fd, VIDIOC_STREAMOFF, &arg);
    if (err < 0) {
        /* ugh, needs VIDEO_CAPTURE */
        tc_log_perror(MOD_NAME, "VIDIOC_STREAMOFF");
        return TC_ERROR;
    }

    return TC_OK;
}

static int tc_v4l2_parse_options(V4L2Source *vs, int layout, const char *options)
{
    switch (layout) {
      case TC_CODEC_RGB24:
        vs->v4l_dst_csp = V4L2_PIX_FMT_RGB24;
        break;
      case TC_CODEC_YUV420P:
        vs->v4l_dst_csp = V4L2_PIX_FMT_YUV420;
        break;
      case TC_CODEC_YUV422P:
        vs->v4l_dst_csp = V4L2_PIX_FMT_YYUV;
        break;
      default:
        tc_log_error(MOD_NAME,
                     "colorspace (0x%X) must be one of"
                     " RGB24, YUV 4:2:0 or YUV 4:2:2",
                     layout);
        return TC_ERROR;
    }

    return TC_OK;
}

/* ============================================================
 * V4L2 CORE
 * ============================================================*/

#define RETURN_IF_FAILED(RET) do { \
    if ((RET) != TC_OK) { \
        return (RET); \
    } \
} while (0)

static int tc_v4l2_video_init(V4L2Source *vs, 
                           int layout, const char *device,
                           int width, int height, int fps,
                           const char *options)
{
    int ret = tc_v4l2_parse_options(vs, layout, options);
    RETURN_IF_FAILED(ret);

    vs->video_fd = v4l2_open(device, O_RDWR, 0);
    if (vs->video_fd < 0) {
        tc_log_error(MOD_NAME, "cannot open video device %s", device);
        return TC_ERROR;
    }

    ret = tc_v4l2_video_check_capabilities(vs);
    RETURN_IF_FAILED(ret);

    ret = tc_v4l2_video_setup_image_format(vs, width, height);
    RETURN_IF_FAILED(ret);

    ret = tc_v4l2_video_setup_stream_parameters(vs, fps);
    RETURN_IF_FAILED(ret);

    ret = tc_v4l2_video_get_capture_buffer_count(vs);
    RETURN_IF_FAILED(ret);

    ret = tc_v4l2_video_setup_capture_buffers(vs);
    RETURN_IF_FAILED(ret);

    return tc_v4l2_capture_start(vs);
}

static int tc_v4l2_video_get_frame(V4L2Source *vs, uint8_t *data, size_t size)
{
    int ret;
    int buffers_filled = tc_v4l2_video_count_buffers(vs);

    if (buffers_filled == -1) {
        tc_log_warn(MOD_NAME, "unable to get the capture buffers count," 
                              " assuming OK");
        buffers_filled = 0;
    }

    if (buffers_filled > (vs->buffers_count * 3 / 4)) {
        tc_log_error(MOD_NAME, "running out of capture buffers (%d left from %d total), "
                               "stopping capture",
                               vs->buffers_count - buffers_filled,
                               vs->buffers_count);

        ret = tc_v4l2_capture_stop(vs);
    } else {
        ret = tc_v4l2_video_grab_frame(vs, data, size);
        vs->video_sequence++;
    }

    return ret;
}

static int tc_v4l2_video_grab_stop(V4L2Source *vs)
{
    int ix, ret;

    tc_v4l2_teardown_image_format(vs);

    ret = tc_v4l2_capture_stop(vs);
    RETURN_IF_FAILED(ret);

    for (ix = 0; ix < vs->buffers_count; ix++)
        v4l2_munmap(vs->buffers[ix].start, vs->buffers[ix].length);

    v4l2_close(vs->video_fd);
    vs->video_fd = -1;

    return TC_OK;
}

/* ============================================================
 * TRANSCODE INTERFACE
 * ============================================================*/

static V4L2Source VS;

/* ------------------------------------------------------------
 * open stream
 * ------------------------------------------------------------*/

MOD_open
{
    if (param->flag == TC_VIDEO) {
        if (tc_v4l2_video_init(&VS,
                               vob->im_v_codec, vob->video_in_file,
                               vob->im_v_width, vob->im_v_height,
                               vob->fps, vob->im_v_string)) {
            return TC_ERROR;
        }
    } else {
        tc_log_error(MOD_NAME, "unsupported request (init)");
        return TC_ERROR;
    }

    return TC_OK;
}

/* ------------------------------------------------------------
 * decode  stream
 * ------------------------------------------------------------*/

MOD_decode
{
    if (param->flag == TC_VIDEO) {
        if (tc_v4l2_video_get_frame(&VS, param->buffer, param->size)) {
            tc_log_error(MOD_NAME, "error in grabbing video");
            return TC_ERROR;
        }
    } else {
        tc_log_error(MOD_NAME, "unsupported request (decode)");
        return TC_ERROR;
    }

    return TC_OK;
}

/* ------------------------------------------------------------
 * close stream
 * ------------------------------------------------------------*/

MOD_close
{
    if (param->flag == TC_VIDEO) {
        tc_v4l2_video_grab_stop(&VS);
    } else {
        tc_log_error(MOD_NAME, "unsupported request (close)");
        return TC_ERROR;
    }

    return TC_OK;
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

