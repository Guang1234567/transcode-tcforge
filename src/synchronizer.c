/*
 * synchronizer.c -- transcode A/V synchronization code - implementation
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

/*
 * HUGE TODO: find a way to merge this code with encoder-buffer.c
 * in a smart, clean way.
 * It would be also nice to better integrate this code with framebuffer.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "tccore/tc_defaults.h"
#include "aclib/ac.h"

#include "synchronizer.h"

#define TC_SYNC_ARG_CHECK(F) do { \
    if (!sy || !filler || !(F)) { \
        return TC_ERROR; \
    } \
} while (0)

/*************************************************************************/

/*
 * Synchronizer methods
 * --------------------
 *
 * A `synchro(nizer) method' is a real backend function implementing the
 * A/V resync code. They are selected when the synchro engine is initialized
 * by the init function. Their common interface is the following:
 *
 * Parameters:
 *      sy: pointer to a TCSynchronizer structure holding the synchro engine
 *          context and common data.
 *  {a,v}f: pointer to a TC{Audio,Video}Frame structure to be filled with
 *          frame data.
 *  filler: filler function to be called to get raw(= potentially out of 
 *          sync) frames from the demuxer.
 *     ctx: opaque data to be passed to the filler function.
 *
 * Return Value:
 *     TC_OK if succesfull, 
 *     TC_ERROR otherwise.
 */

/*************************************************************************/

typedef struct tcsynchronizer_ TCSynchronizer;
struct tcsynchronizer_ {
    const char *method_name;

    int verbose;
    int audio_shift; /* see vob->sync; all methods must support this. */

    void *privdata;

    /* synchro methods */
    int (*get_video)(TCSynchronizer *sy, TCFrameVideo *vf,
                     TCFillFrameVideo filler, void *ctx);
    int (*get_audio)(TCSynchronizer *sy, TCFrameAudio *af,
                     TCFillFrameAudio filler, void *ctx);
    int (*fini)(TCSynchronizer *sy);
};

/*************************************************************************/

/*
 * tc_sync_audio_shift:
 *     (Synchro metod)
 *
 *     attempt to resyncronize A/V tracks by correncting the initial
 *     delay. This isn't a very strong nor proven effective algorhytm,
 *     and it is maintained here mostly for backward compatibility.
 *
 *     So, ALL synchro methods has to call this function to be
 *     backward compatible.
 */
static int tc_sync_audio_shift(TCSynchronizer *sy, TCFrameAudio *af,
                               TCFillFrameAudio filler, void *ctx)
{
    int i;

    /* add silence if needed */
    if (sy->audio_shift < 0) {
        tc_blank_audio_frame(af);
        sy->audio_shift++;
    }
    /* drop frames if needed */
    for (i = 0; i < sy->audio_shift; i++) {
        filler(ctx, af);
    }
    sy->audio_shift = 0;

    return TC_OK;
}

/*************************************************************************/

/*
 * None synchro method: just call the filler once and happily exit.
 */

static int tc_sync_none_get_video(TCSynchronizer *sy, TCFrameVideo *vf,
                                  TCFillFrameVideo filler, void *ctx)
{
    TC_SYNC_ARG_CHECK(vf);
    return filler(ctx, vf);
}

static int tc_sync_none_get_audio(TCSynchronizer *sy, TCFrameAudio *af,
                                  TCFillFrameAudio filler, void *ctx)
{
    TC_SYNC_ARG_CHECK(af);
    tc_sync_audio_shift(sy, af, filler, ctx);
    return filler(ctx, af);
}

static int tc_sync_none_fini(TCSynchronizer *sy)
{
    return TC_OK;
}

static int tc_sync_none_init(TCSynchronizer *sy, vob_t *vob, int master)
{
    sy->method_name = "none";
    sy->audio_shift = vob->sync;
    sy->privdata    = NULL;
    sy->get_video   = tc_sync_none_get_video;
    sy->get_audio   = tc_sync_none_get_audio;
    sy->fini        = tc_sync_none_fini;
    return TC_OK;
}

/*************************************************************************/

/*
 * Adjust synchro metod:
 * FIXME: writeme
 */

typedef enum { 
    AdjustNone = 0,
    AdjustClone,
    AdjustDrop,
} AdjustOperation;

typedef struct adjustcontext_ AdjustContext;
struct adjustcontext_ {
    const char *method_name;
    AdjustOperation op;     /* what to do next? */

    int frames_margin;      /* max drift allowed */
    int frames_interval;    /* how often should I check? */
   
    int video_counter;
    int audio_counter;
    
    int video_cloned;
    int video_dropped;

    TCFrameVideo *saved;    /* in order to support cloning */
};

static int adjust_clone(AdjustContext *ctx, TCFrameVideo *vf)
{
    if (!ctx->video_cloned) {
        tc_blank_video_frame(vf);
    } else {
        if (vf->video_size != ctx->saved->video_size) {
            tc_log_error(__FILE__, "(%s) WRITEME!!",
                         ctx->method_name);
            return TC_ERROR;
        }
        ac_memcpy(vf->video_buf, ctx->saved->video_buf, vf->video_size);
    }
    return TC_OK;
}

static int adjust_save(AdjustContext *ctx, TCFrameVideo *vf)
{
   if (vf->video_size != ctx->saved->video_size) {
        tc_log_error(__FILE__, "(%s) WRITEME!!",
                     ctx->method_name);
        return TC_ERROR;
    }
    ac_memcpy(ctx->saved->video_buf, vf->video_buf, ctx->saved->video_size);
    return TC_OK;
}

static void adjust_print(AdjustContext *ctx, int verbose)
{
    if (ctx->op != AdjustNone  && (verbose >= TC_INFO)) {
        tc_log_info(__FILE__, "(%s) OP: %s VS/AS: %d/%d C/D: %d/%d",
                    ctx->method_name,
                    (ctx->op != AdjustDrop) ?"drop" :"clone",
                    ctx->video_counter, ctx->audio_counter,
                    ctx->video_cloned, ctx->video_dropped);
    }
}

static int tc_sync_adjust_get_video(TCSynchronizer *sy, TCFrameVideo *vf,
                                    TCFillFrameVideo filler, void *ud)
{
    int ret;
    AdjustContext *ctx = NULL;
    TC_SYNC_ARG_CHECK(vf);
    ctx = sy->privdata;

    switch (ctx->op) {
        case AdjustClone:
            ret = adjust_clone(ctx, vf);
            break;
        case AdjustDrop: /* discard frame by overwriting it */
            ret = filler(ud, vf); /* discard frame */
            /* fallthrough */
        case AdjustNone:
            ret = filler(ud, vf);
    }
    ctx->op = AdjustNone;

    if (ctx->frames_margin != 0 && (ctx->video_counter != 0 && ctx->audio_counter != 0)
     && ((ctx->frames_interval == 0) || (ctx->video_counter % ctx->frames_interval) == 0)) {
        if (abs(ctx->audio_counter - ctx->video_counter) > ctx->frames_margin) {
            if (ctx->audio_counter > ctx->video_counter) {
                adjust_save(ctx, vf);
                ctx->video_cloned++;
                ctx->op = AdjustClone;
            } else {
                ctx->op = AdjustDrop;
                ctx->video_dropped++;
            }
        }
        adjust_print(ctx, sy->verbose);
    }

    ctx->video_counter++;
    return TC_OK;
}

static int tc_sync_adjust_get_audio(TCSynchronizer *sy, TCFrameAudio *af,
                                    TCFillFrameAudio filler, void *ud)
{
    AdjustContext *ctx = NULL;
    TC_SYNC_ARG_CHECK(af);
    ctx = sy->privdata;
    tc_sync_audio_shift(sy, af, filler, ud);
    ctx->audio_counter++;
    return filler(ud, af);
}

static int tc_sync_adjust_fini(TCSynchronizer *sy)
{
    if (sy) {
        AdjustContext *ctx = sy->privdata;
        if (ctx) {
            adjust_print(ctx, TC_INFO); /* last summary */

            if (ctx->saved) {
                tc_del_video_frame(ctx->saved);
                ctx->saved = NULL;
            }
            tc_free(ctx);
            sy->privdata = NULL;
        }
    }
    return TC_OK;
}

static int tc_sync_adjust_init(TCSynchronizer *sy, vob_t *vob, int master)
{
    AdjustContext *ctx = NULL;
    
    if (master != TC_AUDIO) {
        tc_log_error(__FILE__, 
                     "(adjust) only audio master source supported yet");
        /* can't yet use method_name */
        return TC_ERROR;
    }
    
    ctx = tc_zalloc(sizeof(AdjustContext));
    if (!ctx) {
        goto no_context;
    }
    ctx->saved = tc_new_video_frame(vob->im_v_width, vob->im_v_height,
                                    vob->im_v_codec, 0);
    if (!ctx->saved) {
        goto no_frame;
    }
    ctx->method_name     = "adjust";
    ctx->op              = AdjustNone;
    ctx->frames_margin   = vob->resync_frame_margin;
    ctx->frames_interval = vob->resync_frame_interval;
    /* vertical alignement intentionally changed */
    sy->method_name = ctx->method_name; /* let's recycle some bytes */
    sy->privdata    = ctx;
    sy->audio_shift = vob->sync;
    sy->verbose     = vob->verbose;
    sy->get_video   = tc_sync_adjust_get_video;
    sy->get_audio   = tc_sync_adjust_get_audio;
    sy->fini        = tc_sync_adjust_fini;

    tc_log_info(__FILE__, "(%s) resync frames: interval=%i/margin=%i",
                sy->method_name,
                ctx->frames_interval, ctx->frames_margin);
    return TC_OK;

no_frame:
    tc_free(ctx);
no_context:
    return TC_ERROR;
}



/*************************************************************************/

typedef struct tcsyncmethod_ TCSyncMethod;
struct tcsyncmethod_ {
    TCSyncMethodID id;
    int (*init)(TCSynchronizer *sy, vob_t *vob, int master);
};

static const TCSyncMethod methods[] = {
    { TC_SYNC_NONE,          tc_sync_none_init     },
    { TC_SYNC_ADJUST_FRAMES, tc_sync_adjust_init   },
    { TC_SYNC_NULL,          NULL                  },
};

/* it doesn't yet make sense to have more than one synchro engine */
static TCSynchronizer tcsync;

/*************************************************************************/

/* front-end functions */

int tc_sync_init(vob_t *vob, TCSyncMethodID method, int master)
{
    int i;
    for (i = 0; methods[i].id != TC_SYNC_NULL; i++) {
        if (methods[i].id == method) {
            return methods[i].init(&tcsync, vob, master);
        }
    }
    return TC_ERROR;
}

int tc_sync_fini(void)
{
    return tcsync.fini(&tcsync);
}

int tc_sync_get_video_frame(TCFrameVideo *vf,
                            TCFillFrameVideo filler, void *ctx)
{
    return tcsync.get_video(&tcsync, vf, filler, ctx);
}

int tc_sync_get_audio_frame(TCFrameAudio *af,
                            TCFillFrameAudio filler, void *ctx)
{
    return tcsync.get_audio(&tcsync, af, filler, ctx);
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

