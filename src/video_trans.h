/*
 * video_trans.h - header for video frame transformation routines
 * Written by Andrew Church <achurch@achurch.org>
 * Based on code written by Thomas Oestreich.
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef _VIDEO_TRANS_H
#define _VIDEO_TRANS_H

#include "transcode.h"

/*************************************************************************/

/* Video frame processing functions. */

int process_vid_frame(TCJob *vob, TCFrameVideo *ptr);
int preprocess_vid_frame(TCJob *vob, TCFrameVideo *ptr);
int postprocess_vid_frame(TCJob *vob, TCFrameVideo *ptr);

/*************************************************************************/

#endif  /* _VIDEO_TRANS_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
