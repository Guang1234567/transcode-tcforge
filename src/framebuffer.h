/*
 *  framebuffer.h -- declarations of audio/video frame ringbuffers.
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Updates and Enhancements
 *  (C) 2007-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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


#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

#include "libtc/tcframes.h"
#include "tccore/tc_defaults.h"


/*************************************************************************
 * Transcode Framebuffer in a Nutshell (aka: how this code works)
 * --------------------------------------------------------------
 *
 * Introduction:
 * -------------
 * This is a quick, terse overview of design principles beyond the
 * framebuffer and about the design of this code. Full-blown
 * documentation is avalaible under doc/.
 *
 * When reading framebuffer documentation/code, always take in mind
 * the thread layout of transcode:
 *
 * - import layer is supposed to run 2 threads concurrently
 * - filter layer is supposed to run 0..N threads concurrently
 * - export layer is supposed to run 1 thread
 *
 * So, in any transcode execution, framebuffer code is supposed to
 * serve from 3 to N+3 concurrent threads.
 *
 * Framebuffer entities:
 * ---------------------
 * XXX
 *
 * frame status transitions scheme (API reminder):
 * -----------------------------------------------
 *
 *       .---------<---------------<------+-------<------.
 *       V                                | 7            | 6
 * .------------.     .--------.     .--------.     .--------.
 * | frame pool | --> | import |     | filter |     | export |
 * `------------'  1  `--------'     `--------'     `--------'
 *                           |         A    |         A
 *                           |       3 |    | 4       |
 *                         2 |         |    V       5 |
 *                           V     .-------------.    |
 *                           `---->| frame chain |--->'
 *                                 `-------------'
 *
 *  In frame lifetime order:
 *   1. {a,v}frame_register   (import)
 *   2. {a,v}frame_push_next  (import)
 *   3. {a,v}frame_reserve    (filter)
 *   4. {a,v}frame_push_next  (filter)
 *   5. {a,v}frame_retrieve   (export)
 *   6. {a,v}frame_remove     (export)
 * [ 7. {a,v}frame_remove     (filter) ]
 *
 * Operating conditions:
 *
 * 1. single source, full range, no interruptions
 * 2. single source, full range, interruption
 * 3. single source, sub range, no interruptions
 * 4. single source, sub range, interruption
 * 5. single source, multi sub ranges, no interruptions
 * 5. single source, multi sub ranges, interruption
 */


/*
 * tc_framebuffer_get_specs: (NOT thread safe)
 *     Get a pointer to a TCFrameSpecs structure representing current
 *     framebuffer structure. Frame handling code will use those parameters
 *     to allocate framebuffers.
 *
 * Parameters:
 *     None.
 * Return Value:
 *     Constant pointer to a TCFrameSpecs structure. There is no need
 *     to *free() this structure.
 */
const TCFrameSpecs *tc_framebuffer_get_specs(void);

/*
 * tc_framebuffer_set_specs: (NOT thread safe)
 *     Setup new framebuffer parameters, to be used by internal framebuffer
 *     code to properly handle frame allocation.
 *     PLEASE NOTE that only allocation performed AFTER calling this function
 *     will use those parameters.
 *     PLEASE ALSO NOTE that is HIGHLY unsafe to mix allocation by changing
 *     TCFrameSpecs in between without freeing ringbuffers. Just DO NOT.
 *
 * Parameters:
 *     Constant pointer to a TCFrameSpecs holding new framebuffer parameters.
 * Return Value:
 *     None.
 */
void tc_framebuffer_set_specs(const TCFrameSpecs *specs);

/*
 * tc_framebuffer_interrupt: (thread safe)
 *     Interrupt the framebuffer immediately (see below for specific meaning
 *     of this act in various functions).
 *     When framebuffer is interrupted, frames belonging to any processing
 *     stage are no longer avalaible; frame unavalaibility is notified as
 *     soon as is possible.
 *     When a framebuffer is interrupted, it becomes ready to be finalized;
 *     Effectively, the only operations that make sense to be performed on
 *     an interrupted framebuffer, is to finalize it.
 *     From statements above easily descend that interruption is irreversible.
 *
 * Parameters:
 *     None.
 * Return Value:
 *     None.
 * Side effects:
 *     Any frame-claiming function will fail after the invocation of this
 *     function (see description above).
 */
void tc_framebuffer_interrupt(void);

/*
 * tc_framebuffer_interrupt_stage: (thread safe)
 *     like tc_framebuffer_interrupt, but involves only a given processing stage.
 *
 * Parameters:
 *     S: a TCFrameStatus representing the processing stage to interrupt.
 * Return Value:
 *     None.
 * Side effects:
 *     Any function claiming frames from the specified processing stage 
 *     will fail after the invocation of this function.
 */
void tc_framebuffer_interrupt_stage(TCFrameStatus S);

/*
 * vframe_alloc, aframe_alloc: (NOT thread safe)
 *     Allocate respectively a video or audio frame ringbuffer capable to hold
 *     given amount of frames, with a minimum of one.
 *     Each framebuffer is allocated using TCFrameSpecs parameters.
 *     Use vframe_free/aframe_free to release acquired ringbuffers.
 *
 * Parameters:
 *     num: size of ringbuffer to allocate (number of framebuffers holded
 *          in ringbuffer).
 * Return Value:
 *      0: succesfull
 *     !0: error, tipically this means that one (or more) frame
 *         can't be allocated.
 */
int vframe_alloc(int num);
int aframe_alloc(int num);

/*
 * vframe_alloc_single, aframe_alloc_single: (NOT thread safe)
 *     allocate a single framebuffer (respectively, video or audio)
 *     in compliacy with last TCFrameSpecs set.
 *     Those functione are mainly intended to provide a convenient
 *     way to encoder/decoder/whatelse to allocate private framebuffers
 *     without doing any size computation or waste memory.
 *     Returned value can be SAFELY deallocated using
 *     tc_del_video_frame or tc_del_audio_frame.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      respectively a pointer to a TCFrameVideo or TCFrameAudio,
 *      like, tc_new_video_frame() or tc_new_audio_frame() called
 *      with right parameters.
 *      NULL if allocation fails.
 */
TCFrameVideo *vframe_alloc_single(void);
TCFrameAudio *aframe_alloc_single(void);

/*
 * vframe_free, aframe_free: (NOT thread safe)
 *     release all framebuffer memory acquired respect. for video and
 *     audio frame ringbuffers.
 *     Please remember thet ffter those functions called, almost
 *     all other ringbuffer functions will fail.
 *
 * Parameters:
 *     None.
 * Return Value:
 *     None.
 */
void vframe_free(void);
void aframe_free(void);

/*
 * vframe_flush, aframe_flush: (NOT thread safe)
 *     flush all framebuffers still in ringbuffer, by marking those as unused.
 *     This will reset ringbuffer to an empty state, ready to be (re)used again.
 *
 * Parameters:
 *     None.
 * Return Value:
 *     None.
 */
void vframe_flush(void);
void aframe_flush(void);

/*
 * tc_framebuffer_flush: (NOT thread safe)
 *     flush all active ringbuffers, and mark all frames as unused.
 *     This will reset ringbuffers to an empty state, ready to be (re)used again.
 *
 * Parameters:
 *     None.
 * Return Value:
 *     None.
 */
void tc_framebuffer_flush(void);

/*
 * vframe_register, aframe_register: (thread safe)
 *     Frame claiming functions.
 *     Respectively wait for an empty audio and video frame,
 *     then register it in frame chain, attach the given `id'
 *     and finally return the pointer to caller.
 *
 *     Those function are (and should be) used at the beginning
 *     of the frame chain. Those should are the first function
 *     that a framebuffer should see in it's lifecycle.
 *
 *     In transcode, those functions are (and should be) used
 *     only in the decoder.
 *
 *     Note:
 *     DO NOT *free() returned pointer! The memory needed for frames is
 *     handled by transcode internally.
 *
 * Parameters:
 *     id: set framebuffer id to this value.
 *         The meaning of `id' is enterely client-depended.
 * Return Value:
 *     A valid pointer to respectively an empty video or audio frame.
 *     If framebuffer is interrupted, both returns NULL.
 * Side effects:
 *     Being frame claiming functions, those functions will block
 *     calling thread until a new frame will be avalaible, OR
 *     until an interruption happens.
 */
TCFrameVideo *vframe_register(int id);
TCFrameAudio *aframe_register(int id);

/*
 * vframe_reserve, aframe_reserve: (thread safe)
 *     Frame claiming functions.
 *     Respectively wait for a processing-needing
 *     (`waiting' in transcode slang) audio and video frame,
 *     then reserve it, preventing other calls to those functions
 *     to claim it twice, and finally return the pointer to caller.
 *
 *     Those function are (and should be) used in the middle
 *     of the frame chain.
 *
 *     In transcode, those functions are (and should be) used
 *     only in the filter layer.
 *
 *     Note:
 *     DO NOT *free() returned pointer! The memory needed for frames is
 *     handled by transcode internally.
 *
 * Parameters:
 *     None.
 * Return Value:
 *     A valid pointer to respectively an empty video or audio frame.
 *     If framebuffer is interrupted, both returns NULL.
 * Side effects:
 *     Being frame claiming functions, those functions will block
 *     calling thread until a new frame will be avalaible, OR
 *     until an interruption happens.
 */
TCFrameVideo *vframe_reserve(void);
TCFrameAudio *aframe_reserve(void);

/*
 * vframe_retrieve, aframe_retrieve: (thread safe)
 *     Frame claiming functions.
 *     Respectively wait for a audio and video frame ready to be
 *     encoded, then retrieve it, preventing other calls to those
 *     functions to claim it twice, and finally return the pointer
 *     to caller.
 *
 *     Those function are (and should be) used at the end
 *     of the frame chain.
 *
 *     In transcode, those functions are (and should be) used
 *     only in the encoder.
 *
 *     Note:
 *     DO NOT *free() returned pointer! The memory needed for frames is
 *     handled by transcode internally.
 *
 * Parameters:
 *     None.
 * Return Value:
 *     A valid pointer to respectively an empty video or audio frame.
 *     If framebuffer is interrupted, both returns NULL.
 * Side effects:
 *     Being frame claiming functions, those functions will block
 *     calling thread until a new frame will be avalaible, OR
 *     until an interruption happens.
 */
TCFrameVideo *vframe_retrieve(void);
TCFrameAudio *aframe_retrieve(void);

/*
 * vframe_remove, aframe_remove: (thread safe)
 *     Respectively release an audio or video frame,
 *     by marking it as unused and putting it back on the frame pool.
 *
 *     Those function are (and should be) used at the end
 *     of the frame chain. Those should are the last function
 *     that a framebuffer should see in it's lifecycle.
 *
 *     In transcode, those functions are (and should be) used
 *     only in the encoder.
 *
 * Parameters:
 *     ptr: framebuffer to release.
 * Return Value:
 *     None.
 */
void vframe_remove(TCFrameVideo *ptr);
void aframe_remove(TCFrameAudio *ptr);

/*
 * vframe_reinject, aframe_reinject: (thread safe)
 *     Respectively reinject an audio or video frame
 *     into the originating frame pool, so the reinjected frame will
 *     be obtained again when the frame pool is queried again.
 *
 *     Those function are (and should be) used when a processing
 *     stage needs to see again a given frame.
 *
 *     In transcode, those functions are (and should be) used
 *     only when the encoder handles a cloned frame.
 *
 * Parameters:
 *     ptr: framebuffer to reinject.
 * Return Value:
 *     None.
 */
void aframe_reinject(TCFrameAudio *ptr);
void vframe_reinject(TCFrameVideo *ptr);

/*
 * vframe_push_next, aframe_push_next: (thread safe)
 *     Push a frame into next processing stage, by changing
 *     its status.
 *     Those functions are used when a processing stage terminate
 *     its operations on a given frame and so it want to pass such
 *     frame to next stage.
 *
 *     In transcode, those functions are (and should be) used
 *     in the decoder and in the filter stage.
 *
 * Parameters:
 *        ptr: framebuffer pointer to be updated.
 *     status: new framebuffer status (= stage).
 * Return Value:
 *     None.
 * Side effects:
 *     A blocked thread can (and it will likely) be awaken
 *     by this operation.
 */
void vframe_push_next(TCFrameVideo *ptr, TCFrameStatus status);
void aframe_push_next(TCFrameAudio *ptr, TCFrameStatus status);

/*
 * vframe_dup, aframe_dup: (thread safe)
 *     Frame claiming functions.
 *     Duplicate given respectively video or audio framebuffer.
 *     New framebuffer will be a full (deep) copy of old one
 *     (see aframe_copy/vframe_copy documentation to learn about
 *     deep copy).
 *
 * Parameters:
 *     f: framebuffer to be copied.
 * Return Value:
 *     A valid pointer to respectively duplicate video or audio frame.
 *     If framebuffer is interrupted, both returns NULL.
 * Side Effects:
 *     Being frame claiming functions, those functions will block
 *     calling thread until a new frame will be avalaible, OR
 *     until an interruption happens.
 */
TCFrameVideo *vframe_dup(TCFrameVideo *f);
TCFrameAudio *aframe_dup(TCFrameAudio *f);

/*
 * vframe_copy, aframe_copy (thread safe)
 *     perform a soft or optionally deep copy respectively of a 
 *     video or audio framebuffer. A soft copy just copies metadata;
 * #ifdef STATBUFFER
 *     soft copy also let the buffer pointers point to original frame
 *     buffers, so data isn't really copied around.
 * #endif
 *     A deep copy just ac_memcpy()s buffer data from a frame to other
 *     one, so new frame will be an independent copy of old one.
 *
 * Parameters:
 *           dst: framebuffer which will hold te copied (meta)data.
 *           src: framebuffer to be copied.
 *                Mind the fact that when using softcopy real buffers will
 *                remain the ones of this frame
 *     copy_data: boolean flag. If 0, do softcopy; do deepcopy otherwise.
 *         
 * Return Value:
 *     None.
 */
void vframe_copy(TCFrameVideo *dst, const TCFrameVideo *src, int copy_data);
void aframe_copy(TCFrameAudio *dst, const TCFrameAudio *src, int copy_data);

/*
 * vframe_dump_status, aframe_dump_status: (NOT thread safe)
 *      tc_log* out current framebuffer ringbuffer internal status, e.g.
 *      counters for null/ready/empty/loacked frames) respectively for
 *      video and audio ringbuffers.
 *
 *      THREAD SAFENESS WARNING:
 *      WRITEME
 *
 * Parameters:
 * 	None.
 * Return Value:
 *      None.
 * Side effects:
 *      See THREAD SAFENESS WARNING above.
 */
void vframe_dump_status(void);
void aframe_dump_status(void);

/*
 * vframe_have_more, aframe_have_more (thread safe):
 *      check if video/audio frame list is empty or not.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      !0 if frame list has at least one frame
 *       0 otherwise
 */
int vframe_have_more(void);
int aframe_have_more(void);

/*
 * {v,a}frame_get_counters (thead safe):
 *     get the number of frames currently hold in the processing layers,
 *     respectively for video and audio pipelines.
 *
 * Parameters:
 *      im: if not NULL, store here the number of frames
 *          hold by import layer.
 *      fl: if not NULL, store here the number of frames
 *          hold by filter layer.
 *      ex: if not NULL, store here the number of frames
 *          hold by export layer.
 * Return Value:
 *      None.
 */
void vframe_get_counters(int *im, int *fl, int *ex);
void aframe_get_counters(int *im, int *fl, int *ex);

/*
 * tc_framebuffer_get_counters (thread safe):
 *     get the total number of frames currently hold in the processing
 *     layers, by considering both video and audio pipelines.
 *
 * Parameters:
 *      im: if not NULL, store here the number of frames
 *          hold by import layer.
 *      fl: if not NULL, store here the number of frames
 *          hold by filter layer.
 *      ex: if not NULL, store here the number of frames
 *          hold by export layer.
 * Return Value:
 *      None.
 */
void tc_framebuffer_get_counters(int *im, int *fl, int *ex);

/*************************************************************************/

/* Internal functions used in unit tests: */
#ifdef FBUF_TEST
typedef struct tcframequeue_ TCFrameQueue;
typedef struct tcframepool_ TCFramePool;

extern const char *frame_status_name(TCFrameStatus S);
extern int is_heap(TCFrameQueue *Q, int debug);

extern void tc_frame_queue_dump_status(TCFrameQueue *Q, const char *tag);
extern void tc_frame_queue_del(TCFrameQueue *Q);
extern int tc_frame_queue_empty(TCFrameQueue *Q);
extern int tc_frame_queue_size(TCFrameQueue *Q);
extern TCFramePtr tc_frame_queue_get(TCFrameQueue *Q);
extern int tc_frame_queue_put(TCFrameQueue *Q, TCFramePtr ptr);
extern TCFrameQueue *tc_frame_queue_new(int size, int sorted);
extern int tc_frame_pool_init(TCFramePool *P, int size, int sorted,
                              const char *tag, const char *ptag);
extern int tc_frame_pool_fini(TCFramePool *P);
extern void tc_frame_pool_dump_status(TCFramePool *P);
extern void tc_frame_pool_put_frame(TCFramePool *P, TCFramePtr ptr);
extern TCFramePtr tc_frame_pool_get_frame(TCFramePool *P);
extern TCFramePtr tc_frame_pool_pull_frame(TCFramePool *P);
extern void tc_frame_pool_push_frame(TCFramePool *P, TCFramePtr ptr);
extern void tc_frame_pool_wakeup(TCFramePool *P, int broadcast);
#endif

#endif /* FRAMEBUFFER_H */
