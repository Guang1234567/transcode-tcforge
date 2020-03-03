/*
 * tcframes.h -- common generic audio/video/whatever frame allocation/disposal
 *               routines for transcode.
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


#ifndef TCFRAMES_H
#define TCFRAMES_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>

#include "libtcutil/tctimer.h"
#include "libtc/tccodecs.h"

/*************************************************************************/
#include "tccore/frame.h"


/*
 * tc_video_planes_size:
 *     compute the size of video planes given frame size and frame format.
 *     Recognizes only video formats used in transcode.
 *
 * Parameters:
 *     psizes: array of size that will be filled with size of respective
 *             plane, in order. If given format isn't a planar one, only
 *             first element in array is significant.
 *      width: width of video frame
 *     height: height of video frame
 *     format: format of video frame
 * Return Value:
 *     >= 0 if succesfull,
 *     TC_NULL_MATCH otherwise (wrong/unknown parameters)
 */
int tc_video_planes_size(size_t psizes[3],
                         int width, int height, int format);

/*
 * tc_video_frame_size:
 *     little helper function that returns the full dimension of a
 *     video frame given dimensions and format.
 *
 * Parameters:
 *      width: width of video frame
 *     height: height of video frame
 *     format: format of video frame
 * Return Value:
 *     size in bytes of video frame
 */
#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static size_t tc_video_frame_size(int width, int height, int format)
{
    size_t psizes[3] = { 0, 0, 0 };
    tc_video_planes_size(psizes, width, height, format);
    return (psizes[0] + psizes[1] + psizes[2]);
}

/* 
 * OK, we have sample rate. But sample rate means "[audio] samples PER SECOND"
 * and we want audio samples PER FRAME.
 */
#define TC_AUDIO_SAMPLES_IN_FRAME(rate, fps)    ((double)rate/(double)fps)

/*
 * tc_audio_frame_size:
 *     compute the size of buffer needed to store the audio data described by
 *     given specifiers. 
 *
 * Parameters:
 *      samples: audio samples PER FRAME. Can (and it's likely that it will)
 *               be a real numner (values after the point are significant!)
 *     channels: audio channels.
 *         bits: audio BITS for sample.
 *       adjust: store here adjustement value. Such value means how much extra
 *               buffer size it's needed to safely store extra samples.
 *               We have extra samples when rate/fps != 0, so we can
 *               spread all samples in frames, there is something that
 *               "exceed" :)
 *               (OK, there is *A LOT* of room for improvement here. But this
 *               API it's also driven by legacy code).
 * Return Value:
 *     amount of buffer needed.
 * Preconditions:
 *     adjust != NULL.
 */
size_t tc_audio_frame_size(double samples, int channels,
                           int bits, int *adjust);

/*
 * tc_alloc_{video,audio}_frame:
 *     allocate, but NOT initialize, a {TCFrameVideo,TCFrameAudio},
 *     large enough to hold a video frame large as given size.
 *     This function guarantee that video buffer(s) memory will
 *     be page-aligned.
 *
 * Parameters:
 *        size: size in bytes of video frame that will be contained.
 *     partial: if !0, doesn't allocate secondary video buffer,
 *              but only primary. This allow to save memory since
 *              secondary video buffer isn't ALWAYS needed.
 * Return Value:
 *     pointer to a new TCFrameVideo (free it using tc_del_video_frame,
 *     not manually! ) if succesfull, NULL otherwise.
 */
TCFrameVideo *tc_alloc_video_frame(size_t size, int partial);
TCFrameAudio *tc_alloc_audio_frame(size_t size);


/*
 * tc_init_video_frame:
 *     properly (re)initialize an already-allocated video frame, by
 *     asjusting plane pointers, (re)setting video buffer pointers,
 *     cleaning flags et. al.
 *     You usually always need to use this function unless you
 *     perfectly knows what you're doing.
 *     Do nothing if missing TCFrameVideo to (re)initialize of
 *     one or more parameter are wrong.
 *
 * Parameters:
 *       vptr: pointer to TCFrameVideo to (re)initialize.
 *      width: video frame width.
 *     height: video frame height.
 *     format: video frame format.
 * Return Value:
 *     None
 * Preconditions:
 *     given TCFrameVideo MUST be already allocated to be large
 *     enough to safely store a video frame with given
 *     parameters. This function DO NOT check if this precondition
 *     is respected.
 */
void tc_init_video_frame(TCFrameVideo *vptr,
                         int width, int height, int format);
/*
 * tc_init_audio_frame:
 *     properly (re)initialize an already-allocated audio frame,
 *     (re)setting video buffer pointers,cleaning flags et. al.
 *     You usually always need to use this function unless you
 *     perfectly knows what you're doing.
 *     Do nothing if missing TCFrameAudio to (re)initialize of
 *     one or more parameter are wrong.
 *
 * Parameters:
 *       aptr: pointer to TCFrameAudio to (re)initialize.
 *    samples: audio frame samples that this audio frame
 *             will contain (WARNING: TCFrameAudio MUST
 *             be allocated accordingly).
 *   channels: audio frame channels.
 *       bits: audio frame bit for sample.
 * Return Value:
 *     None
 * Preconditions:
 *     given TCFrameAudio MUST be already allocated to be large
 *     enough to safely store an audio frame with given
 *     parameters. This function DO NOT check if this precondition
 *     is respected.
 */
void tc_init_audio_frame(TCFrameAudio *aptr,
                         double samples, int channels, int bits);

/*
 * tc_new_video_frame:
 *     allocate and initialize a new TCFrameVideo large enough
 *     to hold a video frame represented by given parameters.
 *     This function guarantee that video buffer(s) memory will
 *     be page-aligned.
 *
 * Parameters:
 *      width: video frame width.
 *     height: video frame height.
 *     format: video frame format.
 *    partial: if !0, doesn't allocate secondary video buffer,
 *             but only primary. This allow to save memory since
 *             secondary video buffer isn't ALWAYS needed.
 * Return Value:
 *     pointer to a new TCFrameVideo (free it using tc_del_video_frame,
 *     not manually! ) if succesfull, NULL otherwise.
 */
TCFrameVideo *tc_new_video_frame(int width, int height, int format,
                                 int partial);

/*
 * tc_new_audio_frame:
 *     allocate and initialize a new TCFrameAudio large enough
 *     to hold an audio frame represented by given parameters.
 *     This function guarantee that audio buffer memory will
 *     be page-aligned.
 *
 * Parameters:
 *    samples: audio frame samples that this audio frame
 *             will contain (WARNING: TCFrameAudio MUST
 *             be allocated accordingly).
 *   channels: audio frame channels.
 *       bits: audio frame bit for sample.
 * Return Value:
 *     pointer to a new TCFrameAudio (free it using tc_del_audio_frame,
 *     not manually! ) if succesfull, NULL otherwise.
 */
TCFrameAudio *tc_new_audio_frame(double samples, int channels, int bits);


/*
 * tc_del_{video,audio}_frame:
 *     safely deallocate memory obtained with tc_new_{video,audio}_frame
 *     or tc_alloc_{video,audio}_frame.
 *
 * Parameters:
 *     {vptr,aptr}: a pointer to a TCFrame{Video,Audio} obtained by calling
 *     tc_new_{video,audio}_frame or tc_alloc_{video,audio}_frame.
 * Return Value:
 *     None
 */
void tc_del_video_frame(TCFrameVideo *vptr);
void tc_del_audio_frame(TCFrameAudio *aptr);

/*
 * tc_blank_{video,audio}_frame:
 *      fill a provided frame with per-format valid but blank (null)
 *      content.
 *
 * Parameters:
 *     ptr: pointer to frame to fill.
 * Return Value:
 *     None.
 */
void tc_blank_video_frame(TCFrameVideo *ptr);
void tc_blank_audio_frame(TCFrameAudio *ptr);

/*
 * tc_reset_{video,audio}_frame:
 *      reset the frame attributes. Lightweight reinitialization.
 *      Pulled by libtcexport needs.
 *      It will probably be merged into tc_init_{video,audio}_frame
 *      in a future release.
 *
 * Parameters:
 *     ptr: pointer to frame to fill.
 * Return Value:
 *     None.
 */
void tc_reset_video_frame(TCFrameVideo *ptr);
void tc_reset_audio_frame(TCFrameAudio *ptr);


#endif  /* TCFRAMES_H */
