/*
 * import_alsa.c -- module for importing audio through ALSA
 * (C) 2008-2010 - Francesco Romani <fromani at gmail dot com>
 *
 * This file is part of transcode, a video stream processing tool.
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */



#include "src/transcode.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

#include <alsa/asoundlib.h>
#ifdef HAVE_GETTIMEOFDAY
# include <sys/time.h>
# include <time.h>
#endif
#include <string.h>

/*%*
 *%* DESCRIPTION 
 *%*   This module reads audio samples from an ALSA device using libalsa.
 *%*
 *%* BUILD-DEPENDS
 *%*   alsa-lib >= 1.0.0
 *%*
 *%* #DEPENDS
 *%*
 *%* PROCESSING
 *%*   import/demuxer
 *%*
 *%* MEDIA
 *%*   audio
 *%*
 *%* #INPUT
 *%*
 *%* OUTPUT
 *%*   PCM*
 *%*
 *%* OPTION
 *%*   device (string)
 *%*     selects ALSA device to use for capturing audio.
 *%*/

#define LEGACY 1

#ifdef LEGACY
# define MOD_NAME    "import_alsa.so"
#else
# define MOD_NAME    "demultiplex_alsa.so"
#endif

#define MOD_VERSION "v0.0.5 (2007-05-12)"
#define MOD_CAP     "capture audio using ALSA"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_DEMULTIPLEX|TC_MODULE_FEATURE_AUDIO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

static const char tc_alsa_help[] = ""
    "Overview:\n"
    "    This module reads audio samples from an ALSA device using libalsa.\n"
    "Options:\n"
    "    device=dev  selects ALSA device to use\n"
    "    help        produce module overview and options explanations\n";


/*
 * TODO:
 * - device naming fix (this will likely require some core changes)
 * - probing/integration with core
 * - suspend recovery?
 * - smarter resync?
 */

/*************************************************************************/

typedef struct tcalsasource_ TCALSASource;
struct tcalsasource_ {
    snd_pcm_t *pcm;

    uint32_t rate;
    int channels;
    int precision;
};


/*************************************************************************/
/* some support functions shamelessly borrowed^Hinspired from alsa-utils */
/*************************************************************************/

#ifdef HAVE_GETTIMEOFDAY

#define TIMERSUB(a, b, result) do { \
    (result)->tv_sec  = (a)->tv_sec  - (b)->tv_sec; \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
    if ((result)->tv_usec < 0) { \
        --(result)->tv_sec; \
        (result)->tv_usec += 1000000; \
    } \
} while (0)

#endif

#define ALSA_PREPARE(HANDLE) do { \
    int ret = snd_pcm_prepare((HANDLE)->pcm); \
    if (ret < 0) { \
        tc_log_error(MOD_NAME, "ALSA prepare error: %s", snd_strerror(ret)); \
        return TC_ERROR; \
    } \
} while (0)

/* I/O error handler */
static int alsa_source_xrun(TCALSASource *handle)
{
    snd_pcm_status_t *status = NULL;
    snd_pcm_state_t state = 0;
    int ret = 0;

    TC_MODULE_SELF_CHECK(handle, "alsa_source_xrun");
    
    snd_pcm_status_alloca(&status);
    ret = snd_pcm_status(handle->pcm, status);
    if (ret < 0) {
        tc_log_error(__FILE__, "error while fetching status: %s",
                     snd_strerror(ret));
        return TC_ERROR;
    }

    state = snd_pcm_status_get_state(status);

    if (state == SND_PCM_STATE_XRUN) {
#ifdef HAVE_GETTIMEOFDAY
        struct timeval now, diff, tstamp;

        gettimeofday(&now, NULL);
        snd_pcm_status_get_trigger_tstamp(status, &tstamp);
        TIMERSUB(&now, &tstamp, &diff);

        tc_log_warn(__FILE__, "overrun at least %.3f ms long",
                    diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
#else /* ! HAVE_GETTIMEOFDAY */
        tc_log_warn(__FILE__, "overrun");
#endif /* HAVE_GETTIMEOFDAY */
        ALSA_PREPARE(handle);
    } else if (state == SND_PCM_STATE_DRAINING) {
        tc_log_warn(__FILE__, "capture stream format change? attempting recover...");
        ALSA_PREPARE(handle);
    } else { /* catch all */
        tc_log_error(__FILE__, "read error, state = %s", snd_pcm_state_name(state));
        return TC_ERROR;
    }
    return TC_OK;
}


#define RETURN_IF_ALSA_FAIL(RET, MSG) do { \
    if ((RET) < 0) { \
        tc_log_error(__FILE__, "%s (%s)", (MSG), snd_strerror((RET))); \
        return TC_ERROR; \
    } \
} while (0) 

static int tc_alsa_source_open(TCALSASource *handle, const char *dev,
                               uint32_t rate, int precision, int channels)
{
    int ret = 0;
    uint32_t alsa_rate = rate;
    snd_pcm_hw_params_t *hwparams = NULL;

    TC_MODULE_SELF_CHECK(handle, "alsa_source_open");

    /* some basic sanity checks */
    if (!strcmp(dev, "/dev/null") || !strcmp(dev, "/dev/zero")) {
        return TC_OK;
    }

    if (!dev || !strlen(dev)) {
        tc_log_warn(__FILE__, "bad ALSA device");
        return TC_ERROR;
    }
    if (precision != 8 && precision != 16) {
        tc_log_warn(__FILE__, "bits/sample must be 8 or 16");
        return TC_ERROR;
    }

    handle->rate      = rate;
    handle->channels  = channels;
    handle->precision = precision;

    /* ok, time to rock */
    snd_pcm_hw_params_alloca(&(hwparams));
    if (hwparams == NULL) {
        tc_log_warn(__FILE__, "cannot allocate ALSA HW parameters");
        return TC_ERROR;
    }

    tc_log_info(__FILE__, "using PCM capture device: %s", dev);
    ret = snd_pcm_open(&(handle->pcm), dev, SND_PCM_STREAM_CAPTURE, 0);
    if (ret < 0) {
        tc_log_warn(__FILE__, "error opening PCM device %s\n", dev);
        return TC_ERROR;
    }

    ret = snd_pcm_hw_params_any(handle->pcm, hwparams);
    RETURN_IF_ALSA_FAIL(ret, "cannot preconfigure PCM device");

    ret = snd_pcm_hw_params_set_access(handle->pcm, hwparams,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    RETURN_IF_ALSA_FAIL(ret, "cannot setup PCM access");

    ret = snd_pcm_hw_params_set_format(handle->pcm, hwparams,
                                       (precision == 16) ?SND_PCM_FORMAT_S16_LE
                                                         :SND_PCM_FORMAT_S8);
    RETURN_IF_ALSA_FAIL(ret, "cannot setup PCM format");

    ret = snd_pcm_hw_params_set_rate_near(handle->pcm, hwparams, &alsa_rate, 0);
    RETURN_IF_ALSA_FAIL(ret, "cannot setup PCM rate");

    if (rate != alsa_rate) {
        tc_log_warn(__FILE__,
                    "rate %"PRIu32" Hz unsupported by hardware,"
                    " using %"PRIu32" Hz instead",
                    rate, alsa_rate);
    }

    ret = snd_pcm_hw_params_set_channels(handle->pcm, hwparams, channels);
    RETURN_IF_ALSA_FAIL(ret, "cannot setup PCM channels");

    ret = snd_pcm_hw_params(handle->pcm, hwparams);
    RETURN_IF_ALSA_FAIL(ret, "cannot setup hardware parameters");

    tc_log_info(__FILE__, "ALSA audio capture: %"PRIu32" Hz, "
                          "%i bps, %i channels",
                          alsa_rate, precision, channels);

    return TC_OK;
}

/* frame size = sample size (bytes) * sample number (= channels number) */
#define ALSA_FRAME_SIZE(HANDLE) \
    ((HANDLE)->channels * (HANDLE)->precision / 8)


static int tc_alsa_source_grab(TCALSASource *handle, uint8_t *buf,
                               size_t bufsize, size_t *buflen)
{
    snd_pcm_uframes_t frames = bufsize / ALSA_FRAME_SIZE(handle);
    snd_pcm_sframes_t ret = 0;

    TC_MODULE_SELF_CHECK(handle, "alsa_source_grab");
    TC_MODULE_SELF_CHECK(buf, "alsa_source_grab");
    
    ret = snd_pcm_readi(handle->pcm, buf, frames);
    if (ret == -EAGAIN || (ret >= 0 && (snd_pcm_uframes_t)ret < frames)) {
        /* this can really happen? */
        snd_pcm_wait(handle->pcm, -1);
    } else if (ret == -EPIPE) { /* xrun (overrun) */
        return alsa_source_xrun(handle);
    } else if (ret == -ESTRPIPE) { /* suspend */
        tc_log_error(__FILE__, "stream suspended (unrecoverable, yet)");
        return TC_ERROR;
    } else if (ret < 0) {
        tc_log_error(__FILE__, "ALSA read error: %s", snd_strerror(ret));
        return TC_ERROR;
    }

    if (buflen != NULL) {
        *buflen = (size_t)ret;
    }
    return TC_OK;
}

static int tc_alsa_source_close(TCALSASource *handle)
{
    TC_MODULE_SELF_CHECK(handle, "alsa_source_close");

    if (handle->pcm != NULL) {
        snd_pcm_close(handle->pcm);
        handle->pcm = NULL;
    }
    return TC_OK;
}

#undef RETURN_IF_ALSA_FAIL


/* ------------------------------------------------------------
 * New-Style module interface
 * ------------------------------------------------------------*/

typedef struct tcalsaprivatedata_ TCALSAPrivateData;
struct tcalsaprivatedata_ {
    TCALSASource handle;

    char device[256];
};


static int tc_alsa_init(TCModuleInstance *self, uint32_t features)
{
    TCALSAPrivateData *priv = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    priv = tc_zalloc(sizeof(TCALSAPrivateData));
    if (priv == NULL) {
        return TC_ERROR;
    }

    self->userdata = priv;    
    return TC_OK;
}

static int tc_alsa_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    tc_free(self->userdata);
    self->userdata = NULL;

    return TC_OK;
}

static int tc_alsa_configure(TCModuleInstance *self,
                             const char *options,
                             TCJob *vob,
                             TCModuleExtraData *xdata[])
{
    TCALSAPrivateData *priv = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    priv = self->userdata;

    strlcpy(priv->device, "default", sizeof(priv->device));
    if (options != NULL) {
        optstr_get(options, "device", "%255s", priv->device);
        priv->device[256-1] = '\0';
        /* yeah, this is pretty ugly -- FR */
    }
    return TC_OK;
}

static int tc_alsa_open(TCModuleInstance *self,
                        const char *filename,
                        TCModuleExtraData *xdata[])
{
    TCALSAPrivateData *priv = NULL;
    vob_t *vob = tc_get_vob();
    int ret = 0;

    TC_MODULE_SELF_CHECK(self, "configure");

    priv = self->userdata;
 
    /* it would be nice to have some more validation in here */
    ret = tc_alsa_source_open(&(priv->handle), priv->device,
                              vob->a_rate, vob->a_bits, vob->a_chan);
    if (ret != 0) {
        tc_log_error(MOD_NAME, "configure: failed to open ALSA device"
                               "'%s'", priv->device);
        return TC_ERROR;
    }
    return TC_OK;
}

static int tc_alsa_inspect(TCModuleInstance *self,
                          const char *param, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");

    if (optstr_lookup(param, "help")) {
        *value = tc_alsa_help;
    }

    return TC_OK;
}

static int tc_alsa_stop(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "stop");
    return TC_OK;
}

static int tc_alsa_close(TCModuleInstance *self)
{
    TCALSAPrivateData *priv = NULL;
    int ret = 0;

    TC_MODULE_SELF_CHECK(self, "close");

    priv = self->userdata;

    ret = tc_alsa_source_close(&(priv->handle));
    if (ret != TC_OK) {
        tc_log_error(MOD_NAME, "stop: failed to close ALSA device");
        return TC_ERROR;
    }

    return TC_OK;
}

static int tc_alsa_read_audio(TCModuleInstance *self,
                             TCFrameAudio *aframe)
{
    TCALSAPrivateData *priv = NULL;
    int ret = TC_OK;
    size_t len = 0;

    TC_MODULE_SELF_CHECK(self, "read_audio");
    
    priv = self->userdata;

    ret = tc_alsa_source_grab(&(priv->handle), aframe->audio_buf,
                              aframe->audio_size, &len);
    aframe->audio_len = (size_t)len;
    return ret;
}

/*************************************************************************/

static const TCCodecID tc_alsa_codecs_audio_in[] = { TC_CODEC_ERROR };

/* a multiplexor is at the end of pipeline */
static const TCCodecID tc_alsa_codecs_audio_out[] = { 
    TC_CODEC_PCM,
    TC_CODEC_ERROR,
};

static const TCFormatID tc_alsa_formats_in[] = {
    TC_FORMAT_ALSA,
    TC_FORMAT_ERROR, 
};

static const TCFormatID tc_alsa_formats_out[] = { TC_FORMAT_ERROR };
TC_MODULE_VIDEO_UNSUPPORTED(tc_alsa);

TC_MODULE_INFO(tc_alsa);

static const TCModuleClass tc_alsa_class = {
    TC_MODULE_CLASS_HEAD(tc_alsa),

    .init         = tc_alsa_init,
    .fini         = tc_alsa_fini,
    .configure    = tc_alsa_configure,
    .stop         = tc_alsa_stop,
    .inspect      = tc_alsa_inspect,

    .open         = tc_alsa_open,
    .close        = tc_alsa_close,
    .read_audio   = tc_alsa_read_audio,
};

TC_MODULE_ENTRY_POINT(tc_alsa)

/*************************************************************************/

/* ------------------------------------------------------------
 * Old-Style module interface
 * ------------------------------------------------------------*/

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_PCM;

#define MOD_PRE alsa
#define MOD_CODEC   "(audio) pcm"

#include "import_def.h"


static TCALSASource handle = {
    .pcm       = NULL,
    .rate      = RATE,
    .channels  = CHANNELS,
    .precision = BITS,
};


MOD_open
{
    int ret = TC_ERROR;
    char device[1024];

    switch (param->flag) {
      case TC_VIDEO:
        tc_log_warn(MOD_NAME, "unsupported request (init video)");
        break;
      case TC_AUDIO:
        if (verbose_flag & TC_DEBUG) {
            tc_log_info(MOD_NAME, "ALSA audio grabbing");
        }

        strlcpy(device, "default", 1024);
        if (vob->im_a_string != NULL) {
            optstr_get(vob->im_a_string, "device", "%1024s", device);
            device[1024-1] = '\0';
            /* yeah, this too is pretty ugly -- FR */
        }

        ret = tc_alsa_source_open(&handle, device,
                                  vob->a_rate, vob->a_bits, vob->a_chan);
        break;
      default:
        tc_log_warn(MOD_NAME, "unsupported request (init)");
        break;
    }

    return ret;
}


MOD_decode
{
    int ret = TC_ERROR;

    switch (param->flag) {
      case TC_VIDEO:
        tc_log_warn(MOD_NAME, "unsupported request (decode video)");
        break;
      case TC_AUDIO:
        ret = tc_alsa_source_grab(&handle, param->buffer,
                                  param->size, NULL);
        break;
      default:
        tc_log_warn(MOD_NAME, "unsupported request (decode)");
        break;
    }

    return ret;
}


MOD_close
{
    int ret = TC_ERROR;

    switch (param->flag) {
      case TC_VIDEO:
        tc_log_warn(MOD_NAME, "unsupported request (close video)");
        break;
      case TC_AUDIO:
        ret = tc_alsa_source_close(&handle);
        break;
      default:
        tc_log_warn(MOD_NAME, "unsupported request (close)");
        break;
    }

    return ret;
}

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
