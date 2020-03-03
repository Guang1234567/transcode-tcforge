/*
 *  filter_fields.c
 *
 *  Copyright (C) Alex Stewart - July 2002
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
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*****************************************************************************
 *              Standard Transcode Filter Defines and Includes               *
 *****************************************************************************/

#define MOD_NAME    "filter_fields.so"
#define MOD_VERSION "v0.2.1 (2009-02-07)"
#define MOD_CAP     "Field adjustment plugin"
#define MOD_AUTHOR  "Alex Stewart"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE|TC_MODULE_FLAG_BUFFERING

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"


/*****************************************************************************
 *                    Global Filter Variables and Defines                    *
 *****************************************************************************/

static const char fields_help[] = ""
    "Transcode field-adjustment filter (filter_fields) help\n"
    "------------------------------------------------------\n"
    "\n"
    "The 'fields' filter is designed to shift, reorder, and\n"
    "generally rearrange independent fields of an interlaced\n"
    "video input.  Input retrieved from broadcast (PAL, NTSC,\n"
    "etc) video sources generally comes in an interlaced form\n"
    "where each pass from top to bottom of the screen displays\n"
    "every other scanline, and then the next pass displays the\n"
    "lines between the lines from the first pass.  Each pass is\n"
    "known as a \"field\" (there are generally two fields per\n"
    "frame).  When this form of video is captured and manipulated\n"
    "digitally, the two fields of each frame are usually merged\n"
    "together into one flat (planar) image per frame.  This\n"
    "usually produces reasonable results, however there are\n"
    "conditions which can cause this merging to be performed\n"
    "incorrectly or less-than-optimally, which is where this\n"
    "filter can help.\n"
    "\n"
    "The following options are supported for this filter\n"
    "(they can be separated by colons):\n"
    "\n"
    "  shift - Shift the video by one field (half a frame),\n"
    "          changing frame boundaries appropriately.  This is\n"
    "          useful if a video capture started grabbing video\n"
    "          half a frame (one field) off from where frame\n"
    "          boundaries were actually intended to be.\n"
    "\n"
    "  flip  - Exchange the top field and bottom field of each\n"
    "          frame.  This can be useful if the video signal was\n"
    "          sent \"bottom field first\" (which can happen\n"
    "          sometimes with PAL video sources) or other\n"
    "          oddities occurred which caused the frame\n"
    "          boundaries to be at the right place, but the\n"
    "          scanlines to be swapped.\n"
    "\n"
    "  flip_first\n"
    "        - Normally shifting is performed before flipping if\n"
    "          both are specified.  This option reverses that\n"
    "          behavior.  You should not normally need to use\n"
    "          this unless you have some extremely odd input\n"
    "          material, it is here mainly for completeness.\n"
    "\n"
    "  help  - Print this text.\n"
    "\n"
    "Note: the 'shift' function may produce slight color\n"
    "discrepancies if YUV is used as the internal transcode\n"
    "video format.  This is because YUV does not contain enough\n"
    "information to do field shifting cleanly. For best (but\n"
    "slower) results, use RGB mode (-V rgb24) for field\n"
    "shifting.\n";

enum {
    FIELD_OP_FLIP    = 0x01,
    FIELD_OP_SHIFT   = 0x02,
    FIELD_OP_REVERSE = 0x04,
};

#define FIELD_OP_SHIFTFLIP (FIELD_OP_SHIFT | FIELD_OP_FLIP)
#define FIELD_OP_FLIPSHIFT (FIELD_OP_SHIFTFLIP | FIELD_OP_REVERSE)

typedef struct {
    uint8_t *buffer;
    int buf_field;

    int field_ops;
    int rgb_mode;

    char conf_str[TC_BUF_MIN];
} FieldsPrivateData;

/*****************************************************************************
 *                         Filter Utility Functions                          *
 *****************************************************************************/

/* 
 * copy_field - Copy one field of a frame (every other line) from one buffer
 *              to another.
 */
static void copy_field(char *to, char *from, int width, int height) 
{
    int increment = width * 2;

    height /= 2;
    while (height--) {
        ac_memcpy(to, from, width);
        to   += increment;
        from += increment;
    }
}

/* 
 * swap_fields - Exchange one field of a frame (every other line) with another
 *               NOTE:  This function uses 'buffer' as a temporary space.
 */
static void swap_fields(char *buffer, char *f1, char *f2, int width, int height)
{
    int increment = width * 2;

    height /= 2;
    while (height--) {
        ac_memcpy(buffer, f1, width);
        ac_memcpy(f1, f2, width);
        ac_memcpy(f2, buffer, width);
        f1 += increment;
        f2 += increment;
    }
}

/*************************************************************************/

/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * fields_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(fields, FieldsPrivateData)

/*************************************************************************/

/**
 * fields_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(fields)

/*************************************************************************/

/**
 * fields_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int fields_configure(TCModuleInstance *self,
            			    const char *options,
                            TCJob *vob,
                            TCModuleExtraData *xdata[])
{
    FieldsPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    // Some of the data in buffer may get used for half of the first frame (when
    // shifting) so make sure it's blank to start with.
    pd->buffer = tc_zalloc(SIZE_RGB_FRAME);
    if (!pd->buffer) {
        tc_log_error(MOD_NAME, "Unable to allocate memory.  Aborting.");
        return TC_ERROR;
    }

    if (options != NULL) {
        if (optstr_lookup(options, "flip"))
            pd->field_ops |= FIELD_OP_FLIP;
        if (optstr_lookup(options, "shift"))
            pd->field_ops |= FIELD_OP_SHIFT;
        if (optstr_lookup(options, "flip_first"))
            pd->field_ops |= FIELD_OP_REVERSE;
    }

    // FIELD_OP_REVERSE (aka flip_first) only makes sense if we're doing
    // both operations.  If we're not, unset it.
    if (pd->field_ops != FIELD_OP_FLIPSHIFT)
        pd->field_ops &= ~FIELD_OP_REVERSE;

    if (verbose) {
        if (pd->field_ops & FIELD_OP_SHIFT)
            tc_log_info(MOD_NAME, "Adjusting frame positions (shift)");
        if (pd->field_ops & FIELD_OP_FLIP)
            tc_log_info(MOD_NAME, "Transposing input fields  (flip)");
        if (pd->field_ops & FIELD_OP_REVERSE)
            tc_log_info(MOD_NAME, "Flipping will occur before shifting (flip_first)");
    }

    if (!pd->field_ops) {
        tc_log_warn(MOD_NAME, "No operations specified to perform.");
        return TC_ERROR;
    }

    if (vob->im_v_codec == TC_CODEC_RGB24);
        pd->rgb_mode = TC_TRUE;

    if (verbose)
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

    return TC_OK;
}

/*************************************************************************/

/**
 * fields_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int fields_stop(TCModuleInstance *self)
{
    FieldsPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    if (pd->buffer) {
        tc_free(pd->buffer);
        pd->buffer = NULL;
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * fields_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int fields_inspect(TCModuleInstance *self,
                          const char *param, const char **value)
{
    FieldsPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    
    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = fields_help;
    }

    if (optstr_lookup(param, "flip")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%s", (pd->field_ops & FIELD_OP_FLIP) ?"yes" :"no");
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "shift")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%s", (pd->field_ops & FIELD_OP_SHIFT) ?"yes" :"no");
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "flip_first")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%s", (pd->field_ops & FIELD_OP_REVERSE) ?"yes" :"no");
        *value = pd->conf_str;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * fields_filter_video:  perform the video frame manipulation for
 * this video stream. See tcmodule-data.h for function details.
 */

static int fields_filter_video(TCModuleInstance *self,
                               vframe_list_t *frame)
{
    FieldsPrivateData *pd = NULL;
    uint8_t *f1 = NULL, *f2 = NULL, *b1 = NULL, *b2 = NULL;
    int width, height;

    TC_MODULE_SELF_CHECK(self, "filter");
    TC_MODULE_SELF_CHECK(frame, "filter");

    pd = self->userdata;

    width = frame->v_width * (pd->rgb_mode ? 3 : 1);
    height = frame->v_height;
    f1 = frame->video_buf;
    f2 = frame->video_buf + width;
    b1 = pd->buffer;
    b2 = pd->buffer + width;

    switch (pd->field_ops) {
      case FIELD_OP_FLIP:
        swap_fields(pd->buffer, f1, f2, width, height);
        break;
      case FIELD_OP_SHIFT:
        copy_field(pd->buf_field ? b2 : b1, f2, width, height);
        copy_field(f2, f1, width, height);
        copy_field(f1, pd->buf_field ? b1 : b2, width, height);
        break;
      case FIELD_OP_SHIFTFLIP:
        // Shift + Flip is the same result as just delaying the second field by
        // one frame, so do that because it's faster.
        copy_field(pd->buf_field ? b1 : b2, f2, width, height);
        copy_field(f2, pd->buf_field ? b2 : b1, width, height);
        break;
      case FIELD_OP_FLIPSHIFT:
        // Flip + Shift is the same result as just delaying the first field by
        // one frame, so do that because it's faster.
        copy_field(pd->buf_field ? b1 : b2, f1, width, height);
        copy_field(f1, pd->buf_field ? b2 : b1, width, height);

        // Chroma information is usually taken from the top field, which we're
        // shifting here.  We probably should move the chroma info with it, but
        // this will be used so rarely (and this is only an issue in YUV mode
        // anyway, which is not reccomended to start with) that it's probably not
        // worth bothering.
        break;
    }
    pd->buf_field ^= 1;

    return TC_OK;
}


/*************************************************************************/

static const TCCodecID fields_codecs_video_in[] = { 
    TC_CODEC_YUV420P, TC_CODEC_RGB24, TC_CODEC_ERROR
};
static const TCCodecID fields_codecs_video_out[] = { 
    TC_CODEC_YUV420P, TC_CODEC_RGB24, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(fields);
TC_MODULE_FILTER_FORMATS(fields);

TC_MODULE_INFO(fields);

static const TCModuleClass fields_class = {
    TC_MODULE_CLASS_HEAD(fields),

    .init         = fields_init,
    .fini         = fields_fini,
    .configure    = fields_configure,
    .stop         = fields_stop,
    .inspect      = fields_inspect,

    .filter_video = fields_filter_video,
};

TC_MODULE_ENTRY_POINT(fields)

/*************************************************************************/

static int fields_get_config(TCModuleInstance *self, char *options)
{
    FieldsPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = self->userdata;

    /* use optstr_param to do introspection */
    optstr_filter_desc(options, MOD_NAME,
                       MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYE", "1");
    optstr_param(options, "flip",
                 "Exchange the top field and bottom field of each frame",
                 "", "0");
    optstr_param(options, "shift",
                 "Shift the video by one field", "", "0");
    optstr_param(options, "flip_first",
                 "Normally shifting is performed before flipping, this option"
                 " reverses that", "", "0");

    return TC_OK;
}

static int fields_process(TCModuleInstance *self, frame_list_t *frame)
{
    FieldsPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "process");

    pd = self->userdata;

    if (frame->tag & TC_PRE_S_PROCESS && frame->tag & TC_VIDEO) {
        return fields_filter_video(self, (vframe_list_t*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE(fields)

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
