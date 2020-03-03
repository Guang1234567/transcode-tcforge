/*
 *  import_framegen.c -- generate an infinite stream of synthetic frames
 *                       for testing purposes.
 *  (C) 2009-2010 Francesco Romani <fromani at gmail dot com>
 *  using some video frame filling code which is
 *  Copyright (C) Thomas Oestreich - June 2001
 *  some code derived by alsa-utils/speakertest, which is
 *  Copyright (C) 2005 Nathan Hurst
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

/*%*
 *%* DESCRIPTION 
 *%*   This module produces an infinite stream of raw synthetic frames.
 *%*   It is intended for testing purposes.
 *%*
 *%* #BUILD-DEPENDS
 *%*
 *%* #DEPENDS
 *%*
 *%* PROCESSING
 *%*   import/demuxer
 *%*
 *%* MEDIA
 *%*   video, audio
 *%*
 *%* #INPUT
 *%*
 *%* OUTPUT
 *%*   YUV420P, PCM
 *%*
 *%* #OPTION
 *%*/

/*
 * TODO (unordered):
 * - add more generators (at least a video/RGB one)
 * - review internal generator API
 * -- to emit cooked frames instead of raw bytes it is better?
 * -- need to explicitely separate A/V generators?
 * -- need to distinguish the function to call for A/V?
 * - accepts (and use!) random seed as module options
 */

#include "src/transcode.h"
#include "aclib/imgconvert.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

#include "pink.h"

#include "config.h"

#define LEGACY 1

#ifdef LEGACY
# define MOD_NAME    "import_framegen.so"
#else
# define MOD_NAME    "demultiplex_framegen.so"
#endif


#define MOD_VERSION "v0.1.0 (2009-06-21)"
#define MOD_CAP     "generate stream of testframes"
#define MOD_AUTHOR  "Francesco Romani"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_DEMULTIPLEX|TC_MODULE_FEATURE_AUDIO|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

/*************************************************************************/

typedef struct tcframegensource_ TCFrameGenSource;

struct tcframegensource_ {
    void        *priv;
    const char  *name;
    const char  *media;

    int32_t     seed;
    
    int (*get_data)(TCFrameGenSource *handle,
                    uint8_t *data, int maxdata, int *datalen);

    int (*close)(TCFrameGenSource *handle);
};

static int tc_framegen_source_get_data(TCFrameGenSource *handle,
                                       uint8_t *data, int maxdata, 
                                       int *datalen)
{
    return handle->get_data(handle, data, maxdata, datalen);
}

static int tc_framegen_source_close(TCFrameGenSource *handle)
{
    return handle->close(handle);
}

static int framegen_generic_close(TCFrameGenSource *handle)
{
    tc_free(handle);
    return TC_OK;
}

/*************************************************************************/

typedef struct pinknoisedata_ PinkNoiseData;
struct pinknoisedata_ {
    pink_noise_t    pink;
};

/* only signed 16 bits LE samples supported so far */
static int framegen_pink_noise_get_data(TCFrameGenSource *handle,
                                        uint8_t *data, int maxdata, int *datalen)
{
    PinkNoiseData *PN = handle->priv;
    int16_t *samples = (int16_t*)data;
    int i;

    for (i = 0; i < maxdata; i++) {
        int32_t res = generate_pink_noise_sample(&(PN->pink)) * 0x03fffffff; /* Don't use MAX volume */
        samples[i]  = res >> 16;
    }

    return TC_OK;
}

static int framegen_pink_noise_init(PinkNoiseData *PN, TCJob *vob)
{
    int ret = TC_ERROR;
    if (vob->a_bits == 16) {
        initialize_pink_noise(&(PN->pink), 16); 
        /* this doesn't depend to the sample size */

        ret = TC_OK;
    }
    return ret;
}

static TCFrameGenSource *tc_framegen_source_open_audio_pink_noise(TCJob *vob,
                                                                  int32_t seed)
{
    void *p = tc_zalloc(sizeof(TCFrameGenSource) + sizeof(PinkNoiseData));
    TCFrameGenSource *FG = p;
    PinkNoiseData *PN = (PinkNoiseData*)((uint8_t*)p + sizeof(TCFrameGenSource));
    if (FG) {
        int ret = framegen_pink_noise_init(PN, vob);
        if (ret != TC_OK) {
            tc_free(p);
            FG = NULL;
            PN = NULL;
        } else {
            FG->priv        = PN;
            FG->name        = "pink noise generator";
            FG->media       = "audio";

            FG->get_data    = framegen_pink_noise_get_data;
            FG->close       = framegen_generic_close;
        }
    }
    return FG;
}

/*************************************************************************/

typedef struct colorwavedata_ ColorWaveData;
struct colorwavedata_ {
    int         width;
    int         height;
    int         index;
};

static int framegen_color_wave_get_data(TCFrameGenSource *handle,
                                        uint8_t *data, int maxdata, int *datalen)

{
    ColorWaveData *CW = handle->priv;
    uint8_t *planes[3] = { NULL, NULL, NULL };
    int size = CW->width * CW->height * 3 / 2;
    int x, y;

    if (maxdata < size) {
        return TC_ERROR;
    }

    YUV_INIT_PLANES(planes, data, IMG_YUV_DEFAULT, CW->width, CW->height);

    memset(data, 0x80, size); /* FIXME */

    for (y = 0; y < CW->height; y++) {
        for (x = 0; x < CW->width; x++) {
            planes[0][y * CW->width + x] = x + y + CW->index * 3;
        }
    }

	for (y = 0; y < CW->height/2; y++) {
        for (x = 0; x < CW->width/2; x++) {
            planes[1][y * CW->width/2 + x] = 128 + y + CW->index * 2;
            planes[2][y * CW->width/2 + x] = 64  + x + CW->index * 5;
         }
    }
    
    CW->index++;
    *datalen = size;

    return TC_OK;
}

static int framegen_color_wave_init(ColorWaveData *CW, TCJob *vob)
{
    int ret = TC_ERROR;
    if (vob->im_v_codec == TC_CODEC_YUV420P) {
        CW->index   = 0;
        CW->width   = vob->im_v_width;
        CW->height  = vob->im_v_height;
        ret         = TC_OK;
    }
    return ret;
}

static TCFrameGenSource *tc_framegen_source_open_video_color_wave(TCJob *vob,
                                                                  int32_t seed)
{
    void *p = tc_zalloc(sizeof(TCFrameGenSource) + sizeof(ColorWaveData));
    TCFrameGenSource *FG = p;
    ColorWaveData *CW = (ColorWaveData*)((uint8_t*)p + sizeof(TCFrameGenSource));
    if (FG) {
        int ret = framegen_color_wave_init(CW, vob);
        if (ret != TC_OK) {
            tc_free(p);
            FG = NULL;
            CW = NULL;
        } else {
            FG->priv        = CW;
            FG->name        = "color wave generator";
            FG->media       = "video";

            FG->get_data    = framegen_color_wave_get_data;
            FG->close       = framegen_generic_close;
        }
    }
    return FG;
}

/*************************************************************************/

#define RETURN_IF_FAILED(RET, MSG) do { \
    if ((RET) < 0) { \
        tc_log_error(MOD_NAME, "%s", (MSG)); \
        return TC_ERROR; \
    } \
} while (0)

#define RETURN_IF_ERROR(RET, MSG) do { \
    if ((RET) != TC_OK) { \
        tc_log_error(MOD_NAME, "%s", (MSG)); \
        return RET; \
    } \
} while (0)

#define RETURN_IF_NULL(PTR, MSG) do { \
    if (!(PTR)) { \
        tc_log_error(MOD_NAME, "%s", (MSG)); \
        return TC_ERROR; \
    } \
} while (0)


/*************************************************************************/
/* New-Style module interface                                            */

static const char tc_framegen_help[] = ""
    "Overview:\n"
    "    This module reads audio samples from an ALSA device using libalsa.\n"
    "Options:\n"
    "    device=dev  selects ALSA device to use\n"
    "    help        produce module overview and options explanations\n";



typedef struct tcframegenprivatedata_ TCFrameGenPrivateData;
struct tcframegenprivatedata_ {
    TCFrameGenSource *video_gen;
    TCFrameGenSource *audio_gen;
};


TC_MODULE_GENERIC_INIT(tc_framegen, TCFrameGenPrivateData)

TC_MODULE_GENERIC_FINI(tc_framegen)

static int tc_framegen_configure(TCModuleInstance *self,
                                 const char *options,
                                 TCJob *vob,
                                 TCModuleExtraData *xdata[])
{
    TCFrameGenPrivateData *priv = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    priv = self->userdata;

    /* FIXME: add proper option handling/mode selection */

    priv->video_gen = tc_framegen_source_open_video_color_wave(vob, 0);
    RETURN_IF_NULL((priv->video_gen),
                   "configure: failed to open the video frame generator");

    priv->audio_gen = tc_framegen_source_open_audio_pink_noise(vob, 0);
    RETURN_IF_NULL((priv->audio_gen),
                   "configure: failed to open the audio frame generator");

    return TC_OK;
}

static int tc_framegen_inspect(TCModuleInstance *self,
                               const char *param, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");

    if (optstr_lookup(param, "help")) {
        *value = tc_framegen_help;
    }

    return TC_OK;
}

static int tc_framegen_stop(TCModuleInstance *self)
{
    TCFrameGenPrivateData *priv = NULL;
    int ret = 0;

    TC_MODULE_SELF_CHECK(self, "stop");

    priv = self->userdata;

    ret = tc_framegen_source_close(priv->video_gen);
    RETURN_IF_ERROR(ret, "stop: failed to close the video frame generator");

    ret = tc_framegen_source_close(priv->audio_gen);
    RETURN_IF_ERROR(ret, "stop: failed to close the audio frame generator");

    return TC_OK;
}

static int tc_framegen_read_video(TCModuleInstance *self,
                                  TCFrameVideo *frame)
{
    int ret;
    TCFrameGenPrivateData *priv = NULL;

    TC_MODULE_SELF_CHECK(self, "read_video");
    
    priv = self->userdata;

    ret = tc_framegen_source_get_data(priv->video_gen,
                                      frame->video_buf,
                                      frame->video_size,
                                      &frame->video_len);
    RETURN_IF_FAILED(ret, "demux: failed to pull a new video frame");
    return frame->video_len;
}

static int tc_framegen_read_audio(TCModuleInstance *self,
                                  TCFrameAudio *frame)
{
    int ret;
    TCFrameGenPrivateData *priv = NULL;

    TC_MODULE_SELF_CHECK(self, "read_audio");
    
    priv = self->userdata;

    ret = tc_framegen_source_get_data(priv->audio_gen,
                                      frame->audio_buf,
                                      frame->audio_size,
                                      &frame->audio_len);
    RETURN_IF_ERROR(ret, "demux: failed to pull a new audio frame");
    return frame->audio_len;
}

/*************************************************************************/

/* a demultiplexor is at the begin of the pipeline */
TC_MODULE_DEMUX_FORMATS_CODECS(tc_framegen);

static const TCCodecID tc_framegen_codecs_video_out[] = { 
    TC_CODEC_YUV420P, TC_CODEC_ERROR,
};

static const TCCodecID tc_framegen_codecs_audio_out[] = { 
    TC_CODEC_PCM, TC_CODEC_ERROR,
};

static const TCFormatID tc_framegen_formats_in[] = {
    TC_FORMAT_ERROR, 
};

TC_MODULE_INFO(tc_framegen);

static const TCModuleClass tc_framegen_class = {
    TC_MODULE_CLASS_HEAD(tc_framegen),

    .init         = tc_framegen_init,
    .fini         = tc_framegen_fini,
    .configure    = tc_framegen_configure,
    .stop         = tc_framegen_stop,
    .inspect      = tc_framegen_inspect,

    .read_video   = tc_framegen_read_video,
    .read_audio   = tc_framegen_read_audio,
};

TC_MODULE_ENTRY_POINT(tc_framegen)

/*************************************************************************/
/* Old-Style module interface                                            */
/*************************************************************************/


static TCFrameGenPrivateData mod_framegen;

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_YUV|TC_CAP_PCM;

#define MOD_PRE     framegen
#define MOD_CODEC   "(video) YUV | (audio) PCM"

#include "import_def.h"

/*************************************************************************/

#define RETURN_WITH_MSG(RET, MSG) do { \
    if ((RET) != TC_OK) { \
        tc_log_error(MOD_NAME, "%s", (MSG)); \
        return RET; \
    } \
    return TC_OK; \
} while (0)



MOD_open
{
    if(param->flag == TC_AUDIO) {
        param->fd = NULL;
        mod_framegen.audio_gen = tc_framegen_source_open_audio_pink_noise(vob, 0);

        RETURN_IF_NULL((mod_framegen.audio_gen),
                        "MOD_open: failed to open the audio frame generator");
        return TC_OK;
    }

    if(param->flag == TC_VIDEO) {
        param->fd = NULL;
        mod_framegen.video_gen = tc_framegen_source_open_video_color_wave(vob, 0);

        RETURN_IF_NULL((mod_framegen.video_gen),
                       "configure: failed to open the video frame generator");
        return TC_OK;
    }

    return TC_ERROR;
}

MOD_decode
{
    int ret;

   if (param->flag == TC_AUDIO) {
        ret = tc_framegen_source_get_data(mod_framegen.audio_gen,
                                          param->buffer,
                                          param->size,
                                          &param->size);
        RETURN_WITH_MSG(ret, "MOD_decode: failed to pull a new audio frame");
    }

    if(param->flag == TC_VIDEO) {
        ret = tc_framegen_source_get_data(mod_framegen.video_gen,
                                          param->buffer,
                                          param->size,
                                          &param->size);
        RETURN_WITH_MSG(ret, "MOD_decode: failed to pull a new video frame");
    }

    return TC_ERROR;
}

MOD_close
{
    int ret;

    if(param->flag == TC_AUDIO) {
        ret = tc_framegen_source_close(mod_framegen.audio_gen);
        RETURN_WITH_MSG(ret,
                        "MOD_close: failed to close the audio frame generator");
    }

    if(param->flag == TC_VIDEO) {
        ret = tc_framegen_source_close(mod_framegen.video_gen);
        RETURN_WITH_MSG(ret,
                        "MOD_close: failed to close the video frame generator");
    }

    return TC_ERROR;
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

