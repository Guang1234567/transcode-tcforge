/*
 *  extract_yuv.c
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

#include "tccore/tcinfo.h"
#include "src/transcode.h"
#include "src/framebuffer.h"
#include "libtc/libtc.h"
#include "libtc/ratiocodes.h"
#include "libtc/tcframes.h"

#include "ioaux.h"
#include "avilib/avilib.h"
#include "tc.h"

#if defined HAVE_MJPEGTOOLS
/* assert using new code (FIXME?) */

#if defined(HAVE_MJPEGTOOLS_INC)
#include "yuv4mpeg.h"
#include "mpegconsts.h"
#else
#include "mjpegtools/yuv4mpeg.h"
#include "mjpegtools/mpegconsts.h"
#endif

/* ------------------------------------------------------------
 *
 * yuv extract thread
 *
 * magic: TC_MAGIC_YUV4MPEG
 *        TC_MAGIC_RAW      <-- default
 *
 *
 * ------------------------------------------------------------*/


static int extract_yuv_y4m(info_t *ipipe)
{
    vframe_list_t *vptr = NULL;
    uint8_t *planes[3];
    int planesize[3];
    int ch_mode, w, h, ret = 0, i = 0, errnum;

    y4m_frame_info_t frameinfo;
    y4m_stream_info_t streaminfo;

    /* initialize stream-information */
    y4m_accept_extensions(1);
    y4m_init_stream_info(&streaminfo);
    y4m_init_frame_info(&frameinfo);
    
    errnum = y4m_read_stream_header(ipipe->fd_in, &streaminfo);
    if (errnum != Y4M_OK) {
        tc_log_error(__FILE__, "Couldn't read YUV4MPEG header: %s!",
                     y4m_strerr (errnum));
        return 1;
    }
    if (y4m_si_get_plane_count(&streaminfo) != 3) {
        tc_log_error(__FILE__, "Only 3-plane formats supported");
        return 1;
    }
    ch_mode =  y4m_si_get_chroma(&streaminfo);
    if (ch_mode != Y4M_CHROMA_420JPEG && ch_mode != Y4M_CHROMA_420MPEG2
      && ch_mode != Y4M_CHROMA_420PALDV) {
        tc_log_error(__FILE__, "sorry, chroma mode `%s' (%i) not supported",
                     y4m_chroma_description(ch_mode), ch_mode);
        return 1;
    }
    
    w = y4m_si_get_width(&streaminfo);
    h = y4m_si_get_height(&streaminfo);
    vptr = tc_new_video_frame(w, h, TC_CODEC_YUV420P, TC_TRUE);

    if (!vptr) {
        tc_log_error(__FILE__, "can't allocate buffer (%ix%i)", w, h);
        return 1;
    }
    planes[0] = vptr->video_buf_Y[0];
    planes[1] = vptr->video_buf_U[0];
    planes[2] = vptr->video_buf_V[0];

    planesize[0] = y4m_si_get_plane_length(&streaminfo, 0);
    planesize[1] = y4m_si_get_plane_length(&streaminfo, 1);
    planesize[2] = y4m_si_get_plane_length(&streaminfo, 2);
    
    while (1) {
        errnum = y4m_read_frame(ipipe->fd_in, &streaminfo, &frameinfo, planes);
        if (errnum != Y4M_OK) {
            break;
        }
        for (i = 0; i < 3; i++) {
            ret = tc_pwrite(ipipe->fd_out, planes[i], planesize[i]);
            if (ret != planesize[i]) {
                tc_log_perror(__FILE__, "error while writing output data");
                break;
            }
        }
    }

    tc_del_video_frame(vptr);
    y4m_fini_frame_info(&frameinfo);
    y4m_fini_stream_info(&streaminfo);

    return 0;
}

static int extract_yuv_avi(info_t *ipipe)
{
    avi_t *avifile=NULL;
    char *video;

    int key;
    long frames, bytes, n;

    if (ipipe->nav_seek_file) {
        avifile = AVI_open_indexfd(ipipe->fd_in, 0, ipipe->nav_seek_file);
    } else {
        avifile = AVI_open_fd(ipipe->fd_in, 1);
    }
    if (NULL == avifile) {
        AVI_print_error("AVI open");
        return 1;
    }

    // read video info;
    frames =  AVI_video_frames(avifile);
    if (ipipe->frame_limit[1] < frames) {
        frames=ipipe->frame_limit[1];
    }

    if (ipipe->verbose & TC_STATS) {
        tc_log_info(__FILE__, "%ld video frames", frames);
    }
    // allocate space, assume max buffer size
    video = tc_bufalloc(SIZE_RGB_FRAME);
    if (video == NULL) {
        tc_log_error(__FILE__, "out of memory");
        return 1;
    }

    AVI_set_video_position(avifile, ipipe->frame_limit[0]);
    for (n = ipipe->frame_limit[0]; n <= frames; ++n) {
        bytes = AVI_read_frame(avifile, video, &key);
        if (bytes < 0) {
            return 1;
        }
        if (tc_pwrite(ipipe->fd_out, video, bytes) != bytes) {
            tc_log_perror(__FILE__, "error while writing output data");
            return 1;
        }
    }
    tc_buffree(video);

    return 0;
}

static int extract_yuv_raw(info_t *ipipe)
{
    if (ipipe->magic == TC_MAGIC_UNKNOWN) {
        tc_log_warn(__FILE__, "no file type specified, assuming (%s)",
                    filetype(TC_MAGIC_RAW));
    }
    return tc_preadwrite(ipipe->fd_in, ipipe->fd_out);
}

void extract_yuv(info_t *ipipe)
{
    int error = 0;
    
    switch(ipipe->magic) {
      case TC_MAGIC_YUV4MPEG:
        error = extract_yuv_y4m(ipipe);
        break;
      case TC_MAGIC_AVI:
        error = extract_yuv_avi(ipipe);
        break;
      case TC_MAGIC_RAW:
      default:
        error = extract_yuv_raw(ipipe);
        break;
    }
    if (error) {
        tc_log_error(__FILE__, "write failed");
        import_exit(error);
    }
}

void probe_yuv(info_t *ipipe)
{
    int errnum = Y4M_OK;
    y4m_frame_info_t frameinfo;
    y4m_stream_info_t streaminfo;
    y4m_ratio_t r;

    /* initialize stream-information */
    y4m_accept_extensions(1);
    y4m_init_stream_info(&streaminfo);
    y4m_init_frame_info(&frameinfo);
    
    errnum = y4m_read_stream_header(ipipe->fd_in, &streaminfo);
    if (errnum != Y4M_OK) {
        tc_log_error(__FILE__, "Couldn't read YUV4MPEG header: %s!",
		     y4m_strerr(errnum));
        import_exit(1);
    }

    ipipe->probe_info->width = y4m_si_get_width(&streaminfo);
    ipipe->probe_info->height = y4m_si_get_height(&streaminfo);
   
    r = y4m_si_get_framerate(&streaminfo);
    ipipe->probe_info->fps = (double)r.n / (double)r.d;
    tc_frc_code_from_ratio(&(ipipe->probe_info->frc), r.n, r.d);

    r = y4m_si_get_sampleaspect(&streaminfo);
    tc_asr_code_from_ratio(&(ipipe->probe_info->asr), r.n, r.d);
   
    ipipe->probe_info->codec=TC_CODEC_YUV420P;
    ipipe->probe_info->magic=TC_MAGIC_YUV4MPEG;

    y4m_fini_frame_info(&frameinfo);
    y4m_fini_stream_info(&streaminfo);
}

#else			/* HAVE_MJPEGTOOLS */

void extract_yuv(info_t *ipipe)
{
    tc_log_error(__FILE__, "No support for YUV4MPEG compiled in.");
    tc_log_error(__FILE__, "Recompile with mjpegtools support enabled.");
    import_exit(1);
}
        
void probe_yuv(info_t * ipipe)
{
    tc_log_error(__FILE__, "No support for YUV4MPEG compiled in.");
    tc_log_error(__FILE__, "Recompile with mjpegtools support enabled.");
    ipipe->probe_info->codec = TC_CODEC_UNKNOWN;
    ipipe->probe_info->magic = TC_MAGIC_UNKNOWN;
}

#endif /* HAVE_MJPEGTOOLS */

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
