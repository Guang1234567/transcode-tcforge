/*
 * demultiplex_x11.c -- extract full-screen images from an X11 connection.
 * (C) 2006-2010 Francesco Romani <fromani at gmail dot com>
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

#include "libtcmodule/tcmodule-plugin.h"
#include "libtcutil/tctimer.h"
#include "libtcutil/optstr.h"

#include "x11source.h"

/*%*
 *%* DESCRIPTION 
 *%*   This module captures video frames from X window system using libX11.
 *%*
 *%* BUILD-DEPENDS
 *%*   libcx11-6 >= 1.0.0
 *%*
 *%* DEPENDS
 *%*   libcx11-6 >= 1.0.0
 *%*
 *%* PROCESSING
 *%*   import/demuxer
 *%*
 *%* MEDIA
 *%*   video
 *%*
 *%* #INPUT
 *%*
 *%* OUTPUT
 *%*   YUV420P, YUV422P, RGB24*
 *%*
 *%* OPTION
 *%*   skew_limit (integer)
 *%*     maximum frame A/V skew (ms) before correction attempt
 *%*/


/*
 * TODO (approx. priority order)
 * - Improve framerate emulation.
 *   It isn't easy without encoder support, and we will not have
 *   any smarter encoder at least until 1.2.0.
 * - Grab cursor.
 * - Make faster where feasible.
 */

#define DEBUG 1

#define LEGACY 1

#ifdef LEGACY
# define MOD_NAME    "import_x11.so"
#else
# define MOD_NAME    "demultiplex_x11.so"
#endif

#define MOD_VERSION "v0.1.0 (2007-07-21)"
#define MOD_CAP     "fetch full-screen frames from an X11 connection"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_DEMULTIPLEX|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

/*************************************************************************/

static const char tc_x11_help[] = ""
    "Overview:\n"
    "    This module acts as a bridge from transcode an a X11 server.\n"
    "    It grabs screenshots at fixed rate from X11 connection, allowing\n"
    "    to record screencast and so on.\n"
    "Options:\n"
    "    skew_limit=N  tune maximum frame skew (ms) before correction\n"
    "    help          produce module overview and options explanations\n";

#define SKEW_LIM_DEFAULT    0
#define SKEW_LIM_MIN        0
#define SKEW_LIM_MAX        5

static const int frame_delay_divs[] = {
/*  div     skew_lim         */
    1,      /* 0 (disabled)  */ 
    2,      /* 1 (weakest)   */
    3,
    5,
    10,
    20      /* 5 (strongest) */
};


typedef struct tcx11privatedata_ TCX11PrivateData;
struct tcx11privatedata_ {
    TCX11Source src;
    TCTimer timer;

    uint64_t frame_delay; 
    /* how much (ms) we must sleep to properly emulate frame rate? */

    uint32_t expired;   /* counter for execessively delayed frames */

    uint64_t reftime;  /* reference time (ms) for skew computation */

    int64_t skew;       /* take in account excess of retard (ms)   */
    int64_t skew_limit; /* how much (ms) skew we can tolerate?     */
    int codec;
};


/*************************************************************************/
/* helpers */

static void tdebug(const TCX11PrivateData *priv, const char *str)
{
#ifdef DEBUG
    uint64_t now = tc_gettime();
    tc_log_info(MOD_NAME, "%-18s %lu", str, (unsigned long)(now - priv->reftime));
#endif
    return;
}

/*************************************************************************/

static int tc_x11_init(TCModuleInstance *self, uint32_t features)
{
    TCX11PrivateData *priv = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    priv = tc_malloc(sizeof(TCX11PrivateData));
    if (priv == NULL) {
        return TC_ERROR;
    }

    self->userdata = priv;    
    return TC_OK;
}

static int tc_x11_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    tc_free(self->userdata);
    self->userdata = NULL;

    return TC_OK;
}

static int tc_x11_configure(TCModuleInstance *self,
                            const char *options,
                            TCJob *vob,
                            TCModuleExtraData *xdata[])
{
    TCX11PrivateData *priv = NULL;
    int ret = 0, skew_lim = SKEW_LIM_DEFAULT;

    TC_MODULE_SELF_CHECK(self, "configure");

    priv = self->userdata;

    if (options != NULL) {
        optstr_get(options, "skew_limit", "%i", &skew_lim);
        if (skew_lim < SKEW_LIM_MIN || skew_lim > SKEW_LIM_MAX) {
            tc_log_warn(MOD_NAME, "skew limit value out of range,"
                                  " reset to defaults [%i]",
                        SKEW_LIM_DEFAULT);
        }
    }

    priv->skew        = 0;
    priv->reftime     = 0;
    priv->expired     = 0;
    priv->frame_delay = (uint64_t)(1000000.0 / vob->fps); /* microseconds */
    priv->skew_limit  = priv->frame_delay / frame_delay_divs[skew_lim];
    priv->codec       = vob->im_v_codec;

    if (verbose >= TC_DEBUG) {
        tc_log_info(MOD_NAME, "frame delay: %lu ms",
                              (unsigned long)priv->frame_delay);
        tc_log_info(MOD_NAME, "skew limit:  %li ms",
                              (long)priv->skew_limit);
    }


    ret = tc_timer_init_soft(&priv->timer, 0);
    if (ret != 0) {
        tc_log_error(MOD_NAME, "configure: can't initialize timer");
        return TC_ERROR;
    }
    return TC_OK;
}

static int tc_x11_open(TCModuleInstance *self, const char *filename,
                       TCModuleExtraData *xdata[])
{
    TCX11PrivateData *priv = NULL;
    int ret = 0;

    TC_MODULE_SELF_CHECK(self, "configure");

    priv = self->userdata;
 
    /* nothing to do here, yet */
    ret = tc_x11source_is_display_name(filename);
    if (ret == TC_FALSE) {
        tc_log_error(MOD_NAME, "configure: given source doesn't look like"
                               " a DISPLAY specifier");
        return TC_ERROR;
    }

    ret = tc_x11source_open(&priv->src, filename,
                            TC_X11_MODE_BEST, priv->codec);
    if (ret != 0) {
        tc_log_error(MOD_NAME, "configure: failed to open X11 connection"
                               " to '%s'", filename);
        return TC_ERROR;
    }

    return TC_OK;
}

static int tc_x11_inspect(TCModuleInstance *self,
                          const char *param, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");

    if (optstr_lookup(param, "help")) {
        *value = tc_x11_help;
    }

    return TC_OK;
}

static int tc_x11_close(TCModuleInstance *self)
{
    TCX11PrivateData *priv = NULL;
    int ret = 0;

    TC_MODULE_SELF_CHECK(self, "close");

    priv = self->userdata;

    ret = tc_x11source_close(&priv->src);
    if (ret != 0) {
        tc_log_error(MOD_NAME, "stop: failed to close X11 connection");
        return TC_ERROR;
    }
    if (verbose >= TC_DEBUG) {
        tc_log_info(MOD_NAME, "expired frames count: %lu",
                              (unsigned long)priv->expired);
    }
    return TC_OK;
}
 
static int tc_x11_stop(TCModuleInstance *self)
{
    TCX11PrivateData *priv = NULL;
    int ret = 0;

    TC_MODULE_SELF_CHECK(self, "stop");

    priv = self->userdata;

    ret = tc_timer_fini(&priv->timer);
    if (ret != 0) {
        tc_log_error(MOD_NAME, "stop: failed to stop timer");
        return TC_ERROR;
    }

   return TC_OK;
}

static int tc_x11_read_video(TCModuleInstance *self,
                             vframe_list_t *vframe)
{
    TCX11PrivateData *priv = NULL;
    uint64_t now = 0;
    int ret = 0;

    TC_MODULE_SELF_CHECK(self, "demultiplex");
    priv = self->userdata;

    priv->reftime = tc_gettime();

    tdebug(priv, "  begin acquire");

    ret = tc_x11source_acquire(&priv->src, vframe->video_buf,
                               vframe->video_size);

    tdebug(priv, "  end acquire");

    if (ret > 0) {
        int64_t naptime = 0;
        uint64_t now = 0;
            
        vframe->attributes |= TC_FRAME_IS_KEYFRAME;
        vframe->video_len = ret;
       
        now = tc_gettime();
        naptime = (priv->frame_delay - (now - priv->reftime));
        if (priv->skew >= priv->skew_limit) {
            tc_log_info(MOD_NAME, "  skew correction (naptime was %lu)", 
                                  (unsigned long)naptime);
            int64_t t = naptime;
            naptime -= priv->skew;
            priv->skew = TC_MAX(0, priv->skew - t);
        }

        if (naptime <= 0) {
            /* don't sleep at all if delay is already excessive */
            tc_log_info(MOD_NAME, "%-18s", "  NO SLEEP!");
            priv->expired++;
        } else {
            tc_log_info(MOD_NAME, "%-18s %lu", "  sleep time",
                                  (unsigned long)(naptime));
            tc_timer_sleep(&priv->timer, (uint64_t)naptime);
        }
    }

    now = tc_gettime();
    now -= priv->reftime;
    priv->skew += now - priv->frame_delay;

    tdebug(priv, "end multiplex");

    tc_log_info(MOD_NAME, "%-18s %li", "detected skew", (long)(priv->skew));
    return (ret > 0) ?ret :-1;
}


/*************************************************************************/

static const TCCodecID tc_x11_codecs_video_in[] = { 
    TC_CODEC_ERROR 
};
/* a demultiplexor is at the beginning of pipeline */
static const TCCodecID tc_x11_codecs_video_out[] = { 
    TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_YUV422P, TC_CODEC_ERROR 
};

static const TCFormatID tc_x11_formats_in[] = { TC_FORMAT_X11, TC_FORMAT_ERROR };
static const TCFormatID tc_x11_formats_out[] = { TC_FORMAT_ERROR };
TC_MODULE_AUDIO_UNSUPPORTED(tc_x11);

TC_MODULE_INFO(tc_x11);

static const TCModuleClass tc_x11_class = {
    TC_MODULE_CLASS_HEAD(tc_x11),

    .init         = tc_x11_init,
    .fini         = tc_x11_fini,
    .configure    = tc_x11_configure,
    .stop         = tc_x11_stop,
    .inspect      = tc_x11_inspect,

    .open         = tc_x11_open,
    .close        = tc_x11_close,
    .read_video   = tc_x11_read_video,
};

TC_MODULE_ENTRY_POINT(tc_x11)

/*************************************************************************/
/*************************************************************************/

/* Old-fashioned module interface. */

static TCModuleInstance mod_video;

static int verbose_flag;
static int capability_flag = TC_CAP_YUV|TC_CAP_RGB|TC_CAP_YUV422|TC_CAP_VID;

#define MOD_PRE x11
#define MOD_CODEC "(video) X11"

#include "import_def.h"

/*************************************************************************/

#define RETURN_IF_FAILED(ret) do { \
    if ((ret) != TC_OK) { \
        return ret; \
    } \
} while (0)

#define COMMON_CHECK(param) do { \
    if ((param)->flag != TC_VIDEO) { \
        return TC_ERROR; \
    } \
} while (0)


MOD_open
{
    TCModuleExtraData *xdata[] = { NULL, NULL };
    int ret;

    COMMON_CHECK(param);

    ret = tc_x11_init(&mod_video, TC_MODULE_FEATURE_DEMULTIPLEX);
    RETURN_IF_FAILED(ret);

    ret = tc_x11_configure(&mod_video, "", vob, xdata);
    RETURN_IF_FAILED(ret);

    ret = tc_x11_open(&mod_video, vob->video_in_file, xdata);
    RETURN_IF_FAILED(ret);

    return TC_OK;
}

MOD_decode
{
    vframe_list_t vframe;
    int ret = 0;

    COMMON_CHECK(param);

    vframe.attributes = 0;
    vframe.video_buf = param->buffer;
    vframe.video_size = param->size;

    ret = tc_x11_read_video(&mod_video, &vframe);

    if (ret <= 0) {
        /* well, frames from X11 never "ends", really :) */
        return TC_ERROR;
    }

    param->size = ret;
    param->attributes = vframe.attributes;
    return TC_OK;
}

MOD_close
{
    int ret;

    COMMON_CHECK(param);

    ret = tc_x11_close(&mod_video);
    RETURN_IF_FAILED(ret);

    ret = tc_x11_stop(&mod_video);
    RETURN_IF_FAILED(ret);

    ret = tc_x11_fini(&mod_video);
    RETURN_IF_FAILED(ret);

    return TC_OK;
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
