/*
 *  import_v4l2.c
 *
 *  By Erik Slagter <erik@slagter.name> Sept 2003
 *  some cleanup and tuning support:
 *  Francesco Romani <fromani@gmail.com> Sept 2008
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
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING. If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define MOD_NAME        "import_v4l2.so"
#define MOD_VERSION     "v1.6.2 (2008-10-25)"
#define MOD_CODEC       "(video) v4l2 | (audio) pcm"

#include "src/transcode.h"

static int verbose_flag     = TC_QUIET;
static int capability_flag  = TC_CAP_RGB|TC_CAP_YUV|TC_CAP_YUV422|TC_CAP_PCM;

/*%*
 *%* DESCRIPTION 
 *%*   This module allow to capture video frames through a V4L2 (V4L api version 2)
 *%*   device. While audio capturing is possible, this kind of usage is discouraged
 *%*   in favour of OSS or ALSA import modules.
 *%*
 *%* #BUILD-DEPENDS
 *%*
 *%* #DEPENDS
 *%*
 *%* PROCESSING
 *%*   import/demuxer
 *%*
 *%* MEDIA
 *%*   video, audio
 *%*
 *%* #INPUT
 *%*
 *%* OUTPUT
 *%*   YUV420P, YUV422P, RGB24, PCM
 *%*
  *%* OPTION
 *%*   ignore_mute (boolean)
 *%*     disable the device audio muting during the operation.
 *%*
 *%* OPTION
 *%*   resync_margin (integer)
 *%*     threshold audio/video desync (in frames) that triggers resync once reached.
 *%*
 *%* OPTION
 *%*   resync_interval (integer)
 *%*     checks the resync_margin every given amount of frames.
 *%*
 *%* OPTION
 *%*   overrun_guard (integer)
 *%*     flag (default off). Toggles the buffer overrun guard, that prevents crash when capture buffers are full.
 *%*
 *%* OPTION
 *%*   crop (string)
 *%*     forces cropping into selected window (format: WIDTHxHEIGHT+LEFTxTOP)
 *%*
 *%* OPTION
 *%*   convert (integer)
 *%*     forces video frames convertion by using index; use the special value "list"
 *%*     to get a list of supported conversions.
 *%*
 *%* OPTION
 *%*   format (integer)
 *%*     forces video frames convertion by using index; use -1 to get a list of supported conversions.
 *%*
 *%* OPTION
 *%*   format (string)
 *%*     forces output format to given one; use "list" to get a list of supported formats.
 *%*
 *%* OPTION
 *%*   input (string)
 *%*     select the V4L input source. V4L cards have often have more than an input source like,
 *%*     say, a tv tuner and a composite source. Use "list" parameter to get a list of supported
 *%*     input sources.
 *%*
 *%* OPTION
 *%*   channel (string)
 *%*     synthonize the V4L tuner to selected TV channel. The channel frequencies are taken by
 *%*     the module configuration file, and they must be expressed in KHz.
 *%*/

#define MOD_PRE         tc_v4l2
#include "import_def.h"

#define _ISOC9X_SOURCE 1

#include <sys/soundcard.h>
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

#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcutil/cfgfile.h"
#include "libtcvideo/tcvideo.h"

#define TC_V4L2_CHANNELS_FILE           "tvchannels.cfg"
#define TC_V4L2_BUFFERS_NUM             (32)
#define TC_V4L2_DEFAULT_TUNER_ID        (0)

/*
    Changelog

    1.0.0   EMS first published version
    1.0.1   EMS added YUV422 and RGB support
                disable timestamp stuff for now, doesn't work anyways
                    as long as tc core doesn't support it.
                missing mute control is not an error.
    1.0.2   EMS changed parameter passing from -T to -x v4l2=a=x,b=y
                try various (native) capture formats before giving up
    1.0.3   EMS changed "videodev2.h" back to <linux/videodev2.h>,
                it doesn't work with linux 2.6.0, #defines are wrong.
            tibit   figure out if the system does have videodev2.h
                    gcc-2.95 bugfix
            tibit   check for struct v4l2_buffer
    1.1.0   EMS added dma overrun protection, use overrun_guard=0 to disable
                    this prevents from crashing the computer when all
                    capture buffers are full while capturing, by stopping capturing
                    when > 75% of the buffers are filled.
            EMS added YUV422 capture -> YUV420 transcode core conversion
                    for those whose cards' hardware downsampling to YUV420 conversion is broken
    1.2.0   EMS added a trick to get a better a/v sync in the beginning:
                    don't start audio (which seems always to be started first)
                    until video is up and running using a mutex.
                    This means that must not use -D anymore.
    1.2.1   EMS added bttv driver to blacklist 'does not support cropping
                    info ioctl'
            tibit added mmx version of yuy2_to_uyvy
                    hacked in alternate fields (#if 0'ed)
                    fixed a typo (UYUV -> UYVY)
    1.2.2   EMS fixed av sync mutex not yet grabbed problem with "busy" wait
    1.3.0   EMS added cropping cap, removed saa7134 and bttv specific code, not
                    necessary
    1.3.1   EMS make conversion user-selectable
    1.3.2   EMS removed a/v sync mutex, doesn't work as expected
            EMS added explicit colour format / frame rate selection
            EMS deleted disfunctional experimental alternating fields code
            EMS added experimental code to make sa7134 survive sync glitches
    1.3.3   EMS adapted fast memcpy to new default transcode method
    1.3.4   EMS fixed RGB24 capturing bug when using saa7134.
    1.3.5   EMS test with unrestricted cloning/dropping of frames using resync_interval=0
                adjusted saa7134 audio message to make clear the user must take action
    1.4.0   AC  switch to aclib for image conversion
    1.5.0   FR  made STYLEish and switched to optstr
    1.6.0   FR  tuning support
            internal rename v4l2_* -> tc_v4l_* to make te code libv4l-safe.
    1.6.1   FR  verbosiness fixes (made module more silent by default).
*/

/* TODO: memset() verify and sanitization */

typedef enum {
    mute_on,
    mute_off
} v4l2_mute_op;

typedef enum { 
    resync_none,
    resync_clone,
    resync_drop
} v4l2_resync_op;

typedef struct tcv4lconversion TCV4LConversion;
struct tcv4lconversion {
    int         v4l_format;
    ImageFormat from;
    ImageFormat to;
    const char  *description;
};

typedef struct tcv4lbuffer TCV4LBuffer;
struct tcv4lbuffer {
    void    *start;
    size_t  length;
};

typedef struct tccroparea_ TCCropArea;
struct tccroparea_ {
    int width;
    int height;
    int left;
    int top;
};

typedef struct v4l2source_ V4L2Source;
struct v4l2source_ {
    int               video_fd;
    int               audio_fd;

    ImageFormat       fmt;
    int               overrun_guard;
    int               buffers_count;

    int               frame_rate;
    int               width;
    int               height;
 
    TCCropArea        crop;
    int               crop_enabled;
    int               convert_id;

    struct v4l2_input input;
    struct v4l2_tuner tuner;
    int               has_tuner; /* flag */

    char              crop_parm[TC_BUF_MIN];
    char              format_name[TC_BUF_MIN];
    char              input_name[TC_BUF_MIN];
    char              channel_name[TC_BUF_MIN];

    TCVHandle         tcvhandle;
    TCV4LBuffer       buffers[TC_V4L2_BUFFERS_NUM];
    int               saa7134_audio;
    int               mute_audio;
    v4l2_resync_op    video_resync_op;
    int               resync_margin_frames;
    int               resync_interval_frames;
    int               video_sequence;
    int               audio_sequence;
    int               video_cloned;
    int               video_dropped;

    uint8_t           *resync_previous_frame;
};

static TCV4LConversion v4l2_format_conversions[] = {
    { V4L2_PIX_FMT_RGB24,   IMG_RGB24,   IMG_RGB_DEFAULT, "RGB24 [packed] -> RGB [packed] (no conversion)" },
    { V4L2_PIX_FMT_BGR24,   IMG_BGR24,   IMG_RGB_DEFAULT, "BGR24 [packed] -> RGB [packed]" },
    { V4L2_PIX_FMT_RGB32,   IMG_RGBA32,  IMG_RGB_DEFAULT, "RGB32 [packed] -> RGB [packed]" },
    { V4L2_PIX_FMT_BGR32,   IMG_BGRA32,  IMG_RGB_DEFAULT, "BGR32 [packed] -> RGB [packed]" },
    { V4L2_PIX_FMT_GREY,    IMG_GRAY8,   IMG_RGB_DEFAULT, "8-bit grayscale -> RGB [packed]" },
    { V4L2_PIX_FMT_YUYV,    IMG_YUY2,    IMG_RGB_DEFAULT, "YUY2 [packed] -> RGB [packed]" },
    /* an exception for the `vivi' v4l testing fake device */

    { V4L2_PIX_FMT_YYUV,    IMG_YUV422P, IMG_YUV422P,     "YUV422 [planar] -> YUV422 [planar] (no conversion)" },
    { V4L2_PIX_FMT_UYVY,    IMG_UYVY,    IMG_YUV422P,     "UYVY [packed] -> YUV422 [planar] (no conversion)" },
    { V4L2_PIX_FMT_YUYV,    IMG_YUY2,    IMG_YUV422P,     "YUY2 [packed] -> YUV422 [planar]" },
    { V4L2_PIX_FMT_YUV420,  IMG_YUV420P, IMG_YUV422P,     "YUV420 [planar] -> YUV422 [planar]" },
    { V4L2_PIX_FMT_YVU420,  IMG_YV12,    IMG_YUV422P,     "YVU420 [planar] -> YUV422 [planar]" },
    { V4L2_PIX_FMT_Y41P,    IMG_YUV411P, IMG_YUV422P,     "YUV411 [planar] -> YUV422 [planar]" },
    { V4L2_PIX_FMT_GREY,    IMG_GRAY8,   IMG_YUV422P,     "8-bit grayscale -> YUV422 [planar]" },

    { V4L2_PIX_FMT_YUV420,  IMG_YUV420P, IMG_YUV_DEFAULT, "YUV420 [planar] -> YUV420 [planar] (no conversion)" },
    { V4L2_PIX_FMT_YVU420,  IMG_YV12,    IMG_YUV_DEFAULT, "YVU420 [planar] -> YUV420 [planar]" },
    { V4L2_PIX_FMT_YYUV,    IMG_YUV422P, IMG_YUV_DEFAULT, "YUV422 [planar] -> YUV420 [planar]" },
    { V4L2_PIX_FMT_Y41P,    IMG_YUV411P, IMG_YUV_DEFAULT, "YUV411 [planar] -> YUV420 [planar]" },
    { V4L2_PIX_FMT_UYVY,    IMG_UYVY,    IMG_YUV_DEFAULT, "UYVY [packed] -> YUV420 [planar]" },
    { V4L2_PIX_FMT_YUYV,    IMG_YUY2,    IMG_YUV_DEFAULT, "YUY2 [packed] -> YUV420 [planar]" },
    { V4L2_PIX_FMT_GREY,    IMG_GRAY8,   IMG_YUV_DEFAULT, "8-bit grayscale -> YUV420 [planar]" },
};
#define CONVERSIONS_NUM (sizeof(v4l2_format_conversions) / sizeof(*v4l2_format_conversions))

/* ============================================================
 * IMAGE FORMAT CONVERSION ROUTINE
 * ============================================================*/

static void tc_v4l2_convert(V4L2Source *vs,
                            uint8_t *source, uint8_t *dest)
{
    if (vs->convert_id >= 0) {
        const TCV4LConversion *conv = &v4l2_format_conversions[vs->convert_id];
        tcv_convert(vs->tcvhandle,
                    source, dest, vs->width, vs->height,
                    conv->from, conv->to);
    }
    return;
}

/* ============================================================
 * UTILS
 * ============================================================*/

static int tc_v4l2_mute(V4L2Source *vs, v4l2_mute_op value)
{
    if (vs->mute_audio) {
        struct v4l2_control control = {
            .id    = V4L2_CID_AUDIO_MUTE,
            .value = value
        };
        int ret = ioctl(vs->video_fd, VIDIOC_S_CTRL, &control);
        if (ret < 0) {
            if (verbose_flag > TC_INFO)
                tc_log_perror(MOD_NAME,
                              "error in muting (ioctl(VIDIOC_S_CTRL) failed)");
            return 0;
        }
    }
    return 1;
}


static int tc_v4l2_video_clone_frame(V4L2Source *vs, uint8_t *dest, size_t size)
{
    if (!vs->resync_previous_frame)
        memset(dest, 0, size);
    else
        ac_memcpy(dest, vs->resync_previous_frame, size);

    return 1;
}

static void tc_v4l2_video_save_frame(V4L2Source *vs, const uint8_t *source, size_t length)
{
    if (!vs->resync_previous_frame)
        vs->resync_previous_frame = tc_malloc(length);

    ac_memcpy(vs->resync_previous_frame, source, length);
}

static int tc_v4l2_video_grab_frame(V4L2Source *vs, uint8_t *dest, size_t length)
{
    static struct v4l2_buffer buffer; /* FIXME */
    int ix, err = 0, eio = 0;

    // get buffer
    buffer.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory   = V4L2_MEMORY_MMAP;

    err = ioctl(vs->video_fd, VIDIOC_DQBUF, &buffer);
    if (err < 0) {
        tc_log_perror(MOD_NAME,
                      "error in setup grab buffer (ioctl(VIDIOC_DQBUF) failed)");

        if (errno != EIO) {
            return TC_OK;
        } else {
            eio = 1;

            for (ix = 0; ix < vs->buffers_count; ix++) {
                buffer.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buffer.memory   = V4L2_MEMORY_MMAP;
                buffer.index    = ix;
                buffer.flags    = 0;

                err = ioctl(vs->video_fd, VIDIOC_DQBUF, &buffer);
                if (err < 0)
                    tc_log_perror(MOD_NAME,
                                  "error in recovering grab buffer (ioctl(DQBUF) failed)");
            }

            for (ix = 0; ix < vs->buffers_count; ix++) {
                buffer.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buffer.memory   = V4L2_MEMORY_MMAP;
                buffer.index    = ix;
                buffer.flags    = 0;

                err = ioctl(vs->video_fd, VIDIOC_QBUF, &buffer);
                if (err < 0)
                    tc_log_perror(MOD_NAME,
                                  "error in recovering grab buffer (ioctl(QBUF) failed)");
            }
        }
    }

    ix  = buffer.index;

    // copy frame
    if (dest) {
        tc_v4l2_convert(vs, vs->buffers[ix].start, dest);
    }

    // enqueue buffer again
    if (!eio) {
        buffer.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory   = V4L2_MEMORY_MMAP;
        buffer.flags    = 0;

        err = ioctl(vs->video_fd, VIDIOC_QBUF, &buffer);
        if (err < 0) {
            tc_log_perror(MOD_NAME, "error in enqueuing buffer (ioctl(VIDIOC_QBUF) failed)");
            return TC_OK;
        }
    }

    return 1;
}

static int tc_v4l2_video_count_buffers(V4L2Source *vs)
{
    struct v4l2_buffer buffer;
    int ix, ret, buffers_filled = 0;

    for (ix = 0; ix < vs->buffers_count; ix++) {
        buffer.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index  = ix;

        ret = ioctl(vs->video_fd, VIDIOC_QUERYBUF, &buffer);
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

static int tc_v4l2_video_setup_cropping(V4L2Source *vs,
                                        const char *v4l2_crop_parm,
                                        int width, int height)
{
    size_t slen = strlen(v4l2_crop_parm);
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    int ret;
    
    if (!v4l2_crop_parm || !slen) {
        return TC_OK;
    }
    if (sscanf(v4l2_crop_parm, "%ux%u+%ux%u",
               &vs->crop.width, &vs->crop.height,
               &vs->crop.left,  &vs->crop.top) == 4) {
        vs->crop_enabled = 1;
    }

    if ((verbose_flag > TC_INFO) && vs->crop_enabled) {
        tc_log_info(MOD_NAME, "source frame set to: %dx%d+%dx%d",
                    vs->crop.width, vs->crop.height,
                    vs->crop.left,  vs->crop.top);
    }

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(vs->video_fd, VIDIOC_CROPCAP, &cropcap);
    if (ret < 0) {
        tc_log_warn(MOD_NAME,
                    "driver does not support cropping"
                    "(ioctl(VIDIOC_CROPCAP) returns \"%s\"), disabled",
                    errno <= sys_nerr ? sys_errlist[errno] : "unknown");
        return TC_ERROR;
    }
    if (verbose_flag > TC_INFO) {
        tc_log_info(MOD_NAME, "frame size: %dx%d", width, height);
        tc_log_info(MOD_NAME, "cropcap bounds: %dx%d +%d+%d",
                    cropcap.bounds.width, cropcap.bounds.height,
                    cropcap.bounds.left,  cropcap.bounds.top);
        tc_log_info(MOD_NAME, "cropcap defrect: %dx%d +%d+%d",
                    cropcap.defrect.width, cropcap.defrect.height,
                    cropcap.defrect.left,  cropcap.defrect.top);
        tc_log_info(MOD_NAME, "cropcap pixelaspect: %d/%d",
                    cropcap.pixelaspect.numerator,
                    cropcap.pixelaspect.denominator);
    }
    if ((width > cropcap.bounds.width)
     || (height > cropcap.bounds.height)
     || (width < 0) || (height < 0)) {
        tc_log_error(MOD_NAME, "capturing dimensions exceed"
                               " maximum crop area: %dx%d",
                     cropcap.bounds.width, cropcap.bounds.height);
        return TC_ERROR;
    }

    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(vs->video_fd, VIDIOC_G_CROP, &crop);
    if (ret < 0) {
        tc_log_warn(MOD_NAME,
                    "driver does not support inquiring cropping"
                    " parameters (ioctl(VIDIOC_G_CROP) returns \"%s\")",
                    errno <= sys_nerr ? sys_errlist[errno] : "unknown");
        return -1;
    }

    if (verbose_flag > TC_INFO) {
        tc_log_info(MOD_NAME, "default cropping: %dx%d +%d+%d",
                    crop.c.width, crop.c.height, crop.c.left, crop.c.top);
    }

    if (vs->crop_enabled) {
        crop.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c.width  = vs->crop.width;
        crop.c.height = vs->crop.height;
        crop.c.left   = vs->crop.left;
        crop.c.top    = vs->crop.top;

        ret = ioctl(vs->video_fd, VIDIOC_S_CROP, &crop);
        if (ret < 0) {
            tc_log_perror(MOD_NAME, "VIDIOC_S_CROP");
            return -1;
        }

        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ret = ioctl(vs->video_fd, VIDIOC_G_CROP, &crop);
        if (ret < 0) {
            tc_log_warn(MOD_NAME,
                        "driver does not support inquering cropping"
                        " parameters (ioctl(VIDIOC_G_CROP) returns \"%s\")",
                        errno <= sys_nerr ? sys_errlist[errno] : "unknown");
            return -1;
        }
        if (verbose_flag > TC_INFO) {
            tc_log_info(MOD_NAME, "cropping after set frame source: %dx%d +%d+%d",
                        crop.c.width, crop.c.height, crop.c.left, crop.c.top);
        }
    }
    return 0;
}

static int tc_v4l2_video_check_capabilities(V4L2Source *vs)
{
    struct v4l2_capability caps;
    int err = 0;

    err = ioctl(vs->video_fd, VIDIOC_QUERYCAP, &caps);
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

    if (verbose_flag > TC_INFO)
        tc_log_info(MOD_NAME, "v4l2 video grabbing, driver = %s, card = %s",
                    caps.driver, caps.card);



    return TC_OK;
}

static int tc_v4l2_video_setup_image_format(V4L2Source *vs, int width, int height)
{
    TCV4LConversion *fcp = v4l2_format_conversions;
    int ix = 0, err = 0, found = 0;
    struct v4l2_format format;

    vs->width  = width;
    vs->height = height;

    for (ix = 0; ix < CONVERSIONS_NUM; ix++) {
        if (fcp[ix].to != vs->fmt)
            continue;

        if ((vs->convert_id >= 0) && (vs->convert_id != ix))
            continue;

        memset(&format, 0, sizeof(format));
        format.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width        = width;
        format.fmt.pix.height       = height;
        format.fmt.pix.pixelformat  = fcp[ix].v4l_format;

        err = ioctl(vs->video_fd, VIDIOC_S_FMT, &format);
        if (err < 0) {
            if (verbose_flag >= TC_INFO) {
                tc_log_warn(MOD_NAME, "bad pixel format conversion: %s", fcp[ix].description);
            }
        } else {
            if (verbose_flag > TC_INFO) {
                tc_log_info(MOD_NAME, "found pixel format conversion: %s", fcp[ix].description);
            }
            vs->convert_id = ix;
            found = 1;
            break;
        }
    }

    if (!found) {
        tc_log_error(MOD_NAME, "no usable pixel format supported by card");
        return TC_ERROR;
    }
    return TC_OK;
}

static int tc_v4l2_video_setup_stream_parameters(V4L2Source *vs, int fps)
{
    struct v4l2_streamparm streamparm;
    int err = 0;

    memset(&streamparm, 0, sizeof(streamparm));
    streamparm.type                                     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparm.parm.capture.capturemode                 = 0;
    streamparm.parm.capture.timeperframe.numerator      = 1e7;
    streamparm.parm.capture.timeperframe.denominator    = fps;

    err = ioctl(vs->video_fd, VIDIOC_S_PARM, &streamparm);
    if (err < 0) {
        if (verbose_flag) {
            tc_log_warn(MOD_NAME, "driver does not support setting parameters (ioctl(VIDIOC_S_PARM) returns \"%s\")",
                        errno <= sys_nerr ? sys_errlist[errno] : "unknown");
        }
    }
    return TC_OK;
}

static int tc_v4l2_video_get_TV_standard(V4L2Source *vs)
{
    struct v4l2_standard standard;
    v4l2_std_id stdid;
    int err = 0;

    err = ioctl(vs->video_fd, VIDIOC_G_STD, &stdid);
    if (err < 0) {
        tc_log_warn(MOD_NAME, "driver does not support get std (ioctl(VIDIOC_G_STD) returns \"%s\")",
                    errno <= sys_nerr ? sys_errlist[errno] : "unknown");
        memset(&stdid, 0, sizeof(v4l2_std_id));
    }

    if (stdid & V4L2_STD_525_60) {
        vs->frame_rate = 30;
    } else if (stdid & V4L2_STD_625_50) {
        vs->frame_rate = 25;
    } else {
        tc_log_info(MOD_NAME, "unknown TV std, defaulting to 50 Hz field rate");
        vs->frame_rate = 25;
    }

    if (verbose_flag > TC_INFO) {
        int ix;

        for (ix = 0; ix < 128; ix++) {
            standard.index = ix;

            err = ioctl(vs->video_fd, VIDIOC_ENUMSTD, &standard);
            if (err < 0) {
                if (errno == EINVAL)
                    break;

                tc_log_perror(MOD_NAME,
                              "error in enumerating TV standards (ioctl(VIDIOC_ENUMSTD) failed)");
                return TC_ERROR;
            }

            if (standard.id == stdid) {
                tc_log_info(MOD_NAME, "V4L2 device supports format [%s] ", standard.name);
                break;
            }
        }

        tc_log_info(MOD_NAME, "receiving %d frames / sec", vs->frame_rate);
    }
    return TC_OK;
}

static int tc_v4l2_video_list_TV_standards(V4L2Source *vs)
{
    struct v4l2_standard standard;
    int ix, err = 0;

    for (ix = 0; ix < 128; ix++) {
        standard.index = ix;

        err = ioctl(vs->video_fd, VIDIOC_ENUMSTD, &standard);
        if (err < 0) {
            if (errno == EINVAL)
                break;

            tc_log_perror(MOD_NAME,
                          "error in enumerating TV standards (ioctl(VIDIOC_ENUMSTD) failed)");
            return TC_ERROR;
        }

        if (standard.id & vs->input.std) {
            tc_log_info(MOD_NAME, "%s", standard.name);
        }
    }

    return TC_ERROR;
}

static int tc_v4l2_video_setup_TV_standard(V4L2Source *vs)
{
    struct v4l2_standard standard;
    int err, ix = 0, found  = 0, supported = 0;

    if (!strcmp(vs->format_name, "list")) {
        return tc_v4l2_video_list_TV_standards(vs);
    }

    if (strlen(vs->format_name) > 0) {
        for (ix = 0; ix < 128; ix++) {
            standard.index = ix;

            err = ioctl(vs->video_fd, VIDIOC_ENUMSTD, &standard);
            if (err < 0) {
                if (errno == EINVAL)
                    break;

                tc_log_perror(MOD_NAME,
                              "error in enumerating TV standards (ioctl(VIDIOC_ENUMSTD) failed)");
                return TC_ERROR;
            }

            if (!strcasecmp((char*)standard.name, vs->format_name)) {
                /* to make gcc happy */
                found = 1;
                if (standard.id & vs->input.std) {
                    supported = 1;
                }
            }
        }

        if (!found) {
            tc_log_error(MOD_NAME, "unknown format '%s'", vs->format_name);
            return TC_ERROR;
        }
        if (!supported) {
            tc_log_error(MOD_NAME, "current input doesn't support format '%s'", vs->format_name);
            return TC_ERROR;
        }

        err = ioctl(vs->video_fd, VIDIOC_S_STD, &standard.id);
        if (err < 0) {
            tc_log_perror(MOD_NAME, "error in setting TV standard (ioctl(VIDIOC_S_STD) failed)");
            return TC_ERROR;
        }

        if (verbose_flag > TC_INFO) {
            tc_log_info(MOD_NAME, "colour & framerate standard set to: [%s]", standard.name);
        }
    }
    return tc_v4l2_video_get_TV_standard(vs);
}

static int tc_v4l2_video_get_capture_buffer_count(V4L2Source *vs)
{
    struct v4l2_requestbuffers reqbuf;
    int err = 0;

    reqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count  = TC_V4L2_BUFFERS_NUM;

    err = ioctl(vs->video_fd, VIDIOC_REQBUFS, &reqbuf);
    if (err < 0) {
        tc_log_perror(MOD_NAME, "VIDIOC_REQBUFS");
        return TC_ERROR;
    }

    vs->buffers_count = TC_MIN(reqbuf.count, TC_V4L2_BUFFERS_NUM);

    if (vs->buffers_count < 2) {
        tc_log_error(MOD_NAME, "not enough buffers for capture");
        return TC_ERROR;
    }

    if (verbose_flag > TC_INFO)
        tc_log_info(MOD_NAME, "%i buffers available (maximum supported: %i)",
                    vs->buffers_count, TC_V4L2_BUFFERS_NUM);

    return TC_OK;
}


static int tc_v4l2_video_setup_capture_buffers(V4L2Source *vs)
{
    struct v4l2_buffer buffer;
    int ix, err = 0;

    /* map the buffers */
    for (ix = 0; ix < vs->buffers_count; ix++) {
        buffer.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory   = V4L2_MEMORY_MMAP;
        buffer.index    = ix;

        err = ioctl(vs->video_fd, VIDIOC_QUERYBUF, &buffer);
        if (err < 0) {
            tc_log_perror(MOD_NAME, "VIDIOC_QUERYBUF");
            return TC_ERROR;
        }

        vs->buffers[ix].length = buffer.length;
        vs->buffers[ix].start  = mmap(0, buffer.length, PROT_READ|PROT_WRITE, MAP_SHARED, vs->video_fd, buffer.m.offset);

        if (vs->buffers[ix].start == MAP_FAILED) {
            tc_log_perror(MOD_NAME, "mmap");
            return TC_ERROR;
        }
    }

    /* then enqueue them all */
    for (ix = 0; ix < vs->buffers_count; ix++) {
        buffer.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory   = V4L2_MEMORY_MMAP;
        buffer.index    = ix;

        err = ioctl(vs->video_fd, VIDIOC_QBUF, &buffer);
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

    err = ioctl(vs->video_fd, VIDIOC_STREAMON, &arg);
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

    err = ioctl(vs->video_fd, VIDIOC_STREAMOFF, &arg);
    if (err < 0) {
        /* ugh, needs VIDEO_CAPTURE */
        tc_log_perror(MOD_NAME, "VIDIOC_STREAMOFF");
        return TC_ERROR;
    }

    return TC_OK;
}

static int tc_v4l2_video_get_tuner_properties(V4L2Source *vs)
{
    int err = 0;

    memset(&(vs->tuner), 0, sizeof(vs->tuner));

    if (vs->input.type != V4L2_INPUT_TYPE_TUNER) {
        if (verbose_flag > TC_INFO) {
            tc_log_info(MOD_NAME, "input has not tuner");
        }
    } else {
        vs->tuner.index = vs->input.tuner;
        err = ioctl(vs->video_fd, VIDIOC_G_TUNER, &(vs->tuner));
        if (err) {
            tc_log_perror(MOD_NAME, "getting input tuner properties");
            return TC_ERROR;
        }
        
        if (verbose_flag > TC_INFO) {
            tc_log_info(MOD_NAME, "input has attached tuner '%s'", vs->tuner.name);
        }
        vs->has_tuner = 1;
    }
    return TC_OK;
}

static int tc_v4l2_video_set_tuner_frequency(V4L2Source *vs)
{
    /* sanity check */
    if (vs->has_tuner && (vs->channel_name && strlen(vs->channel_name))) {
        struct v4l2_frequency freq;
        int ret = 0, chan_freq = 0;

        TCConfigEntry chan_conf[] = {
            { "frequency", &chan_freq, TCCONF_TYPE_INT, 0, 0, 0 },
            /* FIXME: add limits */
            /* End of the config file */
            { NULL, 0, 0, 0, 0, 0 }
        };
        const char *dirs[] = { ".", NULL };

        ret = tc_config_read_file(dirs, TC_V4L2_CHANNELS_FILE,
                                  vs->channel_name,
                                  chan_conf, MOD_NAME);
        if (!ret) {
            tc_log_error(MOD_NAME, "Error reading the frequencies"
                                   " configuration file.");
            return TC_ERROR;
        }

        memset(&freq, 0, sizeof(freq));
        freq.tuner     = vs->tuner.index;
        freq.type      = vs->tuner.type;
        /* 
         * The base unit (see V4L spec) is 62.5 KHz. 
         * From configuration file we got frequency in KHz.
         * In order to safely do an integer division, we multiply
         * both operands by 4 (so 62.5*4 = 250)
         */
        freq.frequency = (chan_freq * 4) / 250;
        if (vs->tuner.capability & V4L2_TUNER_CAP_LOW) {
            freq.frequency *= 1000; /* KHz -> Hz */
        }

        ret = ioctl(vs->video_fd, VIDIOC_S_FREQUENCY, &freq);
        if (ret != 0) {
            tc_log_perror(MOD_NAME, "tuning the channel");
            return TC_ERROR;
        }
    }
    return TC_OK; /* silently skip on error */
}

static int tc_v4l2_parse_options(V4L2Source *vs, int layout, const char *options)
{
    char fmt_name[TC_BUF_MIN] = { '\0' };
    int ix = 0;

    vs->mute_audio = TC_TRUE; /* for back compatibility and comfort */

    switch (layout) {
      case TC_CODEC_RGB24:
        vs->fmt = IMG_RGB_DEFAULT;
        break;
      case TC_CODEC_YUV420P:
        vs->fmt = IMG_YUV_DEFAULT;
        break;
      case TC_CODEC_YUV422P:
        vs->fmt = IMG_YUV422P;
        break;
      default:
        tc_log_error(MOD_NAME,
                     "colorspace (0x%X) must be one of"
                     " RGB24, YUV 4:2:0 or YUV 4:2:2",
                     layout);
        return TC_ERROR;
    }

    /* reset to defaults */
    vs->convert_id = -1;

    if (options) {
        /* flags first */
        if (optstr_lookup(options, "ignore_mute")) {
            vs->mute_audio = TC_FALSE;
        }

        optstr_get(options, "resync_margin",   "%i",    &vs->resync_margin_frames);
        optstr_get(options, "resync_interval", "%i",    &vs->resync_interval_frames);
        optstr_get(options, "overrun_guard",   "%i",    &vs->overrun_guard);
        optstr_get(options, "crop",            "%[^:]", vs->crop_parm);
        optstr_get(options, "format",          "%[^:]", vs->format_name);
        optstr_get(options, "convert",         "%[^:]", fmt_name);
        optstr_get(options, "input",           "%[^:]", vs->input_name);
        optstr_get(options, "channel",         "%[^:]", vs->channel_name);
    }

    if (!strcmp(fmt_name, "list")) {
        TCV4LConversion *fcp = v4l2_format_conversions;
        for (ix = 0; ix < CONVERSIONS_NUM; ix++)
            tc_log_info(MOD_NAME,
                        "conversion index: %d = %s", ix, fcp[ix].description);

        return TC_ERROR;
    }
    if (fmt_name[0]) { /* we can do better */
        vs->convert_id = atoi(fmt_name);
    }

    if (verbose_flag > TC_INFO) {
        if (!vs->mute_audio) {
            tc_log_info(MOD_NAME, "audio muting disabled");
        }

        if (vs->resync_margin_frames == 0) {
            tc_log_info(MOD_NAME, "resync disabled");
        } else {
            tc_log_info(MOD_NAME, "resync enabled, margin = %d frames, interval = %d frames,",
                        vs->resync_margin_frames, vs->resync_interval_frames);
       }
    }

    return TC_OK;
}

static int tc_v4l2_video_get_input_source(V4L2Source *vs)
{
    int err = 0;
    
    err = ioctl(vs->video_fd, VIDIOC_G_INPUT, &(vs->input.index));
    if (err) {
        tc_log_perror(MOD_NAME, "getting the default input source");
        return TC_ERROR;
    }
    err = ioctl(vs->video_fd, VIDIOC_ENUMINPUT, &(vs->input));
    if (err) {
        tc_log_perror(MOD_NAME, "getting the default input source properties");
        return TC_ERROR;
    }
    if (verbose_flag > TC_INFO) {
        tc_log_info(MOD_NAME, "using input '%s'", vs->input.name);
    }
 
    return TC_OK;
}

static int tc_v4l2_video_list_input_sources(V4L2Source *vs)
{
    struct v4l2_input input;
    int err = 0;
    uint32_t i;

    for (i = 0; !err; i++) {
        input.index = i;
        err = ioctl(vs->video_fd, VIDIOC_ENUMINPUT, &input);
        if (!err) {
            tc_log_info(MOD_NAME, "input source: '%s'", input.name);
        }
    }
    return TC_ERROR;
}

static int tc_v4l2_video_setup_input_source(V4L2Source *vs)
{
   if (!strcmp(vs->input_name, "list")) {
        return tc_v4l2_video_list_input_sources(vs);
   }

    if (strlen(vs->input_name) > 0) {
        int err = 0, idx = 0, found = 0;
        uint32_t i = 0;

        for (i = 0; !err; i++) {
            vs->input.index = i;
            err = ioctl(vs->video_fd, VIDIOC_ENUMINPUT, &(vs->input));
            if (!err) {
                if (strcasecmp((char*)vs->input.name, vs->input_name) == 0) {
                    /* to make gcc happy */
                    found = 1;
                }
            }
        }
        /* sanity checks */
        if (err && errno != EINVAL) {
            tc_log_perror(MOD_NAME, "selecting the input source");
            return TC_ERROR;
        }
        if (!found) {
            tc_log_error(MOD_NAME, "unknown input source '%s'", vs->input_name);
            return TC_ERROR;
        } 

        idx = vs->input.index;
        err = ioctl(vs->video_fd, VIDIOC_S_INPUT, &idx);
        if (err) {
            tc_log_perror(MOD_NAME, "setting the input source");
            return TC_ERROR;
        }
    }
    return tc_v4l2_video_get_input_source(vs);
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

    vs->tcvhandle = tcv_init();
    if (!vs->tcvhandle) {
        tc_log_error(MOD_NAME, "tcv_init() failed");
        return TC_ERROR;
    }

    vs->video_fd = open(device, O_RDWR, 0);
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

    ret = tc_v4l2_video_setup_input_source(vs);
    RETURN_IF_FAILED(ret);

    ret = tc_v4l2_video_setup_TV_standard(vs);
    RETURN_IF_FAILED(ret);

    ret = tc_v4l2_video_get_tuner_properties(vs);
    RETURN_IF_FAILED(ret);

    tc_v4l2_video_set_tuner_frequency(vs);
    RETURN_IF_FAILED(ret);

    ret = tc_v4l2_video_setup_cropping(vs, vs->crop_parm, width, height);
    RETURN_IF_FAILED(ret);

    ret = tc_v4l2_video_get_capture_buffer_count(vs);
    RETURN_IF_FAILED(ret);

    ret = tc_v4l2_video_setup_capture_buffers(vs);
    RETURN_IF_FAILED(ret);

    if (!tc_v4l2_mute(vs, mute_on))
        return TC_ERROR;

    return tc_v4l2_capture_start(vs);
}

static int tc_v4l2_video_get_frame(V4L2Source *vs, uint8_t *data, size_t size)
{
    if (vs->overrun_guard) {
        int buffers_filled = tc_v4l2_video_count_buffers(vs);

        if (buffers_filled > (vs->buffers_count * 3 / 4)) {
            tc_log_error(MOD_NAME, "running out of capture buffers (%d left from %d total), "
                                   "stopping capture",
                                   vs->buffers_count - buffers_filled,
                                   vs->buffers_count);

            return tc_v4l2_capture_stop(vs);
        }
    }

    switch (vs->video_resync_op) {
        case resync_clone:
            if (!tc_v4l2_video_clone_frame(vs, data, size))
                return 1;
            break;

        case resync_drop:
            if (!tc_v4l2_video_grab_frame(vs, 0, 0))
                return 1;
            if (!tc_v4l2_video_grab_frame(vs, data, size))
                return 1;
            break;

        case resync_none:
            if (!tc_v4l2_video_grab_frame(vs, data, size))
                return 1;
            break;

        default:
            tc_log_error(MOD_NAME, "impossible case");
            return 1;
    }

    vs->video_resync_op = resync_none;

    if ((vs->resync_margin_frames != 0)
     && (vs->video_sequence != 0)
     && (vs->audio_sequence != 0)
     && ((vs->resync_interval_frames == 0) || (vs->video_sequence % vs->resync_interval_frames) == 0)) {
        if (abs(vs->audio_sequence - vs->video_sequence) > vs->resync_margin_frames) {
            if (vs->audio_sequence > vs->video_sequence) {
                tc_v4l2_video_save_frame(vs, data, size);
                vs->video_cloned++;
                vs->video_resync_op = resync_clone;
            } else {
                vs->video_resync_op = resync_drop;
                vs->video_dropped++;
            }
        }

        if (vs->video_resync_op != resync_none && (verbose_flag > TC_INFO)) {
            tc_log_msg(MOD_NAME, "OP: %s VS/AS: %d/%d C/D: %d/%d",
                       vs->video_resync_op == resync_drop ? "drop" : "clone",
                       vs->video_sequence, vs->audio_sequence,
                       vs->video_cloned, vs->video_dropped);
        }
    }

    vs->video_sequence++;

    return TC_OK;
}

static int tc_v4l2_video_grab_stop(V4L2Source *vs)
{
    int ix, ret;

    if (!tc_v4l2_mute(vs, mute_off))
        return 1;

    ret = tc_v4l2_capture_stop(vs);
    RETURN_IF_FAILED(ret);

    for (ix = 0; ix < vs->buffers_count; ix++)
        munmap(vs->buffers[ix].start, vs->buffers[ix].length);

    close(vs->video_fd);
    vs->video_fd = -1;

    tc_free(vs->resync_previous_frame);
    vs->resync_previous_frame = NULL;

    tcv_free(vs->tcvhandle);
    vs->tcvhandle = 0;

    return TC_OK;
}

static int tc_v4l2_audio_init(V4L2Source *vs, const char *device,
                           int rate, int bits, int channels)
{
    int version, tmp, err = 0;

    vs->audio_fd = open(device, O_RDONLY, 0);
    if (vs->audio_fd < 0) {
        tc_log_perror(MOD_NAME, "open audio device");
        return TC_ERROR;
    }

    if (!strcmp(device, "/dev/null")
     || !strcmp(device, "/dev/zero")) {
        return TC_OK;
    }

    if (bits != 8 && bits != 16) {
        tc_log_error(MOD_NAME, "bits/sample must be 8 or 16");
        return TC_ERROR;
    }

    err = ioctl(vs->audio_fd, OSS_GETVERSION, &version);
    if (err < 0) {
        tc_log_perror(MOD_NAME, "OSS_GETVERSION");
        return TC_ERROR;
    }

    tmp = (bits == 8) ?AFMT_U8 :AFMT_S16_LE;

    err = ioctl(vs->audio_fd, SNDCTL_DSP_SETFMT, &tmp);
    if (err < 0) {
        tc_log_perror(MOD_NAME, "SNDCTL_DSP_SETFMT");
        return TC_ERROR;
    }

    err = ioctl(vs->audio_fd, SNDCTL_DSP_CHANNELS, &channels);
    if (err < 0) {
        tc_log_perror(MOD_NAME, "SNDCTL_DSP_CHANNELS");
        return TC_ERROR;
    }

    // check for saa7134
    // this test will: set sampling to "0 khz", check if this returns "OK" and "32 khz"
    tmp = 0;
    /*
     * http://manuals.opensound.com/developer/SNDCTL_DSP_SPEED.html :
     * Description
     * This ioctl call selects the sampling rate (in Hz) to be used with the stream.
     * After the call the active sampling rate will be returned in the variable
     * pointed by the argument. The application must check this value and adjust
     * it's operation depending on it.
     *
     */
    err = ioctl(vs->audio_fd, SNDCTL_DSP_SPEED, &tmp);
    if (err >= 0) {
        if (tmp == 0 || tmp == 32000)
            vs->saa7134_audio = 1;
    }

    if (vs->saa7134_audio) {
        if(verbose_flag)
            tc_log_info(MOD_NAME,
                        "Audio input from saa7134 detected, you should "
                        "set audio sample rate to 32 Khz using -e");
    } else {
        /* this is the real sample rate setting */
        tmp = rate;
        err = ioctl(vs->audio_fd, SNDCTL_DSP_SPEED, &tmp);
        if (err < 0) {
            tc_log_perror(MOD_NAME, "SNDCTL_DSP_SPEED");
            return TC_ERROR;
        }
        if (tmp != rate) {
            tc_log_warn(MOD_NAME, "sample rate requested=%i obtained=%i",
                                  rate, tmp);
        }
    }

    return TC_OK;
}

static int tc_v4l2_audio_grab_frame(V4L2Source *vs, uint8_t *buffer, size_t size)
{
    int left     = size;
    int offset   = 0;
    int received;

    while (left > 0)  {
        received = read(vs->audio_fd, buffer + offset, left);

        if (received == 0)
            tc_log_warn(MOD_NAME, "audio grab: received == 0");

        if (received < 0) {
            if (errno == EINTR) {
                received = 0;
            } else {
                tc_log_perror(MOD_NAME, "read audio");
                return TC_ERROR;
            }
        }

        if (received > left) {
            tc_log_error(MOD_NAME,
                        "read returns more bytes than requested! (requested: %i, returned: %i",
                        left, received);
            return TC_ERROR;
        }

        offset += received;
        left   -= received;
    }

    vs->audio_sequence++;

    return TC_OK;
}

static int tc_v4l2_audio_grab_stop(V4L2Source *vs)
{
    close(vs->audio_fd);

    if (verbose_flag) {
        tc_log_msg(MOD_NAME, "Totals: sequence V/A: %d/%d, frames C/D: %d/%d",
                   vs->video_sequence, vs->audio_sequence,
                   vs->video_cloned,  vs->video_dropped);
    }

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
    } else if(param->flag == TC_AUDIO) {
        if (tc_v4l2_audio_init(&VS,
                            vob->audio_in_file,
                            vob->a_rate, vob->a_bits, vob->a_chan)) {
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
    } else if (param->flag == TC_AUDIO) {
        if (tc_v4l2_audio_grab_frame(&VS, param->buffer, param->size)) {
            tc_log_error(MOD_NAME, "error in grabbing audio");
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
    } else if (param->flag == TC_AUDIO) {
        tc_v4l2_audio_grab_stop(&VS);
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

