/*
 *  frame_threads.h -- declaration of transcode multithreaded filter
 *                     processing code.
 *
 *  Copyright (C) Thomas Oestreich - June 2001
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

#ifndef FRAME_THREADS_H
#define FRAME_THREADS_H

#include "tccore/job.h"

/*
 * SUMMARY:
 *
 * Those are the frame processing threads, implementing the threaded
 * filter layer. There isn't direct control to those threads. They
 * start to run after init(), and they are stopped by fini().
 * It is important to note that each thread is equivalent to each
 * other, and each one will take care of one frame and applies to
 * it the whole filter chain.
 */

/*
 * tc_frame_threads_init: start the frame threads pool and implicitely
 * and automatically starts the frame filter layer.
 *
 * Parameters:
 *           vob: vob structure.
 *      vworkers: number of threads in the video filter pool.
 *      aworkers: number of threads in the audio filter pool.
 * Return Value:
 *      None.
 */
void tc_frame_threads_init(vob_t *vob, int vworkers, int aworkers);

/*
 * tc_frame_threads_close: destroy both audio and video filter pool threads,
 * and automatically and implicitely stop the whole filter layer.
 * It's important to note that this function assume that all processing loops
 * are already been terminated.
 * This is a blocking function.
 * 
 * Parameters:
 *      None.
 * Return Value:
 *      None.
 * Preconditions:
 *      processing threads are terminated for any reason
 *      (regular stop, end of stream reached, forced interruption).
 */
void tc_frame_threads_close(void);

/*
 * tc_frame_threads_audio_{video,audio}_workers:
 *    query the number of avalaible (not active) audio,video frame
 *    worker threads.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      The number of avalaible audio,video frame worker threads.
 */
int tc_frame_threads_have_video_workers(void);
int tc_frame_threads_have_audio_workers(void);

#endif /* FRAME_THREADS_H */
