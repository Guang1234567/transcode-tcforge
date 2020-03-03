/*
 *  encoder.c -- transcode export layer module, implementation.
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


#include <stdint.h>

#include "tccore/tc_defaults.h"

#include "encoder.h"


/*************************************************************************/

int tc_encoder_init(TCEncoder *enc, 
                    TCJob *job, TCFactory factory)
{
    enc->job        = job;
    enc->factory    = factory;
    enc->aud_mod    = NULL;
    enc->vid_mod    = NULL;
    enc->processed  = 0;

    return TC_OK;
}


int tc_encoder_fini(TCEncoder *enc)
{
    /* do nothing (...yet) succesfully. */
    return TC_OK;
}

uint32_t tc_encoder_processed(TCEncoder *enc)
{
    return enc->processed;
}

/*************************************************************************/

#define CLEAN(ENC)      (ENC)->processed = 0
#define SETOK(ENC, M)   (ENC)->processed |= (M)

int tc_encoder_setup(TCEncoder *enc,
                     const char *vid_mod_name, const char *aud_mod_name)
{
    const char *mod_name = NULL;

    CLEAN(enc);
    tc_debug(TC_DEBUG_MODULES, "loading export modules");

    mod_name = (aud_mod_name) ?aud_mod_name :TC_DEFAULT_EXPORT_AUDIO;
    enc->aud_mod = tc_new_module_from_names(enc->factory,
                                            "encode", mod_name, TC_AUDIO);
    if (enc->aud_mod) {
        SETOK(enc, TC_AUDIO);
    } else {
        tc_log_error(__FILE__, "can't load audio encoder");
        return TC_ERROR;
    }
    mod_name = (vid_mod_name) ?vid_mod_name :TC_DEFAULT_EXPORT_VIDEO;
    enc->vid_mod = tc_new_module_from_names(enc->factory,
                                            "encode", mod_name, TC_VIDEO);
    if (enc->vid_mod) {
        SETOK(enc, TC_VIDEO);
    } else {
        tc_log_error(__FILE__, "can't load video encoder");
        return TC_ERROR;
    }
    return TC_OK;
}

void tc_encoder_shutdown(TCEncoder *enc)
{
    CLEAN(enc);
    tc_debug(TC_DEBUG_MODULES, "unloading export modules");

    tc_del_module(enc->factory, enc->vid_mod);
    SETOK(enc, TC_VIDEO);
    tc_del_module(enc->factory, enc->aud_mod);
    SETOK(enc, TC_AUDIO);
}


int tc_encoder_open(TCEncoder *enc,
                    TCModuleExtraData *vid_xdata,
                    TCModuleExtraData *aud_xdata)
{
    TCModuleExtraData *xdata[] = { NULL, NULL };
    const char *options = NULL;
    int ret;
    CLEAN(enc);

    options = (enc->job->ex_v_string) ?enc->job->ex_v_string :"";
    xdata[0] = vid_xdata;
    ret = tc_module_configure(enc->vid_mod, options, enc->job, xdata);
    if (ret == TC_OK) {
        SETOK(enc, TC_VIDEO);
    } else {
        tc_log_error(__FILE__, "video export module error: init failed");
        return TC_ERROR;
    }

    options = (enc->job->ex_a_string) ?enc->job->ex_a_string :"";
    xdata[0] = aud_xdata;
    ret = tc_module_configure(enc->aud_mod, options, enc->job, xdata);
    if (ret == TC_OK) {
        SETOK(enc, TC_AUDIO);
    } else {
        tc_log_warn(__FILE__, "audio export module error: init failed");
        return TC_ERROR;
    }

    return TC_OK;
}


int tc_encoder_close(TCEncoder *enc)
{
    int ret;
    CLEAN(enc);

    ret = tc_module_stop(enc->vid_mod);
    if (ret == TC_OK) {
        SETOK(enc, TC_VIDEO);
    } else {
        tc_log_warn(__FILE__, "video export module error: stop failed");
        return TC_ERROR;
    }

    ret = tc_module_stop(enc->aud_mod);
    if (ret == TC_OK) {
        SETOK(enc, TC_AUDIO);
    } else {
        tc_log_warn(__FILE__, "audio export module error: stop failed");
        return TC_ERROR;
    }

    tc_debug(TC_DEBUG_CLEANUP, "encoder stopped");
    return TC_OK;
}


int tc_encoder_process(TCEncoder *enc,
                       TCFrameVideo *vin, TCFrameVideo *vout,
                       TCFrameAudio *ain, TCFrameAudio *aout)
{
    int video_delayed = 0;
    int ret, result = TC_OK;

    CLEAN(enc);
    /* remove spurious attributes */
    vin->attributes = 0;
    ain->attributes = 0;

    /* step 1: encode video */
    ret = tc_module_encode_video(enc->vid_mod, vin, vout);
    if (ret == TC_OK) {
        SETOK(enc, TC_VIDEO);
    } else {
        tc_log_error(__FILE__, "error encoding video frame");
        result = TC_ERROR;
    }
    if (vin->attributes & TC_FRAME_IS_DELAYED) {
        vin->attributes &= ~TC_FRAME_IS_DELAYED;
        video_delayed = 1;
    }

    /* step 2: encode audio */
    if (video_delayed) {
        ain->attributes |= TC_FRAME_IS_CLONED;
        tc_log_info(__FILE__, "Delaying audio");
    } else {
        ret = tc_module_encode_audio(enc->aud_mod, ain, aout);
        if (ret == TC_OK) {
            SETOK(enc, TC_AUDIO);
        } else {
            tc_log_error(__FILE__, "error encoding audio frame");
            result = TC_ERROR;
        }
    }

    return result;
}

int tc_encoder_flush(TCEncoder *enc,
                     TCFrameVideo *vout, TCFrameAudio *aout)
{
    int ret, frame_returned, result = 0;

    /* step 1: flush video */
    ret = tc_module_flush_video(enc->vid_mod, vout, &frame_returned);
    if (ret == TC_ERROR) {
        tc_log_error(__FILE__, "error flushing video encoder");
        result = -1;
    } else {
        SETOK(enc, TC_VIDEO);
        if (frame_returned) {
            result |= TC_VIDEO;
        }
    }
    
    ret = tc_module_flush_audio(enc->aud_mod, aout, &frame_returned);
    if (ret == TC_ERROR) {
        tc_log_error(__FILE__, "error flushing audio encoder");
        result = -1;
    } else {
        SETOK(enc, TC_AUDIO);
        if (frame_returned) {
            result |= TC_AUDIO;  // Won't change an error (-1) return value
        }
    }

    return result;
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

