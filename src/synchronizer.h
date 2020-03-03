/*
 * synchronizer.h -- transcode A/V synchronization code - interface
 * (C) 2008 - Francesco Romani <fromani at gmail dot com>
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

#ifndef SYNCHRONIZER_H
#define SYNCHRONIZER_H


#include "libtc/tcframes.h"
#include "tccore/job.h"

/*
 * transcode syncronization infrastructure.
 * ----------------------------------------
 * supports pluggable A/V sync algorythms and provides
 * a common API for demuxer (decoders must see synchronized frames).
 */

/*************************************************************************/

typedef enum tcsyncmethodid_ TCSyncMethodID;
enum tcsyncmethodid_ {
    TC_SYNC_NULL = -1,        /* NULL value (invalid method) */
    TC_SYNC_NONE = 0,         /* no method: don't mess with the sync */
    TC_SYNC_ADJUST_FRAMES,    /* use frame number to enforce the sync */
};

/*
 * TCFillFrame{Video,Audio}:
 *   frame filling callback. The Synchronizer engine calls those when
 *   it needs a new _complete_ frame from the demuxer layer.
 *
 * Parameters:
 *      ctx: opaque data passed back to filler at each call.
 *   {v,a}f: pointer to {video,audio} frame to be filled.
 * Return Value:
 *     TC_OK: frame succesfully obtained;
 *     TC_ERROR: otherwise.
 */
typedef int (*TCFillFrameVideo)(void *ctx, TCFrameVideo *vf);
typedef int (*TCFillFrameAudio)(void *ctx, TCFrameAudio *af);

/*
 * tc_sync_init:
 *    initializes the Synchronizer engine by using one track
 *    (audio or video) as master source. The frames of the master source
 *    are mangled the less as is possible by the Synchronizer engine.
 *
 * Parameters:
 *       vob: pointer to a TCJob structure describing the import streams.
 *    method: sync method to use for A/V syncrhonization.
 *    master: select the master source. Only TC_AUDIO or TC_VIDEO
 *            are meaningful.
 *            NOTE that some sync algorythms can support just one kind of
 *            source as master.
 * Return Value:
 *     TC_OK if succesfull,
 *     TC_ERROR on error or the sync method is unknown. The exact
 *     reason of the error is tc_log()'d out.
 */
int tc_sync_init(TCJob *vob, TCSyncMethodID method, int master);

/*
 * tc_sync_fini:
 *    finalizes the Syncrhonizer engine and frees all acquired resources.
 *
 * Parameters:
 *     None
 * Return Value:
 *     TC_OK if succesfull,
 *     TC_ERROR on error; the exact error reason is tc_log()'d out.
 * Side Effects:
 *     Synchronization statistics can tc_log()'d out depending on the
 *     algorythm used.
 */
int tc_sync_fini(void);

/*
 * tc_sync_get_{video,audio}_frame:
 *     get a new _synchronized_ video or audio frame. 
 *     The code using the Synchronizer engine should no longer worry about
 *     *source* A/V sync as long as it gets the frames from it instead then
 *     pulling directly from the demuxer.
 *     
 *     The Synchronizer engine will fetch an unpredictable (but hopefully
 *     and usually low) number of frames from the demuxer and mangle them
 *     in order to obtain A/V sync.
 *
 * Parameters:
 *   {v,a}f: pointer to structure to be filled with new synchronized 
 *           A/V frames.
 *   filler: function to use (usually exported by demuxer or a thin adapter 
 *           around it) to fetch raw, potentially-unsynched, frames from 
 *           demuxer.
 *      ctx: opaque data to be passed to the filler function.
 * Return Value:
 *    TC_OK if succesfull,
 *    TC_ERROR otherwise. The exact error reason will be tc_log()'d out.
 * Side Effects:
 *    An impredictable (from the client code viewpoint) number of calls
 *    to the demuxer layer will be issued.
 */
int tc_sync_get_video_frame(TCFrameVideo *vf, TCFillFrameVideo filler, void *ctx);
int tc_sync_get_audio_frame(TCFrameAudio *af, TCFillFrameAudio filler, void *ctx);

#endif /* SYNCHRONIZER_H */
