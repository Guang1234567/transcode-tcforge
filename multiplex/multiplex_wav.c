/*
 *  multiplex_wav.c -- pack a pcm stream in WAVE format
 *  (C) 2007-2010 Francesco Romani <fromani at gmail dot com>
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
#include "libtc/ratiocodes.h"

#include "libtcmodule/tcmodule-plugin.h"

#include "avilib/wavlib.h"

#define MOD_NAME    "multiplex_wav.so"
#define MOD_VERSION "v0.1.0 (2009-02-08)"
#define MOD_CAP     "write a WAV audio stream"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_MULTIPLEX|TC_MODULE_FEATURE_AUDIO
    

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE
    
/* XXX */
static const char tc_wav_help[] = ""
    "Overview:\n"
    "    this module writes a pcm stream using WAV format.\n"
    "Options:\n"
    "    help    produce module overview and options explanations\n";

/*
 * PrivateData is missing since we no need anything more than a
 * WAV descriptor.
 */

static int tc_wav_inspect(TCModuleInstance *self,
                          const char *options, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");
    
    if (optstr_lookup(options, "help")) {
        *value = tc_wav_help;
    }

    return TC_OK;
}

static int tc_wav_configure(TCModuleInstance *self,
                            const char *options,
                            TCJob *vob,
                            TCModuleExtraData *xdata[])
{
    TC_MODULE_SELF_CHECK(self, "configure");
    return TC_OK;
}
 
static int tc_wav_stop(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "stop");
    return TC_OK;
}
    

static int tc_wav_open(TCModuleInstance *self, const char *filename,
                       TCModuleExtraData *xdata[])
{
    vob_t *vob = tc_get_vob();
    WAVError err;
    int rate;

    WAV wav = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    wav = wav_open(filename, WAV_WRITE, &err);
    if (!wav) {
        tc_log_error(MOD_NAME, "failed to open audio stream file '%s'"
                               " (reason: %s)", filename,
                               wav_strerror(err));
        return TC_ERROR;
    }

    rate = (vob->mp3frequency != 0) ?vob->mp3frequency :vob->a_rate;
    wav_set_bits(wav, vob->dm_bits);
    wav_set_rate(wav, rate);
    wav_set_bitrate(wav, vob->dm_chan * rate * vob->dm_bits/8);
    wav_set_channels(wav, vob->dm_chan);

    self->userdata = wav;
   
    return TC_OK;
}

static int tc_wav_close(TCModuleInstance *self)
{
    WAV wav = NULL;

    TC_MODULE_SELF_CHECK(self, "close");

    wav = self->userdata;

    if (wav != NULL) {
        int err = wav_close(wav);
        if (err != 0) {
            tc_log_error(MOD_NAME, "closing audio file: %s",
                                   wav_strerror(wav_last_error(wav)));
            return TC_ERROR;
        }
        self->userdata = NULL;
    }

    return TC_OK;
}

static int tc_wav_write_audio(TCModuleInstance *self,
                              TCFrameAudio *aframe)
{
    ssize_t w_aud = 0;
    WAV wav = NULL;

    TC_MODULE_SELF_CHECK(self, "write_audio");

    wav = self->userdata;

    w_aud = wav_write_data(wav, aframe->audio_buf, aframe->audio_len);
    if (w_aud != aframe->audio_len) {
        tc_log_warn(MOD_NAME, "error while writing audio frame: %s",
                              wav_strerror(wav_last_error(wav)));
        return TC_ERROR;
    }

    return (int)w_aud;
}

static int tc_wav_init(TCModuleInstance *self, uint32_t features)
{
    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }

    return TC_OK;
}

static int tc_wav_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    tc_wav_stop(self);

    return TC_OK;
}


/*************************************************************************/

static const TCCodecID tc_wav_codecs_video_in[] = { 
    TC_CODEC_ERROR
};
static const TCCodecID tc_wav_codecs_audio_in[] = { 
    TC_CODEC_PCM, TC_CODEC_ERROR
};
static const TCFormatID tc_wav_formats_out[] = { 
    TC_FORMAT_WAV, TC_FORMAT_ERROR
};
/* a multiplexor is at the end of pipeline */
TC_MODULE_MPLEX_FORMATS_CODECS(tc_wav);

TC_MODULE_INFO(tc_wav);

static const TCModuleClass tc_wav_class = {
    TC_MODULE_CLASS_HEAD(tc_wav),

    .init         = tc_wav_init,
    .fini         = tc_wav_fini,
    .configure    = tc_wav_configure,
    .stop         = tc_wav_stop,
    .inspect      = tc_wav_inspect,

    .open         = tc_wav_open,
    .close        = tc_wav_close,
    .write_audio  = tc_wav_write_audio,
};

TC_MODULE_ENTRY_POINT(tc_wav);

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
