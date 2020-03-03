/*
 *  import_bktr.c
 *
 *  Copyright (C) Jacob Meuser - September 2004
 *    based on code, hints and suggestions from: Roger Hardiman,
 *    Steve O'Hara-Smith, Erik Slagter and Stefan Scheffler
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

#define MOD_NAME	"import_bktr.so"
#define MOD_VERSION	"v0.0.2 (2004-10-02)"
#define MOD_CODEC	"(video) bktr"

#include "src/transcode.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcvideo/tcvideo.h"

/*%*
 *%* DESCRIPTION 
 *%*   This module reads video frames from an capture device using bktr module.
 *%*   This module is designed to work on *BSD. For linux, use the v4l module.
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
 *%*   YUV420, YUV422P, RGB24
 *%*
 *%* OPTION
 *%*   format (string)
 *%*     selects video normalization.
 *%*
 *%* OPTION
 *%*   vsource (string)
 *%*     selects video source (device dependant input).
 *%*
 *%* OPTION
 *%*   asource (string)
 *%*     selects audio source (device dependant input).
 *%*
 *%* OPTION
 *%*   tunerdev (string)
 *%*     help: selects tuner devince.
 *%*/
 
static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB | TC_CAP_YUV | TC_CAP_YUV422;

#define MOD_PRE bktr
#include "import_def.h"

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>

#ifdef HAVE_DEV_IC_BT8XX_H
#include <dev/ic/bt8xx.h>
#else
# ifdef HAVE_DEV_BKTR_IOCTL_BT848_H
#  include <dev/bktr/ioctl_meteor.h>
#  include <dev/bktr/ioctl_bt848.h>
# else
#  ifdef HAVE_MACHINE_IOCTL_BT848_H
#   include <machine/ioctl_meteor.h>
#   include <machine/ioctl_bt848.h>
#  endif
# endif
#endif


static const struct {
    char *name;
    u_int format;
} formats[] = {
    { "ntsc",		METEOR_FMT_NTSC },
    { "pal",		METEOR_FMT_PAL },
    { 0 }
};

static const struct {
    char *name;
    u_int vsource;
} vsources[] = {
    { "composite",	METEOR_INPUT_DEV0 },
    { "tuner",		METEOR_INPUT_DEV1 },
    { "svideo_comp",	METEOR_INPUT_DEV2 },
    { "svideo",		METEOR_INPUT_DEV_SVIDEO },
    { "input3",		METEOR_INPUT_DEV3 },
    { 0 }
};

static const struct {
    char *name;
    u_int asource;
} asources[] = {
    { "tuner",		AUDIO_TUNER },
    { "external",	AUDIO_EXTERN },
    { "internal",	AUDIO_INTERN },
    { 0 }
};

void bktr_usage(void);
int bktr_parse_options(char *);
static void catchsignal(int);
int bktr_init(int, const char *, int, int, int, char *);
int bktr_grab(size_t, char *);
int bktr_stop();
static void copy_buf_yuv422(char *, size_t);
static void copy_buf_yuv(char *, size_t);
static void copy_buf_rgb(char *, size_t);


volatile sig_atomic_t bktr_frame_waiting;

sigset_t sa_mask;

uint8_t	*bktr_buffer;
size_t	 bktr_buffer_size;
static	 int bktr_vfd = -1;
static	 int bktr_tfd = -1;
char	 bktr_tuner[128] = "/dev/tuner0";
int	 bktr_convert;
#define BKTR2RGB 0
#define BKTR2YUV422 1
#define BKTR2YUV 2
u_int	 bktr_format = 0;
u_int	 bktr_vsource = METEOR_INPUT_DEV1;  /* tuner */
u_int	 bktr_asource = AUDIO_TUNER;
int	 bktr_hwfps = 0;
int	 bktr_mute = 0;
TCVHandle bktr_tcvhandle = 0;


void bktr_usage(void)
{
    int i;

    tc_log_info(MOD_NAME,
                "* Overview");
    tc_log_info(MOD_NAME,
                "    This module grabs video frames from bktr(4) devices");
    tc_log_info(MOD_NAME,
                "    found on BSD systems.");

    tc_log_info(MOD_NAME,
                "* Options");

    tc_log_info(MOD_NAME,
                "   'format=<format>' Video norm, valid arguments:");
    for (i = 0; formats[i].name; i++)
        tc_log_info(MOD_NAME, "      %s", formats[i].name);
    tc_log_info(MOD_NAME,
                "       default: driver default");

    tc_log_info(MOD_NAME,
                "   'vsource=<vsource>' Video source, valid arguments:");
    for (i = 0; vsources[i].name; i++)
        tc_log_info(MOD_NAME, "      %s", vsources[i].name);
    tc_log_info(MOD_NAME,
                "       default: driver default (usually 'composite')");

    tc_log_info(MOD_NAME,
                "   'asource=<asource>' Audio source, valid arguments:");
    for (i = 0; asources[i].name; i++)
        tc_log_info(MOD_NAME, "      %s", asources[i].name);
    tc_log_info(MOD_NAME,
                "       default: driver default (usually 'tuner')");

    tc_log_info(MOD_NAME,
                "   'tunerdev=<tunerdev>' Tuner device, default: %s",
                bktr_tuner);

    tc_log_info(MOD_NAME,
                "   'mute' Mute the bktr device, off by default.");

    tc_log_info(MOD_NAME,
                "   'hwfps' Set frame rate in hardware, off by default.");
    tc_log_info(MOD_NAME,
                "      It's possible to get smoother captures by using");
    tc_log_info(MOD_NAME,
                "      -f to capture in the highest possible frame rate");
    tc_log_info(MOD_NAME,
                "      along with a frame rate filter to get a lower fps.");

    tc_log_info(MOD_NAME,
                "   'help' Show this help message");

    tc_log_info(MOD_NAME, "");
}

int bktr_parse_options(char *options)
{
    char format[128];
    char vsource[128];
    char asource[128];
    char tuner[128];
    int i;

    if (optstr_lookup(options, "help") != NULL) {
        bktr_usage();
        return(1);
    }

    if (optstr_lookup(options, "hwfps") != NULL)
        bktr_hwfps = 1;

    if (optstr_lookup(options, "mute") != NULL)
        bktr_mute = 1;

    if (optstr_get(options, "format", "%[^:]", &format) >= 0) {
        for (i = 0; formats[i].name; i++)
            if (strncmp(formats[i].name, format, 128) == 0)
                break;
        if (formats[i].name)
            bktr_format = formats[i].format;
        else {
            tc_log_warn(MOD_NAME,
                "invalid format: %s",
                format);
            return(1);
        }
    }

    if (optstr_get(options, "vsource", "%[^:]", &vsource) >= 0) {
        for (i = 0; vsources[i].name; i++)
            if (strncmp(vsources[i].name, vsource, 128) == 0)
                break;
        if (vsources[i].name)
            bktr_vsource = vsources[i].vsource;
        else {
            tc_log_warn(MOD_NAME,
                "invalid vsource: %s",
                vsource);
            return(1);
        }
    }

    if (optstr_get(options, "asource", "%[^:]", &asource) >= 0) {
        for (i = 0; asources[i].name; i++)
            if (strncmp(asources[i].name, asource, 128) == 0)
                break;
        if (asources[i].name)
            bktr_asource = asources[i].asource;
        else {
            tc_log_warn(MOD_NAME,
                "invalid asource: %s",
                asource);
            return(1);
        }
    }

    if (optstr_get(options, "tunerdev", "%[^:]", &tuner) >= 0)
        strlcpy(bktr_tuner, tuner, sizeof(bktr_tuner));

    return(0);
}

static void catchsignal(int signal)
{
    if (signal == SIGUSR1)
        bktr_frame_waiting = 1;
}

int bktr_init(int video_codec, const char *video_device,
                    int width, int height,
                    int fps, char *options)
{
    struct meteor_geomet geo;
    struct meteor_pixfmt pxf;
    struct sigaction act;
    int h_max, w_max;
    int rgb_idx = -1;
    int yuv422_idx = -1;
    int yuv_idx = -1;
    int i;

    if (options != NULL)
        if (bktr_parse_options(options))
            return(1);

    switch (bktr_format) {
      case METEOR_FMT_NTSC: h_max = 480; w_max = 640; break;
      case METEOR_FMT_PAL:  h_max = 576; w_max = 768; break;
      default:              h_max = 576; w_max = 768; break;
    }

    if (width > w_max) {
        tc_log_warn(MOD_NAME,
            "import width '%d' too large! "
            "PAL max width = 768, NTSC max width = 640",
            width);
        return(1);
    }

    if (height > h_max) {
        tc_log_warn(MOD_NAME,
            "import height %d too large! "
            "PAL max height = 576, NTSC max height = 480",
            height);
        return(1);
    }

    bktr_tcvhandle = tcv_init();
    if (!bktr_tcvhandle) {
        tcv_log_warn(MOD_NAME, "tcv_init() failed");
        return(1);
    }

    /* set the audio via the tuner.  opening the device unmutes it. */
    /* closing the device mutes it again.  so we hold it open */

    bktr_tfd = open(bktr_tuner, O_RDONLY);
    if (bktr_tfd < 0) {
        tc_log_perror(MOD_NAME, "open tuner");
        return(1);
    }

    if (ioctl(bktr_tfd, BT848_SAUDIO, &bktr_asource) < 0) {
        tc_log_perror(MOD_NAME, "BT848_SAUDIO asource");
        return(1);
    }

    if (bktr_mute) {
        i = AUDIO_MUTE;
        if (ioctl(bktr_tfd, BT848_SAUDIO, &i) < 0) {
            tc_log_perror(MOD_NAME, "BT848_SAUDIO AUDIO_MUTE");
            return(1);
        }
    } else {
        i = AUDIO_UNMUTE;
        if (ioctl(bktr_tfd, BT848_SAUDIO, &i) < 0) {
            tc_log_perror(MOD_NAME, "BT848_SAUDIO AUDIO_UNMUTE");
            return(1);
        }
    }

    /* open the video device */

    bktr_vfd = open(video_device, O_RDONLY);
    if (bktr_vfd < 0) {
        tc_log_perror(MOD_NAME, video_device);
        return(1);
    }

    /* get the indices of supported formats that transcode can use */

    for (i = 0; ; i++) {
        pxf.index = i;
        if (ioctl(bktr_vfd, METEORGSUPPIXFMT, &pxf) < 0) {
            if (errno == EINVAL)
                break;
            else
                return(1);
        }
        switch(pxf.type) {
            case METEOR_PIXTYPE_RGB:
                if ((pxf.Bpp == 4) && (pxf.swap_bytes == 0) &&
                       (pxf.swap_shorts == 0)) {
                    rgb_idx = pxf.index;
                }
                break;
            case METEOR_PIXTYPE_YUV_PACKED:
                if ((pxf.swap_bytes == 0) && (pxf.swap_shorts == 1)) {
                    yuv422_idx = pxf.index;
                }
                break;
            case METEOR_PIXTYPE_YUV_12:
                if ((pxf.swap_bytes == 1) && (pxf.swap_shorts == 1)) {
                    yuv_idx = pxf.index;
                }
                break;
            case METEOR_PIXTYPE_YUV:
            default:
                break;
        }
    }

    /* set format, conversion function, and buffer size */

    switch(video_codec) {
      case CODEC_RGB:
        i = rgb_idx;
        bktr_convert = BKTR2RGB;
        bktr_buffer_size = width * height * 4;
        break;
      case CODEC_YUV422:
        i = yuv422_idx;
        bktr_convert = BKTR2YUV422;
        bktr_buffer_size = width * height * 2;
        break;
      case CODEC_YUV:
        i = yuv_idx;
        bktr_convert = BKTR2YUV;
        bktr_buffer_size = width * height * 3 / 2;
        break;
      default:
        tc_log_warn(MOD_NAME,
            "video_codec (%d) must be %d or %d or %d\n",
            video_codec, CODEC_RGB, CODEC_YUV422, CODEC_YUV);
        return(1);
    }

    if (ioctl(bktr_vfd, METEORSACTPIXFMT, &i) < 0) {
        tc_log_perror(MOD_NAME, "METEORSACTPIXFMT");
        return(1);
    }

    /* set the geometry */

    geo.rows = height;
    geo.columns = width;
    geo.frames = 1;
    geo.oformat = 0;

    if (verbose_flag & TC_DEBUG) {
        tc_log_info(MOD_NAME,
            "geo.rows = %d, geo.columns = %d, "
            "geo.frames = %d, geo.oformat = %ld",
            geo.rows, geo.columns,
            geo.frames, (long)geo.oformat);
    }

    if (ioctl(bktr_vfd, METEORSETGEO, &geo) < 0) {
        tc_log_perror(MOD_NAME, "METEORSETGEO");
        return(1);
    }

    /* extra options */

    if (bktr_vsource) {
        if (ioctl(bktr_vfd, METEORSINPUT, &bktr_vsource) < 0) {
            tc_log_perror(MOD_NAME, "METEORSINPUT");
            return(1);
        }
    }

    if (bktr_format) {
        if (ioctl(bktr_vfd, METEORSFMT, &bktr_format) < 0) {
            tc_log_perror(MOD_NAME, "METEORSFMT");
            return(1);
        }
    }

    if (bktr_hwfps) {
        if (ioctl(bktr_vfd, METEORSFPS, &fps) < 0) {
            tc_log_perror(MOD_NAME, "METEORSFPS");
            return(1);
        }
    }

    /* mmap the buffer */

    bktr_buffer = mmap(0, bktr_buffer_size, PROT_READ, MAP_SHARED, bktr_vfd, 0);

    if (bktr_buffer == MAP_FAILED) {
        tc_log_perror(MOD_NAME, "mmap bktr_buffer");
        return(1);
    }

    /* for sigsuspend() */
    sigfillset(&sa_mask);
    sigdelset(&sa_mask, SIGUSR1);
    sigdelset(&sa_mask, SIGALRM);

    /* signal handler to know when data is ready to be read() */

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_handler = catchsignal;
    sigaction(SIGUSR1, &act, NULL);
    sigaction(SIGALRM, &act, NULL);

    i = SIGUSR1;
    if (ioctl(bktr_vfd, METEORSSIGNAL, &i) < 0) {
        tc_log_perror(MOD_NAME, "METEORSSIGNAL");
        return(1);
    }

    /* let `er rip! */

    i = METEOR_CAP_CONTINOUS;
    if (ioctl(bktr_vfd, METEORCAPTUR, &i) < 0) {
        tc_log_perror(MOD_NAME, "METEORCAPTUR");
        return(1);
    }

    return(0);
}

int bktr_grab(size_t size, char *dest)
{
    /* wait for a "buffer full" signal, but longer than 1 second */

    alarm(1);
    sigsuspend(&sa_mask);
    alarm(0);

    if (bktr_frame_waiting) {
        bktr_frame_waiting = 0;
        if (dest) {
            if (verbose_flag & TC_DEBUG) {
                tc_log_info(MOD_NAME, "copying %lu bytes, buffer size is %lu",
                                 (unsigned long)size,
                                 (unsigned long)bktr_buffer_size);
            }
            switch (bktr_convert) {
              case BKTR2RGB:    copy_buf_rgb(dest, size);    break;
              case BKTR2YUV422: copy_buf_yuv422(dest, size); break;
              case BKTR2YUV:    copy_buf_yuv(dest, size);    break;
              default:
                tc_log_warn(MOD_NAME,
                    "unrecognized video conversion request");
                return(1);
                break;
            }
        } else {
            tc_log_warn(MOD_NAME,
                "no destination buffer to copy frames to");
            return(1);
        }
    } else {  /* bktr_frame_waiting */
        tc_log_warn(MOD_NAME, "sigalrm");
    }

    return(0);
}

static void copy_buf_yuv422(char *dest, size_t size)
{
    uint8_t *planes;

    if (bktr_buffer_size != size) {
        tc_log_warn(MOD_NAME,
            "buffer sizes do not match (input %lu != output %lu)",
            (unsigned long)bktr_buffer_size, (unsigned long)size);
    }
    tcv_convert(bktr_tcvhandle, bktr_buffer, dest, size/2, 1,
		IMG_UYVY, IMG_YUV422P);
}

static void copy_buf_yuv(char *dest, size_t size)
{
    int y_size = bktr_buffer_size * 4 / 6;
    int u_size = bktr_buffer_size * 1 / 6;
    int y_offset = 0;
    int u1_offset = y_size + 0;
    int u2_offset = y_size + u_size;

    if (bktr_buffer_size != size)
        tc_log_warn(MOD_NAME,
            "buffer sizes do not match (input %lu != output %lu)",
            (unsigned long)bktr_buffer_size, (unsigned long)size);

    ac_memcpy(dest + y_offset,  bktr_buffer + y_offset,  y_size);
    ac_memcpy(dest + u1_offset, bktr_buffer + u1_offset, u_size);
    ac_memcpy(dest + u2_offset, bktr_buffer + u2_offset, u_size);
}

static void copy_buf_rgb(char *dest, size_t size)
{
    int i;

    /* 24 bit RGB packed into 32 bits (NULL, R, G, B) */

    if ((bktr_buffer_size * 3 / 4) != size)
        tc_log_warn(MOD_NAME,
            "buffer sizes do not match (input %lu != output %lu)",
            (unsigned long)bktr_buffer_size * 3 / 4, (unsigned long)size);

    /* bktr_buffer_size was set to width * height * 4 (32 bits) */
    /* so width * height = bktr_buffer_size / 4                 */
    tcv_convert(bktr_tcvhandle, bktr_buffer, dest, bktr_buffer_size/4, 1,
                IMG_ARGB32, IMG_RGB24);
}


int bktr_stop()
{
    int c;

    /* shutdown signals first */

    c = METEOR_SIG_MODE_MASK;
    ioctl(bktr_vfd, METEORSSIGNAL, &c);

    alarm(0);

    c = METEOR_CAP_STOP_CONT;
    ioctl(bktr_vfd, METEORCAPTUR, &c);

    c = AUDIO_MUTE;
    if (ioctl(bktr_tfd, BT848_SAUDIO, &c) < 0) {
        tc_log_perror(MOD_NAME, "BT848_SAUDIO AUDIO_MUTE");
        return(1);
    }

    if (bktr_vfd > 0) {
        close(bktr_vfd);
        bktr_vfd = -1;
    }

    if (bktr_tfd > 0) {
        close(bktr_tfd);
        bktr_tfd = -1;
    }

    munmap(bktr_buffer, bktr_buffer_size);

    return(0);
}



/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
    int ret = TC_IMPORT_OK;

    switch (param->flag) {
      case TC_VIDEO:
        if (verbose_flag & TC_DEBUG) {
            tc_log_info(MOD_NAME,
                "bktr video grabbing");
        }
        if (bktr_init(vob->im_v_codec, vob->video_in_file,
                      vob->im_v_width, vob->im_v_height,
                      vob->fps, vob->im_v_string)) {
            ret = TC_IMPORT_ERROR;
        }
        break;
      case TC_AUDIO:
        tc_log_warn(MOD_NAME,
            "unsupported request (init audio)\n");
        break;
      default:
        tc_log_warn(MOD_NAME,
            "unsupported request (init)\n");
        ret = TC_IMPORT_ERROR;
        break;
    }

    return(ret);
}


/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

MOD_decode
{
    int ret = TC_IMPORT_OK;

    switch (param->flag) {
      case TC_VIDEO:
        if (bktr_grab(param->size, param->buffer)) {
            tc_log_warn(MOD_NAME,
                "error in grabbing video");
            ret = TC_IMPORT_ERROR;
        }
        break;
      case TC_AUDIO:
        tc_log_warn(MOD_NAME,
            "unsupported request (decode audio)");
        ret = TC_IMPORT_ERROR;
        break;
      default:
        tc_log_warn(MOD_NAME,
            "unsupported request (decode)");
        ret = TC_IMPORT_ERROR;
        break;
    }

    return(ret);
}

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
    int ret = TC_IMPORT_OK;

    switch (param->flag) {
      case TC_VIDEO:
        bktr_stop();
        break;
      case TC_AUDIO:
        tc_log_warn(MOD_NAME,
            "unsupported request (close audio)");
        ret = TC_IMPORT_ERROR;
        break;
      default:
        tc_log_warn(MOD_NAME,
            "unsupported request (close)");
        ret = TC_IMPORT_ERROR;
        break;
    }

    return(ret);
}
