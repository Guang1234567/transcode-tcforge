/*
 *  Copyright (C) 2004 Bryan Mayland <bmayland@leoninedev.com>
 *  For use in transcode by Tilmann Bitterberg <transcode@tibit.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define MOD_NAME    "filter_levels.so"
#define MOD_VERSION "v1.2.1 (2009-02-07)"
#define MOD_CAP     "Luminosity level scaler"
#define MOD_AUTHOR  "Bryan Mayland"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

#include <stdint.h>
#include <math.h>


/*************************************************************************/

static const char levels_help[] = ""
    "Overview:\n"
    "    Scales luminosity values in the source image, similar to\n"
    "    VirtualDub's 'levels' filter.  This is useful to scale ITU-R601\n"
    "    video (which limits luma to 16-235) back to the full 0-255 range.\n"
    "Options:\n"
    "    input   luma range of input (0-255)\n"
    "    gamma   gamma ramp to apply to input luma (F)\n"
    "    output  luma range of output (0-255)\n"
    "    pre     act as pre processing filter (I)\n"
    "    help    print this help message\n";


#define DEFAULT_IN_GAMMA   1.0
#define DEFAULT_IN_BLACK   0
#define DEFAULT_IN_WHITE   255
#define DEFAULT_OUT_BLACK  0
#define DEFAULT_OUT_WHITE  255

#define MAP_SIZE           256

typedef struct levelsprivatedata_ LevelsPrivateData;
struct levelsprivatedata_ {
    int in_black;
    int in_white;
    float in_gamma;

    int out_black;
    int out_white;

    uint8_t lumamap[MAP_SIZE];
    int is_prefilter;

    char conf_str[TC_BUF_MIN];
};


/*************************************************************************/

static void build_map(uint8_t *map, int inlow, int inhigh,
                      float ingamma, int outlow, int outhigh)
{
    int i;

    for (i = 0; i < MAP_SIZE; i++) {
        if (i <= inlow) {
            map[i] = outlow;
        } else if (i >= inhigh) {
            map[i] = outhigh;
        } else {
            float f = (float)(i - inlow) / (inhigh - inlow);
            map[i] = pow(f, 1/ingamma) * (outhigh - outlow) + outlow; // XXX
        }
    }
}

/*************************************************************************/

/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * levels_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(levels, LevelsPrivateData)

/*************************************************************************/

/**
 * levels_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(levels)

/*************************************************************************/

/**
 * levels_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int levels_configure(TCModuleInstance *self,
                            const char *options,
                            TCJob *vob,
                            TCModuleExtraData *xdata[])
{
    LevelsPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    if (vob->im_v_codec != TC_CODEC_YUV420P) {
        tc_log_error(MOD_NAME, "This filter is only capable of YUV mode");
        return TC_ERROR;
    }

    /* enforce defaults */
    pd->in_black     = DEFAULT_IN_BLACK;
    pd->in_white     = DEFAULT_IN_WHITE;
    pd->in_gamma     = DEFAULT_IN_GAMMA;
    pd->out_black    = DEFAULT_OUT_BLACK;
    pd->out_white    = DEFAULT_OUT_WHITE;
    pd->is_prefilter = TC_FALSE;

    if (options) {
        optstr_get(options, "input",  "%d-%d", &pd->in_black, &pd->in_white);
        optstr_get(options, "gamma",  "%f",    &pd->in_gamma);
        optstr_get(options, "output", "%d-%d", &pd->out_black, &pd->out_white);
        optstr_get(options, "pre",    "%d",    &pd->is_prefilter);
    }

    build_map(pd->lumamap, pd->in_black, pd->in_white,
              pd->in_gamma, pd->out_black, pd->out_white);

    if (verbose) {
        tc_log_info(MOD_NAME, "scaling %d-%d gamma %f to %d-%d (%s-process)",
                    pd->in_black, pd->in_white,
                    pd->in_gamma,
                    pd->out_black, pd->out_white,
                    (pd->is_prefilter) ?"pre" :"post");
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * levels_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int levels_stop(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "stop");

    /* nothing to do in here */

    return TC_OK;
}

/*************************************************************************/

/**
 * levels_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int levels_inspect(TCModuleInstance *self,
                          const char *param, const char **value)
{
    LevelsPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self,  "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    
    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = levels_help;
    }

    if (optstr_lookup(param, "pre")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%i", pd->is_prefilter);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "gamma")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%.3f", pd->in_gamma);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "input")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%i-%i",
                    pd->in_black, pd->in_white);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "output")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%i-%i",
                    pd->out_black, pd->out_white);
        *value = pd->conf_str;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * levels_filter_video:  perform the Y-plane rescaling for each frame of
 * this video stream. See tcmodule-data.h for function details.
 */

static int levels_filter_video(TCModuleInstance *self,
                               vframe_list_t *frame)
{
    LevelsPrivateData *pd = NULL;
    int y_size = 0, i = 0;

    TC_MODULE_SELF_CHECK(self,  "filter");
    TC_MODULE_SELF_CHECK(frame, "filter");

    pd = self->userdata;

    y_size = frame->v_width * frame->v_height;

    for (i = 0; i < y_size; i++) {
        frame->video_buf[i] = pd->lumamap[frame->video_buf[i]];
    }

    return TC_OK;
}


/*************************************************************************/

static const TCCodecID levels_codecs_video_in[] = { 
    TC_CODEC_YUV420P, TC_CODEC_ERROR
};
static const TCCodecID levels_codecs_video_out[] = { 
    TC_CODEC_YUV420P, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(levels);
TC_MODULE_FILTER_FORMATS(levels);

TC_MODULE_INFO(levels);

static const TCModuleClass levels_class = {
    TC_MODULE_CLASS_HEAD(levels),

    .init         = levels_init,
    .fini         = levels_fini,
    .configure    = levels_configure,
    .stop         = levels_stop,
    .inspect      = levels_inspect,

    .filter_video = levels_filter_video,
};

TC_MODULE_ENTRY_POINT(levels)

/*************************************************************************/

static int levels_get_config(TCModuleInstance *self, char *options)
{
    LevelsPrivateData *pd = NULL;
    char buf[TC_BUF_MIN];

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = self->userdata;

    /* use optstr_param to do introspection */
    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       MOD_AUTHOR, "VYMEO", "1");

    tc_snprintf(buf, sizeof(buf), "%d-%d", DEFAULT_IN_BLACK,
                DEFAULT_IN_WHITE );
    optstr_param(options, "input", "input luma range (black-white)",
                 "%d-%d", buf, "0", "255", "0", "255" );

    tc_snprintf(buf, sizeof(buf), "%f", DEFAULT_IN_GAMMA );
    optstr_param(options, "gamma", "input luma gamma",
                 "%f", buf, "0.5", "3.5" );

    tc_snprintf(buf, sizeof(buf), "%d-%d",
                DEFAULT_OUT_BLACK, DEFAULT_OUT_WHITE );
    optstr_param(options, "output", "output luma range (black-white)",
                 "%d-%d", buf, "0", "255", "0", "255" );

    tc_snprintf(buf, sizeof(buf), "%i", TC_FALSE);
    optstr_param(options, "pre", "pre processing filter",
                 "%i", buf, "0", "1" );
    
    return TC_OK;
}

static int levels_process(TCModuleInstance *self, frame_list_t *frame)
{
    LevelsPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "process");

    pd = self->userdata;

    if ((frame->tag & TC_VIDEO) && !(frame->attributes & TC_FRAME_IS_SKIPPED)
       && (((frame->tag & TC_POST_M_PROCESS) && !pd->is_prefilter)
         || ((frame->tag & TC_PRE_M_PROCESS) && pd->is_prefilter))) {
        return levels_filter_video(self, (vframe_list_t*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE_M(levels)

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

