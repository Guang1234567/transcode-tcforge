/*
 *  filter_resample.c
 *
 *  Copyright (C) Thomas Oestreich - February 2002
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

#define MOD_NAME    "filter_resample.so"
#define MOD_VERSION "v0.1.7 (2009-02-07)"
#define MOD_CAP     "audio resampling filter plugin using libavcodec"
#define MOD_AUTHOR  "Thomas Oestreich, Stefan Scheffler"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_AUDIO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE|TC_MODULE_FLAG_BUFFERING

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"
#include "libtcext/tc_avcodec.h"


typedef struct {
    uint8_t *resample_buf;
    size_t resample_bufsize;

    int bytes_per_sample;

    ReSampleContext *resample_ctx;
} ResamplePrivateData;

static const char resample_help[] = ""
    "Overview:\n"
    "    This filter resample an audio stream using libavcodec facilties.\n"
    "    i.e. changes input sample rate to 22050 Hz to 48000 Hz.\n"
    "Options:\n"
    "    help    show this message.\n";


/*-------------------------------------------------*/

TC_MODULE_GENERIC_INIT(resample, ResamplePrivateData)

static int resample_configure(TCModuleInstance *self,
                              const char *options,
                              TCJob *vob,
                              TCModuleExtraData *xdata[])
{
    double samples_per_frame, ratio;
    ResamplePrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");
    TC_MODULE_SELF_CHECK(vob, "configure"); /* paranoia */

    pd = self->userdata;

    if (!vob->a_rate || !vob->mp3frequency) {
        tc_log_error(MOD_NAME, "Invalid settings");
        return TC_ERROR;
    }
    tc_log_info(MOD_NAME, "resampling: %i Hz -> %i Hz",
                vob->a_rate, vob->mp3frequency);
    if (vob->a_rate == vob->mp3frequency) {
        tc_log_error(MOD_NAME, "Frequencies are identical,"
                     " filter skipped");
        return TC_ERROR;
    }
 
    pd->bytes_per_sample = vob->a_chan * vob->a_bits/8;
    samples_per_frame = vob->a_rate/vob->ex_fps;
    ratio = (float)vob->mp3frequency/(float)vob->a_rate;

    pd->resample_bufsize = (int)(samples_per_frame * ratio) * pd->bytes_per_sample + 16 // frame + 16 bytes
                            + ((vob->a_leap_bytes > 0)?(int)(vob->a_leap_bytes * ratio) :0); 
                           // leap bytes .. kinda
    /* XXX */

    pd->resample_buf = tc_malloc(pd->resample_bufsize);
    if (pd->resample_buf == NULL) {
        tc_log_error(MOD_NAME, "Buffer allocation failed");
        return TC_ERROR;
    }

    if (verbose >= TC_DEBUG) {
        tc_log_info(MOD_NAME,
                    "bufsize : %lu, bytes : %i, bytesfreq/fps: %i, rest %i",
                    (unsigned long)pd->resample_bufsize, pd->bytes_per_sample,
                    vob->mp3frequency * pd->bytes_per_sample / (int)vob->fps,
                    (vob->a_leap_bytes > 0 )?(int)(vob->a_leap_bytes * ratio):0);
    }

    if ((size_t)(pd->bytes_per_sample * vob->mp3frequency / vob->fps) > pd->resample_bufsize) {
        goto abort;
    }

    pd->resample_ctx = av_audio_resample_init(vob->a_chan, vob->a_chan,
                                              vob->mp3frequency, vob->a_rate,
                                              SAMPLE_FMT_S16, SAMPLE_FMT_S16,
                                              16, 10, 0, 0.8);
    if (pd->resample_ctx == NULL) {
        tc_log_error(MOD_NAME, "can't get a resample context");
        goto abort;
    }

    /* 
     * this will force this resample filter to do the job, not the export module.
     * Yeah, that's nasty. -- FR.
     */

    vob->a_rate = vob->mp3frequency;
    vob->mp3frequency = 0;
    vob->ex_a_size = pd->resample_bufsize;

    self->userdata = pd;

    return TC_OK;

abort:
    tc_free(pd->resample_buf);
    pd->resample_buf = NULL;
    return TC_ERROR;
}

TC_MODULE_GENERIC_FINI(resample)

static int resample_stop(TCModuleInstance *self)
{
    ResamplePrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    if (pd->resample_ctx != NULL) {
        audio_resample_close(pd->resample_ctx);
        pd->resample_ctx = NULL;
    }
    if (pd->resample_buf != NULL) {
        tc_free(pd->resample_buf);
        pd->resample_buf = NULL;
    }

    return TC_OK;
}

static int resample_inspect(TCModuleInstance *self,
                            const char *param, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    
    if (optstr_lookup(param, "help")) {
        *value = resample_help;
    }

    return TC_OK;
}

/* internal helper to avoid an useless double if() */
static int resample_filter_audio(TCModuleInstance *self, aframe_list_t *frame)
{
    ResamplePrivateData *pd = self->userdata;

    if (pd->resample_bufsize == 0) {
        /* XXX: really useful? can happen? */
        tc_log_error(__FILE__, "wrong (insane) buffer size");
        return TC_ERROR;
    }
    if (verbose >= TC_STATS)
        tc_log_info(MOD_NAME, "inbuf: %i, bufsize: %lu",
                    frame->audio_size, (unsigned long)pd->resample_bufsize);
    frame->audio_size = audio_resample(pd->resample_ctx,
                                       (int16_t*)pd->resample_buf,
                                       (int16_t*)frame->audio_buf,
                                       frame->audio_size/pd->bytes_per_sample);
    frame->audio_size *= pd->bytes_per_sample;
    if (verbose >= TC_STATS)
        tc_log_info(MOD_NAME, "outbuf: %i", frame->audio_size);

    if (frame->audio_size < 0)
        frame->audio_size = 0;

    ac_memcpy(frame->audio_buf, pd->resample_buf, frame->audio_size);

    return TC_OK;
}


/**************************************************************************/

static const TCCodecID resample_codecs_audio_in[] = { 
    TC_CODEC_PCM, TC_CODEC_ERROR
};
static const TCCodecID resample_codecs_audio_out[] = { 
    TC_CODEC_PCM, TC_CODEC_ERROR
};
TC_MODULE_VIDEO_UNSUPPORTED(resample);
TC_MODULE_FILTER_FORMATS(resample);

TC_MODULE_INFO(resample);

static const TCModuleClass resample_class = {
    TC_MODULE_CLASS_HEAD(resample),

    .init         = resample_init,
    .fini         = resample_fini,
    .configure    = resample_configure,
    .stop         = resample_stop,
    .inspect      = resample_inspect,

    .filter_audio = resample_filter_audio,
};

TC_MODULE_ENTRY_POINT(resample)

/*************************************************************************/

static int resample_get_config(TCModuleInstance *self, char *options)
{
    TC_MODULE_SELF_CHECK(self, "get_config");

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       MOD_AUTHOR, "AE", "1");

    return TC_OK;
}


static int resample_process(TCModuleInstance *self, frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    if (frame->tag & TC_PRE_S_PROCESS && frame->tag & TC_AUDIO) {
        return resample_filter_audio(self, (aframe_list_t*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

TC_FILTER_OLDINTERFACE(resample)

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
