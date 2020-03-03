/*
 * tcinfo.h - definitions of info_t and decode_t
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef TCCORE_INFO_H
#define TCCORE_INFO_H

#include <stdint.h>

#include "probe.h"  // for ProbeInfo

/*************************************************************************/

typedef struct _info_t {

    int fd_in;          // Input stream file descriptor
    int fd_out;         // Output stream file descriptor

    long magic;         // Specifies file magic for extract thread
    int track;          // Track to extract
    long stype;         // Specifies stream type for extract thread
    long codec;         // Specifies codec for extract thread
    int verbose;        // Verbosity

    int dvd_title;
    int dvd_chapter;
    int dvd_angle;

    int vob_offset;

    int ps_unit;
    int ps_seq1;
    int ps_seq2;

    int ts_pid;

    int seek_allowed;

    int demux;
    int select;         // Selected packet payload type
    int subid;          // Selected packet substream ID
    int keep_seq;       // Do not drop first sequence (cluster mode)

    double fps;

    int fd_log;

    const char *name;   // Source name as supplied with -i option
    const char *nav_seek_file; // Seek/index file

    int probe;          // Flag for probe only mode
    int factor;         // Amount of file to probe, in MB

    ProbeInfo *probe_info;

    int quality;
    int error;

    long frame_limit[3];
    int hard_fps_flag;  // If this is set, disable demuxer smooth drop

} info_t;

typedef struct {
    int fd_in;          // Input stream file descriptor
    int fd_out;         // Output stream file descriptor
    double ac3_gain[3];
    long frame_limit[3];
    int dv_yuy2_mode;
    int padrate;        // Zero padding rate
    long magic;         // Specifies file magic
    long stype;         // Specifies stream type
    long codec;         // Specifies codec
    int verbose;        // Verbosity
    int quality;
    const char *name;   // Source name as supplied with -i option
    int width;
    int height;
    int a52_mode;
    long format;        // Specifies raw stream format for output
    int select;         // Selected packet payload type
    int accel; 
} decode_t;

/*************************************************************************/

#include "libtc/tccodecs.h"

typedef struct tcarea_ TCArea;
struct tcarea_ {
    int top;
    int left;
    int bottom;
    int right;
};

typedef struct tcmoduledescription_ TCModuleDescription;
struct tcmoduledescription_ {
    char *parm;
    char *name;
    char *opts;
};

typedef struct tcexportinfo_ TCExportInfo;
struct tcexportinfo_ {
    uint32_t attributes;

    /*
     * common fields for video/audio/mplex sections:
     * string      : generic opaque string identifier
     *               (for video, mainly used for fourcc).
     * module      : (NMS) encode module to use.
     * module_opts : (opaque) option string to module.
     *
     * common fields for video/audio sections:
     * format      : identifier of video format to use in encoding phase.
     *               Isn't a proper codec identifer since it can be a
     *               'special' format like TC_CODEC_COPY
     * FIXME: can sometimes be CODEC_XXX not TC_CODEC_XXX.
     * quality     : encoding quality condensed in a single param.
     *               Rarely used but still needed.
     * bitrate     : MEAN video bitrate to use (kbps) in encoding.
     */
    struct {
	TCModuleDescription module;
        
        TCCodecID format;
        int quality;
        int bitrate;

        char *log_file;
        /* path to log file to use, if needed, for multipass encoding. */

        int width;
        int height;
        /*
         * FINAL requested video frame width/height: encoded video stream will
         * have this width/height.
         */
        int keep_asr_flag;
        /* final asr should be forced equal to import asr? */
        int fast_resize_flag; /* self explanatory :) */
        int zoom_interlaced_flag; /* self explanatory :) */
        int asr;
        /* 
         * frame aspect ratio; often (but NOT always)
         * computed from width/height pair
         */

        int frc; /* frame rate code */
        int par;
        /* pixel aspect ratio; 1:1 by default, overridden by user in case */
        int encode_fields;
        /* field based encoding selection */

        TCArea pre_clip;
        /* clip specified area BEFORE any other operation */
        TCArea post_clip;
        /* clip specified area AFTER any other operation */

        int gop_size;
        /* video GOP size also known as keyframe interval */
        int quantizer_min;
        int quantizer_max;
        /* quantizer range to use in encoding */

        int bitrate_max;
        /* 
         * Maximum video bitrate to use (kbps) in encoding.
         * Rarely used but still needed.
         */

        int pass_number; /* set for usage in multipass encoding */
    } video;

    struct {
	TCModuleDescription module;

        TCCodecID format;
        int quality;
        int bitrate;

        int sample_rate; /* audio sample rate (Hz) */
        int sample_bits; /* bits to use for each audio sample */
        int channels; /* number of channels in audio stream */    
        int mode; /* audio mode: mode, stereo, joint stereo... */
        /* 
         * those last fields are mainly used by lame, but they
         * should be generalized
         */
        int vbr_flag;
        int flush_flag;
    } audio;

    struct {
	TCModuleDescription module;
	TCModuleDescription module_aux;

        char *out_file; /* self explanatory :) */
        char *out_file_aux; 
        /*
         * path of extra output file (separate audio track.
         * Provided for back compatibilty, can go away in future revisions.
         */
    } mplex;
};

/*************************************************************************/

#endif  // TCCORE_INFO_H

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
