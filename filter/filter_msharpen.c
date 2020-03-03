/*
 * filter_msharpen.c
 *
 * Copyright (C) 1999-2000 Donal A. Graft
 *   modified 2003 by William Hawkins for use with transcode
 *   modified 2007 by Francesco Romani for transcode NMS
 *
 * MSharpen Filter for VirtualDub -- performs sharpening
 * limited to edge areas of the frame.
 * Copyright (C) 1999-2000 Donald A. Graft
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * The author can be contacted at:
 * Donald Graft
 * neuron2@home.com.
 */

#define MOD_NAME    "filter_msharpen.so"
#define MOD_VERSION "(1.1.1) (2009-02-07)"
#define MOD_CAP     "VirtualDub's MSharpen Filter"
#define MOD_AUTHOR  "Donald Graft, William Hawkins"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE|TC_MODULE_FLAG_BUFFERING|TC_MODULE_FLAG_CONVERSION
    // XXX

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"
#include "libtcvideo/tcvideo.h"



typedef struct MsharpenPrivateData {
    uint8_t       *convertFrameIn;
    uint8_t       *convertFrameOut;
    unsigned char *blur;
    unsigned char *work;
    int            strength;
    int            threshold;
    int            mask;
    int            highq;
    TCVHandle      tcvhandle;
    ImageFormat    out_fmt;
    char           conf_str[TC_BUF_MIN]; 
} MsharpenPrivateData;


static const char msharpen_help[] = ""
    "* Overview\n"
    "    This plugin implements an unusual concept in spatial sharpening.\n"
    "    Although designed specifically for anime, it also works well with\n"
    "    normal video. The filter is very effective at sharpening important\n"
    "    edges without amplifying noise.\n"
    "\n"
    "* Options\n"
    "  * Strength 'strength' (0-255) [100]\n"
    "    This is the strength of the sharpening to be applied to the edge\n"
    "    detail areas. It is applied only to the edge detail areas as\n"
    "    determined by the 'threshold' parameter. Strength 255 is the\n"
    "    strongest sharpening.\n"
    "\n"
    "  * Threshold 'threshold' (0-255) [10]\n"
    "    This parameter determines what is detected as edge detail and\n"
    "    thus sharpened. To see what edge detail areas will be sharpened,\n"
    "    use the 'mask' parameter.\n"
    "\n"
    "  * Mask 'mask' (0-1) [0]\n"
    "    When set to true, the areas to be sharpened are shown in white\n"
    "    against a black background. Use this to set the level of detail to\n"
    "    be sharpened. This function also makes a basic edge detection filter.\n"
    "\n"
    "  * HighQ 'highq' (0-1) [1]\n"
    "    This parameter lets you tradeoff speed for quality of detail\n"
    "    detection. Set it to true for the best detail detection. Set it to\n"
    "    false for maximum speed.\n";

/*************************************************************************/

/**
 * msharpen_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(msharpen, MsharpenPrivateData)

/*************************************************************************/

/**
 * msharpen_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(msharpen)

/*************************************************************************/

/**
 * msharpen_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int msharpen_configure(TCModuleInstance *self,
                              const char *options,
                              TCJob *vob,
                              TCModuleExtraData *xdata[])
{
    MsharpenPrivateData *pd = NULL;
    int width = vob->ex_v_width, height = vob->ex_v_height;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    /* default values */
    pd->strength  = 100; /* A little bird told me this was a good value */
    pd->threshold = 10;
    pd->mask      = TC_FALSE; /* not sure what this does at the moment */
    pd->highq     = TC_TRUE; /* high Q or not? */
    pd->out_fmt   = (vob->im_v_codec == TC_CODEC_YUV420P)
                        ?IMG_YUV_DEFAULT :IMG_RGB24;

    if (options) {
        optstr_get(options, "strength",  "%d", &pd->strength);
        optstr_get(options, "threshold", "%d", &pd->threshold);
        optstr_get(options, "highq",     "%d", &pd->highq);
        optstr_get(options, "mask",      "%d", &pd->mask);
    }

    if (verbose) {
        tc_log_info(MOD_NAME, "strength=%i threshold=%i (masking %s|highq %s)",
                              pd->strength, pd->threshold,
                              (pd->mask)  ?"yes"     :"no",
                              (pd->highq) ?"enabled" :"disabled");
    }

    /* fetch memory */
    pd->blur = tc_malloc(4 * width * height);
    if (!pd->blur) {
        goto no_blur_mem;
    }
    pd->work = tc_malloc(4 * width * height);
    if (!pd->work) {
        goto no_work_mem;
    }
    pd->convertFrameIn = tc_zalloc(4 * width * height);
    if (!pd->convertFrameIn) {
        goto no_framein_mem;
    }
    pd->convertFrameOut = tc_zalloc(4 * width * height);
    if (!pd->convertFrameOut) {
        goto no_frameout_mem;
    }
    pd->tcvhandle = tcv_init();
    if (!pd->tcvhandle) {
        goto no_tcvhandle;
    }

    return TC_OK;

  no_tcvhandle:
    tc_free(pd->convertFrameOut);
  no_frameout_mem:
    tc_free(pd->convertFrameIn);
  no_framein_mem:
    tc_free(pd->work);
  no_work_mem:
    tc_free(pd->blur);
  no_blur_mem:
    return TC_ERROR;
}

/*************************************************************************/

/**
 * msharpen_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

#define FREE_MEM(PTR) do { \
    if ((PTR)) \
        tc_free((PTR)); \
    (PTR) = NULL; \
} while (0)

static int msharpen_stop(TCModuleInstance *self)
{
    MsharpenPrivateData *pd = NULL;
    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    FREE_MEM(pd->convertFrameIn);
    FREE_MEM(pd->convertFrameOut);
    FREE_MEM(pd->blur);
    FREE_MEM(pd->work);

    tcv_free(pd->tcvhandle);
    pd->tcvhandle = 0;

    return TC_OK;
}

#undef FREE_MEM

/*************************************************************************/

/**
 * msharpen_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

#define INSPECT_PARAM(PARM, TYPE) do { \
    if (optstr_lookup(param, # PARM)) { \
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str), \
                    "%s=" TYPE, # PARM, pd->PARM); \
        *value = pd->conf_str; \
    } \
} while (0)

static int msharpen_inspect(TCModuleInstance *self,
                          const char *param, const char **value)
{
    MsharpenPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    
    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = msharpen_help;
    }

    INSPECT_PARAM(strength,  "%i");
    INSPECT_PARAM(threshold, "%i");
    INSPECT_PARAM(highq,     "%i");
    INSPECT_PARAM(mask,      "%i");

    return TC_OK;
}

#undef INSPECT_PARAM

/*************************************************************************/


static int msharpen_filter_video(TCModuleInstance *self,
                                 vframe_list_t *frame)
{
    const int  width  = frame->v_width;
    const int  height = frame->v_height;
    const long pitch = frame->v_width * 4;
    int        bwidth = 4 * width;
    uint8_t *src = NULL;
    uint8_t *dst = NULL;
    uint8_t *srcpp, *srcp, *srcpn, *workp, *blurp, *blurpn, *dstp;
    int r1, r2, r3, r4, g1, g2, g3, g4, b1, b2, b3, b4;
    int x, y, max, strength, invstrength, threshold;
    const int dstpitch = frame->v_width * 4;

    // MsharpenPrivateData *pd = NULL; /* yes, I'm (too) lazy */
    MsharpenPrivateData *mfd = NULL;

    TC_MODULE_SELF_CHECK(self, "filter");
    TC_MODULE_SELF_CHECK(frame, "filter");

    mfd = self->userdata;

    threshold   = mfd->threshold;
    strength    = mfd->strength;
    invstrength = 255 - strength;

    tcv_convert(mfd->tcvhandle, frame->video_buf, mfd->convertFrameIn,
                frame->v_width, frame->v_height, mfd->out_fmt, IMG_BGRA32);

    src = mfd->convertFrameIn;
    dst = mfd->convertFrameOut;

    /* Blur the source image prior to detail detection. Separate
       dimensions for speed. */
    /* Vertical. */
    srcpp = src;
    srcp  = srcpp + pitch;
    srcpn = srcp + pitch;
    workp = mfd->work + bwidth;
    for (y = 1; y < height - 1; y++) {
        for (x = 0; x < bwidth; x++) {
            workp[x] = (srcpp[x] + srcp[x] + srcpn[x]) / 3;
        }
        srcpp += pitch;
        srcp  += pitch;
        srcpn += pitch;
        workp += bwidth;
    }

    /* Horizontal. */
    workp  = mfd->work;
    blurp  = mfd->blur;
    for (y = 0; y < height; y++) {
        for (x = 4; x < bwidth - 4; x++) {
            blurp[x] = (workp[x-4] + workp[x] + workp[x+4]) / 3;
        }
        workp += bwidth;
        blurp += bwidth;
    }

    /* Fix up blur frame borders. */
    srcp  = src;
    blurp = mfd->blur;
    ac_memcpy(blurp, srcp, bwidth);
    ac_memcpy(blurp + (height-1)*bwidth, srcp + (height-1)*pitch, bwidth);
    for (y = 0; y < height; y++) {
        *((unsigned int *)(&blurp[0])) = *((unsigned int *)(&srcp[0]));
        *((unsigned int *)(&blurp[bwidth-4])) = *((unsigned int *)(&srcp[bwidth-4]));
        srcp  += pitch;
        blurp += bwidth;
    }

    /* Diagonal detail detection. */
    blurp  = mfd->blur;
    blurpn = blurp + bwidth;
    workp  = mfd->work;
    for (y = 0; y < height - 1; y++) {
        b1 = blurp[0];
        g1 = blurp[1];
        r1 = blurp[2];
        b3 = blurpn[0];
        g3 = blurpn[1];
        r3 = blurpn[2];
        for (x = 0; x < bwidth - 4; x+=4) {
            b2 = blurp[x+4];
            g2 = blurp[x+5];
            r2 = blurp[x+6];
            b4 = blurpn[x+4];
            g4 = blurpn[x+5];
            r4 = blurpn[x+6];
            if ((abs(b1 - b4) >= threshold) || (abs(g1 - g4) >= threshold) || (abs(r1 - r4) >= threshold) 
             || (abs(b2 - b3) >= threshold) || (abs(g2 - g3) >= threshold) || (abs(g2 - g3) >= threshold)) {
                *((unsigned int *)(&workp[x])) = 0xffffffff;
            } else {
                *((unsigned int *)(&workp[x])) = 0x0;
            }
            b1 = b2;
            b3 = b4;
            g1 = g2;
            g3 = g4;
            r1 = r2;
            r3 = r4;
        }
        workp  += bwidth;
        blurp  += bwidth;
        blurpn += bwidth;
    }

    if (mfd->highq == TC_TRUE) {
        /* Vertical detail detection. */
        for (x = 0; x < bwidth; x += 4) {
            blurp  = mfd->blur;
            blurpn = blurp + bwidth;
            workp  = mfd->work;
            b1 = blurp[x];
            g1 = blurp[x+1];
            r1 = blurp[x+2];
            for (y = 0; y < height - 1; y++) {
                b2 = blurpn[x];
                g2 = blurpn[x+1];
                r2 = blurpn[x+2];
                if (abs(b1 - b2) >= threshold || abs(g1 - g2) >= threshold || abs(r1 - r2) >= threshold) {
                    *((unsigned int *)(&workp[x])) = 0xffffffff;
                }
                b1 = b2;
                g1 = g2;
                r1 = r2;
                workp += bwidth;
                blurp += bwidth;
                blurpn += bwidth;
            }
        }

        /* Horizontal detail detection. */
        blurp = mfd->blur;
        workp = mfd->work;
        for (y = 0; y < height; y++) {
            b1 = blurp[0];
            g1 = blurp[1];
            r1 = blurp[2];
            for (x = 0; x < bwidth - 4; x += 4) {
                b2 = blurp[x+4];
                g2 = blurp[x+5];
                r2 = blurp[x+6];
                if (abs(b1 - b2) >= threshold || abs(g1 - g2) >= threshold || abs(r1 - r2) >= threshold) {
                    *((unsigned int *)(&workp[x])) = 0xffffffff;
                }
                b1 = b2;
                g1 = g2;
                r1 = r2;
            }
            workp += bwidth;
            blurp += bwidth;
        }
    }

    /* Fix up detail map borders. */
    memset(mfd->work + (height-1)*bwidth, 0, bwidth);
    workp = mfd->work;
    for (y = 0; y < height; y++) {
        *((unsigned int *)(&workp[bwidth-4])) = 0;
        workp += bwidth;
    }

    if (mfd->mask == TC_TRUE) {
        workp = mfd->work;
        dstp  = dst;
        for (y = 0; y < height; y++) {
            for (x = 0; x < bwidth; x++) {
                dstp[x] = workp[x];
            }
            workp += bwidth;
            dstp = dstp + dstpitch;
        }
        return TC_OK;
    }

    /* Fix up output frame borders. */
    srcp = src;
    dstp = dst;
    ac_memcpy(dstp, srcp, bwidth);
    ac_memcpy(dstp + (height-1)*pitch, srcp + (height-1)*pitch, bwidth);
    for (y = 0; y < height; y++) {
        *((unsigned int *)(&dstp[0])) = *((unsigned int *)(&srcp[0]));
        *((unsigned int *)(&dstp[bwidth-4])) = *((unsigned int *)(&srcp[bwidth-4]));
        srcp += pitch;
        dstp += pitch;
    }

    /* Now sharpen the edge areas and we're done! */
    srcp = src + pitch;
    dstp = dst + pitch;
    workp = mfd->work + bwidth;
    blurp = mfd->blur + bwidth;
    for (y = 1; y < height - 1; y++) {
        for (x = 4; x < bwidth - 4; x+=4) {
            int xplus1 = x + 1, xplus2 = x + 2;

            if (workp[x]) {
                b4 = (4*(int)srcp[x] - 3*blurp[x]);
                g4 = (4*(int)srcp[x+1] - 3*blurp[x+1]);
                r4 = (4*(int)srcp[x+2] - 3*blurp[x+2]);

                if (b4 < 0) b4 = 0;
                if (g4 < 0) g4 = 0;
                if (r4 < 0) r4 = 0;
                max = b4;
                if (g4 > max) max = g4;
                if (r4 > max) max = r4;
                if (max > 255) {
                    b4 = (b4 * 255) / max;
                    g4 = (g4 * 255) / max;
                    r4 = (r4 * 255) / max;
                }
                dstp[x]      = (strength * b4 + invstrength * srcp[x])      >> 8;
                dstp[xplus1] = (strength * g4 + invstrength * srcp[xplus1]) >> 8;
                dstp[xplus2] = (strength * r4 + invstrength * srcp[xplus2]) >> 8;
            } else {
                dstp[x]   = srcp[x];
                dstp[xplus1] = srcp[xplus1];
                dstp[xplus2] = srcp[xplus2];
            }
        }
        srcp  += pitch;
        dstp  += pitch;
        workp += bwidth;
        blurp += bwidth;
    }

    tcv_convert(mfd->tcvhandle, mfd->convertFrameOut, frame->video_buf,
                frame->v_width, frame->v_height, IMG_BGRA32, mfd->out_fmt);

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID msharpen_codecs_video_in[] = { 
    TC_CODEC_YUV420P, TC_CODEC_RGB24, TC_CODEC_ERROR
};
static const TCCodecID msharpen_codecs_video_out[] = { 
    TC_CODEC_YUV420P, TC_CODEC_RGB24, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(msharpen);
TC_MODULE_FILTER_FORMATS(msharpen);

TC_MODULE_INFO(msharpen);

static const TCModuleClass msharpen_class = {
    TC_MODULE_CLASS_HEAD(msharpen),

    .init         = msharpen_init,
    .fini         = msharpen_fini,
    .configure    = msharpen_configure,
    .stop         = msharpen_stop,
    .inspect      = msharpen_inspect,

    .filter_video = msharpen_filter_video,
};

TC_MODULE_ENTRY_POINT(msharpen)

/*************************************************************************/

static int msharpen_get_config(TCModuleInstance *self, char *options)
{
    MsharpenPrivateData *pd = NULL;
    char buf[TC_BUF_MIN];

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = self->userdata;

    /* use optstr_param to do introspection */
    if (options) {
        optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYO", "1");
        tc_snprintf(buf, sizeof(buf), "%d", pd->strength);
        optstr_param(options, "strength", "How much  of the effect", "%d", buf, "0", "255");

        tc_snprintf(buf, sizeof(buf), "%d", pd->threshold);
        optstr_param(options, "threshold",
              "How close a pixel must be to the brightest or dimmest pixel to be mapped",
              "%d", buf, "0", "255");
        tc_snprintf(buf, sizeof(buf), "%d", pd->highq);
        optstr_param(options, "highq",  "Tradeoff speed for quality of detail detection",
                  "%d", buf, "0", "1");
        tc_snprintf(buf, sizeof(buf), "%d", pd->mask);
        optstr_param(options, "mask",  "Areas to be sharpened are shown in white",
                  "%d", buf, "0", "1");
    }
    return TC_OK;
}

static int msharpen_process(TCModuleInstance *self, frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    if (frame->tag & TC_POST_M_PROCESS && frame->tag & TC_VIDEO) {
        return msharpen_filter_video(self, (vframe_list_t*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE(msharpen)

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
