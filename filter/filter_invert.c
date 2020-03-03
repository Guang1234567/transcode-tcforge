/*
 *  filter_invert.c -- invert filter: inverts the image
 *
 *  Copyright (C) Tilmann Bitterberg - June 2002
 *      modified 2007 by Branko Kokanovic to use NMS
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

#define MOD_NAME    "filter_invert.so"
#define MOD_VERSION "v0.1.6 (2009-02-07)"
#define MOD_CAP     "invert the image"
#define MOD_AUTHOR  "Tilmann Bitterberg"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

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


static const char invert_help[]=""
    "Overview\n"
    "   Invert an image\n"
    "Options\n"
    "    'range' apply filter to [start-end]/step frames [0-oo/1]\n";

/*************************************************************************/

typedef struct InvertPrivateData {
	unsigned int start;
	unsigned int end;
	unsigned int step;
	int boolstep;

    char opt_buf[TC_BUF_MIN];
} InvertPrivateData;

/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * invert_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(invert, InvertPrivateData)

/*************************************************************************/

/**
 * invert_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(invert)

/*************************************************************************/

/**
 * invert_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int invert_configure(TCModuleInstance *self,
                            const char *options,
                            TCJob *vob,
                            TCModuleExtraData *xdata[])
{
    InvertPrivateData *mfd = NULL;

    TC_MODULE_SELF_CHECK(vob, "configure");
    TC_MODULE_SELF_CHECK(self, "configure");

    mfd = self->userdata;

    /* setup defaults */
    mfd->start = 0;
    mfd->end = (unsigned int)-1;
    mfd->step = 1;

    if (options != NULL) {
        if(verbose >= TC_STATS){
            tc_log_info(MOD_NAME, "options=%s", options);
        }

	    optstr_get(options, "range",  "%u-%u/%d",    &mfd->start, &mfd->end, &mfd->step);
    }

    if (verbose > TC_INFO) {
	    tc_log_info(MOD_NAME, " Invert Image Settings:");
	    tc_log_info(MOD_NAME, "             range = %u-%u", mfd->start, mfd->end);
	    tc_log_info(MOD_NAME, "              step = %u", mfd->step);
    }

    if (mfd->start % mfd->step == 0)
        mfd->boolstep = 0;
    else
        mfd->boolstep = 1;

    return TC_OK;
}

/*************************************************************************/

/**
 * invert_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int invert_stop(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "stop");
    return TC_OK;
}

/*************************************************************************/

/**
 * invert_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int invert_inspect(TCModuleInstance *self,
                        const char *param, const char **value)
{
    InvertPrivateData *mfd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    mfd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = invert_help; 
    }
    if (optstr_lookup(param, "range")) {
	    tc_snprintf(mfd->opt_buf, sizeof(mfd->opt_buf), "%u-%u/%d",
                    mfd->start, mfd->end, mfd->step);
        *value = mfd->opt_buf; 
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * invert_filter_video:  show something on given frame of the video
 * stream.  See tcmodule-data.h for function details.
 */

static int invert_filter_video(TCModuleInstance *self, vframe_list_t *frame)
{
    InvertPrivateData *mfd = NULL;
    int w;

    TC_MODULE_SELF_CHECK(self, "filer_video");
    TC_MODULE_SELF_CHECK(frame, "filer_video");

    mfd = self->userdata;

    if (!(frame->attributes & TC_FRAME_IS_SKIPPED))  {
        uint8_t *p = frame->video_buf;

        if (mfd->start <= frame->id && frame->id <= mfd->end
         && frame->id%mfd->step == mfd->boolstep) {
            for (w = 0; w < frame->video_size; w++, p++)
                *p = 255 - *p;
        }
    }

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID invert_codecs_video_in[] = { 
    TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_YUV422P, TC_CODEC_ERROR
};
static const TCCodecID invert_codecs_video_out[] = {
    TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_YUV422P, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(invert);
TC_MODULE_FILTER_FORMATS(invert);

TC_MODULE_INFO(invert);

static const TCModuleClass invert_class = {
    TC_MODULE_CLASS_HEAD(invert),

    .init         = invert_init,
    .fini         = invert_fini,
    .configure    = invert_configure,
    .stop         = invert_stop,
    .inspect      = invert_inspect,

    .filter_video = invert_filter_video
};

TC_MODULE_ENTRY_POINT(invert)

/*************************************************************************/

static int invert_get_config(TCModuleInstance *self, char *options)
{
    InvertPrivateData *mfd = NULL;
    char buf[TC_BUF_MIN];

    TC_MODULE_SELF_CHECK(self, "get_config");

    mfd = self->userdata;

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                                MOD_AUTHOR, "VRY4O", "1");

    optstr_param(options, "help", "Inverts the image", "", "0");

    tc_snprintf(buf, sizeof(buf), "%u-%u/%d", mfd->start, mfd->end, mfd->step);
    optstr_param(options, "range", "apply filter to [start-end]/step frames",
                 "%u-%u/%d", buf, "0", "oo", "0", "oo", "1", "oo");

    return TC_OK;
}

static int invert_process(TCModuleInstance *self, 
                            frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    /* choose what to do by frame->tag */
    if (frame->tag & TC_VIDEO && frame->tag & TC_POST_M_PROCESS) {
        return invert_filter_video(self, (vframe_list_t*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE(invert)

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
