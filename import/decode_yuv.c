/*
 *  decode_yuv.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *
 *  This file is part of transcode, a video stream  processing tool
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
#include "libtc/libtc.h"

#include "aclib/imgconvert.h"
#include "ioaux.h"
#include "tc.h"

/*
 * About this code:
 *
 * based on video_out.h, video_out.c, video_out_ppm.c
 *
 * Copyright (C) 1999-2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 * Stripped and rearranged for transcode by
 * Francesco Romani <fromani@gmail.com> - July 2005
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 *
 * What?
 * Basically, this code does only a colorspace conversion from ingress frames
 * (YV12) to egress frames (RGB). It uses basically the same routines of libvo
 * and old decode_yuv.c
 *
 * Why?
 * decode_yuv was the one and the only transcode module which uses the main
 * libvo routines, not the colorspace conversion ones. It not make sense to me
 * to have an extra library just for one module, so I stripped down to minimum
 * the libvo code used by decode_yuv.c and I moved it here.
 * This code has only few things recalling it's ancestor, but I'm still remark
 * it's origin.
 */

typedef struct vo_s {
    // frame size
    unsigned int width;
    unsigned int height;

    // internal frame buffers
    uint8_t *rgb;
    uint8_t *yuv[3];
} vo_t;

#define vo_convert(instp) \
	ac_imgconvert((instp)->yuv, IMG_YUV420P, &(instp)->rgb, IMG_RGB24, \
		      (instp)->width, (instp)->height)

#define READ_PLANE(fd, V, H, plane) do { \
    int i, bytes; \
    \
    for (i = 0; i < (V); i++) { \
       bytes = tc_pread((fd), (plane) + i * (H), (H)); \
       if (bytes != (H)) { \
    	   if (bytes < 0) \
	          tc_log_error(__FILE__, "read failed: %s", strerror(errno)); \
    	   return 0; \
       } \
    } \
} while (0) 

/*
 * legacy (and working :) ) code:
 * read one YUV420P plane at time from file descriptor (pipe, usually)
 * and store it in internal buffer
 */
static int vo_read_yuv(vo_t *vo, int fd)
{
    unsigned int v = vo->height, h = vo->width;

    /* Read luminance scanlines */

    READ_PLANE(fd, v, h, vo->yuv[0]);
    
    v /= 2;
    h /= 2;

    /* Read chrominance scanlines */
    READ_PLANE(fd, v, h, vo->yuv[1]);
    READ_PLANE(fd, v, h, vo->yuv[2]);

    return 1;
}

#undef READ_PLANE

/*
 * simpler than above:
 * write the whole RGB buffer in to file descriptor (pipe, usually).
 * WARNING: caller must ensure that RGB buffer holds valid data by
 * invoking vo_convert *before* invoking this function
 */
static int vo_write_rgb (vo_t *vo, int fd)
{
    int framesize = vo->width * vo->height * 3;
    int bytes = tc_pwrite(fd, vo->rgb, framesize);
    if (bytes != framesize) {
    	if (bytes < 0)
	        tc_log_error(__FILE__, "read failed: %s", strerror(errno));
    	return 0;
    }
    return 1;
}

/*
 * finalize a vo structure, free()ing it's internal buffers.
 * WARNING: DOES NOT cause a buffer flush, you must do it manually.
 */
static void vo_clean (vo_t *vo)
{
    free(vo->yuv[0]);
    free(vo->yuv[1]);
    free(vo->yuv[2]);
    free(vo->rgb);
}

/*
 * initialize a vo structure, allocate internal buffers
 * and so on
 */
static int vo_alloc(vo_t *vo, int width, int height)
{
    if (width <= 0 || height <= 0) {
        return -1;
    }

    vo->width = (unsigned int)width;
    vo->height = (unsigned int)height;

    vo->yuv[0] = tc_zalloc(width * height);
    if (!vo->yuv[0]) {
        tc_log_error(__FILE__, "out of memory");
    	return -1;
    }
    vo->yuv[1] = tc_zalloc((width/2) * (height/2));
    if (!vo->yuv[1]) {
        tc_log_error(__FILE__, "out of memory");
	    free(vo->yuv[0]);
    	return -1;
    }
    vo->yuv[2] = tc_zalloc((width/2) * (height/2));
    if (!vo->yuv[2]) {
        tc_log_error(__FILE__, "out of memory");
    	free(vo->yuv[0]);
	    free(vo->yuv[1]);
    	return -1;
    }

    vo->rgb = tc_zalloc(width * height * 3);
    if (!vo->rgb) {
        tc_log_error(__FILE__, "out of memory");
    	free(vo->yuv[0]);
	    free(vo->yuv[1]);
    	free(vo->yuv[2]);
        return -1;
    }

    return 0;
}


/* ------------------------------------------------------------
 *
 * decoder thread
 *
 * ------------------------------------------------------------*/

void decode_yuv(decode_t *decode)
{
    vo_t vo;

    if (decode->width <= 0 || decode->height <= 0) {
        tc_log_error(__FILE__, "invalid frame parameter %dx%d",
                     decode->width, decode->height);
        import_exit(1);
    }

    vo_alloc(&vo, decode->width, decode->height);

    // read frame by frame - decode into RGB - pipe to stdout
    while (vo_read_yuv(&vo, decode->fd_in)) {
        vo_convert(&vo);
        vo_write_rgb(&vo, decode->fd_out);
    }

    // ends
    vo_clean(&vo);

    import_exit(0);
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
