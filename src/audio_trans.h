/*
 * audio_trans.h - header for audio frame transformation routines
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef _AUDIO_TRANS_H
#define _AUDIO_TRANS_H

#include "framebuffer.h"

int process_aud_frame(TCJob *vob, TCFrameAudio *ptr);

#endif
