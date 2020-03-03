/*
 *  filter_detectsilence.c
 *
 *  Copyright (C) Tilmann Bitterberg - July 2003
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

#define MOD_NAME    "filter_detectsilence.so"
#define MOD_VERSION "v0.1.4 (2009-02-07)"
#define MOD_CAP     "audio silence detection with optional tcmp3cut commandline generation"
#define MOD_AUTHOR  "Tilmann Bitterberg"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_AUDIO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include "libtcmodule/tcmodule-plugin.h"

#include "libtcaudio/tcaudio.h"

#include <math.h>



#define SILENCE_FRAMES  4
#define MAX_SONGS      50

typedef struct privatedata_ DSPrivateData;
struct privatedata_ {
    int aframe_size;

    int scan_only; /* flag */

    int zeros;
    int next;
    int songs[MAX_SONGS];

    int silence_frames;
};


/*************************************************************************/

static const char detectsilence_help[] = ""
    "Overview:\n"
    "    This filter detect silence intervals in audio track. It can just\n"
    "    print out to screen the position and duration of audio silence\n"
    "    intervals, or, assuming the audio track is a soundtrack or something\n"
    "    like that, it can generate a tcmp3cut command line to cut the track\n"
    "    in songs.\n"
    "Options:\n"
    "    silence_frames  threshold used internally by filter to decide if\n"
    "                    silence interval is a song transition or not.\n"
    "                    The higher is this value, the longer should silence\n"
    "                    interval be.\n"
    "    scan_only       scan and print silence intervals, do not generate\n"
    "                    the tcmp3cut commandline.\n"
    "    help            produce module overview and options explanations.\n";


/*************************************************************************/


static int print_tcmp3cut_cmdline(DSPrivateData *pd)
{
    char cmd[TC_BUF_MAX];
    char songbuf[MAX_SONGS * 12];  /* up to 11 chars and , per value */
    int i, res, len = 0, songlen = 0;

    if (pd->next < 1) {
        /* nothing to do in here */
        return TC_OK;
    }

    res = tc_snprintf(cmd, sizeof(cmd), "tcmp3cut -i in.mp3 -o base ");
    if (res < 0) {
        tc_log_error(MOD_NAME, "cmd buffer overflow");
        return TC_ERROR;
    }
    len += res;
    
    for (i = 0; i < pd->next; i++) {
        res = tc_snprintf(songbuf + songlen, sizeof(songbuf) - songlen,
                          ",%d", pd->songs[i]);
        if (res < 0) {
            tc_log_error(MOD_NAME, "cmd buffer overflow");
            return TC_ERROR;
        }
        songlen += res;
    }

    tc_log_info(MOD_NAME, "********** Songs ***********");
    tc_log_info(MOD_NAME, "%s", songbuf);

    res = tc_snprintf(cmd + len, sizeof(cmd) - len, "-t %s", songbuf);
    if (res < 0) {
        tc_log_error(MOD_NAME, "cmd buffer overflow");
        return TC_ERROR;
    }
    len += res;
    tc_log_info(MOD_NAME, "Execute: %s", cmd);

    return TC_OK;
}


/*************************************************************************/

/**
 * detectsilence_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(detectsilence, DSPrivateData)

/*************************************************************************/

/**
 * detectsilence_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(detectsilence)

/*************************************************************************/

/**
 * detectsilence_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int detectsilence_configure(TCModuleInstance *self,
                                   const char *options,
                                   TCJob *vob,
                                   TCModuleExtraData *xdata[])
{
    DSPrivateData *pd = NULL;
    int i;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    for (i = 0; i < MAX_SONGS; i++) {
        pd->songs[i] = -1;
    }

    pd->scan_only      = TC_FALSE;
    pd->silence_frames = SILENCE_FRAMES;
    pd->aframe_size    = (vob->a_rate * vob->a_chan * vob->a_bits / 8) / 1000;
    pd->zeros          = 0;
    pd->next           = 0;
    
    if (options != NULL) {
        optstr_get(options, "scan_only",      "%d", &pd->scan_only);
        optstr_get(options, "silence_frames", "%d", &pd->silence_frames);
    }

    if (verbose) {
        tc_log_info(MOD_NAME, "frame size = %i bytes;"
                              " silence interval = %i frames",
                              pd->aframe_size, pd->silence_frames);
        if (pd->scan_only) {
            tc_log_info(MOD_NAME, "silence interval detection enabled");
        } else {
            tc_log_info(MOD_NAME, "tcmp3cut commandline creation enabled");
        }
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * detectsilence_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int detectsilence_stop(TCModuleInstance *self)
{
    DSPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    /* 
     * print out summary a stop time. There isn't any configure()d
     * stuff to revert, anyway.
     */

    if (!pd->scan_only) {
        print_tcmp3cut_cmdline(pd);
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * detectsilence_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int detectsilence_inspect(TCModuleInstance *self,
                                 const char *param, const char **value)
{
    static char buf[TC_BUF_MIN]; // XXX
    DSPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self,  "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = detectsilence_help; 
    }

    if (optstr_lookup(param, "scan_only")) {
        tc_snprintf(buf, sizeof(buf), "%d", pd->scan_only);
        *value = buf;
    }
    if (optstr_lookup(param, "silence_frames")) {
        tc_snprintf(buf, sizeof(buf), "%d", pd->silence_frames);
        *value = buf;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * detectsilence_filter_audio:  Perform the per-frame analysis on the audio
 * stream.  See tcmodule-data.h for function details.
 */

static int detectsilence_filter_audio(TCModuleInstance *self,
                                      aframe_list_t *frame)
{
    DSPrivateData *pd = NULL;
    int16_t *s = (int16_t*)frame->audio_buf;
    double p = 0.0;
    int i, sum;

    TC_MODULE_SELF_CHECK(self,  "filter_audio");
    TC_MODULE_SELF_CHECK(frame, "filter_audio");

    pd = self->userdata;

    for (i = 0; i < frame->audio_size / 2; i++) {
        p += fabs((double)(*s++)/((double)TCA_S16LE_MAX * 1.0)); 
        /* FIXME: constantize in libtcaudio */
    }

    sum = (int)p;

    /* Is this frame silence? */
    if (sum == 0)
        pd->zeros++;

    /*
     * former silence, now no more silence
     */
    if (pd->zeros >= pd->silence_frames && sum > 0) {
        if (pd->scan_only) {
            tc_log_info(MOD_NAME, "silence interval in frames [%i-%i]",
                                  frame->id - pd->zeros, frame->id - 1);
        } else {
            /* somwhere in the middle of silence */
            pd->songs[pd->next] = ((frame->id - pd->zeros) * frame->audio_size) / pd->aframe_size;
            pd->next++;

            if (pd->next > MAX_SONGS) {
                tc_log_error(MOD_NAME, "Cannot save more songs");
                return TC_ERROR;
            }
        }
        pd->zeros = 0;
    }

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID detectsilence_codecs_audio_in[] = { 
    TC_CODEC_PCM, TC_CODEC_ERROR
};
static const TCCodecID detectsilence_codecs_audio_out[] = { 
    TC_CODEC_PCM, TC_CODEC_ERROR 
};
TC_MODULE_VIDEO_UNSUPPORTED(detectsilence);
TC_MODULE_FILTER_FORMATS(detectsilence);

TC_MODULE_INFO(detectsilence);

static const TCModuleClass detectsilence_class = {
    TC_MODULE_CLASS_HEAD(detectsilence),

    .init         = detectsilence_init,
    .fini         = detectsilence_fini,
    .configure    = detectsilence_configure,
    .stop         = detectsilence_stop,
    .inspect      = detectsilence_inspect,

    .filter_audio = detectsilence_filter_audio,
};

TC_MODULE_ENTRY_POINT(detectsilence)


/*************************************************************************/
/* Old-style helpers */

static int detectsilence_get_config(TCModuleInstance *self, char *options)
{
    DSPrivateData *pd = NULL;
    char buf[TC_BUF_MIN];

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = self->userdata;

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       MOD_AUTHOR, "AE", "1");

    tc_snprintf(buf, sizeof(buf), "%i", pd->scan_only);
    optstr_param(options, "scan_only",
                 "only print out silence interval boundaries",
                 "%d", buf, "0", "1"); /* max is arbitrary here */

    tc_snprintf(buf, sizeof(buf), "%i", pd->silence_frames);
    optstr_param(options, "silence_frames",
                 "minimum number of silence frames to detect a song change",
                 "%d", buf, "0", "1024"); /* max is arbitrary here */

    return TC_OK;
}

static int detectsilence_process(TCModuleInstance *self, 
                                 frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    if (frame->tag & TC_PRE_S_PROCESS && frame->tag & TC_AUDIO) {
        return detectsilence_filter_audio(self, (aframe_list_t *)frame);
    }
    return TC_OK;
}

/*************************************************************************/

TC_FILTER_OLDINTERFACE(detectsilence)

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * 
 * vim: expandtab shiftwidth=4:
 */
