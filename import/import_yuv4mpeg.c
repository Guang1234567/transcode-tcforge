/*
 *  import_yuv4mpeg.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Copyright (C) Francesco Romani - March 2006
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

#define MOD_NAME    "import_yuv4mpeg.so"
#define MOD_VERSION "v0.3.0 (2006-03-03)"
#define MOD_CODEC   "(video) YUV4MPEG2 | (audio) WAVE"

#include "src/transcode.h"
#include "libtcvideo/tcvideo.h"
#include "avilib/wavlib.h"

#if defined(HAVE_MJPEGTOOLS_INC)
#include "yuv4mpeg.h"
#include "mpegconsts.h"
#else
#include "mjpegtools/yuv4mpeg.h"
#include "mjpegtools/mpegconsts.h"
#endif

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB|TC_CAP_YUV|TC_CAP_PCM;

#define MOD_PRE yuv4mpeg
#include "import_def.h"

typedef struct {
    int fd_vid;
    WAV wav;

    y4m_frame_info_t frameinfo;
    y4m_stream_info_t streaminfo;

    TCVHandle tcvhandle;
    ImageFormat dstfmt;

    int width;
    int height;
    
    uint8_t *planes[3];
} YWPrivateData;

static YWPrivateData pd = {
    .fd_vid = -1,
    .wav = NULL,

    .tcvhandle = NULL,

    .width = 0,
    .height = 0,
};

/* ------------------------------------------------------------
 * helpers: declarations
 * ------------------------------------------------------------*/

static int yw_open_video(YWPrivateData *pd, vob_t *vob);
static int yw_open_audio(YWPrivateData *pd, vob_t *vob);
static int yw_decode_video(YWPrivateData *pd, transfer_t *param);
static int yw_decode_audio(YWPrivateData *pd, transfer_t *param);
static int yw_close_video(YWPrivateData *pd);
static int yw_close_audio(YWPrivateData *pd);

MOD_open
{
    if(param->flag == TC_VIDEO) {
        return yw_open_video(&pd, vob);
    }
    if(param->flag == TC_AUDIO) {
        return yw_open_audio(&pd, vob);
    }
    return(TC_IMPORT_ERROR);
}


MOD_decode
{
    if(param->flag == TC_VIDEO) {
        return yw_decode_video(&pd, param);
    }
    if(param->flag == TC_AUDIO) {
        return yw_decode_audio(&pd, param);
    }
    return(TC_IMPORT_ERROR);
}


MOD_close
{
    if(param->flag == TC_VIDEO) {
        return yw_close_video(&pd);
    }
    if(param->flag == TC_AUDIO) {
        return yw_close_audio(&pd);
    }
    return(TC_IMPORT_ERROR);
}

/* ------------------------------------------------------------
 * helpers: implementations
 * ------------------------------------------------------------*/

static int yw_open_video(YWPrivateData *pd, vob_t *vob)
{
    int errnum = Y4M_OK;
    int ch_mode = 0;
        
    /* initialize stream-information */
    y4m_accept_extensions(1);
    y4m_init_stream_info(&pd->streaminfo);
    y4m_init_frame_info(&pd->frameinfo);
    
    if (vob->im_v_codec == TC_CODEC_YUV420P) {
	    pd->dstfmt = IMG_YUV_DEFAULT;
    } else if (vob->im_v_codec == TC_CODEC_RGB24) {
	    pd->dstfmt = IMG_RGB_DEFAULT;
    } else {
	    tc_log_error(MOD_NAME, "unsupported video format %d",
		            vob->im_v_codec);
        return(TC_EXPORT_ERROR);
    }

    /* we trust autoprobing */
    pd->width = vob->im_v_width;
    pd->height = vob->im_v_height;
    
    pd->fd_vid = open(vob->video_in_file, O_RDONLY);
    if (pd->fd_vid == -1) {
        tc_log_error(MOD_NAME, "can't open video source '%s'"
                               " (reason: %s)", vob->video_in_file,
                               strerror(errno));
    } else {
        if (verbose >= TC_DEBUG) {
            tc_log_info(MOD_NAME, "using video source: %s",
                                  vob->video_in_file);
        }
    }
    
    pd->tcvhandle = tcv_init();
    if (!pd->tcvhandle) {
	tc_log_error(MOD_NAME, "image conversion init failed");
        return(TC_EXPORT_ERROR);
    }

    errnum = y4m_read_stream_header(pd->fd_vid, &pd->streaminfo);
    if (errnum != Y4M_OK) {
        tc_log_error(MOD_NAME, "Couldn't read YUV4MPEG header: %s!",
                     y4m_strerr(errnum));
        tcv_free(pd->tcvhandle);
        close(pd->fd_vid);
        return(TC_IMPORT_ERROR);
    }
    
    if (y4m_si_get_plane_count(&pd->streaminfo) != 3) {
        tc_log_error(MOD_NAME, "Only 3-plane formats supported");
        close(pd->fd_vid);
        return(TC_IMPORT_ERROR);
    }
    ch_mode = y4m_si_get_chroma(&pd->streaminfo);
    if (ch_mode != Y4M_CHROMA_420JPEG
     && ch_mode != Y4M_CHROMA_420MPEG2
     && ch_mode != Y4M_CHROMA_420PALDV) {
        tc_log_error(MOD_NAME, "sorry, chroma mode `%s' (%i) not supported",
                     y4m_chroma_description(ch_mode), ch_mode);
        tcv_free(pd->tcvhandle);
        close(pd->fd_vid);
        return(TC_IMPORT_ERROR);
    }

    if (verbose) {
        tc_log_info(MOD_NAME, "chroma mode: %s",
                    y4m_chroma_description(ch_mode));
    }
    return(TC_IMPORT_OK);
}

static int yw_open_audio(YWPrivateData *pd, vob_t *vob)
{
    WAVError err;

    if (!vob->audio_in_file
      || !strcmp(vob->video_in_file, vob->audio_in_file)) {
        tc_log_error(MOD_NAME, "missing or bad audio source file,"
                               " please specify it");
        return(TC_IMPORT_ERROR);
    }
        
    pd->wav = wav_open(vob->audio_in_file, WAV_READ, &err);
    if (!pd->wav) {
        tc_log_error(MOD_NAME, "can't open audio source '%s'"
                               " (reason: %s)", vob->audio_in_file,
                               wav_strerror(err));
    } else {
        if (verbose >= TC_DEBUG) {
            tc_log_info(MOD_NAME, "using audio source: %s",
                                  vob->audio_in_file);
        }
    }
    return(TC_IMPORT_OK);
}

static int yw_decode_video(YWPrivateData *pd, transfer_t *param)
{
    int errnum = 0;
    YUV_INIT_PLANES(pd->planes, param->buffer, pd->dstfmt,
                    pd->width, pd->height);
    
    errnum = y4m_read_frame(pd->fd_vid, &pd->streaminfo,
                            &pd->frameinfo, pd->planes);
    if (errnum != Y4M_OK) {
        if (verbose & TC_DEBUG) {
            tc_log_warn(MOD_NAME, "YUV4MPEG2 video read failed: %s",
                        y4m_strerr(errnum));
        }
        return(TC_IMPORT_ERROR);
    }
    return(TC_IMPORT_OK);
}
        
static int yw_decode_audio(YWPrivateData *pd, transfer_t *param)
{
    ssize_t r = wav_read_data(pd->wav, param->buffer, param->size);
    
    if (r <= 0 || (int)r < param->size) {
        if (verbose & TC_DEBUG) {
            tc_log_warn(MOD_NAME, "WAV audio read failed");
        }
        return(TC_IMPORT_ERROR);
    }
    return(TC_IMPORT_OK);
}

/* errors not fatal (silently ignored) */
static int yw_close_video(YWPrivateData *pd)
{
    if (pd->fd_vid != -1) {
        y4m_fini_frame_info(&pd->frameinfo);
        y4m_fini_stream_info(&pd->streaminfo);
   
        close(pd->fd_vid);
        pd->fd_vid = -1;
    }
    return(TC_IMPORT_OK);
}

/* errors not fatal (silently ignored) */
static int yw_close_audio(YWPrivateData *pd)
{
    if (pd->wav != NULL) {
        wav_close(pd->wav);
        pd->wav = NULL;
    }
    return(TC_IMPORT_OK);
}

