/*
 * probe.h - input probing data structures
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef TCCORE_PROBE_H
#define TCCORE_PROBE_H

#include "tc_defaults.h"

/*************************************************************************/

/* Structures to hold probed data */

typedef struct {
    int samplerate;
    int chan;
    int bits;
    int bitrate;
    int padrate;        // Padding byterate
    int format;
    int lang;
    int attribute;      // 0=subtitle, 1=AC3, 2=PCM
    int tid;            // Logical track id, in case of gaps
    double pts_start;
} ProbeTrackInfo;


typedef struct {

    int width;          // Frame width
    int height;         // Frame height

    double fps;         // Encoder fps

    long codec;         // Video codec
    long magic;         // File type/magic
    long magic_xml;     // Type/magic of content in XML file

    int asr;            // Aspect ratio code
    int frc;            // Frame cate code

    int par_width;      // Pixel aspect (== sample aspect ratio)
    int par_height;

    int attributes;     // Video attributes

    int num_tracks;     // Number of audio tracks

    ProbeTrackInfo track[TC_MAX_AUD_TRACKS];

    long frames;        // Total frames
    long time;          // Total time in secs

    int unit_cnt;       // Detected presentation units
    double pts_start;   // Video PTS start

    long bitrate;       // Video stream bitrate

    int ext_attributes[4]; // Reserved for MPEG

    int is_video;       // NTSC flag

} ProbeInfo;

#endif  // TCCORE_PROBE_H

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
