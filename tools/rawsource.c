/*
 * rawsource.c -- (almost) raw source reader interface for encoder
 *                expect WAV audio and YUV4MPEG2 video
 * (C) 2006-2010 - Francesco Romani <fromani at gmail dot com>
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


#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "transcode.h"
#include "framebuffer.h"
#include "dl_loader.h"
#include "rawsource.h"
#include "libtc/libtc.h"
#include "libtc/tcframes.h"
#include "avilib/wavlib.h"
#include "rawsource.h"

#define RAWSOURCE_IM_MOD    "yuv4mpeg"



/*************************************************************************/

typedef struct tcrawsource_ TCRawSource;
struct tcrawsource_ {
    int             inited;
    void            *im_handle;

    int             eof_flag;
    int             sources;

    int             vframe_id;
    int             aframe_id;

    TCFrameVideo    *vframe;
    TCFrameAudio    *aframe;
    int             acount;
    int             num_sources;
};

/*************************************************************************/


static TCFrameVideo *rawsource_read_video(TCFrameSource *FS)
{
    TCRawSource *rawsource = NULL;
    transfer_t im_para;
    int ret = 0;

    if (!FS) {
        /* paranoia */
        return NULL;
    }

    rawsource = FS->privdata;

    if (FS->job->im_v_size > rawsource->vframe->video_size) {
        /* paranoia */
        tc_log_error(__FILE__, "video buffer too small"
                               " (this should'nt happen)");
        return NULL;
    }

    im_para.fd         = NULL;
    im_para.attributes = 0;
    im_para.buffer     = rawsource->vframe->video_buf;
    im_para.buffer2    = NULL;
    im_para.size       = FS->job->im_v_size;
    im_para.flag       = TC_VIDEO;

    ret = tcv_import(TC_IMPORT_DECODE, &im_para, FS->job);
    if (ret != TC_IMPORT_OK) {
        /* read failed */
        rawsource->eof_flag = TC_TRUE;
        return NULL;
    }
    rawsource->vframe->video_size = im_para.size;
    rawsource->vframe->attributes = im_para.attributes;
    rawsource->vframe->id         = rawsource->vframe_id++;

    return rawsource->vframe;
}

static TCFrameAudio *rawsource_read_audio(TCFrameSource *FS)
{
    TCRawSource *rawsource = NULL;
    transfer_t im_para;
    int ret = 0, abytes = 0;

    if (!FS) {
        /* paranoia */
        return NULL;
    }

    rawsource = FS->privdata;

    abytes = FS->job->im_a_size;
    // audio adjustment for non PAL frame rates:
    if (rawsource->acount != 0 && rawsource->acount % TC_LEAP_FRAME == 0) {
        abytes += FS->job->a_leap_bytes;
    }

    if (abytes > rawsource->aframe->audio_size) {
        /* paranoia */
        tc_log_error(__FILE__, "audio buffer too small"
                               " (this should'nt happen)");
        return NULL;
    }

    im_para.fd         = NULL;
    im_para.attributes = 0;
    im_para.buffer     = rawsource->aframe->audio_buf;
    im_para.buffer2    = NULL;
    im_para.size       = abytes;
    im_para.flag       = TC_AUDIO;

    ret = tca_import(TC_IMPORT_DECODE, &im_para, FS->job);
    if (ret != TC_IMPORT_OK) {
        /* read failed */
        rawsource->eof_flag = TC_TRUE;
        return NULL;
    }
    rawsource->acount++;
    rawsource->aframe->audio_size = im_para.size;
    rawsource->aframe->attributes = im_para.attributes;
    rawsource->aframe->id         = rawsource->aframe_id++;

    return rawsource->aframe;
}

static void rawsource_free_video(TCFrameSource *FS, TCFrameVideo *vf)
{
    /* nothing to do here. */
    return;
}

static void rawsource_free_audio(TCFrameSource *FS, TCFrameAudio *af)
{
    /* nothing to do here. */
    return;
}


/*************************************************************************/

static TCRawSource rawsource = {
    .im_handle  = NULL,

    .eof_flag   = TC_FALSE,
    .sources    = 0,

    .vframe     = NULL,
    .aframe     = NULL,
    .acount     = 0,
};
static TCFrameSource framesource = {
    .privdata           = &rawsource,
    .job                = NULL,
    
    .get_video_frame    = rawsource_read_video,
    .get_audio_frame    = rawsource_read_audio,
    .free_video_frame   = rawsource_free_video,
    .free_audio_frame   = rawsource_free_audio,
};

/*************************************************************************/

static int tc_rawsource_do_open(TCFrameSource *FS, TCJob *job)
{
    TCRawSource *rawsource = FS->privdata;
    transfer_t im_para;
    double samples = 0.0;
    int ret = 0;

    rawsource->num_sources = 0;
    rawsource->vframe_id   = 0;
    rawsource->aframe_id   = 0;

    if (!job) {
        goto vframe_failed;
    }

    rawsource->vframe = tc_new_video_frame(job->im_v_width, job->im_v_height,
                                           job->im_v_codec, TC_TRUE);
    if (!rawsource->vframe) {
        tc_log_error(__FILE__, "can't allocate video frame buffer");
        goto vframe_failed;
    }
    samples = TC_AUDIO_SAMPLES_IN_FRAME(job->a_rate, job->ex_fps);
    rawsource->aframe = tc_new_audio_frame(samples, job->a_chan, job->a_bits);
    if (!rawsource->aframe) {
        tc_log_error(__FILE__, "can't allocate audio frame buffer");
        goto aframe_failed;
    }

	rawsource->im_handle = load_module(RAWSOURCE_IM_MOD, TC_IMPORT|TC_AUDIO|TC_VIDEO);
	if (!rawsource->im_handle) {
        tc_log_error(__FILE__, "can't load import module");
        goto load_failed;
    }

    /* hello, module! */
	memset(&im_para, 0, sizeof(transfer_t));
	im_para.flag = job->verbose;
	tca_import(TC_IMPORT_NAME, &im_para, NULL);

    memset(&im_para, 0, sizeof(transfer_t));
	im_para.flag = job->verbose;
	tcv_import(TC_IMPORT_NAME, &im_para, NULL);

    /* open the sources! */
	memset(&im_para, 0, sizeof(transfer_t));
    im_para.flag = TC_AUDIO;
    ret = tca_import(TC_IMPORT_OPEN, &im_para, job);
    if (TC_IMPORT_OK != ret) {
        tc_log_warn(__FILE__, "audio open failed (ret=%i)", ret);
    } else {
        rawsource->sources |= TC_AUDIO;
        rawsource->num_sources++;
    }

	memset(&im_para, 0, sizeof(transfer_t));
    im_para.flag = TC_VIDEO;
    ret = tcv_import(TC_IMPORT_OPEN, &im_para, job);
    if (TC_IMPORT_OK != ret) {
        tc_log_warn(__FILE__, "video open failed (ret=%i)", ret);
    } else {
        rawsource->sources |= TC_VIDEO;
        rawsource->num_sources++;
    }

    return rawsource->num_sources;

load_failed:
    tc_del_audio_frame(rawsource->aframe);
aframe_failed:
    tc_del_video_frame(rawsource->vframe);
vframe_failed:
    return -1;
}

static void tc_rawsource_free(TCRawSource *rawsource)
{
    if (rawsource->vframe != NULL) {
        tc_del_video_frame(rawsource->vframe);
        rawsource->vframe = NULL;
    }
    if (rawsource->aframe != NULL) {
        tc_del_audio_frame(rawsource->aframe);
        rawsource->aframe = NULL;
    }
}

static int tc_rawsource_do_close(TCFrameSource *FS)
{
    TCRawSource *rawsource = FS->privdata;

    tc_rawsource_free(rawsource);

    if (rawsource->im_handle != NULL) {
        transfer_t im_para;
        int ret = 0;

	    memset(&im_para, 0, sizeof(transfer_t));
        im_para.flag = TC_VIDEO;
        ret = tcv_import(TC_IMPORT_CLOSE, &im_para, NULL);
        if(ret != TC_IMPORT_OK) {
            tc_log_warn(__FILE__, "video import module error: CLOSE failed");
        } else {
            rawsource->sources &= ~TC_VIDEO;
        }

        memset(&im_para, 0, sizeof(transfer_t));
        im_para.flag = TC_AUDIO;
        ret = tca_import(TC_IMPORT_CLOSE, &im_para, NULL);
        if(ret != TC_IMPORT_OK) {
            tc_log_warn(__FILE__, "audio import module error: CLOSE failed");
        } else {
            rawsource->sources &= ~TC_AUDIO;
        }

        if (!rawsource->sources) {
            unload_module(rawsource->im_handle);
            rawsource->im_handle = NULL;
        }
    }
    return 0;
}

/*************************************************************************/
/* last but not least, our entry points                                  */
/*************************************************************************/

int tc_rawsource_num_sources(void)
{
    TCRawSource *rawsource = framesource.privdata;
    return rawsource->num_sources;
}

TCFrameSource *tc_rawsource_open(TCJob *job)
{
    int ret = 0;

    framesource.job = job;

    ret = tc_rawsource_do_open(&framesource, job);
    if (ret > 0) {
        return &framesource;
    }
    return NULL;
}

/* errors not fatal, but notified */
int tc_rawsource_close(void)
{
    return tc_rawsource_do_close(&framesource);
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
