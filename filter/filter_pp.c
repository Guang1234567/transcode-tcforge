/*
 *  filter_pp.c
 *
 *  Copyright (C) Gerhard Monzel - Januar 2002
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

#define MOD_NAME    "filter_pp.so"
#define MOD_VERSION "v1.2.6 (2009-02-07)"
#define MOD_CAP     "Mplayers postprocess filters"
#define MOD_AUTHOR  "Michael Niedermayer et al, Gerhard Monzel"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

#include <ctype.h>

#include <libpostproc/postprocess.h>


/*************************************************************************/

typedef struct {
    pp_mode_t *mode;
    pp_context_t *context;

    int width, height;

    int pre_flag;
} PPPrivateData;


static const char tc_pp_help[] = ""
    "FIXME: WRITEME\n"
    "<filterName>[:<option>[:<option>...]][[|/][-]<filterName>[:<option>...]]...\n"
    "long form example:\n"
    "vdeblock:autoq/hdeblock:autoq/linblenddeint    default,-vdeblock\n"
    "short form example:\n"
    "vb:a/hb:a/lb                                   de,-vb\n"
    "more examples:\n"
    "tn:64:128:256\n"
    "Filters                        Options\n"
    "short  long name       short   long option     Description\n"
    "*      *               a       autoq           cpu power dependant enabler\n"
    "                       c       chrom           chrominance filtring enabled\n"
    "                       y       nochrom         chrominance filtring disabled\n"
    "hb     hdeblock        (2 Threshold)           horizontal deblocking filter\n"
    "       1. difference factor: default=64, higher -> more deblocking\n"
    "       2. flatness threshold: default=40, lower -> more deblocking\n"
    "                       the h & v deblocking filters share these\n"
    "                       so u cant set different thresholds for h / v\n"
    "vb     vdeblock        (2 Threshold)           vertical deblocking filter\n"
    "h1     x1hdeblock                              Experimental h deblock filter 1\n"
    "v1     x1vdeblock                              Experimental v deblock filter 1\n"
    "dr     dering                                  Deringing filter\n"
    "al     autopp                              automatic brightness / contrast\n"
    "                       f       fullyrange      stretch luminance to (0..255)\n"
    "lb     linblenddeint                           linear blend deinterlacer\n"
    "li     linipoldeint                            linear interpolating deinterlace\n"
    "ci     cubicipoldeint                          cubic interpolating deinterlacer\n"
    "md     mediandeint                             median deinterlacer\n"
    "fd     ffmpegdeint                             ffmpeg deinterlacer\n"
    "de     default                                 hb:a,vb:a,dr:a,al\n"
    "fa     fast                                    h1:a,v1:a,dr:a,al\n"
    "tn     tmpnoise        (3 Thresholds)          Temporal Noise Reducer\n"
    "                       1. <= 2. <= 3.          larger -> stronger filtering\n"
    "fq     forceQuant      <quantizer>             Force quantizer\n"
    "pre    pre                                     run as a pre filter\n";

/*************************************************************************/

static uint32_t translate_accel(int tc_accel)
{
    if (tc_accel & AC_MMXEXT)
        return PP_CPU_CAPS_MMX2;
    if(tc_accel & AC_3DNOW)
        return PP_CPU_CAPS_3DNOW;
    if(tc_accel & AC_MMX)
        return PP_CPU_CAPS_MMX;
    return 0;
}

// FIXME: legacy

static int no_optstr (char *s)
{
  int result = 0; // decrement if transcode, increment if mplayer
  char *c = s;

  while (c && *c && (c = strchr (c, '=')))  { result--; c++; }
  c = s;
  while (c && *c && (c = strchr (c, '/')))  { result++; c++; }
  c = s;
  while (c && *c && (c = strchr (c, '|')))  { result++; c++; }
  c = s;
  while (c && *c && (c = strchr (c, ',')))  { result++; c++; }


  return (result<=0)?0:1;
}

static void do_optstr(char *opts)
{
    opts++;

    while (*opts) {
        if (*(opts-1) == ':') {
	        if (isalpha(*opts)) {
                if ((strncmp(opts, "autoq", 5)   == 0)
                 || (strncmp(opts, "chrom", 5)   == 0)
                 || (strncmp(opts, "nochrom", 7) == 0)
                 || ((strncmp(opts, "a", 1)==0) && (strncmp(opts,"al",2)!=0))
                 || ((strncmp(opts, "c", 1)==0) && (strncmp(opts,"ci",2)!=0))
                 || (strncmp(opts, "y", 1)==0)) {
                    opts++;
                    continue;
                } else {
                    *(opts-1) = '/';
		        }
            }
        }
        if (*opts == '=')
	        *opts = ':';
        opts++;
    }
}

static char *pp_lookup(char *haystack, char *needle)
{
	char *ch = haystack;
	int len = strlen(needle);
	int found = 0;

	while (!found) {
		ch = strstr(ch, needle);

		if (!ch) break;

		if (ch[len] == '\0' || ch[len] == '=' || ch[len] == '/') {
			found = 1;
		} else {
			ch++;
		}
	}

	return (ch);
}

/*************************************************************************/

/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * pp_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(pp, PPPrivateData)

/*************************************************************************/

/**
 * pp_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(pp)

/*************************************************************************/

/**
 * pp_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int pp_configure(TCModuleInstance *self,
            			const char *options,
                        TCJob *vob,
                        TCModuleExtraData *xdata[])
{
    int tc_accel = tc_get_session()->acceleration; /* XXX ugly */
    PPPrivateData *pd = NULL;
    int len = strlen(options); // XXX
    char *c = NULL, opts[TC_BUF_LINE];

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    strlcpy(opts, options, sizeof(opts));
    if (vob->im_v_codec != TC_CODEC_YUV420P) {
        tc_log_error(MOD_NAME, "This filter is only capable of YUV 4:2:0 mode");
        return TC_ERROR;
    }
    if (!options || !len)  {
        tc_log_error(MOD_NAME, "this filter needs options !");
        return TC_ERROR;
    }

    if (!no_optstr(opts)) {
	    do_optstr(opts);
    }

    /* delete module options, so they can passed to libpostproc */
    c = pp_lookup(opts, "pre");
    if (c) {
    	memmove(c, c+3, &opts[len]-c);
        pd->pre_flag = 1;
    }

    if (pd->pre_flag) {
        pd->width  = vob->im_v_width;
        pd->height = vob->im_v_height;
    } else {
        pd->width  = vob->ex_v_width;
        pd->height = vob->ex_v_height;
    }

    pd->mode = pp_get_mode_by_name_and_quality(opts, PP_QUALITY_MAX);
    if (!pd->mode) {
        tc_log_error(MOD_NAME, "internal error (pp_get_mode_by_name_and_quality)");
        return TC_ERROR;
    }

    pd->context = pp_get_context(pd->width, pd->height, 
                                 translate_accel(tc_accel));
    if (!pd->context) {
        tc_log_error(MOD_NAME, "internal error (pp_get_context)");
        return TC_ERROR;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * pp_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int pp_stop(TCModuleInstance *self)
{
    PPPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    if (pd->mode) {
        pp_free_mode(pd->mode);
        pd->mode = NULL;
    }
    if (pd->context) {
        pp_free_context(pd->context);
        pd->context = NULL;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * pp_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int pp_inspect(TCModuleInstance *self,
                      const char *param, const char **value)
{
    PPPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    
    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = tc_pp_help;
    }
    // FIXME

    return TC_OK;
}

/*************************************************************************/

/**
 * pp_filter_video:  perform the postprocessing for each frame of
 * this video stream. See tcmodule-data.h for function details.
 */

static int pp_filter_video(TCModuleInstance *self,
                           TCFrameVideo *frame)
{
    PPPrivateData *pd;
    uint8_t *pp_page[3];
    const uint8_t *pp_srcpage[3];  // To avoid a compiler warning
    int ppStride[3];

    TC_MODULE_SELF_CHECK(self, "filter");
    TC_MODULE_SELF_CHECK(frame, "filter");

    pd = self->userdata;

    YUV_INIT_PLANES(pp_page, frame->video_buf,
                    IMG_YUV420P, pd->width, pd->height);
    pp_srcpage[0] = pp_page[0];
    pp_srcpage[1] = pp_page[1];
    pp_srcpage[2] = pp_page[2];

    ppStride[0] = pd->width;
    ppStride[1] = pd->width/2;
    ppStride[2] = pd->width/2;

    pp_postprocess(pp_srcpage, ppStride, pp_page, ppStride,
                   pd->width, pd->height, NULL, 0, 
                   pd->mode, pd->context, 0);

    return TC_OK;
}

/**************************************************************************/

static const TCCodecID pp_codecs_video_in[] = { 
    TC_CODEC_YUV420P, TC_CODEC_ERROR
};
static const TCCodecID pp_codecs_video_out[] = { 
    TC_CODEC_YUV420P, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(pp);
TC_MODULE_FILTER_FORMATS(pp);

TC_MODULE_INFO(pp);

static const TCModuleClass pp_class = {
    TC_MODULE_CLASS_HEAD(pp),

    .init         = pp_init,
    .fini         = pp_fini,
    .configure    = pp_configure,
    .stop         = pp_stop,
    .inspect      = pp_inspect,

    .filter_video = pp_filter_video,
};

TC_MODULE_ENTRY_POINT(pp);

/*************************************************************************/

static int pp_get_config(TCModuleInstance *self, char *options)
{
    TC_MODULE_SELF_CHECK(self, "get_config");

    optstr_filter_desc(options, MOD_NAME,
                       MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VYMOE", "1");

    optstr_param(options, "hb", "Horizontal deblocking filter",
                 "%d:%d", "64:40", "0", "255", "0", "255");
    optstr_param(options, "vb", "Vertical deblocking filter",
                 "%d:%d", "64:40", "0", "255", "0", "255");
    optstr_param(options, "h1", "Experimental h deblock filter 1", "", "0");
    optstr_param(options, "v1", "Experimental v deblock filter 1", "", "0");
    optstr_param(options, "dr", "Deringing filter", "", "0");
    optstr_param(options, "al", "Automatic brightness / contrast", "", "0");
    optstr_param(options, "f", "Stretch luminance to (0..255)", "", "0");
    optstr_param(options, "lb", "Linear blend deinterlacer", "", "0");
    optstr_param(options, "li", "Linear interpolating deinterlace", "", "0");
    optstr_param(options, "ci", "Cubic interpolating deinterlacer", "", "0");
    optstr_param(options, "md", "Median deinterlacer", "", "0");
    optstr_param(options, "de", "Default preset (hb:a/vb:a/dr:a/al)", "", "0");
    optstr_param(options, "fa", "Fast preset (h1:a/v1:a/dr:a/al)", "", "0");
    optstr_param(options, "tn", "Temporal Noise Reducer (1<=2<=3)",
                 "%d:%d:%d", "64:128:256",
                 "0", "700", "0", "1500", "0", "3000");
    optstr_param(options, "fq", "Force quantizer", "%d", "15", "0", "255");
    optstr_param(options, "pre", "Run as a PRE filter", "", "0");

    return TC_OK;
}


static int pp_process(TCModuleInstance *self, frame_list_t *frame)
{
    PPPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "process");

    pd = self->userdata;

    if (((frame->tag & TC_PRE_M_PROCESS  &&  pd->pre_flag)
     ||  (frame->tag & TC_POST_M_PROCESS && !pd->pre_flag))
     && !(frame->attributes & TC_FRAME_IS_SKIPPED)) {
        return pp_filter_video(self, (TCFrameVideo*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

TC_FILTER_OLDINTERFACE_M(pp)

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
