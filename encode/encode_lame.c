/*
 * encode_lame.c - encode audio frames using lame
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

#include <lame/lame.h>

#define MOD_NAME        "encode_lame.so"
#define MOD_VERSION     "v1.2.1 (2009-02-07)"
#define MOD_CAP         "Encodes audio to MP3 using LAME"
#define MOD_AUTHOR      "Andrew Church"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_ENCODE|TC_MODULE_FEATURE_AUDIO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE


/*************************************************************************/

/* Local data structure: */

typedef struct {
    lame_global_flags *lgf;
    int bps;  /* bytes per sample */
    int channels;
    int flush_flag;
    int need_flush;
} PrivateData;

/*************************************************************************/

/**
 * lame_log_error, lame_log_msg, lame_log_debug:  Internal logging
 * functions for LAME.
 *
 * Parameters:
 *     format: Log message format string.
 *       args: Log message format arguments.
 * Return value:
 *     None.
 */

static void lame_log_error(const char *format, va_list args)
{
    char buf[TC_BUF_MAX];
    tc_vsnprintf(buf, sizeof(buf), format, args);
    tc_log_error(MOD_NAME, "%s", buf);
}

static void lame_log_msg(const char *format, va_list args)
{
    if (verbose >= TC_INFO) {
        char buf[TC_BUF_MAX];
        tc_vsnprintf(buf, sizeof(buf), format, args);
        tc_log_info(MOD_NAME, "%s", buf);
    }
}

static void lame_log_debug(const char *format, va_list args)
{
    if (verbose >= TC_DEBUG) {
        char buf[TC_BUF_MAX];
        tc_vsnprintf(buf, sizeof(buf), format, args);
        tc_log_msg(MOD_NAME, "%s", buf);
    }
}

/*************************************************************************/
/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * lamemod_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.  Note the name of this function--
 * we don't want to conflict with libmp3lame's lame_init().
 */

static int lamemod_init(TCModuleInstance *self, uint32_t features)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    self->userdata = pd = tc_malloc(sizeof(PrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }
    pd->lgf = NULL;

    /* FIXME: shouldn't this test a specific flag? */
    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
        if (verbose >= TC_INFO)
            tc_log_info(MOD_NAME, "Using LAME %s", get_lame_version());
    }
    return TC_OK;
}

/*************************************************************************/

/* FIXME: vbr handling is clumsy */
static int lame_setup_preset(PrivateData *pd,
                             const char *lame_preset, vob_t *vob)
{
    preset_mode preset;
    int fast = 0;

    char *s = strchr(lame_preset, ',');
    if (s) {
        *s++ = 0;
        if (strcmp(s, "fast") == 0)
            fast = 1;
    }

    if (strcmp(lame_preset, "standard") == 0) {
        preset = (fast ? STANDARD_FAST : STANDARD);
        vob->a_vbr = 1;
    } else if (strcmp(lame_preset, "medium") == 0) {
        preset = (fast ? MEDIUM_FAST : MEDIUM);
        vob->a_vbr = 1;
    } else if (strcmp(lame_preset, "extreme") == 0) {
        preset = (fast ? EXTREME_FAST : EXTREME);
        vob->a_vbr = 1;
    } else if (strcmp(lame_preset, "insane") == 0) {
        preset = INSANE;
        vob->a_vbr = 1;
    } else {
        preset = strtol(lame_preset, &s, 10);
        if (*s || preset < 8 || preset > 320) {
            tc_log_error(MOD_NAME, "Invalid preset \"%s\"",
                         lame_preset);
            return TC_ERROR;
        } else {
            vob->a_vbr = 1;
        }
    }
    if (lame_set_preset(pd->lgf, preset) < 0) {
        tc_log_error(MOD_NAME, "lame_set_preset(%d) failed", preset);
        return TC_ERROR;
    }
    return TC_OK;
}


/**
 * lame_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int lame_configure(TCModuleInstance *self,
                          const char *options,
                          TCJob *vob,
                          TCModuleExtraData *xdata[])
{
    char lame_preset[TC_BUF_MIN] = { '\0' };
    PrivateData *pd;
    int samplerate = vob->mp3frequency ? vob->mp3frequency : vob->a_rate;
    int tc_accel = tc_get_session()->acceleration; /* XXX ugly */
    int ret, quality;
    MPEG_mode mode;

    TC_MODULE_SELF_CHECK(self, "configure");
    pd = self->userdata;

    pd->flush_flag = vob->encoder_flush;
    pd->need_flush = TC_FALSE;
    /* Save bytes per sample */
    pd->bps = (vob->dm_chan * vob->dm_bits) / 8;
    /* And audio channels */
    pd->channels = vob->dm_chan;

    /* Create LAME object (freeing any old one that might be left over) */
    if (pd->lgf)
        lame_close(pd->lgf);
    pd->lgf = lame_init();
    if (!pd->lgf) {
        tc_log_error(MOD_NAME, "LAME initialization failed");
        return TC_ERROR;
    }

    /* Set up logging functions (assume no failure) */
    lame_set_errorf(pd->lgf, lame_log_error);
    lame_set_msgf  (pd->lgf, lame_log_msg  );
    lame_set_debugf(pd->lgf, lame_log_debug);

    /* Set up audio parameters */
    if (vob->dm_bits != 16) {
        tc_log_error(MOD_NAME, "Only 16-bit samples supported");
        return TC_ERROR;
    }
    if (lame_set_in_samplerate(pd->lgf, samplerate) < 0) {
        tc_log_error(MOD_NAME, "lame_set_in_samplerate(%d) failed",samplerate);
        return TC_ERROR;
    }
    if (lame_set_num_channels(pd->lgf, pd->channels) < 0) {
        tc_log_error(MOD_NAME, "lame_set_num_channels(%d) failed",
                     pd->channels);
        return TC_ERROR;
    }
    if (lame_set_scale(pd->lgf, vob->volume) < 0) {
        tc_log_error(MOD_NAME, "lame_set_scale(%f) failed", vob->volume);
        return TC_ERROR;
    }
    if (lame_set_bWriteVbrTag(pd->lgf, (vob->a_vbr!=0)) < 0) {
        tc_log_error(MOD_NAME, "lame_set_bWriteVbrTag(%d) failed",
                     (vob->a_vbr!=0));
        return TC_ERROR;
    }
    quality = (int)TC_CLAMP(vob->mp3quality, 0.0, 9.0);
    if (lame_set_quality(pd->lgf, quality) < 0) {
        tc_log_error(MOD_NAME, "lame_set_quality(%d) failed", quality);
        return TC_ERROR;
    }
    switch (vob->mp3mode) {
      case 0: mode = JOINT_STEREO; break;
      case 1: mode = STEREO;       break;
      case 2: mode = MONO;         break;
      default:
        tc_log_warn(MOD_NAME,"Invalid audio mode, defaulting to joint stereo");
        mode = JOINT_STEREO;
        break;
    }
    /* FIXME: add coherency check with given audio channels? */
    if (lame_set_mode(pd->lgf, mode) < 0) {
        tc_log_error(MOD_NAME, "lame_set_mode(%d) failed", mode);
        return TC_ERROR;
    }
    if (lame_set_brate(pd->lgf, vob->mp3bitrate) < 0) {
        tc_log_error(MOD_NAME, "lame_set_brate(%d) failed", vob->mp3bitrate);
        return TC_ERROR;
    }
    /* A bit less ugly preset handling */
    ret = optstr_get(options, "preset", "%[^:]", lame_preset); /* FIXME */
    if (ret > 0) {
        ret = lame_setup_preset(pd, lame_preset, vob);
        if (ret != TC_OK) {
            return ret;
        }
    }
    /* Acceleration setting failures aren't fatal */
    if (lame_set_asm_optimizations(pd->lgf, MMX, (tc_accel&AC_MMX  )?1:0) < 0)
        tc_log_warn(MOD_NAME, "lame_set_asm_optimizations(MMX,%d) failed",
                    (tc_accel&AC_MMX)?1:0);
    if (lame_set_asm_optimizations(pd->lgf, AMD_3DNOW,
                                   (tc_accel&AC_3DNOW)?1:0) < 0)
        tc_log_warn(MOD_NAME, "lame_set_asm_optimizations(3DNOW,%d) failed",
                    (tc_accel&AC_3DNOW)?1:0);
    if (lame_set_asm_optimizations(pd->lgf, SSE, (tc_accel&AC_SSE  )?1:0) < 0)
        tc_log_warn(MOD_NAME, "lame_set_asm_optimizations(SSE,%d) failed",
                    (tc_accel&AC_SSE)?1:0);
    /* FIXME: this function is documented as "for testing only"--should we
     * really expose it to the user? */
    if (optstr_lookup(options, "nobitres")) {
        ret = lame_set_disable_reservoir(pd->lgf, 1);
        if (ret < 0) {
            tc_log_error(MOD_NAME, "lame_set_disable_reservoir(1) failed");
            return TC_ERROR;
        }
    }
    if (lame_set_VBR(pd->lgf, vob->a_vbr ? vbr_default : vbr_off) < 0) {
        tc_log_error(MOD_NAME, "lame_set_VBR(%d) failed",
                     vob->a_vbr ? vbr_default : vbr_off);
        return TC_ERROR;
    }
    if (vob->a_vbr) {
        /* FIXME: we should have a separate VBR quality control */
        if (lame_set_VBR_q(pd->lgf, quality) < 0) {
            tc_log_error(MOD_NAME, "lame_set_VBR_q(%d) failed", quality);
            return TC_ERROR;
        }
    }

    /* Initialize encoder */
    if (lame_init_params(pd->lgf) < 0) {
        tc_log_error(MOD_NAME, "lame_init_params() failed");
        return TC_ERROR;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * lame_inspect:  Return the value of an option in this instance of the
 * module.  See tcmodule-data.h for function details.
 */

static int lame_inspect(TCModuleInstance *self,
                       const char *param, const char **value)
{
    static char buf[TC_BUF_MAX];

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");

    if (optstr_lookup(param, "help")) {
        tc_snprintf(buf, sizeof(buf),
                "Overview:\n"
                "    Encodes audio to MP3 using the LAME library.\n"
                "No options available.\n");
        *value = buf;
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * lame_stop:  Reset this instance of the module.  See tcmodule-data.h for
 * function details.
 */

static int lame_stop(TCModuleInstance *self)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    if (pd->lgf) {
        lame_close(pd->lgf);
        pd->lgf = NULL;
    }

    pd->need_flush = TC_FALSE;

    return TC_OK;
}

/*************************************************************************/

/**
 * lame_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int lame_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");
    
    lame_stop(self);
    tc_free(self->userdata);
    self->userdata = NULL;
    return TC_OK;
}

/*************************************************************************/

/**
 * lame_encode:  Encode a frame of data.  See tcmodule-data.h for
 * function details.
 */

#define LAME_FLUSH_BUFFER_SIZE  7200 /* from lame/lame.h */

static int lame_flush(TCModuleInstance *self, TCFrameAudio *out,
                      int *frame_returned)
{
    PrivateData *pd;
    int res;

    TC_MODULE_SELF_CHECK(self, "flush");
    if (out == NULL) {
        tc_log_error(MOD_NAME, "no output buffer supplied");
        return TC_ERROR;
    }

    pd = self->userdata;

    *frame_returned = 0;

    if (!pd->flush_flag) {
        /* No-flush option given, so don't do anything */
        return TC_OK;
    }
    if (!pd->need_flush) {
        return TC_OK;
    }

    if (out->audio_size < LAME_FLUSH_BUFFER_SIZE) {
        /* paranoia is a virtue */
        tc_log_error(MOD_NAME, "output buffer too small for"
                               " flushing (%i|%i)",
                               out->audio_size,
                               LAME_FLUSH_BUFFER_SIZE);
        return TC_ERROR;
    }

    pd->need_flush = TC_FALSE;

    /*
     * Looks like _nogap should behave better when
     * splitting/rotating output files.
     * Moreover, our streams should'nt contain any ID3 tag,
     * -- FR
     */
    res = lame_encode_flush_nogap(pd->lgf, out->audio_buf, 0);
    if (verbose >= TC_DEBUG) {
        tc_log_info(MOD_NAME, "flushing %d audio bytes", res);
    }
    out->audio_len = res;

    if (res > 0) {
        *frame_returned = 1;
    }
    return TC_OK;
}

static int lame_encode(TCModuleInstance *self,
                       TCFrameAudio *in, TCFrameAudio *out)
{
    PrivateData *pd;
    int res;

    TC_MODULE_SELF_CHECK(self, "encode");
    if (out == NULL) {
        tc_log_error(MOD_NAME, "no output buffer supplied");
        return TC_ERROR;
    }

    pd = self->userdata;

    if (pd->channels == 1) { /* mono */
        res = lame_encode_buffer(pd->lgf,
                                 (short *)(in->audio_buf),
                                 (short *)(in->audio_buf),
                                 in->audio_size / pd->bps,
                                 out->audio_buf,
                                 out->audio_size);
    } else { /* all stereo flavours */
        res = lame_encode_buffer_interleaved(pd->lgf,
                                             (short *)in->audio_buf,
                                             in->audio_size / pd->bps,
                                             out->audio_buf,
                                             out->audio_size);
    }

    if (res < 0) {
        if (verbose >= TC_DEBUG) {
            tc_log_error(MOD_NAME, "lame_encode_buffer_interleaved() failed"
                         " (%d: %s)", res,
                         res==-1 ? "output buffer overflow" :
                         res==-2 ? "out of memory" :
                         res==-3 ? "not initialized" :
                         res==-4 ? "psychoacoustic problems" : "unknown");
        } else {
            tc_log_error(MOD_NAME, "Audio encoding failed!");
        }
        return TC_ERROR;
    }

    out->audio_len = res;
    pd->need_flush = TC_TRUE;
    return TC_OK;
}

/*************************************************************************/

static const TCCodecID lame_codecs_audio_in[] = { 
    TC_CODEC_PCM, TC_CODEC_ERROR 
};
static const TCCodecID lame_codecs_audio_out[] = { 
    TC_CODEC_MP3, TC_CODEC_ERROR 
};
TC_MODULE_VIDEO_UNSUPPORTED(lame);
TC_MODULE_CODEC_FORMATS(lame);

TC_MODULE_INFO(lame);

static const TCModuleClass lame_class = {
    TC_MODULE_CLASS_HEAD(lame),

    .init         = lamemod_init,
    .fini         = lame_fini,
    .configure    = lame_configure,
    .stop         = lame_stop,
    .inspect      = lame_inspect,

    .encode_audio = lame_encode,
    .flush_audio  = lame_flush,
};

TC_MODULE_ENTRY_POINT(lame);

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
