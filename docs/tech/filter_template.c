/*
 * filter_template.c -- template code for NMS and back compatible 
 *                      transcode filters
 * Written     by Andrew Church <achurch@achurch.org>
 * Templatized by Francesco Romani <fromani at gmail dot com>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#define MOD_NAME        "filter_template.so"
#define MOD_VERSION     "v1.1.0 (2007-05-31)"
#define MOD_CAP         "WRITE SUMMARY OF THE MODULE HERE"
#define MOD_AUTHOR      "Andrew Church, Francesco Romani"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO|TC_MODULE_FEATURE_AUDIO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

#include "transcode.h"
#include "filter.h"
#include "libtc/libtc.h"
#include "libtc/optstr.h"
#include "libtc/tcmodule-plugin.h"

static const char help_string[] = \
    "WRITE LONG AND DETAILED DESCRIPTION OF THE MODULE HERE";

/*************************************************************************/

typedef struct {
    ;
} PrivateData;

/*************************************************************************/
/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * template_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int template_init(TCModuleInstance *self, uint32_t features)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    pd = tc_malloc(sizeof(PrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }
    self->userdata = pd;

    /* initialize data */

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * template_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int template_fini(TCModuleInstance *self)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "fini");

    pd = self->userdata;

    /* free data allocated in _init */

    tc_free(self->userdata);
    self->userdata = NULL;
    return TC_OK;
}

/*************************************************************************/

/**
 * template_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int template_configure(TCModuleInstance *self,
                               const char *options, vob_t *vob)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    if (options) {
        /* optstr_get() them */
    }

    /* handle other options */

    return TC_OK;
}

/*************************************************************************/

/**
 * template_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int template_stop(TCModuleInstance *self)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    /* reverse all stuff done in _configure */

    return TC_OK;
}

/*************************************************************************/

/**
 * template_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int template_inspect(TCModuleInstance *self,
                             const char *param, const char **value)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = help_string; 
    }
    /* put back configurable options */

    return TC_OK;
}

/*************************************************************************/

/**
 * template_filter_video:  Perform the filter operation on the video
 * stream.  See tcmodule-data.h for function details.
 */

static int template_filter_video(TCModuleInstance *self, vframe_list_t *frame)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "filter_video");
    TC_MODULE_SELF_CHECK(frame, "filter_video");

    pd = self->userdata;

    /* do the magic */

    return TC_OK;
}

/*************************************************************************/

/**
 * template_filter_audio:  Perform the filter operation on the audio
 * stream.  See tcmodule-data.h for function details.
 */

static int template_filter_audio(TCModuleInstance *self, aframe_list_t *frame)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "filter_audio");
    TC_MODULE_SELF_CHECK(frame, "filter_audio");

    pd = self->userdata;

    /* do the magic */

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID template_codecs_in[] = { 
    TC_CODEC_ERROR 
};
static const TCCodecID template_codecs_out[] = { 
    TC_CODEC_ERROR 
};

TC_MODULE_FILTER_FORMATS(template);

TC_MODULE_INFO(template);

static const TCModuleClass template_class = {
    TC_MODULE_CLASS_HEAD(template),

    .init         = template_init,
    .fini         = template_fini,
    .configure    = template_configure,
    .stop         = template_stop,
    .inspect      = template_inspect,

    .filter_video = template_filter_video,
    /* We have to handle the audio too! */
    .filter_audio = template_filter_audio,
};

TC_MODULE_ENTRY_POINT(template)

/*************************************************************************/

static int template_get_config(TCModuleInstance *self, char *options)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = self->userdata;

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       MOD_AUTHOR, "VAMEO", "1");

    /* use optstr_param to do introspection */

    return TC_OK;
}

static int template_process(TCModuleInstance *self, 
                            frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    /* choose what to do by frame->tag */

    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE(template)

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
