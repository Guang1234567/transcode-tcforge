/*
 *  multiplexor.c -- transcode multiplexor, implementation.
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Updated and partially rewritten by
 *  Francesco Romani - January 2006
 *  New rotation code written by
 *  Francesco Romani - May 2006
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "tccore/tc_defaults.h"
#include "multiplexor.h"

#include <stdint.h>

/*************************************************************************/
/* private function prototypes                                           */
/*************************************************************************/

/* new-style rotation support */
static void tc_rotate_init(TCRotateContext *rotor, const char *base_name);

static void tc_rotate_set_frames_limit(TCRotateContext *rotor,
                                       uint32_t frames);
static void tc_rotate_set_bytes_limit(TCRotateContext *rotor,
                                      uint64_t bytes);

static const char *tc_rotate_output_name_add_id(TCRotateContext *rotor);
static const char *tc_rotate_output_name_null(TCRotateContext *rotor);

static int tc_rotate_needed_never(TCRotateContext *rotor,
                                  uint32_t frames,
                                  uint32_t bytes);
static int tc_rotate_needed_by_frames(TCRotateContext *rotor,
                                      uint32_t frames,
                                      uint32_t bytes);
static int tc_rotate_needed_by_bytes(TCRotateContext *rotor,
                                     uint32_t frames,
                                     uint32_t bytes);

static int tc_rotate_needed(TCRotateContext *rotor,
                            uint32_t frames, uint32_t bytes);

/*************************************************************************/

struct tcrotatecontext_ {
    char                path_buf[PATH_MAX+1];
    const char          *base_name;
    uint32_t            chunk_num;
    int                 null_flag;

    uint32_t            chunk_frames;
    uint32_t            encoded_frames;

    uint64_t            encoded_bytes;
    uint64_t            chunk_bytes;

    int                 (*rotate_needed)(TCRotateContext *rotor,
                                         uint32_t frames, uint32_t bytes);
    const char*         (*output_name)(TCRotateContext *rotor);
};

/*************************************************************************/

static int tc_rotate_needed(TCRotateContext *rotor,
                            uint32_t frames, uint32_t bytes)
{
    return rotor->rotate_needed(rotor, frames, bytes);
}

static const char *tc_rotate_output_name(TCRotateContext *rotor)
{
    return rotor->output_name(rotor);
}


static void tc_rotate_init(TCRotateContext *rotor, const char *base_name)
{
    if (rotor != NULL) {
        memset(rotor, 0, sizeof(TCRotateContext));

        rotor->base_name = base_name;
        if (base_name == NULL || strlen(base_name) == 0
         || strcmp(base_name, "/dev/null") == 0) {
            rotor->null_flag = TC_TRUE;
        } else {
            rotor->null_flag = TC_FALSE;
            strlcpy(rotor->path_buf, base_name, sizeof(rotor->path_buf));
        }
        rotor->rotate_needed = tc_rotate_needed_never;
        rotor->output_name   = tc_rotate_output_name_null;
    }
}

static const char *tc_rotate_output_name_null(TCRotateContext *rotor)
{
    return rotor->base_name;
}

static const char *tc_rotate_output_name_add_id(TCRotateContext *rotor)
{
    tc_snprintf(rotor->path_buf, sizeof(rotor->path_buf),
                "%s-%03i", rotor->base_name, rotor->chunk_num);
    rotor->encoded_frames = 0;
    rotor->encoded_bytes = 0;
    rotor->chunk_num++;
    return rotor->path_buf;
}

static void tc_rotate_set_frames_limit(TCRotateContext *rotor,
                                       uint32_t frames)
{
    if (rotor != NULL && !rotor->null_flag) {
        rotor->chunk_frames  = frames;
        rotor->rotate_needed = tc_rotate_needed_by_frames;
        rotor->output_name   = tc_rotate_output_name_add_id;
    }
}

static void tc_rotate_set_bytes_limit(TCRotateContext *rotor,
                                      uint64_t bytes)
{
    if (rotor != NULL && !rotor->null_flag) {
        rotor->chunk_bytes   = bytes;
        rotor->rotate_needed = tc_rotate_needed_by_bytes;
        rotor->output_name   = tc_rotate_output_name_add_id;
    }
}

/*************************************************************************/
/*
 * real rotation policy implementations. Rotate output file(s)
 * respectively:
 *  - never (_null)
 *  - when encoded frames reach the limit (_by_frames)
 *  - when encoded AND written *bytes* reach the limit (_by_bytes).
 *
 * For details see documentation of TCRotateContext above.
 */

#define ROTATE_UPDATE_COUNTERS(bytes, frames) do { \
    rotor->encoded_bytes  += (bytes); \
    rotor->encoded_frames += (frames); \
} while (0);

static int tc_rotate_needed_never(TCRotateContext *rotor,
                                  uint32_t frames,
                                  uint32_t bytes)
{
    ROTATE_UPDATE_COUNTERS(bytes, frames);
    return TC_FALSE;
}

static int tc_rotate_needed_by_frames(TCRotateContext *rotor,
                                      uint32_t frames,
                                      uint32_t bytes)
{
    int ret = TC_FALSE;
    ROTATE_UPDATE_COUNTERS(bytes, frames);

    if (rotor->encoded_frames >= rotor->chunk_frames) {
        ret = TC_TRUE;
    }
    return ret;
}

static int tc_rotate_needed_by_bytes(TCRotateContext *rotor,
                                     uint32_t frames,
                                     uint32_t bytes)
{
    int ret = TC_FALSE;
    ROTATE_UPDATE_COUNTERS(bytes, frames);

    if (rotor->encoded_bytes >= rotor->chunk_bytes) {
        ret = TC_TRUE;
    }
    return ret;
}

#undef ROTATE_UPDATE_COUNTERS

/*************************************************************************/
/* real multiplexor code                                                 */
/*************************************************************************/

void tc_multiplexor_limit_frames(TCMultiplexor *mux, uint32_t frames)
{
    tc_rotate_set_frames_limit(mux->rotor, frames);
}

void tc_multiplexor_limit_megabytes(TCMultiplexor *mux, uint32_t megabytes)
{
    tc_rotate_set_bytes_limit(mux->rotor, megabytes * 1024 * 1024);
}

/*************************************************************************/

static int muxer_open(TCModule mux_mod, TCRotateContext *rotor,
                      TCModuleExtraData *xdata[],
                      const char *tag)
{
    int ret;

    ret = tc_module_open(mux_mod,
                         tc_rotate_output_name(rotor),
                         xdata);
    if (ret != TC_OK) {
        tc_log_error(__FILE__,
                     "%s multiplexor module error: open failed",
                     tag);
    }
    return ret;
}

static int muxer_close(TCModule mux_mod, TCRotateContext **rotor,
                       const char *tag)
{
    int ret = TC_ERROR;
 
    ret = tc_module_close(mux_mod);
    if (ret == TC_OK && (rotor && *rotor)) {
        tc_free(*rotor);
        *rotor = NULL;
    }
    return ret;
}

/*************************************************************************/

static int mono_open(TCMultiplexor *mux)
{
    TCModuleExtraData *xdata[] = { mux->vid_xdata, mux->aud_xdata, NULL };
    return muxer_open(mux->mux_main, mux->rotor, xdata, "main");
}

static int mono_close(TCMultiplexor *mux)
{
    return muxer_close(mux->mux_main, &(mux->rotor), "main");
}

/* FIXME FIXME FIXME */
static int mono_rotate(TCMultiplexor *mux)
{
    int ret = muxer_close(mux->mux_main, NULL, "main");
    if (ret == TC_OK) {
        tc_log_info(__FILE__,
                    "rotating the main output stream to %s",
                    mux->rotor->path_buf);
        ret = mono_open(mux);
    }
    return ret;
}

static int mono_write(TCMultiplexor *mux, int can_rotate,
                      TCFrameVideo *vframe, TCFrameAudio *aframe)
{
    int vret = TC_ERROR, aret = TC_ERROR, need_rotate = TC_FALSE;

    mux->processed = 0;

    if (vframe) {
        vret = tc_module_write_video(mux->mux_main, vframe);
        if (vret >= 0) {
            need_rotate = tc_rotate_needed(mux->rotor, 1, vret);
            mux->processed |= TC_VIDEO;
        }
    } else {
        vret = TC_OK;
    }

    /* in mono muxer mode, a pair of frames is an atomic unit */ 

    if (aframe) {
        aret = tc_module_write_audio(mux->mux_main, aframe);
        if (aret >= 0) {
            need_rotate = tc_rotate_needed(mux->rotor, 1, aret);
            mux->processed |= TC_AUDIO;
        }
    } else {
        aret = TC_OK;
    }

    if (vret == TC_ERROR || aret == TC_ERROR) {
        return TC_ERROR;
    }
    if (can_rotate && need_rotate) {
        return mono_rotate(mux);
    }
    return TC_OK;
}

static int mono_setup(TCMultiplexor *mux, const char *sink_name)
{
    int ret;

    mux->rotor = tc_zalloc(sizeof(TCRotateContext));
    if (!mux->rotor) {
        goto alloc_failed;
    }
    tc_rotate_init(mux->rotor, mux->job->video_out_file);
    
    ret = mono_open(mux);
    if (ret != TC_OK) {
        goto open_failed;
    }
    
    mux->rotor_aux = mux->rotor;

    mux->open  = mono_open;
    mux->write = mono_write;
    mux->close = mono_close;

    return TC_OK;

open_failed:
    tc_free(mux->rotor);
    mux->rotor = NULL;
alloc_failed:
    tc_log_error(__FILE__,
                 "multiplexor module error: open failed");
    return TC_ERROR;
}


/*************************************************************************/

static int dual_open(TCMultiplexor *mux)
{
    int ret;

    TCModuleExtraData *vid_xdata[] = { mux->vid_xdata, NULL };
    TCModuleExtraData *aud_xdata[] = { mux->aud_xdata, NULL };

    ret = muxer_open(mux->mux_main, mux->rotor, vid_xdata, "main");
    if (ret == TC_OK) {
        ret = muxer_open(mux->mux_aux, mux->rotor_aux, aud_xdata, "aux");
    }
    return ret;
}

static int dual_close(TCMultiplexor *mux)
{
    int ret = muxer_close(mux->mux_main, &(mux->rotor), "main");
    if (ret == TC_OK) {
        ret = muxer_close(mux->mux_aux, &(mux->rotor_aux), "aux");
    }
    return ret;
}

/* FIXME */
static int stream_rotate(TCModule mux_mod, TCRotateContext *rotor,
                         TCModuleExtraData *xdata[],
                         const char *tag)
{
    int ret = muxer_close(mux_mod, NULL, tag);
    if (ret == TC_OK) {
        tc_log_info(__FILE__,
                    "rotating the %s output stream to %s",
                    tag, rotor->path_buf);
        ret = muxer_open(mux_mod, rotor, xdata, tag);
    }
    return ret;
}

static int dual_write(TCMultiplexor *mux, int can_rotate,
                      TCFrameVideo *vframe, TCFrameAudio *aframe)
{
    int vret = TC_ERROR, aret = TC_ERROR, need_rotate = TC_FALSE;
    TCModuleExtraData *vid_xdata[] = { mux->vid_xdata, NULL };
    TCModuleExtraData *aud_xdata[] = { mux->aud_xdata, NULL };

    mux->processed = 0;

    need_rotate = TC_FALSE;
    if (vframe) {
        vret = tc_module_write_video(mux->mux_main, vframe);
        if (vret >= 0) {
            need_rotate = tc_rotate_needed(mux->rotor, 1, vret);
            mux->processed |= TC_VIDEO;
        }
    } else {
        vret = TC_OK;
    }

    if (can_rotate && need_rotate) {
        vret = stream_rotate(mux->mux_main, mux->rotor,
                             vid_xdata, "video");
    }

    need_rotate = TC_FALSE;
    if (aframe) {
        aret = tc_module_write_audio(mux->mux_main, aframe);
        if (aret >= 0) {
            need_rotate = tc_rotate_needed(mux->rotor, 1, aret);
            mux->processed |= TC_AUDIO;
        }
    } else {
        aret = TC_OK;
    }

    if (can_rotate && need_rotate) {
        vret = stream_rotate(mux->mux_aux, mux->rotor_aux,
                             aud_xdata, "audio");
    }

    if (vret == TC_ERROR || aret == TC_ERROR) {
        return TC_ERROR;
    }
    return TC_OK;
}

static int dual_setup(TCMultiplexor *mux,
                      const char *sink_name, const char *sink_name_aux)
{
    int ret;

    mux->rotor = tc_zalloc(sizeof(TCRotateContext));
    if (!mux->rotor) {
        goto main_alloc_failed;
    }
    tc_rotate_init(mux->rotor, mux->job->video_out_file);

    mux->rotor_aux = tc_zalloc(sizeof(TCRotateContext));
    if (!mux->rotor_aux) {
        goto aux_alloc_failed;
    }
    tc_rotate_init(mux->rotor_aux, mux->job->audio_out_file);

    ret = dual_open(mux);
    if (ret != TC_OK) {
        goto open_failed;
    }

    mux->open  = dual_open;
    mux->write = dual_write;
    mux->close = dual_close;

    return TC_OK;

open_failed:
    tc_free(mux->rotor_aux);
    mux->rotor_aux = NULL;
aux_alloc_failed:
    tc_free(mux->rotor);
    mux->rotor = NULL;
main_alloc_failed:
    tc_log_error(__FILE__,
                 "multiplexor module error: open failed");
    return TC_ERROR;
}

/*************************************************************************/

static TCModule muxer_setup(TCMultiplexor *mux,
                            const char *mux_mod_name, int mtype,
                            const char *tag)
{
    TCModuleExtraData *xdata[] = { NULL };
    const char *options = NULL;
    TCModule mux_mod = NULL;
    int ret;

    mux_mod = tc_new_module_from_names(mux->factory,
                                       "multiplex", mux_mod_name, mtype);
    if (!mux_mod) {
        tc_log_error(__FILE__, "can't load %s module ", mux_mod_name);
        return NULL;
    }

    options = (mux->job->ex_m_string) ?mux->job->ex_m_string :"";
    ret = tc_module_configure(mux_mod, options, mux->job, xdata);
    if (ret != TC_OK) {
        tc_log_error(__FILE__, "%s module error: init failed",
                     mux_mod_name);
        return NULL;
    }
    return mux_mod;
}

static int muxer_shutdown(TCMultiplexor *mux, TCModule mux_mod)
{
    int ret = tc_module_stop(mux_mod);
    if (ret == TC_OK) {
        tc_del_module(mux->factory, mux_mod);
    }
    return ret;
}

/*************************************************************************/

int tc_multiplexor_init(TCMultiplexor *mux, TCJob *job, TCFactory factory)
{
    int ret = TC_ERROR;

    mux->processed  = 0;
    mux->job        = job;
    mux->factory    = factory;
    mux->mux_main   = NULL;
    mux->mux_aux    = NULL;

    mux->rotor      = NULL;
    mux->rotor_aux  = NULL;

    mux->vid_xdata  = NULL;
    mux->aud_xdata  = NULL;

    mux->has_aux    = TC_FALSE;

    mux->open       = NULL;
    mux->close      = NULL;
    mux->write      = NULL;

    ret = TC_OK;
    return ret;
}

int tc_multiplexor_fini(TCMultiplexor *mux)
{
    return TC_OK;
}


uint32_t tc_multiplexor_processed(TCMultiplexor *mux)
{
    return mux->processed;
}

/*************************************************************************/


int tc_multiplexor_setup(TCMultiplexor *mux,
                         const char *mux_mod_name,
                         const char *mux_mod_name_aux)
{
    int mtype = (mux_mod_name_aux) ?TC_VIDEO :(TC_VIDEO|TC_AUDIO);
    int ret = TC_ERROR;

    tc_debug(TC_DEBUG_MODULES, "loading multiplexor modules");

    mux->mux_main = muxer_setup(mux, mux_mod_name, mtype, "multiplexor");
    if (mux->mux_main) {
        if (!mux_mod_name_aux) {
            mux->has_aux = TC_FALSE;
            mux->mux_aux = mux->mux_main;
            ret          = TC_OK;
        } else {
            mux->has_aux = TC_TRUE;
            mux->mux_aux = muxer_setup(mux, mux_mod_name_aux,
                                       TC_AUDIO, "aux multiplexor");
            if (mux->mux_aux) {
                ret = TC_OK;
            }
        }
    }
    return ret;
}

int tc_multiplexor_shutdown(TCMultiplexor *mux)
{
    int ret;

    tc_debug(TC_DEBUG_MODULES, "unloading multiplexor modules");

    ret = muxer_shutdown(mux, mux->mux_main);
    if (mux->has_aux) {
        ret = muxer_shutdown(mux, mux->mux_aux);
    }
    return ret;
}

/*************************************************************************/

int tc_multiplexor_open(TCMultiplexor *mux,
                        const char *sink_name,
                        const char *sink_name_aux,
                        TCModuleExtraData *vid_xdata,
                        TCModuleExtraData *aud_xdata)
{
    /* sanity checks */
    if (mux->has_aux && !sink_name_aux) {
        tc_log_error(__FILE__, "multiplexor: missing auxiliary file name");
        return TC_ERROR;
    }
    tc_debug(TC_DEBUG_MODULES, "multiplexor opened");

    mux->vid_xdata = vid_xdata;
    mux->aud_xdata = aud_xdata;

    if (mux->has_aux) {
        return dual_setup(mux, sink_name, sink_name_aux);
    }
    return mono_setup(mux, sink_name);
}

int tc_multiplexor_close(TCMultiplexor *mux)
{
    tc_debug(TC_DEBUG_CLEANUP, "multiplexor closed");

    return mux->close(mux);
 }

/*************************************************************************/

/* write and rotate if needed */
int tc_multiplexor_export(TCMultiplexor *mux,
                          TCFrameVideo *vframe, TCFrameAudio *aframe)
{
    return mux->write(mux, TC_TRUE, vframe, aframe);
}

/* just write */
int tc_multiplexor_write(TCMultiplexor *mux,
                         TCFrameVideo *vframe, TCFrameAudio *aframe)
{
    return mux->write(mux, TC_FALSE, vframe, aframe);
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */

