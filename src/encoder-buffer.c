/*
 *  encoder-buffer.c -- encoder interface to transcode frame ringbuffers.
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Updated and partially rewritten by
 *  Francesco Romani - January 2006
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "libtcexport/export.h"

#include "transcode.h"
#include "decoder.h"
#include "filter.h"
#include "framebuffer.h"
#include "counter.h"
#include "video_trans.h"
#include "audio_trans.h"
#include "frame_threads.h"

#include <stdint.h>
#include <sys/types.h>

/*
 * Quick Summary:
 *
 * This code provide glue between frames ringbuffer and real encoder
 * code, in order to make encoder modular and independent from
 * a single frame source, to promote reusability (tcexport).
 * This code also take acare of some oddities like handling filtering
 * if no frame threads are avalaible.
 *
 * This code isn't clean as one (i.e.: me) would like since it
 * must cope with a lot of legacy constraints and some other nasty
 * stuff. Of course situation will be improved in future releases
 * when we keep going away from legacy oddities and continue
 * sanitize/modernize the codebase.
 *
 * NOTE about counter/condition/mutex handling inside various
 * encoder helpers.
 *
 * Code are still a little bit confusing since things aren't
 * updated or used at the same function level.
 * Code works, but isn't still well readable.
 * We need stil more cleanup and refactoring for future releases.
 */


/*************************************************************************/
/*************************************************************************/

typedef struct tcringbuffersource_ TCRingBufferSource;
struct tcringbuffersource_ {
    int             inited;

    int             have_aud_threads;
    int             have_vid_threads;

    int             frame_id;

    TCFrameVideo    *vptr;
    TCFrameAudio    *aptr;
};

/*************************************************************************/

/*
 * apply_{video,audio}_filters:
 *       Apply the filter chain to current respectively video
 *       or audio frame.
 *       This function is used if no frame threads are avalaible,
 *       but of course filtering still must be applied.
 *       This function should be never exported.
 *
 * Parameters:
 *       vptr, aptr: respectively video or aufio framebuffer
 *                   pointer to frame to be filtered.
 *              vob: pointer to vob_t structure holding stream
 *                   parameters.
 */
static void apply_video_filters(TCRingBufferSource *buf,
                                TCFrameVideo *vptr, TCJob *job);
static void apply_audio_filters(TCRingBufferSource *buf,
                                TCFrameAudio *aptr, TCJob *job);

/*
 * encoder_acquire_{v,a}frame:
 *      Get respectively a new video or audio framebuffer for encoding.
 *      This roughly means:
 *      1. to wait for a new frame avalaible for encoder
 *      2. apply the filters if no frame threads are avalaible
 *      3. apply the encoder filters (POST_S_PROCESS)
 *      4. verify the status of audio framebuffer after all filtering.
 *         if acquired framebuffer is skipped, we must acquire a new one
 *         before continue with encoding, so we must restart from step 1.
 *
 * Parameters:
 *      buf: encoder buffer to use (and fullfull with acquired frame).
 *      vob: pointer to vob_t structure describing stream parameters.
 *           Used Internally.
 * Return Value:
 *       0 when a new framebuffer is avalaible for encoding;
 *      <0 if no more framebuffers are avalaible.
 *         As for encoder_wait_{v,a}frame, this usually happens
 *         when video/audio stream ends.
 */
static TCFrameVideo *encoder_acquire_vframe(TCFrameSource *FS);
static TCFrameAudio *encoder_acquire_aframe(TCFrameSource *FS);

/*
 * encoder_dispose_{v,a}frame:
 *       Mark a framebuffer (respectively video or audio) as completed
 *       from encoder viewpoint, so release it to source ringbuffer,
 *       update counters and do all cleanup actions needed internally.
 *
 * Parameters:
 *       buf: encoder buffer to use (release currente framebuffers
 *            and update related counters and internal variables).
 * Return Value:
 *       None.
 */
static void encoder_dispose_vframe(TCFrameSource *FS, TCFrameVideo *frame);
static void encoder_dispose_aframe(TCFrameSource *FS, TCFrameAudio *frame);


/*************************************************************************/

static void apply_video_filters(TCRingBufferSource *buf,
                                TCFrameVideo *vptr, TCJob *job)
{
    if (!buf->have_vid_threads) {
        if (TC_FRAME_NEED_PROCESSING(vptr)) {
            /* external plugin pre-processing */
            vptr->tag = TC_VIDEO|TC_PRE_M_PROCESS;
            tc_filter_process((frame_list_t *)vptr);

            /* internal processing of video */
            vptr->tag = TC_VIDEO;
            process_vid_frame(job, vptr);

            /* external plugin post-processing */
            vptr->tag = TC_VIDEO|TC_POST_M_PROCESS;
            tc_filter_process((frame_list_t *)vptr);
        }
    }

    if (TC_FRAME_NEED_PROCESSING(vptr)) {
        /* second stage post-processing - (synchronous) */
        vptr->tag = TC_VIDEO|TC_POST_S_PROCESS;
        tc_filter_process((frame_list_t *)vptr);
        postprocess_vid_frame(job, vptr);
        /* preview _after_ all post-processing */
        vptr->tag = TC_VIDEO|TC_PREVIEW;
        tc_filter_process((frame_list_t *)vptr);
    }
}

static void apply_audio_filters(TCRingBufferSource *buf,
                                TCFrameAudio *aptr, TCJob *job)
{
    /* now we try to process the audio frame */
    if (!buf->have_aud_threads) {
        if (TC_FRAME_NEED_PROCESSING(aptr)) {
            /* external plugin pre-processing */
            aptr->tag = TC_AUDIO|TC_PRE_M_PROCESS;
            tc_filter_process((frame_list_t *)aptr);

            /* internal processing of audio */
            aptr->tag = TC_AUDIO;
            process_aud_frame(job, aptr);

            /* external plugin post-processing */
            aptr->tag = TC_AUDIO|TC_POST_M_PROCESS;
            tc_filter_process((frame_list_t *)aptr);
        }
    }

    if (TC_FRAME_NEED_PROCESSING(aptr)) {
        /* second stage post-processing - (synchronous) */
        aptr->tag = TC_AUDIO|TC_POST_S_PROCESS;
        tc_filter_process((frame_list_t *)aptr);
        /* preview _after_ all post-processing */
        aptr->tag = TC_AUDIO|TC_PREVIEW;
        tc_filter_process((frame_list_t *)aptr);
    }
}

static TCFrameVideo *encoder_acquire_vframe(TCFrameSource *FS)
{
    TCRingBufferSource *buf = FS->privdata;
    TCFrameVideo *frame = NULL;

    do {
        frame = vframe_retrieve();
        if (!frame) {
            tc_debug(TC_DEBUG_THREADS, "(V) frame retrieve interrupted!");
            return NULL; /* can't acquire video frame */
        }
        buf->frame_id = frame->id + tc_get_frames_skipped_cloned();

        /*
         * now we do the post processing ... this way, if just a video frame is
         * skipped, we'll know.
         *
         * we have to check to make sure that before we do any processing
         * that this frame isn't out of range (if it is, and one is using
         * the "-t" split option, we'll see this frame again.
         */
        apply_video_filters(buf, frame, FS->job);

        if (frame->attributes & TC_FRAME_IS_SKIPPED) {
            if (frame->attributes & TC_FRAME_WAS_CLONED) {
                /* XXX do we want to track skipped cloned flags? */
                tc_update_frames_cloned(1);
            }

            if (frame->attributes & TC_FRAME_IS_CLONED) {
                /* XXX what to do when a frame is cloned and skipped? */
                /*
                 * I'd like to say they cancel, but perhaps they will end
                 * up also skipping the clone?  or perhaps they'll keep,
                 * but modify the clone?  Best to do the whole drill :/
                 */

                tc_debug(TC_DEBUG_FLIST,  "[%i|%i] (V) pointer done. "
                         "Skipped and Cloned: (%i)",
                         frame->id, frame->bufid,
                         (frame->attributes));

                /* update flags */
                frame->attributes &= ~TC_FRAME_IS_CLONED;
                frame->attributes |= TC_FRAME_WAS_CLONED;

                vframe_reinject(frame);
                frame = NULL;
            } else{
                vframe_remove(frame);
                frame = NULL;
            }
        }
    } while (!frame);

    tc_debug(TC_DEBUG_FLIST, 
             "(V) acquired frame [%p] (id=%i|%i)",
             frame, frame->id, buf->frame_id);

    return frame;
}

static TCFrameAudio *encoder_acquire_aframe(TCFrameSource *FS)
{
    TCRingBufferSource *buf = FS->privdata;
    TCFrameAudio *frame = NULL;

    do {
        frame = aframe_retrieve();
        if (!frame) {
            tc_debug(TC_DEBUG_THREADS, "(A) frame retrieve interrupted!");
            return NULL;
        }

        apply_audio_filters(buf, frame, FS->job);

        if (frame->attributes & TC_FRAME_IS_SKIPPED) {
            if (!(frame->attributes & TC_FRAME_IS_CLONED)) {
                aframe_remove(frame);
                frame = NULL;
            } else {
                tc_debug(TC_DEBUG_FLIST,  "[%i|%i] (A) pointer done. "
                         "Skipped and Cloned: (%i)",
                         frame->id, frame->bufid,
                         (frame->attributes));

                /* adjust clone flags */
                frame->attributes &= ~TC_FRAME_IS_CLONED;
                frame->attributes |= TC_FRAME_WAS_CLONED;

                aframe_reinject(frame);
                frame = NULL;
            }
        }
    } while (!frame);

    tc_debug(TC_DEBUG_FLIST, 
             "(A) acquired frame [%p] (id=%i)",
             frame, frame->id);

    return frame;
}


static void encoder_dispose_vframe(TCFrameSource *FS, TCFrameVideo *frame)
{
    if (frame) {
        if (frame->attributes & TC_FRAME_WAS_CLONED) {
            tc_update_frames_cloned(1);
        }

        if (frame->attributes & TC_FRAME_IS_CLONED) {
            tc_debug(TC_DEBUG_FLIST,
                     "[%i] (V) pointer done. Cloned: (%i)",
                     frame->id, (frame->attributes));

            frame->attributes &= ~TC_FRAME_IS_CLONED;
            frame->attributes |= TC_FRAME_WAS_CLONED;

            vframe_reinject(frame);
        } else {
            tc_debug(TC_DEBUG_FLIST, 
                     "(V) disposed frame [%p] (id=%i)",
                     frame, frame->id);

            vframe_remove(frame);
        }
    }
    return;
}


static void encoder_dispose_aframe(TCFrameSource *FS, TCFrameAudio *frame)
{
    if (frame) {
        if (frame->attributes & TC_FRAME_IS_CLONED) {
            tc_debug(TC_DEBUG_FLIST,
                     "[%i] (A) pointer done. Cloned: (%i)",
                     frame->id, (frame->attributes));

            frame->attributes &= ~TC_FRAME_IS_CLONED;
            frame->attributes |= TC_FRAME_WAS_CLONED;

            aframe_reinject(frame);
        } else {
            tc_debug(TC_DEBUG_FLIST, 
                     "(A) disposed frame [%p] (id=%i)",
                     frame, frame->id);

            aframe_remove(frame);
        }
    }
    return;
}

/*************************************************************************/

static TCRingBufferSource ringsource = {
    .inited             = TC_FALSE,
    .have_aud_threads   = 0,
    .have_vid_threads   = 0,
    .aptr               = NULL,
    .vptr               = NULL
};
static TCFrameSource framesource = {
    .privdata           = &ringsource,
    .job                = NULL,
    
    .get_video_frame    = encoder_acquire_vframe,
    .get_audio_frame    = encoder_acquire_aframe,
    .free_video_frame   = encoder_dispose_vframe,
    .free_audio_frame   = encoder_dispose_aframe,
};

/* default main transcode buffer */
TCFrameSource *tc_get_ringbuffer(TCJob *job, int aworkers, int vworkers)
{
    ringsource.have_aud_threads = aworkers;
    ringsource.have_vid_threads = vworkers;
    framesource.job             = job;

    return &framesource;
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
