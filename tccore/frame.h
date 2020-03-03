/*
 * frame.h -- transcode audio/video frames.
 * (C) 2009-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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


#ifndef FRAME_H
#define FRAME_H

#include "job.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>

/*************************************************************************/

// default PAL video size
#define PAL_W                  720
#define PAL_H                  576
#define BPP                     24
#define PAL_FPS                 25.0
#define MIN_FPS                  1.0
#define NTSC_FILM    ((double)(24000)/1001.0)
#define NTSC_VIDEO   ((double)(30000)/1001.0)

//NTSC
#define NTSC_W                  720
#define NTSC_H                  480

//new max frame size:
#define TC_MAX_V_FRAME_WIDTH     2500
#define TC_MAX_V_FRAME_HEIGHT    2000

// max bytes per pixel
#define TC_MAX_V_BYTESPP        4

// audio defaults
#define RATE         48000
#define BITS            16
#define CHANNELS         2


#define SIZE_RGB_FRAME ((int) TC_MAX_V_FRAME_WIDTH*TC_MAX_V_FRAME_HEIGHT*(BPP/8))
#define SIZE_PCM_FRAME ((int) (RATE/MIN_FPS) * BITS/8 * CHANNELS * 3)

#define TC_LEAP_FRAME        1000

/*************************************************************************/

/* frame attributes */
typedef enum tcframeattributes_ TCFrameAttributes;
enum tcframeattributes_ {
    TC_FRAME_IS_KEYFRAME       =   1,
    TC_FRAME_IS_INTERLACED     =   2,
    TC_FRAME_IS_BROKEN         =   4,
    TC_FRAME_IS_SKIPPED        =   8,
    TC_FRAME_IS_CLONED         =  16,
    TC_FRAME_WAS_CLONED        =  32,
    TC_FRAME_IS_OUT_OF_RANGE   =  64,
    TC_FRAME_IS_DELAYED        = 128,
    TC_FRAME_IS_END_OF_STREAM  = 256,
};

#define TC_FRAME_NEED_PROCESSING(PTR) \
    (!((PTR)->attributes & TC_FRAME_IS_OUT_OF_RANGE) \
     && !((PTR)->attributes & TC_FRAME_IS_END_OF_STREAM))

typedef enum tcframestatus_ TCFrameStatus;
enum tcframestatus_ {
    TC_FRAME_NULL    = -1, /* on the frame pool, not yet claimed   */
    TC_FRAME_EMPTY   = 0,  /* claimed and being filled by decoder  */
    TC_FRAME_WAIT,         /* needs further processing (filtering) */
    TC_FRAME_LOCKED,       /* being procedded by filter layer      */
    TC_FRAME_READY,        /* ready to be processed by encoder     */
};

/*
 * frame status transitions scheme (overview)
 *
 *     
 *     .-------<----- +-------<------+------<------+-------<-------.
 *     |              ^              ^             ^               ^
 *     V              |              |             |               |
 * FRAME_NULL -> FRAME_EMPTY -> FRAME_WAIT -> FRAME_LOCKED -> FRAME_READY
 * :_buffer_:    \_decoder_/    \______filter_stage______/    \encoder_%/
 * \__pool__/         |         :                                  ^    :
 *                    |         \_______________encoder $__________|____/
 *                    V                                            ^
 *                    `-------------->------------->---------------'
 *
 * Notes:
 *  % - regular case, frame (processing) threads avalaibles
 *  $ - practical (default) case, filtering is carried by encoder thread.
 */

/*************************************************************************/

/*
 * NOTE: The following warning will become irrelevant once NMS is
 *       in place, and frame_list_t can go away completely.  --AC
 *       (here's a FIXME tag so we don't forget)
 *
 * BIG FAT WARNING:
 *
 * These structures must be kept in sync: meaning that if you add
 * another field to the vframe_list_t you must add it at the end
 * of the structure.
 *
 * aframe_list_t, vframe_list_t and the wrapper frame_list_t share
 * the same offsets to their elements up to the field "size". That
 * means that when a filter is called with at init time with the
 * anonymouse frame_list_t, it can already access the size.
 *
 *          -- tibit
 */

/* This macro factorizes common frame data fields.
 * Is not possible to completely factor out all frame_list_t fields
 * because video and audio typess uses different names for same fields,
 * and existing code relies on this assumption.
 * Fixing this is stuff for 1.2.0 and beyond, for which I would like
 * to introduce some generic frame structure or something like it. -- FR.
 */
#define TC_FRAME_COMMON \
    int id;                       /* frame id (sequential uint) */ \
    int bufid;                    /* buffer id                  */ \
    int tag;                      /* init, open, close, ...     */ \
    int filter_id;                /* filter instance to run     */ \
    TCFrameStatus status;         /* see enumeration above      */ \
    TCFrameAttributes attributes; /* see enumeration above      */ \
    uint64_t timestamp;                                            \
/* BEWARE: semicolon NOT NEEDED */

/* 
 * Size vs Length
 *
 * Size represent the effective size of audio/video buffer,
 * while length represent the amount of valid data into buffer.
 * Until 1.1.0, there isn't such distinction, and 'size'
 * have approximatively a mixed meaning of above.
 *
 * In the long shot[1] (post-1.1.0) transcode will start
 * intelligently allocate frame buffers based on highest
 * request of all modules (core included) through filter
 * mangling pipeline. This will lead on circumstances on
 * which valid data into a buffer is less than buffer size:
 * think to demuxer->decoder transition or RGB24->YUV420.
 * 
 * There also are more specific cases like a full-YUV420P
 * pipeline with final conversion to RGB24 and raw output,
 * so we can have something like
 *
 * framebuffer size = sizeof(RGB24_frame)
 * after demuxer:
 *     frame length << frame size (compressed data)
 * after decoder:
 *     frame length < frame size (YUV420P smaller than RGB24)
 * in filtering:
 *      frame length < frame size (as above)
 * after encoding (in fact just colorspace transition):
 *     frame length == frame size (data becomes RGB24)
 * into muxer:
 *     frame length == frame size (as above)
 *
 * In all those cases having a distinct 'lenght' fields help
 * make things nicer and easier.
 *
 * +++
 *
 * [1] in 1.1.0 that not happens due to module interface constraints
 * since we're still bound to Old Module System.
 */


typedef struct tcframe_ TCFrame;
struct tcframe_ {
    TC_FRAME_COMMON

    int codec;   /* codec identifier */

    int size;    /* buffer size avalaible */
    int len;     /* how much data is valid? */

    int param1;  /* v_width  or a_rate */
    int param2;  /* v_height or a_bits */
    int param3;  /* v_bpp    or a_chan */

    struct tcframe_ *next;
    struct tcframe_ *prev;
};
typedef struct tcframe_ frame_list_t;


typedef struct tcframevideo_ TCFrameVideo;
struct tcframevideo_ {
    TC_FRAME_COMMON
    /* frame physical parameter */
    
    int v_codec;       /* codec identifier */

    int video_size;    /* buffer size avalaible */
    int video_len;     /* how much data is valid? */

    int v_width;
    int v_height;
    int v_bpp;

    struct tcframevideo_ *next;
    struct tcframevideo_ *prev;

    uint8_t *video_buf;  /* pointer to current buffer */
    uint8_t *video_buf2; /* pointer to backup buffer */

    int free; /* flag */

#ifdef STATBUFFER
    uint8_t *internal_video_buf_0;
    uint8_t *internal_video_buf_1;
#else
    uint8_t internal_video_buf_0[SIZE_RGB_FRAME];
    uint8_t internal_video_buf_1[SIZE_RGB_FRAME];
#endif

    int deinter_flag;
    /* set to N for internal de-interlacing with "-I N" */

    uint8_t *video_buf_RGB[2];

    uint8_t *video_buf_Y[2];
    uint8_t *video_buf_U[2];
    uint8_t *video_buf_V[2];
};
typedef struct tcframevideo_ vframe_list_t;


typedef struct tcframeaudio_ TCFrameAudio;
struct tcframeaudio_ {
    TC_FRAME_COMMON

    int a_codec;       /* codec identifier */

    int audio_size;    /* buffer size avalaible */
    int audio_len;     /* how much data is valid? */

    int a_rate;
    int a_bits;
    int a_chan;

    struct tcframeaudio_ *next;
    struct tcframeaudio_ *prev;

    uint8_t *audio_buf;
    uint8_t *audio_buf2;

    int free; /* flag */

#ifdef STATBUFFER
    uint8_t *internal_audio_buf;
    uint8_t *internal_audio_buf_1;
#else
    uint8_t internal_audio_buf[SIZE_PCM_FRAME * 2];
    uint8_t internal_audio_buf_1[SIZE_PCM_FRAME * 2];
#endif
};
typedef struct tcframeaudio_ aframe_list_t;

/* 
 * generic pointer type, needed at least by internal code.
 * In the long (long) shot I'd like to use a unique generic
 * data container, like AVPacket (libavcodec) or something like it.
 * (see note about TC_FRAME_COMMON above) -- FR
 */
typedef union tcframeptr_ TCFramePtr;
union tcframeptr_ {
    TCFrame *generic;
    TCFrameVideo *video;
    TCFrameAudio *audio;
};

/*************************************************************************
 * FIXME
 * A TCFrameSource structure, along with it's operations, incapsulate
 * the actions needed by encoder to acquire and dispose a single A/V frame
 * to encode.
 *
 * Main purpose of this structure is to help to modularize and cleanup
 * encoder core code. Unfortunately, a propoer cleanup and refactoring isn't
 * fully possible without heavily reviewing transcode's inner frame buffering
 * and frame handling, but this task is really critical and should planned
 * really carefully.
 *
 * The need of TCFrameSource also emerges given the actual frame buffer
 * handling. TCFrameSource operations take care of hide the most part
 * of nasty stuff needed by current structure (see comments in
 * encoder-buffer.c).
 *
 * A proper reorganization of frame handling core code will GREATLY shrink,
 * or even make completely unuseful, the whole TCFrameSource machinery.
 */

typedef struct tcframesource_ TCFrameSource;
struct tcframesource_ {
    void          *privdata;

    TCJob         *job;

    TCFrameVideo* (*get_video_frame)(TCFrameSource *fs);
    TCFrameAudio* (*get_audio_frame)(TCFrameSource *fs);
    void          (*free_video_frame)(TCFrameSource *fs, TCFrameVideo *vf);
    void          (*free_audio_frame)(TCFrameSource *fs, TCFrameAudio *af);
};

/*************************************************************************/

/* 
 * frame*buffer* specifications, needed to properly allocate
 * and initialize single frame buffers
 */
typedef struct tcframespecs_ TCFrameSpecs;
struct tcframespecs_ {
    int frc;   /* frame ratio code is more precise than value */

    /* video fields */
    int width;
    int height;
    int format; /* TC_CODEC_reserve preferred,
                 * CODEC_reserve still supported for compatibility
                 */
    /* audio fields */
    int rate;
    int channels;
    int bits;

    /* private field, used internally */
    double samples;
};


/*************************************************************************/

#endif  /* FRAME_H */
