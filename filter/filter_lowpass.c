/*
 *  filter_lowpass.c
 *
 *  Based on `filt'-code by Markus Wandel
 *    http://wandel.ca/homepage/audiohacks.html
 *  Copyright (C) Tilmann Bitterberg
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

#define MOD_NAME    "filter_lowpass.so"
#define MOD_VERSION "v0.5.1 (2009-02-07)"
#define MOD_CAP     "High and low pass filter"
#define MOD_AUTHOR  "Tilmann Bitterberg"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_AUDIO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE|TC_MODULE_FLAG_BUFFERING

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

#include <stdint.h>


/*************************************************************************/


static const char lowpass_help[] = ""
    "Overview:\n"
    "    FIXME: WRITEME\n"
    "Options:\n"
    "    taps    FIXME: whatabout?\n"
    "    help    print this help message\n";


/*************************************************************************/

typedef struct lowpassprivatedata_ LowPassPrivateData;
struct lowpassprivatedata_ {
    int16_t *array_l, *array_r;
    int highpass;               /* flag */
    int is_mono;                /* flag */
    int taps;
    int p;                      /* pointer */
    char conf_str[TC_BUF_MIN];
};

/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * lowpass_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(lowpass, LowPassPrivateData)

/*************************************************************************/

/**
 * lowpass_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(lowpass)


/*************************************************************************/

/**
 * lowpass_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int lowpass_configure(TCModuleInstance *self,
                             const char *options,
                             TCJob *vob,
                             TCModuleExtraData *xdata[])
{
    LowPassPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    if (vob->a_bits != 16) {
        tc_log_error(MOD_NAME, "This filter only supports 16 bit samples");
        return TC_ERROR;
    }

    pd->taps     = 30;
    pd->highpass = 0;
    pd->p        = 0;
    pd->is_mono  = (vob->a_chan == 1);

    if (options != NULL) {
        optstr_get(options, "taps", "%i", &pd->taps);
    }

    if (pd->taps < 0) {
        pd->taps = -pd->taps;
        pd->highpass = 1;
    }

    pd->array_r = tc_zalloc(pd->taps * sizeof(int16_t));
    pd->array_l = tc_zalloc(pd->taps * sizeof(int16_t));

    if (!pd->array_r || !pd->array_l) {
        return TC_ERROR;
    }

    if (verbose) {
        tc_log_info(MOD_NAME, "taps = %i (%spass)",
                    pd->taps, (pd->highpass) ?"high" :"low");
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * lowpass_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int lowpass_stop(TCModuleInstance *self)
{
    LowPassPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;
    
    if (pd->array_r) {
        tc_free(pd->array_r);
        pd->array_r = NULL; 
    }
    if (pd->array_l) {
        tc_free(pd->array_l);
        pd->array_l = NULL;
    }

    return TC_OK;
}

/*************************************************************************/


/*************************************************************************/

/**
 * lowpass_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int lowpass_inspect(TCModuleInstance *self,
                          const char *param, const char **value)
{
    LowPassPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    
    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = lowpass_help;
    }

    if (optstr_lookup(param, "taps")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "taps=%i", pd->taps);
        *value = pd->conf_str;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * lowpass_filter_audio:  perform the Y-plane rescaling for each frame of
 * this video stream. See tcmodule-data.h for function details.
 */

static int lowpass_filter_audio(TCModuleInstance *self,
                                aframe_list_t *frame)
{
    LowPassPrivateData *pd = NULL;
    int16_t *s = (int16_t *)frame->audio_buf;
    int i, j, al = 0, ar = 0;


    TC_MODULE_SELF_CHECK(self, "filter");
    TC_MODULE_SELF_CHECK(frame, "filter");

    pd = self->userdata;

    if (!pd->taps)
        return 0;

    if (pd->is_mono) {
        for (i = 0; i < frame->audio_size / 2; i++) {
            pd->array_r[pd->p] = s[i];
            for (j = 0; j < pd->taps; j++) {
                ar += pd->array_r[j];
	        }
	        pd->p = (pd->p+1) % pd->taps;
            ar /= pd->taps;
            if (pd->highpass) {
                s[i] -= ar;
            } else {
		        s[i]  = ar;
	        }
	    }
    } else {
        for (i = 0; i < frame->audio_size / 2; i++) {
            pd->array_l[pd->p] = s[i+0];
	        pd->array_r[pd->p] = s[i+1];
            for (j = 0; j < pd->taps; j++) {
                al += pd->array_l[j];
                ar += pd->array_r[j];
	        }
	        pd->p = (pd->p+1) % pd->taps;
            al /= pd->taps;
            ar /= pd->taps;
            if (pd->highpass) {
                s[i+0] -= al;
		        s[i+1] -= ar;
	        } else {
		        s[i+0]  = al;
		        s[i+1]  = ar;
	        }
	    }
    }
    return TC_OK;
}



/*************************************************************************/

static const TCCodecID lowpass_codecs_audio_in[] = { 
    TC_CODEC_PCM, TC_CODEC_ERROR
};
static const TCCodecID lowpass_codecs_audio_out[] = { 
    TC_CODEC_PCM, TC_CODEC_ERROR
};
TC_MODULE_VIDEO_UNSUPPORTED(lowpass);
TC_MODULE_FILTER_FORMATS(lowpass);

TC_MODULE_INFO(lowpass);

static const TCModuleClass lowpass_class = {
    TC_MODULE_CLASS_HEAD(lowpass),

    .init         = lowpass_init,
    .fini         = lowpass_fini,
    .configure    = lowpass_configure,
    .stop         = lowpass_stop,
    .inspect      = lowpass_inspect,

    .filter_audio = lowpass_filter_audio,
};

TC_MODULE_ENTRY_POINT(lowpass)

/*************************************************************************/

static int lowpass_get_config(TCModuleInstance *self, char *options)
{
    LowPassPrivateData *pd = NULL;
    char buf[TC_BUF_MIN];

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = self->userdata;

    /* use optstr_param to do introspection */
    if (options) {
        optstr_filter_desc(options, MOD_NAME, MOD_CAP,
                           MOD_VERSION, MOD_AUTHOR, "AE", "1");

        tc_snprintf(buf, sizeof(buf), "%d", pd->taps);
        optstr_param(options, "taps", "strength (may be negative)",
                     "%d", buf, "-50", "50");
    }
    return TC_OK;
}

static int lowpass_process(TCModuleInstance *self, frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    if (frame->tag & TC_PRE_S_PROCESS && frame->tag & TC_AUDIO) {
        return lowpass_filter_audio(self, (aframe_list_t*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE_M(lowpass)

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

