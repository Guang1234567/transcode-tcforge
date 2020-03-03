/*
 * encode_faac.c - encode audio frames using FAAC
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "src/transcode.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

#include <faac.h>

#define MOD_NAME    	"encode_faac.so"
#define MOD_VERSION 	"v0.1.1 (2009-02-07)"
#define MOD_CAP         "Encodes audio to AAC using FAAC (currently BROKEN)"
#define MOD_AUTHOR      "Andrew Church"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_ENCODE|TC_MODULE_FEATURE_AUDIO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

/*************************************************************************/

/* Local data structure: */

typedef struct {
    faacEncHandle handle;
    unsigned long framesize;  // samples per AAC frame
    int bps;  // bytes per sample
    /* FAAC only takes complete frames as input, so we buffer as needed. */
    uint8_t *audiobuf;
    int audiobuf_len;  // in samples
    int need_flush;  // 1 if there may be unflushed data
} PrivateData;

/*************************************************************************/
/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * faacmod_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int faac_init(TCModuleInstance *self, uint32_t features)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    self->userdata = pd = tc_malloc(sizeof(PrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }
    pd->handle = 0;
    pd->audiobuf = NULL;
    pd->need_flush = TC_FALSE;

    /* FIXME: shouldn't this test a specific flag? */
    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
        if (verbose >= TC_INFO) {
            char *id, *copyright;
            faacEncGetVersion(&id, &copyright);
            tc_log_info(MOD_NAME, "Using FAAC %s", id);
        }
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * faac_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int faac_configure(TCModuleInstance *self,
                          const char *options,
                          TCJob *vob,
                          TCModuleExtraData *xdata[])
{
    PrivateData *pd;
    int samplerate = vob->mp3frequency ? vob->mp3frequency : vob->a_rate;
    int ret;
    unsigned long dummy;
    faacEncConfiguration conf;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    /* Save bytes per sample */
    pd->bps = (vob->dm_chan * vob->dm_bits) / 8;

    /* Create FAAC handle (freeing any old one that might be left over) */
    if (pd->handle)
        faacEncClose(pd->handle);
    pd->handle = faacEncOpen(samplerate, vob->dm_chan, &pd->framesize, &dummy);
    if (!pd->handle) {
        tc_log_error(MOD_NAME, "FAAC initialization failed");
        return TC_ERROR;
    }

    /* Set up our default audio parameters */
    /* why can't just use a pointer here? -- FR */
    /* Because the function returns a pointer to an internal buffer  --AC */
    conf = *faacEncGetCurrentConfiguration(pd->handle);
    conf.mpegVersion = MPEG4;
    conf.aacObjectType = MAIN;
    conf.allowMidside = 1;
    conf.useLfe = 0;
    conf.useTns = 1;
    conf.bitRate = vob->mp3bitrate / vob->dm_chan;
    conf.bandWidth = 0;  // automatic configuration
    conf.quantqual = 100;  // FIXME: quality should be a per-module setting
    conf.outputFormat = 1;
    if (vob->dm_bits != 16) {
        tc_log_error(MOD_NAME, "Only 16-bit samples supported");
        return TC_ERROR;
    }
    conf.inputFormat = FAAC_INPUT_16BIT;
    conf.shortctl = SHORTCTL_NORMAL;

    ret = optstr_get(options, "quality", "%li", &conf.quantqual);
    if (ret >= 0) {
        if (verbose >= TC_INFO) {
            tc_log_info(MOD_NAME, "using quality=%li", conf.quantqual);
        }
    }

    if (!faacEncSetConfiguration(pd->handle, &conf)) {
        tc_log_error(MOD_NAME, "Failed to set FAAC configuration");
        faacEncClose(pd->handle);
        pd->handle = 0;
        return TC_ERROR;
    }

    /* Allocate local audio buffer */
    if (pd->audiobuf)
        free(pd->audiobuf);
    pd->audiobuf = tc_malloc(pd->framesize * pd->bps);
    if (!pd->audiobuf) {
        tc_log_error(MOD_NAME, "Unable to allocate audio buffer");
        faacEncClose(pd->handle);
        pd->handle = 0;
        return TC_ERROR;
    }

    pd->need_flush = TC_FALSE;

    return TC_OK;
}

/*************************************************************************/

/**
 * faac_inspect:  Return the value of an option in this instance of the
 * module.  See tcmodule-data.h for function details.
 */

static int faac_inspect(TCModuleInstance *self,
                       const char *param, const char **value)
{
    static char buf[TC_BUF_MAX];

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");

    if (optstr_lookup(param, "help")) {
        tc_snprintf(buf, sizeof(buf),
                "Overview:\n"
                "    Encodes audio to AAC using the FAAC library.\n"
                "Options:\n"
                "    quality: set encoder quality [0-100]\n");
        *value = buf;
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * faac_stop:  Reset this instance of the module.  See tcmodule-data.h for
 * function details.
 */

static int faac_stop(TCModuleInstance *self)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    if (pd->handle) {
        faacEncClose(pd->handle);
        pd->handle = NULL;
    }
    pd->need_flush = TC_FALSE;

    return TC_OK;
}

/*************************************************************************/

/**
 * faac_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int faac_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    faac_stop(self);
    tc_free(self->userdata);
    self->userdata = NULL;
    return TC_OK;
}

/*************************************************************************/

/**
 * faac_encode:  Encode a frame of data.  See tcmodule-data.h for
 * function details.
 */

static int faac_encode(TCModuleInstance *self,
                       TCFrameAudio *in, TCFrameAudio *out)
{
    PrivateData *pd;
    uint8_t *inptr;
    int nsamples;

    TC_MODULE_SELF_CHECK(self, "encode");

    pd = self->userdata;

    if (in) {
        inptr = in->audio_buf;
        nsamples = in->audio_size / pd->bps;
    } else {
        inptr = NULL;
        nsamples = 0;
    }
    out->audio_len = 0;

    while (pd->audiobuf_len + nsamples >= pd->framesize) {
        int res;
        const int tocopy = (pd->framesize - pd->audiobuf_len) * pd->bps;
        ac_memcpy(pd->audiobuf + pd->audiobuf_len*pd->bps, inptr, tocopy);
        inptr += tocopy;
        nsamples -= tocopy / pd->bps;
        pd->audiobuf_len = 0;
        res = faacEncEncode(pd->handle, (int32_t *)pd->audiobuf, pd->framesize,
                            out->audio_buf + out->audio_len,
                            out->audio_size - out->audio_len);
        if (res > out->audio_size - out->audio_len) {
            tc_log_error(MOD_NAME,
                         "Output buffer overflow!  Try a lower bitrate.");
            return TC_ERROR;
        }
        out->audio_len += res;
    }

    if (nsamples > 0) {
        ac_memcpy(pd->audiobuf + pd->audiobuf_len*pd->bps, inptr,
                  nsamples*pd->bps);
        pd->audiobuf_len += nsamples;
    }
    pd->need_flush = TC_TRUE;
    return TC_OK;
}

/* FIXME: redo it better */
static int faac_flush(TCModuleInstance *self, TCFrameAudio *frame,
                      int *frame_returned)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "flush");

    pd = self->userdata;

    *frame_returned = 0;
    if (pd->need_flush) {
        pd->need_flush = TC_FALSE;
        if (TC_OK == faac_encode(self, NULL, frame)) {
            *frame_returned = 1;
        } else {
            return TC_ERROR;
        }
    }
    return TC_OK;
}

/*************************************************************************/

static const TCCodecID faac_codecs_audio_in[] = { 
    TC_CODEC_PCM, TC_CODEC_ERROR
};
static const TCCodecID faac_codecs_audio_out[] = { 
    TC_CODEC_AAC, TC_CODEC_ERROR 
};
TC_MODULE_VIDEO_UNSUPPORTED(faac);
TC_MODULE_CODEC_FORMATS(faac);

TC_MODULE_INFO(faac);

static const TCModuleClass faac_class = {
    TC_MODULE_CLASS_HEAD(faac),

    .init         = faac_init,
    .fini         = faac_fini,
    .configure    = faac_configure,
    .stop         = faac_stop,
    .inspect      = faac_inspect,

    .encode_audio = faac_encode,
    .flush_audio  = faac_flush,
};

TC_MODULE_ENTRY_POINT(faac);

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
