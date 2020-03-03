/*
 *  transcode.h
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

#ifndef _TRANSCODE_H
#define _TRANSCODE_H

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

#include "aclib/ac.h"

#include "libtc/libtc.h"
#include "libtcmodule/tcmodule-registry.h"

#include "tccore/tc_defaults.h"
#include "tccore/frame.h"
#include "tccore/job.h"

#include "framebuffer.h"

/*************************************************************************/

typedef struct subtitle_header_s {

    unsigned int header_length;
    unsigned int header_version;
    unsigned int payload_length;

    unsigned int lpts;
    double rpts;

    unsigned int discont_ctr;

} subtitle_header_t;

/* anyone with a better naming? */
/* FIXME: for starter, that's just a repack of the bunch of globals formerly
 * found on transcode.c/cmdline.c. We need a serious cleanup/redesign ASAP.
 */
typedef struct tcsession_ TCSession;
struct tcsession_ {
/* those belongs to a session (aka: are here by purpose) */
    TCJob *job;

    pid_t tc_pid;

    int acceleration;

    TCFactory factory;
    TCRegistry registry;

/* reorganize the following */
    int core_mode;

    char *im_aud_mod;
    char *im_vid_mod;

    const char *ex_aud_mod;
    const char *ex_vid_mod;
    const char *ex_mplex_mod;
    const char *ex_mplex_mod_aux;

    char *plugins_string;

    char *nav_seek_file;
    char *socket_file;
    char *chbase;
    char base[TC_BUF_MIN];

    int buffer_delay_dec;
    int buffer_delay_enc;
    int cluster_mode;
    int decoder_delay;
    int progress_meter;
    int progress_rate;

    int niceness;

    int max_frame_buffers;
    int max_frame_threads;
    int hw_threads;
    /* how many threads the HW can do in parallel? */

    int psu_frame_threshold;
    
    // FIXME: those must go away soon
    // begin
    int no_vin_codec;
    int no_ain_codec;
    int no_v_out_codec;
    int no_a_out_codec;
    // end
    
    int frame_a; /* processing interval: start frame */
    int frame_b; /* processing interval: stop frame */

    int split_time; /* frames */
    int split_size; /* megabytes */
    int psu_mode;

    int preset_flag;
    int auto_probe;
    int seek_range;

    int audio_adjust;
    int split; /* XXX */

    char *fc_ttime_string;

    int sync_seconds;

    pid_t tc_probe_pid;
};

/*************************************************************************/

// Module functions

int tc_import(int opt, void *para1, void *para2);
int tc_export(int opt, void *para1, void *para2);

// Some functions exported by transcode

TCSession *tc_get_session(void);

vob_t *tc_get_vob(void);

int tc_next_video_in_file(vob_t *vob);
int tc_next_audio_in_file(vob_t *vob);

int tc_has_more_video_in_file(TCSession *session);
int tc_has_more_audio_in_file(TCSession *session);

/* default main transcode buffer */
TCFrameSource *tc_get_ringbuffer(TCJob *job, int aworkers, int vworkers);

void version(void);

extern int verbose;
extern int rescale;
extern int im_clip;
extern int ex_clip;
extern int pre_im_clip;
extern int post_ex_clip;
extern int resize1;
extern int resize2;
extern int decolor;

// Core parameters

// Various constants
enum {
    TC_EXPORT_NAME = 10,
    TC_EXPORT_OPEN,
    TC_EXPORT_INIT,
    TC_EXPORT_ENCODE,
    TC_EXPORT_CLOSE,
    TC_EXPORT_STOP,
};

enum {
    TC_EXPORT_ERROR   = -1,
    TC_EXPORT_OK      =  0,
    TC_EXPORT_UNKNOWN =  1,
};

enum {
    TC_IMPORT_NAME = 20,
    TC_IMPORT_OPEN,
    TC_IMPORT_DECODE,
    TC_IMPORT_CLOSE,
};

enum {
    TC_IMPORT_ERROR    = -1,
    TC_IMPORT_OK       =  0,
    TC_IMPORT_UNKNOWN  =  1,
};

enum {
    TC_CAP_NONE   =   0,
    TC_CAP_PCM    =   1,
    TC_CAP_RGB    =   2,
    TC_CAP_AC3    =   4,
    TC_CAP_YUV    =   8,
    TC_CAP_AUD    =  16,
    TC_CAP_VID    =  32,
    TC_CAP_MP3    =  64,
    TC_CAP_YUY2   = 128,
    TC_CAP_DV     = 256,
    TC_CAP_YUV422 = 512,
};

/*************************************************************************/
/*************************************************************************/

#endif  // _TRANSCODE_H

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
