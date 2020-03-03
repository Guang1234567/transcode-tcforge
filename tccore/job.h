/*
 *  job.h
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

#ifndef TCJOB_H
#define TCJOB_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "avilib/avilib.h"
#include "libtc/framecode.h"
#include "libtcutil/tcutil.h"
#include "libtcvideo/tcvideo.h"


#ifdef __bsdi__
typedef unsigned int uint32_t;
#endif

/*************************************************************************/

typedef struct _transfer_t {
    int flag;
    FILE *fd;
    int size;
    uint8_t *buffer;
    uint8_t *buffer2;
    int attributes;
} transfer_t;

typedef struct _vob_t vob_t;
struct _vob_t {
    // import info

    const char *vmod_probed;
    const char *amod_probed;
    const char *vmod_probed_xml; // Modules for reading XML data
    const char *amod_probed_xml;

    int verbose;

    TCGlob *video_in_files;
    TCGlob *audio_in_files;
    const char *video_in_file;  // Video source file
    const char *audio_in_file;  // Audio source file

    const char *nav_seek_file;  // Seek/index information

    int has_audio;              // Does the stream have audio?
    int has_audio_track;        // Does the requested audio track exist?
    int has_video;              // Does the stream have video?

    int lang_code;              // Language of audio track

    int a_track;                // Audio track ID
    int v_track;                // Video track ID
    int s_track;                // Subtitle track ID

    int sync;                   // Frame offset for audio/video synchronization
    int sync_ms;                // Fine-tuning for audio/video synchronization
    int sync_samples;           // sync_ms converted to samples

    int dvd_title;
    int dvd_chapter1;
    int dvd_chapter2;
    int dvd_max_chapters;
    int dvd_angle;

    int ps_unit;
    int ps_seq1;
    int ps_seq2;

    int ts_pid1;
    int ts_pid2;

    int vob_offset;
    int vob_chunk;
    int vob_chunk_num1;
    int vob_chunk_num2;
    int vob_chunk_max;
    int vob_percentage;

    int vob_psu_num1;
    int vob_psu_num2;

    const char *vob_info_file;

    double pts_start;

    double psu_offset;          // PSU offset to pass to extsub

    int demuxer;

    long v_format_flag;         // Video stream format
    long v_codec_flag;          // Video codec
    long a_format_flag;         // Audio stream format
    long a_codec_flag;          // Audio codec

    int quality;

    // Audio stream parameters

    int a_stream_bitrate;       // Source stream bitrate

    int a_chan;
    int a_bits;
    int a_rate;

    int a_padrate;              // Zero padding rate

    int im_a_size;              // Import total bytes per audio frame
    int ex_a_size;              // Export total bytes per audio frame

    int im_a_codec;             // True frame buffer audio codec

    int a_leap_frame;
    int a_leap_bytes;

    int a_vbr;                  // LAME VBR switch

    int a52_mode;

    int dm_bits;
    int dm_chan;

    // Video stream parameters

    int v_stream_bitrate;       // Source stream bitrate

    double fps;                 // Import frame rate (default 25 fps)
    int im_frc;                 // Import frame rate code
    double ex_fps;              // Export frame rate (default 25 fps)
    int ex_frc;                 // Export frame rate code
    int hard_fps_flag;          // If this is set, disable demuxer smooth drop

    int pulldown;               // Set 3:2 pulldown flags on MPEG export

    int im_v_height;            // Import picture height
    int im_v_width;             // Import picture width
    int im_v_size;              // Total number of bytes per frame

    int im_asr;                 // Import aspect ratio code
    int im_par;                 // Import pixel aspect (code)
    int im_par_width;           // Import pixel aspect width
    int im_par_height;          // Import pixel aspect height
    int ex_asr;                 // Export aspect ratio code
    int ex_par;                 // Export pixel aspect (code)
    int ex_par_width;           // Export pixel aspect width
    int ex_par_height;          // Export pixel aspect height

    int attributes;             // More video frame attributes

    int im_v_codec;             // True frame buffer video codec

    int encode_fields;          // Interlaced field handling flag

    int dv_yuy2_mode;           // Decode DV video in YUY2 mode?

    // Audio frame manipulation info

    double volume;              // Audio amplitude rescale parameter
    double ac3_gain[3];         // Audio amplitude rescale parameter for ac3
    int clip_count;             // # of bytes clipped after volume adjustment

    int ex_v_width;             // Export picture width
    int ex_v_height;            // Export picture height
    int ex_v_size;              // Total number of bytes per frame

    int reduce_h;               // Reduction factor for frame height
    int reduce_w;               // Reduction factor for frame width

    int resize1_mult;           // Multiplier for {vert,hori}_resize1
    int vert_resize1;           // Height resize amount (shrink)
    int hori_resize1;           // Width resize amount (shrink)

    int resize2_mult;           // Multiplier for {vert,hori}_resize2
    int vert_resize2;           // Height resize amount (expand)
    int hori_resize2;           // Width resize amount (expand)

    int zoom_flag;              // Zoom enabled or not?
    int zoom_width;             // Zoom width
    int zoom_height;            // Zoom height
    int zoom_interlaced;        // Zoom in interlaced mode?

    TCVZoomFilter zoom_filter;

    int antialias;
    int deinterlace;
    int decolor;

    double aa_weight;           // Antialiasing center pixel weight
    double aa_bias;             // Antialiasing horizontal/vertical bias

    double gamma;

    int ex_clip_top;
    int ex_clip_bottom;
    int ex_clip_left;
    int ex_clip_right;

    int im_clip_top;
    int im_clip_bottom;
    int im_clip_left;
    int im_clip_right;

    int post_ex_clip_top;
    int post_ex_clip_bottom;
    int post_ex_clip_left;
    int post_ex_clip_right;

    int pre_im_clip_top;
    int pre_im_clip_bottom;
    int pre_im_clip_left;
    int pre_im_clip_right;

    // Export info

    const char *video_out_file;
    const char *audio_out_file;

    avi_t *avifile_in;
    avi_t *avifile_out;
    int avi_comment_fd;         // Text file to read AVI header comments from

    int audio_file_flag;        // Nonzero if audio goes to its own file

    // Resync parameters

    int resync_frame_interval;
    int resync_frame_margin;

    // Encoding parameters

    int divxbitrate;
    int divxkeyframes;
    int divxquality;
    int divxcrispness;
    int divxmultipass;
    int video_max_bitrate;
    const char *divxlogfile;

    int min_quantizer;
    int max_quantizer;

    int mp3bitrate;
    int mp3frequency;
    float mp3quality;           // 0=best (very slow), 9=worst (default=5)
    int mp3mode;                // 0=joint-stereo, 1=full-stereo, 2=mono

    const char *audiologfile;

    int ex_a_codec;             // Audio codec for export module
    int ex_v_codec;             // Video codec for export module

    const char *ex_v_fcc;       // Video fourcc string
    const char *ex_a_fcc;       // Audio fourcc string/identifier
    const char *ex_profile_name; // User profile name

    int pass_flag;
    int encoder_flush;          // flush encoders on close (yes)

    const char *mod_path;
    const char *reg_path;
    const char *prof_path;

    struct fc_time *ttime;      // For framecode parsing (list of structs)

    unsigned int frame_interval; // Select every `frame_interval' frames only

    char *im_v_string;          // Extra options for import video module
    char *im_a_string;          // Extra options for import audio module

    char *ex_v_string;          // Extra options for export video module
    char *ex_a_string;          // Extra options for export audio module
    char *ex_m_string;          // Extra options for multiplexor module
    char *ex_mx_string;         // Extra options for auxiliary multiplexor module

    float m2v_requant;          // Requantize factor for mpeg2 video streams

    unsigned int export_attributes;

    const char *ex_prof_name;

    int rgbswap;
    int pcmswap;
    int dgamma;
    int keepasr;
    int fast_resize;
    int flip;
    int mirror;
};
typedef struct _vob_t TCJob;

enum {
    TC_MODE_DEFAULT     =  0,
    TC_MODE_AVI_SPLIT   =  1,
    TC_MODE_DVD_CHAPTER =  2,
    TC_MODE_PSU         =  4,
    TC_MODE_DIRECTORY   = 16,
    TC_MODE_DEBUG       = 32,
};

enum {
    TC_ENCODE_FIELDS_PROGRESSIVE = 0,
    TC_ENCODE_FIELDS_TOP_FIRST,
    TC_ENCODE_FIELDS_BOTTOM_FIRST,
    TC_ENCODE_FIELDS_UNKNOWN,
};

/*************************************************************************/
/*************************************************************************/

#endif  // TCJOB_H

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
