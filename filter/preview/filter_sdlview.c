/*
 * filter_sdlview.c -- simple preview filter based on SDL.
 * (C) 2007-2010 Francesco Romani <fromani at gmail dot com>
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


#define MOD_NAME        "filter_sdlview.so"
#define MOD_VERSION     "v1.0.3 (2009-02-07)"
#define MOD_CAP         "preview video frames using SDL"
#define MOD_AUTHOR      "Francesco Romani"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

#include "src/transcode.h"
#include "src/filter.h"
#include "aclib/ac.h"
#include "aclib/imgconvert.h"
#include "libtc/libtc.h"
#include "libtc/tccodecs.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

#include <SDL.h>


/*************************************************************************/

typedef struct sdlprivatedata_ SDLPrivateData;
struct sdlprivatedata_ {
    SDL_Surface *surface;
    SDL_Overlay *overlay;
    SDL_Rect rectangle;

    int w, h;

    ImageFormat src_fmt;
};

/*************************************************************************/

static const char sdlview_help[] = ""
    "Overview:\n"
    "    preview images to be encoded using SDL. Every internal transcode\n"
    "    colorspace is supported and dinamically translated into YV12\n"
    "    (NOT YUV420P), that is the overlay format used by SDL.\n"
    "    This plugin is intentionally extremely simple: it does preview\n"
    "    only, and does not support screenshotting, key commands not any\n"
    "    other feature of pv and preview plugins.\n"
    "Options:\n"
    "    help    produces this message\n";


/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * sdlview_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int sdlview_init(TCModuleInstance *self, uint32_t features)
{
    int err = 0;
    SDLPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    err = SDL_Init(SDL_INIT_VIDEO);
    if (err) {
        tc_log_error(MOD_NAME, "SDL initialization failed: %s", SDL_GetError());
        return TC_ERROR;
    }

    pd = tc_malloc(sizeof(SDLPrivateData));
    if (pd == NULL) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }

    pd->surface = NULL;
    pd->overlay = NULL;

    pd->w = 0;
    pd->h = 0;

    self->userdata = pd;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * sdlview_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int sdlview_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    SDL_Quit();

    tc_free(self->userdata);
    self->userdata = NULL;
    return TC_OK;
}

/*************************************************************************/

/*
 * configure_colorspace: find correct source colorspace, and setup
 * module private data accordingly
 *
 * Parameters:
 *        pd: module private data to setup
 *       fmt: internal transcode colorspace format
 *   verbose: if nonzero, tc_log out operation.
 * Return Value:
 *     TC_ERROR on failure (unknown internal format), TC_OK on success
 */
static int configure_colorspace(SDLPrivateData *pd, int fmt, int verbose)
{
    const char *fmt_name = "unknown";

    switch (fmt) {
      case TC_CODEC_YUV420P:
        pd->src_fmt = IMG_YUV420P;
        fmt_name = "YUV420";
        break;
      case TC_CODEC_YUV422P:
        pd->src_fmt = IMG_YUV422P;
        fmt_name = "YUV422";
        break;
      case TC_CODEC_RGB24:
        pd->src_fmt = IMG_RGB24;
        fmt_name = "RGB24";
        break;
      default:
        tc_log_error(MOD_NAME, "unknown colorspace");
        return TC_ERROR;
    }
    if (verbose) {
        tc_log_info(MOD_NAME, "colorspace conversion: %s -> YV12",
                    fmt_name);
    }
    return TC_OK;
}

/**
 * sdlview_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */
static int sdlview_configure(TCModuleInstance *self,
                             const char *options,
                             TCJob *vob,
                             TCModuleExtraData *xdata[])
{
    SDLPrivateData *pd = NULL;
    int ret;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;


    ret = configure_colorspace(pd, vob->im_v_codec, verbose);
    if (ret != TC_OK) {
        /* failure reason already tc_log()'d out */
        return ret;
    }

    pd->w = vob->ex_v_width;
    pd->h = vob->ex_v_height;

    SDL_WM_SetCaption("transcode SDL preview", NULL);
    pd->surface = SDL_SetVideoMode(pd->w, pd->h, 0, SDL_HWSURFACE);
    if (!pd->surface) {
        tc_log_error(MOD_NAME, "cannot setup SDL Video Mode: %s",
                     SDL_GetError());
        return TC_ERROR;
    }

    pd->overlay = SDL_CreateYUVOverlay(pd->w, pd->h,
                                       SDL_YV12_OVERLAY, pd->surface);
    if (!pd->overlay) {
        tc_log_error(MOD_NAME, "cannot setup SDL YUV overlay: %s",
                     SDL_GetError());
        return TC_ERROR;
    }

    if (verbose) {
        tc_log_info(MOD_NAME, "preview window: %ix%i YV12 overlay",
                    pd->w, pd->h);
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * sdlview_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int sdlview_stop(TCModuleInstance *self)
{
    SDLPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;
    
    SDL_FreeYUVOverlay(pd->overlay);
    /* 
     * surface obtained by SetVideoMode will be automagically
     * released by SDL_Quit
     */
    
    return TC_OK;
}

/*************************************************************************/

/**
 * sdlview_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int sdlview_inspect(TCModuleInstance *self,
                           const char *param, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    if (optstr_lookup(param, "help")) {
        *value = sdlview_help;
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * sdlview_filter_video:  display the video frame on preview window.
 * See tcmodule-data.h for function details.
 */

static int sdlview_filter_video(TCModuleInstance *self, vframe_list_t *frame)
{
    int ret = 0;
    uint8_t *src_planes[3] = { NULL, NULL, NULL };
    uint8_t *dst_planes[3] = { NULL, NULL, NULL };
    SDLPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "filter_video");
    TC_MODULE_SELF_CHECK(frame, "filter_video");

    pd = self->userdata;

    SDL_LockYUVOverlay(pd->overlay);

    YUV_INIT_PLANES(src_planes, frame->video_buf,
                    pd->src_fmt, pd->w, pd->h);
    dst_planes[0] = pd->overlay->pixels[0];
    dst_planes[1] = pd->overlay->pixels[1];
    dst_planes[2] = pd->overlay->pixels[2];

    ret = ac_imgconvert(src_planes, pd->src_fmt, dst_planes, IMG_YV12,
                        pd->w, pd->h);

    SDL_UnlockYUVOverlay(pd->overlay);

    pd->rectangle.x = 0;
    pd->rectangle.y = 0;
    pd->rectangle.w = pd->w;
    pd->rectangle.h = pd->h;

    SDL_DisplayYUVOverlay(pd->overlay, &(pd->rectangle)); 
    /* this one can fail? */

    return TC_OK;
}

/*************************************************************************/

/*************************************************************************/

static const TCCodecID sdlview_codecs_video_in[] = { 
    TC_CODEC_YUV420P, TC_CODEC_YUV422P, TC_CODEC_RGB24, TC_CODEC_ERROR 
};
static const TCCodecID sdlview_codecs_video_out[] = { 
    TC_CODEC_YUV420P, TC_CODEC_YUV422P, TC_CODEC_RGB24, TC_CODEC_ERROR 
};
TC_MODULE_AUDIO_UNSUPPORTED(sdlview);
TC_MODULE_FILTER_FORMATS(sdlview);

TC_MODULE_INFO(sdlview);

static const TCModuleClass sdlview_class = {
    TC_MODULE_CLASS_HEAD(sdlview),

    .init         = sdlview_init,
    .fini         = sdlview_fini,
    .configure    = sdlview_configure,
    .stop         = sdlview_stop,
    .inspect      = sdlview_inspect,

    .filter_video = sdlview_filter_video,
};

TC_MODULE_ENTRY_POINT(sdlview)

/*************************************************************************/

static int sdlview_get_config(TCModuleInstance *self, char *options)
{
    TC_MODULE_SELF_CHECK(self, "get_config");

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       MOD_AUTHOR, "VRY4", "1");

    return TC_OK;
}

static int sdlview_process(TCModuleInstance *self, frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    if (frame->tag & TC_PREVIEW && frame->tag & TC_VIDEO) {
        return sdlview_filter_video(self, (vframe_list_t *)frame);
    }
    return TC_OK;
}

/*************************************************************************/

TC_FILTER_OLDINTERFACE(sdlview)

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
