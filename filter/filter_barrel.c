/*
 *  filter_barrel.c -- barrel filter: applies or removes barrel distortion
 *
 *  Copyright (C) 2009-2010 Andrew Church; based on filter_invert.c, with
 *      concept taken from GIMP lens distortion filter
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

#define MOD_NAME      "filter_barrel.so"
#define MOD_VERSION   "v0.1.0 (2009-07-30)"
#define MOD_CAP       "apply/remove barrel distortion"
#define MOD_AUTHOR    "Andrew Church"
#define MOD_CAPSTRING "VYE"
#define MOD_MINFRAMES "1"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

/*************************************************************************/

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

#include <math.h>

/*----------------------------------*/

static const char barrel_help[] =
    "Overview\n"
    "    Apply or remove barrel distortion, such as that produced by a\n"
    "    wide-angle camera lens.  Positive values for \"order2\" or \"order4\"\n"
    "    apply barrel distortion, while negative values remove barrel\n"
    "    distortion (or, conversely, apply pincushion distortion).\n"
    "    Filter is applied before basic transformations (zoom, etc).\n"
    "Options\n"
    "    order2=strength        Strength of order-2 distortion [0]\n"
    "    order4=strength        Strength of order-4 distortion [0]\n"
    "    center=x/y             Center of distortion [center of frame]\n"
    "    range=start-end/step   Apply filter only to given frames [0-oo/1]\n"
    ;

/*----------------------------------*/

/* Internal module data */

typedef struct DistortionMapEntry {
    int16_t x, y;
    uint16_t weight[3][3];  // Weight of [y-1..y+1][x-1..x+1] out of 0x8000
} DistortionMapEntry;

typedef struct BarrelPrivateData {
    double order2, order4;          // Order-2 and order-4 filter strength
    int cx, cy;                     // Center of distortion
    unsigned int start, end, step;  // Filter application range and step

    char opt_buf[TC_BUF_MIN];       // Return buffer for inspect() method

    int width, height;              // Frame width/height (for reference)
    uint8_t *buf_y, *buf_u, *buf_v; // Temporary frame buffer for each plane
    DistortionMapEntry *map_y;      // Distortion map (Y plane)
    DistortionMapEntry *map_uv;     // Distortion map (U/V planes)
} BarrelPrivateData;

/*************************************************************************/
/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * barrel_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int barrel_init(TCModuleInstance *self, uint32_t features)
{
    BarrelPrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    pd = tc_malloc(sizeof(*pd));
    if (pd == NULL) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }
    pd->map_y = NULL;
    pd->map_uv = NULL;

    self->userdata = pd;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * barrel_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int barrel_fini(TCModuleInstance *self)
{
    BarrelPrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "fini");

    pd = (BarrelPrivateData *)self->userdata;
    if (pd) {
        tc_free(pd->buf_y);
        tc_free(pd->buf_u);
        tc_free(pd->buf_v);
        tc_free(pd->map_y);
        tc_free(pd->map_uv);
    }
    tc_free(self->userdata);
    self->userdata = NULL;
    return TC_OK;
}

/*************************************************************************/

/**
 * barrel_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static DistortionMapEntry *gen_distortion_map(int width, int height,
                                              double cx, double cy,
                                              double order2, double order4);

static int barrel_configure(TCModuleInstance *self, const char *options,
                            TCJob *vob, TCModuleExtraData *xdata[])
{
    BarrelPrivateData *pd;

    TC_MODULE_SELF_CHECK(vob, "configure");
    TC_MODULE_SELF_CHECK(self, "configure");

    pd = (BarrelPrivateData *)self->userdata;

    /* Copy frame size into convenience fields */
    // FIXME: this breaks if any preceding filters change the frame size!
    //        --> need to pass "current" width/height around
    pd->width = vob->im_v_width;
    pd->height = vob->im_v_height;

    /* Set defaults */
    pd->order2 = 0;
    pd->order4 = 0;
    pd->cx = pd->width / 2;
    pd->cy = pd->height / 2;
    pd->start = 0;
    pd->end = (unsigned int)-1;
    pd->step = 1;

    /* Read options */
    if (options != NULL) {
        if(verbose >= TC_STATS) {
            tc_log_info(MOD_NAME, "options=%s", options);
        }
        optstr_get(options, "order2", "%lf", &pd->order2);
        optstr_get(options, "order4", "%lf", &pd->order4);
        optstr_get(options, "center", "%d/%d", &pd->cx, &pd->cy);
        optstr_get(options, "range", "%u-%u/%d",
                   &pd->start, &pd->end, &pd->step);
    }
    if (verbose > TC_INFO) {
        tc_log_info(MOD_NAME, "Barrel distortion settings:");
        tc_log_info(MOD_NAME, "    order2 = %f", pd->order2);
        tc_log_info(MOD_NAME, "    order4 = %f", pd->order4);
        tc_log_info(MOD_NAME, "    center = %d/%d", pd->cx, pd->cy);
        tc_log_info(MOD_NAME, "     range = %u-%u/%u",
                    pd->start, pd->end, pd->step);
    }

    /* Allocate temporary frame buffers */
    tc_free(pd->buf_y);
    pd->buf_y = tc_malloc(pd->width * pd->height);
    tc_free(pd->buf_u);
    pd->buf_u = tc_malloc((pd->width/2) * (pd->height/2));
    tc_free(pd->buf_v);
    pd->buf_v = tc_malloc((pd->width/2) * (pd->height/2));
    if (!pd->buf_y || !pd->buf_u || !pd->buf_v) {
        tc_log_error(MOD_NAME, "Not enough memory for %dx%d frame buffer",
                     pd->width, pd->height);
        return TC_ERROR;
    }

    /* Compute distortion map based on settings */
    tc_free(pd->map_y);
    pd->map_y = gen_distortion_map(pd->width, pd->height, pd->cx, pd->cy,
                                   pd->order2, pd->order4);
    tc_free(pd->map_uv);
    pd->map_uv = gen_distortion_map(pd->width/2, pd->height/2,
                                    (double)pd->cx/2, (double)pd->cy/2,
                                    pd->order2, pd->order4);
    if (!pd->map_y || !pd->map_uv) {
        tc_log_error(MOD_NAME, "Not enough memory for %dx%d distortion map",
                     pd->width, pd->height);
        return TC_ERROR;
    }

    return TC_OK;
}

/*-----------------------------------------------------------------------*/

/**
 * gen_distortion_map:  Generates a distortion map for the given filter
 * parameters.
 *
 * Parameters:
 *      width: Frame width, in pixels.
 *     height: Frame height, in pixels.
 *     cx, cy: Pixel coordinates of center of distortion effect.
 *     order2: Strength of second-order distortion.
 *     order4: Strength of fourth-order distortion.
 * Return value:
 *     Newly-allocated distortion map (NULL on error).
 */

static DistortionMapEntry *gen_distortion_map(int width, int height,
                                              double cx, double cy,
                                              double order2, double order4)
{
    DistortionMapEntry *map;
    const double r_scale_sq = 4.0 / (width*width + height*height);
    int x, y, index;

    map = tc_malloc(sizeof(*map) * (width * height));
    if (!map) {
        return NULL;
    }

    for (index = 0, y = 0; y < height; y++) {
        for (x = 0; x < width; x++, index++) {
            const double dx = (x+0.5) - cx, dy = (y+0.5) - cy;
            const double r_sq = (dx*dx + dy*dy) * r_scale_sq;
            const double mult = 1 + (order2 * r_sq)
                                  + (order4 * r_sq * r_sq);
            const double srcx = cx + (mult * dx);
            const double srcy = cy + (mult * dy);
            map[index].x = (int16_t)floor(srcx);
            map[index].y = (int16_t)floor(srcy);
            /* Compute raw weights */
            double weight[3][3], total_weight = 0;
            int xx, yy;
            for (yy = -1; yy <= 1; yy++) {
                const double weight_dy = (map[index].y + yy + 0.5) - srcy;
                for (xx = -1; xx <= 1; xx++) {
                    const double weight_dx = (map[index].x + xx + 0.5) - srcx;
                    const double dist =
                        sqrt(weight_dx*weight_dx + weight_dy*weight_dy);
                    /* Simple cubic (FIXME: just a quick hack; better ideas?)*/
                    weight[yy+1][xx+1] = dist>=1 ? 0 :
                        (3.0 + (dist * dist * (-7.0 + (dist * 4.0)))) / 3.0;
                    total_weight += weight[yy+1][xx+1];
                }
            }
            /* Rescale to a total of 0x8000 */
            uint16_t int_total = 0;
            for (yy = -1; yy <= 1; yy++) {
                for (xx = -1; xx <= 1; xx++) {
                    map[index].weight[yy+1][xx+1] = (uint16_t)floor(
                        ((weight[yy+1][xx+1] / total_weight) * 0x8000) + 0.5
                    );
                    int_total += map[index].weight[yy+1][xx+1];
                }
            }
            if (int_total != 0x8000) {  // Tweak for rounding error
                map[index].weight[1][1] += 0x8000 - (int)int_total;
            }
        }
    }

    return map;
}

/*************************************************************************/

/**
 * barrel_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int barrel_stop(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "stop");
    return TC_OK;
}

/*************************************************************************/

/**
 * barrel_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int barrel_inspect(TCModuleInstance *self,
                          const char *param, const char **value)
{
    BarrelPrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    pd = (BarrelPrivateData *)self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = barrel_help; 
    }
    if (optstr_lookup(param, "order2")) {
        tc_snprintf(pd->opt_buf, sizeof(pd->opt_buf), "%f", pd->order2);
        *value = pd->opt_buf; 
    }
    if (optstr_lookup(param, "order4")) {
        tc_snprintf(pd->opt_buf, sizeof(pd->opt_buf), "%f", pd->order4);
        *value = pd->opt_buf; 
    }
    if (optstr_lookup(param, "center")) {
        tc_snprintf(pd->opt_buf, sizeof(pd->opt_buf), "%d/%d", pd->cx, pd->cy);
        *value = pd->opt_buf; 
    }
    if (optstr_lookup(param, "range")) {
        tc_snprintf(pd->opt_buf, sizeof(pd->opt_buf), "%u-%u/%d",
                    pd->start, pd->end, pd->step);
        *value = pd->opt_buf; 
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * barrel_filter_video:  show something on given frame of the video
 * stream.  See tcmodule-data.h for function details.
 */

static void filter_plane(const uint8_t *src, uint8_t *dest,
                         const DistortionMapEntry *map, uint8_t defval,
                         int width, int height);

static int barrel_filter_video(TCModuleInstance *self, vframe_list_t *frame)
{
    BarrelPrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "filter_video");
    TC_MODULE_SELF_CHECK(frame, "filter_video");

    pd = (BarrelPrivateData *)self->userdata;

    if (!(frame->attributes & TC_FRAME_IS_SKIPPED)
     && frame->id >= pd->start && frame->id <= pd->end
     && (frame->id - pd->start) % pd->step == 0
    ) {
        /* Get size of and pointers to each plane in frame buffer */
        uint32_t size_y = pd->width * pd->height;
        uint32_t size_uv = (pd->width/2) * (pd->height/2);
        uint8_t *ptr_y = frame->video_buf;
        uint8_t *ptr_u = ptr_y + size_y;
        uint8_t *ptr_v = ptr_u + size_uv;

        /* Copy frame data to temporary buffers */
        memcpy(pd->buf_y, ptr_y, size_y);
        memcpy(pd->buf_u, ptr_u, size_uv);
        memcpy(pd->buf_v, ptr_v, size_uv);

        /* Apply filter to each plane */
        filter_plane(pd->buf_y, ptr_y, pd->map_y,   16,
                     pd->width,   pd->height);
        filter_plane(pd->buf_u, ptr_u, pd->map_uv, 128,
                     pd->width/2, pd->height/2);
        filter_plane(pd->buf_v, ptr_v, pd->map_uv, 128,
                     pd->width/2, pd->height/2);
    }

    return TC_OK;
}

/*-----------------------------------------------------------------------*/

/**
 * filter_plane:  Apply the barrel distortion filter to a single plane of a
 * video frame.
 *
 * Parameters:
 *        src: Pointer to source data.
 *       dest: Pointer to destination buffer.
 *        map: Pointer to distortion map.
 *     defval: Value for out-of-frame pixels.
 *      width: Width of plane, in pixels.
 *     height: Height of plane, in pixels.
 * Return value: None.
 */

static void filter_plane(const uint8_t *src, uint8_t *dest,
                         const DistortionMapEntry *map, uint8_t defval,
                         int width, int height)
{
    int x, y, index;

    if (!src || !dest) {
        tc_log_error(MOD_NAME, "filter_plane(): NULL pointer(s)!");
        return;
    }

    for (index = 0, y = 0; y < height; y++) {
        for (x = 0; x < width; x++, index++) {
            uint32_t pixel_total = 0;
            int xx, yy;
            for (yy = -1; yy <= 1; yy++) {
                const int srcy = map[index].y + yy;
                for (xx = -1; xx <= 1; xx++) {
                    const int srcx = map[index].x + xx;
                    uint32_t pixel;
                    if (srcx < 0 || srcx >= width
                     || srcy < 0 || srcy >= height
                    ) {
                        pixel = defval;
                    } else {
                        pixel = src[srcy * width + srcx];
                    }
                    pixel_total += pixel * map[index].weight[yy+1][xx+1];
                }
            }
            dest[index] = pixel_total >> 15;
        }
    }
}

/*************************************************************************/

static const TCCodecID barrel_codecs_video_in[] =
    { TC_CODEC_YUV420P, TC_CODEC_ERROR };
static const TCCodecID barrel_codecs_video_out[] =
    { TC_CODEC_YUV420P, TC_CODEC_ERROR };
static const TCCodecID barrel_codecs_audio_in[] = { TC_CODEC_ERROR };
static const TCCodecID barrel_codecs_audio_out[] = { TC_CODEC_ERROR };
TC_MODULE_FILTER_FORMATS(barrel);

TC_MODULE_INFO(barrel);

static const TCModuleClass barrel_class = {
    TC_MODULE_CLASS_HEAD(barrel),

    .init         = barrel_init,
    .fini         = barrel_fini,
    .configure    = barrel_configure,
    .stop         = barrel_stop,
    .inspect      = barrel_inspect,

    .filter_video = barrel_filter_video
};

TC_MODULE_ENTRY_POINT(barrel)

/*************************************************************************/

static int barrel_get_config(TCModuleInstance *self, char *options)
{
    BarrelPrivateData *pd;
    char buf[TC_BUF_MIN];

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = (BarrelPrivateData *)self->userdata;

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                                MOD_AUTHOR, MOD_CAPSTRING, MOD_MINFRAMES);

    optstr_param(options, "help", "Applies or removes barrel distortion",
                 "", "0");

    tc_snprintf(buf, sizeof(buf), "%f", pd->order2);
    optstr_param(options, "order2", "Strength of order-2 distortion",
                 "%f", buf);
    tc_snprintf(buf, sizeof(buf), "%f", pd->order4);
    optstr_param(options, "order4", "Strength of order-4 distortion",
                 "%f", buf);
    tc_snprintf(buf, sizeof(buf), "%u-%u/%d", pd->start, pd->end, pd->step);
    optstr_param(options, "range", "Apply filter only to given frames",
                 "%u-%u/%d", buf, "0", "oo", "0", "oo", "1", "oo");

    return TC_OK;
}

static int barrel_process(TCModuleInstance *self, frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    if ((frame->tag & TC_VIDEO) && (frame->tag & TC_PRE_M_PROCESS)) {
        return barrel_filter_video(self, (vframe_list_t *)frame);
    }

    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE(barrel)

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
