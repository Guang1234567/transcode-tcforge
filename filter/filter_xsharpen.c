/*
 *  filter_xsharpen.c -- VirtualDub's XSharpen Filter
 *
 *  Copyright (C) 1999-2000 Donald A. Graft
 *    modified 2002 by Tilmann Bitterberg for use with transcode
 *    modified 2007 by Branko Kokanovic <branko.kokanovic at gmail dot com> to use NMS
 *  This file is part of transcode, a video stream processing tool
 *
 *  Xsharpen Filter for VirtualDub -- sharpen by mapping pixels
 *  to the closest of window max or min.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  The author can be contacted at:
 *  Donald Graft
 *  http://sauron.mordor.net/dgraft/
 *  neuron2@home.com.
 */

#define MOD_NAME    "filter_xharpen.so"
#define MOD_VERSION "(1.1.0) (2009-02-07)"
#define MOD_CAP     "VirtualDub's XSharpen Filter"
#define MOD_AUTHOR  "Donald Graft, Tilmann Bitterberg"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE|TC_MODULE_FLAG_BUFFERING|TC_MODULE_FLAG_CONVERSION
    // XXX

/* -------------------------------------------------
 *
 * mandatory include files
 *
 *-------------------------------------------------*/

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"
#include "aclib/imgconvert.h"


/* vdub compat (doubtfl) */
typedef uint32_t Pixel;
typedef uint32_t Pixel32;
typedef uint8_t  Pixel8;
typedef int      PixCoord;
typedef int      PixDim;
typedef int      PixOffset;

static const char xsharpen_help[] = ""
    "Overview\n"
    "   This filter performs a subtle but useful sharpening effect. The\n"
    "   result is a sharpening effect that not only avoids amplifying\n"
    "   noise, but also tends to reduce it. A welcome side effect is that\n"
    "   files processed with this filter tend to compress to smaller files.\n"
    "\n"
    "Options\n"
    "   Strength 'strength' (0-255) [200]\n"
    "   When this value is 255, mapped pixels are not blended with the\n"
    "   original pixel values, so a full-strength effect is obtained. As\n"
    "   the value is reduced, each mapped pixel is blended with more of the\n"
    "   original pixel. At a value of 0, the original pixels are passed\n"
    "   through and there is no sharpening effect.\n"
    "\n"
    "   Threshold 'threshold' (0-255) [255]\n"
    "   This value determines how close a pixel must be to the brightest or\n"
    "   dimmest pixel to be mapped. If a pixel is more than threshold away\n"
    "   from the brightest or dimmest pixel, it is not mapped.  Thus, as\n"
    "   the threshold is reduced, pixels in the mid range start to be\n"
    "   spared.\n";

/*************************************************************************/

typedef struct myfilterdata_ XsharpenPrivateData;
struct myfilterdata_ {
    Pixel32     *convertFrameIn;
    Pixel32     *convertFrameOut;
    int         strength;
    int         strengthInv;
    int         threshold;
    int         srcPitch;
    int         dstPitch;
    int         codec;
    TCVHandle   tcvhandle;
    char        conf_str[TC_BUF_MIN];

    int (*filter_frame)(XsharpenPrivateData *mfd, vframe_list_t *frame);
    uint8_t *dst_buf;
};

/* forward declarations */
static int xsharpen_rgb_frame(XsharpenPrivateData *mfd,
                              vframe_list_t *frame);
static int xsharpen_yuv_frame(XsharpenPrivateData *mfd,
                              vframe_list_t *frame);

/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * xsharpen_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(xsharpen, XsharpenPrivateData)

/*************************************************************************/

/**
 * xsharpen_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(xsharpen)

/*************************************************************************/

/**
 * xsharpen_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int xsharpen_configure(TCModuleInstance *self,
                              const char *options,
                              TCJob *vob,
                              TCModuleExtraData *xdata[])
{
    XsharpenPrivateData *mfd = NULL;
    int width, height;

    TC_MODULE_SELF_CHECK(self, "configure");
    TC_MODULE_SELF_CHECK(vob, "configure");

    mfd = self->userdata;

    height = vob->ex_v_height;
    width  = vob->ex_v_width;

    /* setup defaults */
    mfd->codec          = vob->im_v_codec;
    mfd->strength       = 200; /* 255 is too much */
    mfd->strengthInv    = 255 - mfd->strength;
    mfd->threshold      = 255;
    mfd->srcPitch       = 0;
    mfd->dstPitch       = 0;
    mfd->dst_buf        = NULL;

    switch (mfd->codec) {
      case TC_CODEC_RGB24:
        mfd->filter_frame = xsharpen_rgb_frame;
        break;
      case TC_CODEC_YUV420P:
        mfd->dst_buf = tc_malloc(width*height*3/2); /* FIXME */
        if (!mfd->dst_buf) {
            tc_log_error(MOD_NAME, "cannot allocate internal YUV buffer");
            return TC_ERROR;
        }
        mfd->filter_frame = xsharpen_yuv_frame;
        break;
      default:
        tc_log_error(MOD_NAME, "unsupported colorspace");
        return TC_ERROR;
    }

    if (options) {
        optstr_get(options, "strength",  "%d", &mfd->strength);
        optstr_get(options, "threshold", "%d", &mfd->threshold);
    }
    mfd->strengthInv    = 255 - mfd->strength;

    if (verbose > TC_INFO) {
        tc_log_info(MOD_NAME, " XSharpen Filter Settings (%dx%d):", width,height);
        tc_log_info(MOD_NAME, "          strength = %d", mfd->strength);
        tc_log_info(MOD_NAME, "         threshold = %d", mfd->threshold);
    }

    /* fetch memory */
    mfd->convertFrameIn = tc_zalloc(width * height * sizeof(Pixel32));
    if (!mfd->convertFrameIn) {
        tc_log_error(MOD_NAME, "No memory at %d!", __LINE__);
        return TC_ERROR;
    }

    mfd->convertFrameOut = tc_zalloc(width * height * sizeof(Pixel32));
    if (!mfd->convertFrameOut) {
        tc_log_error(MOD_NAME, "No memory at %d!", __LINE__);
        return TC_ERROR;
    }

    mfd->tcvhandle = tcv_init();

    return TC_OK;
}

/*************************************************************************/

/**
 * xsharpen_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int xsharpen_stop(TCModuleInstance *self)
{
    XsharpenPrivateData *mfd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    mfd = self->userdata;

    if (mfd->dst_buf)
        tc_free(mfd->dst_buf);
    mfd->dst_buf = NULL;

    if (mfd->convertFrameIn)
        tc_free(mfd->convertFrameIn);
    mfd->convertFrameIn = NULL;

    if (mfd->convertFrameOut)
        tc_free(mfd->convertFrameOut);
    mfd->convertFrameOut = NULL;

    if (mfd->tcvhandle)
        tcv_free(mfd->tcvhandle);
    mfd->tcvhandle = 0;

    return TC_OK;
}

/*************************************************************************/

/**
 * xsharpen_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int xsharpen_inspect(TCModuleInstance *self,
                            const char *param, const char **value)
{
    XsharpenPrivateData *mfd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    mfd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = xsharpen_help;
    }
    if (optstr_lookup(param, "strength")) {
        tc_snprintf(mfd->conf_str, sizeof(mfd->conf_str),
                    "strength=%d", mfd->strength);
        *value = mfd->conf_str;
    }
    if (optstr_lookup(param, "threshold")) {
        tc_snprintf(mfd->conf_str, sizeof(mfd->conf_str),
                    "threshold=%d", mfd->threshold);
        *value = mfd->conf_str;
    }

    return TC_OK;
}

/*************************************************************************/

#define UPDATE_RGB_RANGE(p, luma) do { \
    if (luma > lumamax) { \
        lumamax = luma; \
        max = p; \
    } \
    if (luma < lumamin) { \
        lumamin = luma; \
        min = p; \
    } \
} while (0)

#define SEARCH_RGB_LUMA(PTR, X) do { \
    p = ((Pixel32 *)(PTR))[(X)-1]; \
    luma = p >> 24; \
    UPDATE_RGB_RANGE(p, luma); \
    \
    p = ((Pixel32 *)(PTR))[(X)]; \
    luma = p >> 24; \
    UPDATE_RGB_RANGE(p, luma); \
    \
    p = ((Pixel32 *)(PTR))[(X)+1]; \
    luma = p >> 24; \
    UPDATE_RGB_RANGE(p, luma); \
} while (0)


static int xsharpen_rgb_frame(XsharpenPrivateData *mfd, vframe_list_t *frame)
{
    const PixDim    width  = frame->v_width;
    const PixDim    height = frame->v_height;
    Pixel32         *src= NULL, *dst = NULL;
    int             x, y;
    int             r, g, b, R, G, B;
    Pixel32         p, min=1000, max=-1;
    int             luma, lumac, lumamax, lumamin, mindiff, maxdiff;
    const int       srcpitch = frame->v_width*sizeof(Pixel32);
    const int       dstpitch = frame->v_width*sizeof(Pixel32);

    Pixel32 *dst_buf = NULL;
    Pixel32 *src_buf = NULL;

    tcv_convert(mfd->tcvhandle, frame->video_buf,
                (uint8_t *)mfd->convertFrameIn, frame->v_width, frame->v_height,
                IMG_RGB24, IMG_BGRA32);

    src_buf = mfd->convertFrameIn;
    dst_buf = mfd->convertFrameOut;

    /* First copy through the four border lines. */
    src = src_buf;
    dst = dst_buf;
    for (x = 0; x < width; x++){
        dst[x] = src[x];
    }
    src = (Pixel *)((char *)src_buf + (height - 1) * srcpitch);
    dst = (Pixel *)((char *)dst_buf + (height - 1) * dstpitch);
    for (x = 0; x < width; x++){
        dst[x] = src[x];
    }
    src = src_buf;
    dst = dst_buf;
    for (y = 0; y < height; y++){
        dst[0] = src[0];
        dst[width-1] = src[width-1];
        src = (Pixel *)((char *)src + srcpitch);
        dst = (Pixel *)((char *)dst + dstpitch);
    }

    /* Now calculate and store the pixel luminances for the remaining pixels. */
    src = src_buf;
    for (y = 0; y < height; y++){
        for (x = 0; x < width; x++){
            r = (src[x] >> 16) & 0xff;
            g = (src[x] >> 8) & 0xff;
            b = src[x] & 0xff;
            luma = (55 * r + 182 * g + 19 * b) >> 8;
            src[x] &= 0x00ffffff;
            src[x] |= (luma << 24);
        }
        src = (Pixel *)((char *)src + srcpitch);
    }

    /* Finally run the 3x3 rank-order sharpening kernel over the pixels. */
    src = (Pixel *)((char *)src_buf + srcpitch);
    dst = (Pixel *)((char *)dst_buf + dstpitch);
    for (y = 1; y < height - 1; y++){
        for (x = 1; x < width - 1; x++){
            /* Find the brightest and dimmest pixels in the 3x3 window
            surrounding the current pixel. */
            lumamax = -1;
            lumamin = 1000;

            SEARCH_RGB_LUMA(((char *)src - srcpitch), x);

            p = src[x-1];
            luma = p >> 24;
            UPDATE_RGB_RANGE(p, luma);

            p = src[x];
            lumac = luma = p >> 24;
            UPDATE_RGB_RANGE(p, luma);

            p = src[x+1];
            luma = p >> 24;
            UPDATE_RGB_RANGE(p, luma);

            SEARCH_RGB_LUMA(((char *)src + srcpitch), x);

            /* Determine whether the current pixel is closer to the
               brightest or the dimmest pixel. Then compare the current
               pixel to that closest pixel. If the difference is within
               threshold, map the current pixel to the closest pixel;
               otherwise pass it through. */
            p = -1;
            if (mfd->strength != 0){
                mindiff = lumac - lumamin;
                maxdiff = lumamax - lumac;
                if (mindiff > maxdiff) {
                    if (maxdiff < mfd->threshold) {
                        p = max;
                    }
                } else {
                    if (mindiff < mfd->threshold) {
                        p = min;
                    }
                }
            }
            if (p == -1){
                dst[x] = src[x];
            }else{
                R = (src[x] >> 16) & 0xff;
                G = (src[x] >> 8 ) & 0xff;
                B =  src[x]        & 0xff;
                r = (p >> 16) & 0xff;
                g = (p >> 8 ) & 0xff;
                b =  p        & 0xff;
                r = (mfd->strength * r + mfd->strengthInv * R) / 255;
                g = (mfd->strength * g + mfd->strengthInv * G) / 255;
                b = (mfd->strength * b + mfd->strengthInv * B) / 255;
                dst[x] = (r << 16) | (g << 8) | b;
            }
        }
        src = (Pixel *)((char *)src + srcpitch);
        dst = (Pixel *)((char *)dst + dstpitch);
    }

    tcv_convert(mfd->tcvhandle, (uint8_t *)mfd->convertFrameOut,
                frame->video_buf, frame->v_width, frame->v_height,
                IMG_BGRA32, IMG_RGB24);

    return TC_OK;
}

#undef UPDATE_RGB_RANGE

#define UPDATE_YUV_RANGE(luma) do { \
    if (luma > lumamax) { \
        lumamax = luma; \
    } \
    if (luma < lumamin) { \
        lumamin = luma; \
    } \
} while (0)


#define SEARCH_YUV_LUMA(PTR, X) do { \
    luma = (PTR)[(X)-1] & 0xff; \
    UPDATE_YUV_RANGE(luma); \
    luma = (PTR)[(X)]   & 0xff; \
    UPDATE_YUV_RANGE(luma); \
    luma = (PTR)[(X)+1] & 0xff; \
    UPDATE_YUV_RANGE(luma); \
} while (0)


static int xsharpen_yuv_frame(XsharpenPrivateData *mfd, vframe_list_t *frame)
{
    const PixDim       width = frame->v_width;
    const PixDim       height = frame->v_height;
    char              *src, *dst;
    int                x, y;
    int                luma = 0, lumac = 0, lumamax, lumamin;
    int                p, mindiff, maxdiff;
    const int          srcpitch = frame->v_width;
    const int          dstpitch = frame->v_width;

    char *src_buf = frame->video_buf;

    /* First copy through the four border lines. */
    /* first */
    src = src_buf;
    dst = mfd->dst_buf;
    ac_memcpy(dst, src, width);

    /* last */
    src = src_buf+srcpitch*(height-1);
    dst = mfd->dst_buf+dstpitch*(height-1);
    ac_memcpy(dst, src, width);

    /* copy Cb and Cr */
    ac_memcpy(mfd->dst_buf+dstpitch*height,
              src_buf+srcpitch*height,
              width*height>>1);

    src = src_buf;
    dst = mfd->dst_buf;
    for (y = 0; y < height; y++){
        *dst = *src;
        *(dst+width-1) = *(src+width-1);
        dst += dstpitch;
        src += srcpitch;
    }

    src = src_buf + srcpitch;
    dst = mfd->dst_buf + dstpitch;

    /* Finally run the 3x3 rank-order sharpening kernel over the pixels. */
    for (y = 1; y < height - 1; y++){
        for (x = 1; x < width - 1; x++){
            /* Find the brightest and dimmest pixels in the 3x3 window
               surrounding the current pixel. */
            lumamax = -1000;
            lumamin = 1000;

            SEARCH_YUV_LUMA((src - srcpitch), x);
            SEARCH_YUV_LUMA( src,             x);
            SEARCH_YUV_LUMA((src + srcpitch), x);

            /* Determine whether the current pixel is closer to the
               brightest or the dimmest pixel. Then compare the current
               pixel to that closest pixel. If the difference is within
               threshold, map the current pixel to the closest pixel;
               otherwise pass it through. */

            p = -1;
            if (mfd->strength != 0){
                mindiff = lumac   - lumamin;
                maxdiff = lumamax - lumac;
                if (mindiff > maxdiff){
                    if (maxdiff < mfd->threshold)
                    p = lumamax & 0xff;
                } else {
                    if (mindiff < mfd->threshold)
                    p = lumamin & 0xff;
                }
            }
            if (p == -1) {
                dst[x] = src[x];
            } else {
                int t;
                lumac = src[x] & 0xff;
                t = ((mfd->strength*p + mfd->strengthInv*lumac)/255) & 0xff;
                t = TC_CLAMP(t, 16, 240);
                dst[x] = t & 0xff;
            }
        }
        src += srcpitch;
        dst += dstpitch;
    }

    ac_memcpy(frame->video_buf, mfd->dst_buf, width*height*3/2);
    return TC_OK;
}

/**
 * xsharpen_filter_video:  show something on given frame of the video
 * stream.  See tcmodule-data.h for function details.
 */

static int xsharpen_filter_video(TCModuleInstance *self, vframe_list_t *frame)
{
    XsharpenPrivateData *mfd = NULL;

    TC_MODULE_SELF_CHECK(self, "filer_video");
    TC_MODULE_SELF_CHECK(frame, "filer_video");

    mfd = self->userdata;

    if (!(frame->attributes & TC_FRAME_IS_SKIPPED)) {
        return mfd->filter_frame(mfd, frame);
    }
    return TC_OK;
}

/*************************************************************************/

static const TCCodecID xsharpen_codecs_video_in[] = { 
    TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_ERROR
};
static const TCCodecID xsharpen_codecs_video_out[] = {
    TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(xsharpen);
TC_MODULE_FILTER_FORMATS(xsharpen);

TC_MODULE_INFO(xsharpen);

static const TCModuleClass xsharpen_class = {
    TC_MODULE_CLASS_HEAD(xsharpen),

    .init         = xsharpen_init,
    .fini         = xsharpen_fini,
    .configure    = xsharpen_configure,
    .stop         = xsharpen_stop,
    .inspect      = xsharpen_inspect,

    .filter_video = xsharpen_filter_video
};

TC_MODULE_ENTRY_POINT(xsharpen)

/*************************************************************************/

static int xsharpen_get_config(TCModuleInstance *self, char *options)
{
    XsharpenPrivateData *mfd = NULL;
    char buf[TC_BUF_MIN];

    TC_MODULE_SELF_CHECK(self, "get_config");

    mfd = self->userdata;

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       MOD_AUTHOR, "VRYO", "1");

    /* can be omitted */
    optstr_param(options, "help", "VirtualDub's XSharpen Filter", "", "0");

    tc_snprintf(buf, sizeof(buf), "%d", mfd->strength);
    optstr_param(options, "strength", "How much  of the effect", "%d", buf, "0", "255");

    tc_snprintf(buf, sizeof(buf), "%d", mfd->threshold);
    optstr_param(options, "threshold",
        "How close a pixel must be to the brightest or dimmest pixel to be mapped",
        "%d", buf, "0", "255");

    return TC_OK;
}


static int xsharpen_process(TCModuleInstance *self, 
                            frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    if ((frame->tag & TC_VIDEO) && (frame->tag & TC_POST_M_PROCESS)) {
        return xsharpen_filter_video(self, (vframe_list_t*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE(xsharpen)

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
