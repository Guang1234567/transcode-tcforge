/*
 * filter_null.c -- demo filter: does nothing, and it does that very well
 * Written     by Thomas Oestreich - June 2001
 * Updated by  by Francesco Romani <fromani at gmail dot com>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

/* public (user-visible) Name of the filter */
#define MOD_NAME    "filter_null.so"
/* version of the filter */
#define MOD_VERSION "v1.2.0 (2009-02-07)"
/* A short description */
#define MOD_CAP     "demo filter plugin; does nothing"
/* Author(s) of the filter */
#define MOD_AUTHOR  "Thomas Oestreich, Thomas Wehrspann"
/* What this plugin can do (see NMS documentation for details) */
#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO|TC_MODULE_FEATURE_AUDIO
/* How this module can work (see NMS documentation for details) */
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE


/* API reminder (mostly for OMS, but take in mind for NMS too):
 * ========================================================================
 *
 * (1) need more infos, than get pointer to transcode global
 *     information structure vob_t as defined in transcode.h.
 *
 * (2) 'tc_get_vob' and 'verbose' are exported by transcode.
 *
 * (3) filter is called first time with TC_FILTER_INIT flag set.
 *
 * (4) make sure to exit immediately if context (video/audio) or
 *     placement of call (pre/post) is not compatible with the filters
 *     intended purpose, since the filter is called 4 times per frame.
 *
 * (5) see framebuffer.h for a complete list of frame_list_t variables.
 *
 * (6) filter is last time with TC_FILTER_CLOSE flag set
 */

 
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


static const char null_help[] = ""
    "Overview:\n"
    "    This filter exists for demonstration purposes only; it doesn nothing.\n"
    "Options:\n"
    "    help    produce module overview and options explanations\n";


/*************************************************************************/

typedef struct {
    uint32_t video_frames; /* dumb frame counter */
    uint32_t audio_frames; /* dumb frame counter */
} NullPrivateData;

/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * null_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(null, NullPrivateData)

/*************************************************************************/

/**
 * null_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(null)

/*************************************************************************/

/**
 * null_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int null_configure(TCModuleInstance *self,
                          const char *options,
                          TCJob *vob,
                          TCModuleExtraData *xdata[])
{
    NullPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    /* setup defaults */
    pd->video_frames = 0;
    pd->audio_frames = 0;

    if (options) {
        if (verbose >= TC_STATS) {
            tc_log_info(MOD_NAME, "options=%s", options);
        }
        /* optstr_get() them */
    }

    /* handle other options */

    return TC_OK;
}

/*************************************************************************/

/**
 * null_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int null_stop(TCModuleInstance *self)
{
    NullPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    /* this is the right place for print out summary or collected stuff */
    tc_log_info(MOD_NAME, "elapsed frames audio/video: %u/%u",
                pd->audio_frames, pd->video_frames);

    /* reverse all stuff done in _configure */

    return TC_OK;
}

/*************************************************************************/

/**
 * null_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int null_inspect(TCModuleInstance *self,
                        const char *param, const char **value)
{
    NullPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = null_help; 
    }
    /* put back configurable options */

    return TC_OK;
}

/*************************************************************************/

/**
 * null_filter_video:  show something on given frame of the video
 * stream.  See tcmodule-data.h for function details.
 */

static int null_filter_video(TCModuleInstance *self, vframe_list_t *frame)
{
    NullPrivateData *pd = NULL;
    int pre = 0;

    TC_MODULE_SELF_CHECK(self, "filter_video");
    TC_MODULE_SELF_CHECK(frame, "filter_video");

    pd = self->userdata;
    pre = (frame->tag & TC_PRE_M_PROCESS);

    if (verbose & TC_STATS) {

        /*
         * tag variable indicates, if we are called before
         * transcodes internal video/audo frame processing routines
         * or after and determines video/audio context
         */
        tc_log_info(MOD_NAME, "frame [%06d] video %16s call",
                    frame->id, 
                    (pre) ?"pre-process filter" :"post-process filter");
    }

    if (!pre) {
        /* do not count frames twice */
        pd->video_frames++;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * null_filter_audio:  show something on given frame of the audio
 * stream.  See tcmodule-data.h for function details.
 */

static int null_filter_audio(TCModuleInstance *self, aframe_list_t *frame)
{
    NullPrivateData *pd = NULL;
    int pre = 0;

    TC_MODULE_SELF_CHECK(self, "filter_audio");
    TC_MODULE_SELF_CHECK(frame, "filter_audio");

    pd = self->userdata;
    pre = (frame->tag & TC_PRE_M_PROCESS);

    if (verbose & TC_STATS) {

        /*
         * tag variable indicates, if we are called before
         * transcodes internal video/audo frame processing routines
         * or after and determines video/audio context
         */
        tc_log_info(MOD_NAME, "frame [%06d] audio %16s call",
                    frame->id, 
                    (pre) ?"pre-process filter" :"post-process filter");
    }

    if (!pre) {
        /* do not count frames twice */
        pd->audio_frames++;
    }

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID null_codecs_video_in[] = { 
    TC_CODEC_ANY, TC_CODEC_ERROR
};
static const TCCodecID null_codecs_video_out[] = {
    TC_CODEC_ANY, TC_CODEC_ERROR
};
static const TCCodecID null_codecs_audio_in[] = { 
    TC_CODEC_ANY, TC_CODEC_ERROR
};
static const TCCodecID null_codecs_audio_out[] = {
    TC_CODEC_ANY, TC_CODEC_ERROR
};
TC_MODULE_FILTER_FORMATS(null);

TC_MODULE_INFO(null);

static const TCModuleClass null_class = {
    TC_MODULE_CLASS_HEAD(null),

    .init         = null_init,
    .fini         = null_fini,
    .configure    = null_configure,
    .stop         = null_stop,
    .inspect      = null_inspect,

    .filter_video = null_filter_video,
    .filter_audio = null_filter_audio,
};

TC_MODULE_ENTRY_POINT(null)

/*************************************************************************/

static int null_get_config(TCModuleInstance *self, char *options)
{
    NullPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = self->userdata;

    /*
     * Valid flags for the string of filter capabilities:
     *  "V" :  Can do Video
     *  "A" :  Can do Audio
     *  "R" :  Can do RGB
     *  "Y" :  Can do YUV
     *  "4" :  Can do YUV422
     *  "M" :  Can do Multiple Instances
     *  "E" :  Is a PRE filter
     *  "O" :  Is a POST filter
     */
    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       MOD_AUTHOR, "VAMEO", "1");

    /* can be omitted */
    optstr_param (options, "help", "Prints out a short help", "", "0");
 
    /* use optstr_param to do introspection */

    return TC_OK;
}

static int null_process(TCModuleInstance *self, 
                            frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    /* choose what to do by frame->tag */
    if (frame->tag & TC_VIDEO) {
        return null_filter_video(self, (vframe_list_t*)frame);
    }
    if (frame->tag & TC_AUDIO) {
        return null_filter_audio(self, (aframe_list_t*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE(null)

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
