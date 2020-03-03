/*
 * tcmodule-plugin.h -- transcode module system, take two: plugin parts.
 * (C) 2005-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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


#ifndef TCMODULE_PLUGIN_H
#define TCMODULE_PLUGIN_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>

#include "tcmodule-info.h"
#include "tcmodule-data.h"


#define TC_MODULE_SELF_CHECK(self, WHERE) do { \
    if ((self) == NULL) { \
        tc_log_error(MOD_NAME, WHERE ": " # self " is NULL"); \
        return TC_ERROR; /* catch all for filter/encoders/decoders/(de)muxers */ \
    } \
} while (0)


#define TC_HAS_FEATURE(flags, feat) \
    ((flags & (TC_MODULE_FEATURE_ ## feat)) ?1 :0)

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_av_check(uint32_t flags)
{
    int i = 0;

    i += TC_HAS_FEATURE(flags, AUDIO);
    i += TC_HAS_FEATURE(flags, VIDEO);
    i += TC_HAS_FEATURE(flags, EXTRA);

    return i;
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_cap_check(uint32_t flags)
{
    int i = 0;

    i += TC_HAS_FEATURE(flags, DECODE);
    i += TC_HAS_FEATURE(flags, FILTER);
    i += TC_HAS_FEATURE(flags, ENCODE);
    i += TC_HAS_FEATURE(flags, MULTIPLEX);
    i += TC_HAS_FEATURE(flags, DEMULTIPLEX);

    return i;
}

#undef TC_HAS_FEATURE


#define TC_MODULE_INIT_CHECK(self, FEATURES, feat) do { \
    int j = tc_module_cap_check((feat)); \
    \
    if ((!((FEATURES) & TC_MODULE_FEATURE_MULTIPLEX) \
      && !((FEATURES) & TC_MODULE_FEATURE_DEMULTIPLEX)) \
     && (tc_module_av_check((feat)) > 1)) { \
        tc_log_error(MOD_NAME, "unsupported stream types for" \
                           " this module instance"); \
    return TC_ERROR; \
    } \
    \
    if (j != 0 && j != 1) { \
        tc_log_error(MOD_NAME, "feature request mismatch for" \
                           " this module instance (req=%i)", j); \
    return TC_ERROR; \
    } \
    /* is perfectly fine to request to do nothing */ \
    if ((feat != 0) && ((FEATURES) & (feat))) { \
        (self)->features = (feat); \
    } else { \
        tc_log_error(MOD_NAME, "this module does not support" \
                               " requested feature"); \
        return TC_ERROR; \
    } \
} while (0)

/*
 * autogeneration macros for generic init/fini pair
 * looks like generic pair is needed often that expected;
 * In future module system revision, maybe they will be
 * moved into core.
 */

#define TC_MODULE_GENERIC_INIT(MODNAME, MODDATA) \
    static int MODNAME ## _init(TCModuleInstance *self, uint32_t features) \
    { \
        MODDATA *pd = NULL; \
        \
        TC_MODULE_SELF_CHECK(self, "init"); \
        TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features); \
        \
        pd = tc_malloc(sizeof(MODDATA)); \
        if (pd == NULL) { \
            tc_log_error(MOD_NAME, "init: out of memory!"); \
            return TC_ERROR; \
        } \
        \
        self->userdata = pd; \
        \
        if (verbose) { \
            tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP); \
        } \
        \
        return TC_OK; \
    }

#define TC_MODULE_GENERIC_FINI(MODNAME) \
    static int MODNAME ## _fini(TCModuleInstance *self) \
    { \
        TC_MODULE_SELF_CHECK(self, "fini"); \
        \
        tc_free(self->userdata); \
        self->userdata = NULL; \
        return TC_OK; \
    }

/*
 * autogeneration macro for TCModuleInfo descriptor
 */
#define TC_MODULE_INFO(PREFIX) \
static const TCModuleInfo PREFIX ## _info = { \
    .features         = MOD_FEATURES,          \
    .flags            = MOD_FLAGS,             \
    .name             = MOD_NAME,              \
    .version          = MOD_VERSION,           \
    .description      = MOD_CAP,               \
    .codecs_video_in  = PREFIX ## _codecs_video_in,  \
    .codecs_audio_in  = PREFIX ## _codecs_audio_in,  \
    .codecs_video_out = PREFIX ## _codecs_video_out, \
    .codecs_audio_out = PREFIX ## _codecs_audio_out, \
    .formats_in       = PREFIX ## _formats_in, \
    .formats_out      = PREFIX ## _formats_out \
}

/* please note the MISSING trailing comma */
#define TC_MODULE_CLASS_HEAD(PREFIX) \
    .version     = TC_MODULE_VERSION,  \
    .info        = & ( PREFIX ## _info)


/*
 * autogeneration for supported codecs/multiplexors
 */
#define TC_MODULE_FILTER_FORMATS(PREFIX) \
static const TCFormatID PREFIX ## _formats_in[]  = { TC_FORMAT_ERROR }; \
static const TCFormatID PREFIX ## _formats_out[] = { TC_FORMAT_ERROR }

#define TC_MODULE_CODEC_FORMATS(PREFIX) \
static const TCFormatID PREFIX ## _formats_in[]  = { TC_FORMAT_ERROR }; \
static const TCFormatID PREFIX ## _formats_out[] = { TC_FORMAT_ERROR }

#define TC_MODULE_MPLEX_FORMATS_CODECS(PREFIX) \
static const TCCodecID  PREFIX ## _codecs_video_out[] = { TC_CODEC_ERROR }; \
static const TCCodecID  PREFIX ## _codecs_audio_out[] = { TC_CODEC_ERROR }; \
static const TCFormatID PREFIX ## _formats_in[] = { TC_FORMAT_ERROR }

#define TC_MODULE_DEMUX_FORMATS_CODECS(PREFIX) \
static const TCCodecID  PREFIX ## _codecs_video_in[] = { TC_CODEC_ERROR }; \
static const TCCodecID  PREFIX ## _codecs_audio_in[] = { TC_CODEC_ERROR }; \
static const TCFormatID PREFIX ## _formats_out[] = { TC_FORMAT_ERROR }

#define TC_MODULE_VIDEO_UNSUPPORTED(PREFIX) \
static const TCCodecID  PREFIX ## _codecs_video_in[]  = { TC_CODEC_ERROR }; \
static const TCCodecID  PREFIX ## _codecs_video_out[] = { TC_CODEC_ERROR }

#define TC_MODULE_AUDIO_UNSUPPORTED(PREFIX) \
static const TCCodecID  PREFIX ## _codecs_audio_in[]  = { TC_CODEC_ERROR }; \
static const TCCodecID  PREFIX ## _codecs_audio_out[] = { TC_CODEC_ERROR }


/*
 * plugin entry point prototype
 */
const TCModuleClass *tc_plugin_setup(void);

#define TC_MODULE_ENTRY_POINT(MODNAME) \
    extern const TCModuleClass *tc_plugin_setup(void) \
    { \
        return &( MODNAME ## _class); \
    }


/* TODO: unify in a proper way OLDINTERFACE and OLDINTERFACE_M */
#define TC_FILTER_OLDINTERFACE(name) \
    /* Old-fashioned module interface. */ \
    static TCModuleInstance mod; \
    \
    int tc_filter(frame_list_t *frame, char *options) \
    { \
        if (frame->tag & TC_FILTER_INIT) { \
            TCModuleExtraData *xdata[] = { NULL, NULL }; \
            if (name ## _init(&mod, TC_MODULE_FEATURE_FILTER) < 0) { \
                return TC_ERROR; \
            } \
            return name ## _configure(&mod, options, tc_get_vob(), xdata); \
        \
        } else if (frame->tag & TC_FILTER_GET_CONFIG) { \
            return name ## _get_config(&mod, options); \
        \
        } else if (frame->tag & TC_FILTER_CLOSE) { \
            if (name ## _stop(&mod) < 0) { \
                return TC_ERROR; \
            } \
            return name ## _fini(&mod); \
        } \
        \
        return name ## _process(&mod, frame); \
    } 



#define TC_FILTER_OLDINTERFACE_INSTANCES	128

/* FIXME:
 * this uses the filter ID as an index--the ID can grow
 * arbitrarily large, so this needs to be fixed
 */
#define TC_FILTER_OLDINTERFACE_M(name) \
    /* Old-fashioned module interface. */ \
    static TCModuleInstance mods[TC_FILTER_OLDINTERFACE_INSTANCES]; \
    \
    int tc_filter(frame_list_t *frame, char *options) \
    { \
	TCModuleInstance *mod = &mods[frame->filter_id]; \
	\
        if (frame->tag & TC_FILTER_INIT) { \
            TCModuleExtraData *xdata[] = { NULL, NULL }; \
            tc_log_info(MOD_NAME, "instance #%i", frame->filter_id); \
            if (name ## _init(mod, TC_MODULE_FEATURE_FILTER) < 0) { \
                return TC_ERROR; \
            } \
            return name ## _configure(mod, options, tc_get_vob(), xdata); \
        \
        } else if (frame->tag & TC_FILTER_GET_CONFIG) { \
            return name ## _get_config(mod, options); \
        \
        } else if (frame->tag & TC_FILTER_CLOSE) { \
            if (name ## _stop(mod) < 0) { \
                return TC_ERROR; \
            } \
            return name ## _fini(mod); \
        } \
        \
        return name ## _process(mod, frame); \
    } 



#endif /* TCMODULE_PLUGIN_H */
