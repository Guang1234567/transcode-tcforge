/*
 *  filter_compare
 *
 *  Copyright (C) Antonio Beamud Montero <antonio.beamud@linkend.com>
 *  Copyright (C) Microgenesis S.A.
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
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/tclist.h"
#include "libtcutil/optstr.h"
#include "libtcext/tc_magick.h"
#include "libtcmodule/tcmodule-plugin.h"

#include <math.h>
#include <stdint.h>

/* TODO:
   - docs
   - REtesting
 */

#define MOD_NAME    "filter_compare.so"
#define MOD_VERSION "v0.2.0 (2009-03-06)"
#define MOD_CAP     "compare with other image to find a pattern"
#define MOD_AUTHOR  "Antonio Beamud"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE


#define DELTA_COLOR            45.0
#define DEFAULT_COMPARE_IMG    "compare.png"
#define DEFAULT_RESULTS_LOG    "compare.log"


typedef struct pixelsmask_ PixelsMask;
struct pixelsmask_ {
    unsigned int    row;
    unsigned int    col;

    uint8_t         r;
    uint8_t         g;
    uint8_t         b;
};

typedef struct comparedata_ ComparePrivateData;
struct comparedata_ {
    TCMagickContext magick;
    FILE            *results;

    float           delta;
    int             step;

    TCList          pixel_mask;
    int             pixel_count;

    vob_t           *vob;

    unsigned int    frames;

    int             width;
    int             height;
    int             size;

    int             flip;
    int             rgbswap;
    char            conf_str[TC_BUF_MIN];

    /* the following are used only during setup routines.
     * before and after thet, they point to NULL.
     */
    char            *pattern_name;
    char            *results_name;
};


static const char compare_help[] = ""
    "* Overview\n"
    "    Generate a file in with information about the times, \n"
    "    frame, etc the pattern defined in the image \n"
    "    parameter is observed.\n"
    "* Options\n"
    "    'pattern' path to the file used like pattern\n"
    "    'results' path to the file used to write the results\n"
    "    'delta'   delta error allowed\n"
    "    'rgbswap' enable G/B color swapping\n"
    "    'flip'    flip the pattern image\n";


/*************************************************************************/
/* Helpers                                                               */
/*************************************************************************/

static void compare_defaults(ComparePrivateData *pd, vob_t *vob)
{
    pd->vob          = vob;
    pd->width        = vob->ex_v_width;
    pd->height       = vob->ex_v_height;
    pd->rgbswap      = vob->rgbswap;
    pd->flip         = TC_TRUE;
    pd->delta        = DELTA_COLOR;
    pd->step         = 1;
    pd->frames       = 0;
    pd->results      = NULL;
    pd->pattern_name = NULL; /* see note above */
    pd->results_name = NULL; /* see note above */
}

static int compare_parse_options(ComparePrivateData *pd,
                                 const char *options)
{
    int ret = TC_OK;

    if (optstr_get(options, "pattern", "%[^:]", pd->pattern_name) != 1) {
        strlcpy(pd->pattern_name, DEFAULT_COMPARE_IMG, PATH_MAX);
    }
    if (optstr_get(options, "results", "%[^:]", pd->results_name) != 1) {
        strlcpy(pd->results_name, DEFAULT_RESULTS_LOG, PATH_MAX);
    }

    optstr_get(options, "delta",   "%f",    &pd->delta);
    optstr_get(options, "rgbswap", "%i",    &pd->rgbswap);
    optstr_get(options, "flip",    "%i",    &pd->flip);

    if (verbose) {
        tc_log_info(MOD_NAME, "Compare Image Settings:");
        tc_log_info(MOD_NAME, "      pattern = %s", pd->pattern_name);
        tc_log_info(MOD_NAME, "      results = %s", pd->results_name);
        tc_log_info(MOD_NAME, "        delta = %f", pd->delta);
        tc_log_info(MOD_NAME, "      rgbswap = %i", pd->rgbswap);
        tc_log_info(MOD_NAME, "         flip = %i", pd->flip);
    }

    return ret;
}

static int compare_open_log(ComparePrivateData *pd)
{
    int ret = TC_OK;
    pd->results = fopen(pd->results_name, "w");
    if (pd->results) {
        fprintf(pd->results, "#fps:%f\n", pd->vob->fps);
    } else {
        tc_log_error(MOD_NAME, "could not open file for writing");
        ret =  TC_ERROR;
    }
    return ret;
}

#define RETURN_IF_GM_ERROR(PTR, PD) do { \
    if (!(PTR)) { \
        CatchException(&(PD)->magick.exception_info); \
        return TC_ERROR; \
    } \
} while (0)
    

static int compare_setup_pattern(ComparePrivateData *pd)
{
    int r = 0, t = 0, j = 0;
	Image *pattern = NULL, *resized = NULL;
    PixelPacket *pixels = NULL;

    /* FIXME: filter used */
    /* FIXME: switch to tcvideo? */
    resized = ResizeImage(pd->magick.image,
                          pd->width, pd->height,
                          GaussianFilter,  1.0,
                          &pd->magick.exception_info);
    RETURN_IF_GM_ERROR(resized, pd);

    if (pd->flip) {
        pattern = FlipImage(resized, &pd->magick.exception_info);
    } else {
        pattern = resized;
    }
    RETURN_IF_GM_ERROR(pattern, pd);

    pixels = GetImagePixels(pattern, 0, 0,
                            pattern->columns,
                            pattern->rows);

    for (t = 0; t < pattern->rows; t++) {
        for (r = 0; r < pattern->columns; r++) {
            j = t * pattern->columns + r;
            if (pixels[j].opacity == 0) {
                PixelsMask pm = {
                    .row = t,
                    .col = r,
                    .r   = (uint8_t)ScaleQuantumToChar(pixels[j].red),
                    .g   = (uint8_t)ScaleQuantumToChar(pixels[j].green),
                    .b   = (uint8_t)ScaleQuantumToChar(pixels[j].blue),
                };
                tc_list_append_dup(&pd->pixel_mask, &pm, sizeof(pm));
                /* FIXME: return value */
            }
        }
    }

    pd->pixel_count = tc_list_size(&pd->pixel_mask);
    return TC_OK;
}

/*************************************************************************/
/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * compare_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(compare, ComparePrivateData)

/*************************************************************************/

/**
 * compare_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(compare)

/*************************************************************************/

#define RETURN_IF_NOT_OK(PD, RET) do { \
    if ((RET) != TC_OK) { \
        /* avoid dangling pointers */ \
        (PD)->pattern_name = NULL; \
        (PD)->results_name = NULL; \
        \
        return (RET); \
    } \
} while (0)

/**
 * compare_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */
static int compare_configure(TCModuleInstance *self,
                             const char *options,
                             vob_t *vob,
                             TCModuleExtraData *xdata[])
{
    ComparePrivateData *pd = NULL;
    char pattern_name[PATH_MAX + 1] = { '\0' };
    char results_name[PATH_MAX + 1] = { '\0' };
    int ret = TC_OK;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    tc_list_init(&pd->pixel_mask, TC_FALSE);

    /* careful here, see note above */
    compare_defaults(pd, vob);

    pd->pattern_name = pattern_name;
    pd->results_name = results_name;

    ret = compare_parse_options(pd, options);
    RETURN_IF_NOT_OK(pd, ret);

    ret = tc_magick_init(&pd->magick, TC_MAGICK_QUALITY_DEFAULT);
    RETURN_IF_NOT_OK(pd, ret);

    ret = tc_magick_filein(&pd->magick, pd->pattern_name);
    RETURN_IF_NOT_OK(pd, ret);

    ret = compare_open_log(pd);
    RETURN_IF_NOT_OK(pd, ret);

    ret = compare_setup_pattern(pd);
    RETURN_IF_NOT_OK(pd, ret);

    /* no longer needed (see note above) */
    pd->pattern_name = NULL;
    pd->results_name = NULL;
    return TC_OK;
}


/*************************************************************************/

/**
 * compare_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int compare_stop(TCModuleInstance *self)
{
    ComparePrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    tc_list_fini_cleanup(&pd->pixel_mask);
    /* FIXME: free images */

    if (pd->results) {
        fclose(pd->results);
        pd->results = NULL;
    }

    tc_magick_fini(&pd->magick);

    return TC_OK;
}

/*************************************************************************/

/**
 * compare_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int compare_inspect(TCModuleInstance *self,
                           const char *param, const char **value)
{
    ComparePrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self,  "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    
    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = compare_help;
    }
    if (optstr_lookup(param, "delta")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str), "%f", pd->delta);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "rgbswap")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str), "%i", pd->rgbswap);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "flip")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str), "%i", pd->flip);
        *value = pd->conf_str;
    }
    /* see note above for pattern_name and results_name */
    return TC_OK;
}

/*************************************************************************/

typedef struct workitem_ WorkItem;
struct workitem_ {
    const uint8_t   *buf;
    int             stride;
    double          sr;
    double          sg;
    double          sb;
};


static int image_compare(TCListItem *item, void *userdata)
{
    PixelsMask *pix = item->data;
    WorkItem  *wi  = userdata;

    int r   = pix->row * wi->stride + pix->col * 3;
    int g   = pix->row * wi->stride + pix->col * 3 + 1;
    int b   = pix->row * wi->stride + pix->col * 3 + 2;

    wi->sr += (double)abs(wi->buf[r] - pix->r);
    wi->sg += (double)abs(wi->buf[g] - pix->g);
    wi->sb += (double)abs(wi->buf[b] - pix->b);

    return 0;
}

/**
 * compare_filter_video:  perform the image comparation for each frame of
 * this video stream. See tcmodule-data.h for function details.
 */

static int compare_filter_video(TCModuleInstance *self,
                                TCFrameVideo *frame)
{
    ComparePrivateData *pd = NULL;
    double avg_dr = 0.0, avg_dg = 0.0, avg_db = 0.0;
    WorkItem W;

    TC_MODULE_SELF_CHECK(self,  "filter");
    TC_MODULE_SELF_CHECK(frame, "filter");

    pd = self->userdata;

    W.stride = pd->width * 3;
    W.sr     = 0.0;
    W.sg     = 0.0;
    W.sb     = 0.0;
    W.buf    = frame->video_buf;

    tc_list_foreach(&pd->pixel_mask, image_compare, &W);

    avg_dr = W.sr / pd->pixel_count;
    avg_dg = W.sg / pd->pixel_count;
    avg_db = W.sb / pd->pixel_count;

    if ((avg_dr < pd->delta) && (avg_dg < pd->delta) && (avg_db < pd->delta))
        fprintf(pd->results,"1");
    else
        fprintf(pd->results,"n");

    fflush(pd->results); /* FIXME */
    pd->frames++;

    return TC_OK;
}


/*************************************************************************/

static const TCCodecID compare_codecs_video_in[] = { 
    TC_CODEC_RGB24, TC_CODEC_ERROR
};
static const TCCodecID compare_codecs_video_out[] = { 
    TC_CODEC_RGB24, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(compare);
TC_MODULE_FILTER_FORMATS(compare);

TC_MODULE_INFO(compare);

static const TCModuleClass compare_class = {
    TC_MODULE_CLASS_HEAD(compare),

    .init         = compare_init,
    .fini         = compare_fini,
    .configure    = compare_configure,
    .stop         = compare_stop,
    .inspect      = compare_inspect,

    .filter_video = compare_filter_video,
};

TC_MODULE_ENTRY_POINT(compare)

/*************************************************************************/

static int compare_get_config(TCModuleInstance *self, char *options)
{
    ComparePrivateData *pd = NULL;
    char buf[TC_BUF_MIN];

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = self->userdata;

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       MOD_AUTHOR, "VRMO", "1");

    tc_snprintf(buf, TC_BUF_MIN, DEFAULT_COMPARE_IMG);
    optstr_param(options, "pattern", "Pattern image file path", "%s", buf);
    tc_snprintf(buf, TC_BUF_MIN, DEFAULT_RESULTS_LOG);
    optstr_param(options, "results", "Results file path" , "%s", buf);
    tc_snprintf(buf, TC_BUF_MIN, "%f", pd->delta);
    optstr_param(options, "delta", "Delta error", "%f",buf,"0.0", "100.0");
    tc_snprintf(buf, TC_BUF_MIN, "%i", pd->rgbswap);
    optstr_param(options, "rgbswap", "RGB swapping", "%i",buf,"0", "1");

    return TC_OK;
}

static int compare_process(TCModuleInstance *self, TCFrame *frame)
{
    ComparePrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "process");

    pd = self->userdata;

    if ((frame->tag & TC_POST_M_PROCESS) && (frame->tag & TC_VIDEO)) {
        return compare_filter_video(self, (TCFrameVideo*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE_M(compare)

/*************************************************************************/

// Proposal:
// Tilmann Bitterberg Sat, 14 Jun 2003 00:29:06 +0200 (CEST)
//
//    char *Y, *Cb, *Cr;
//    Y  = p->video_buf;
//    Cb = p->video_buf + mydata->width*mydata->height;
//    Cr = p->video_buf + 5*mydata->width*mydata->height/4;

//    for (i=0; i<mydata->width*mydata->height; i++) {
//      pixels->red == *Y++;
//      get_next_pixel();
//    }

//    for (i=0; i<mydata->width*mydata->height>>2; i++) {
//      pixels->green == *Cr++;
//      pixels->blue == *Cb++;
//      get_next_pixel();
//    }

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */

