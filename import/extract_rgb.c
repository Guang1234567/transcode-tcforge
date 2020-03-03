/*
 *  extract_rgb.c
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

#include "tccore/tcinfo.h"
#include "src/transcode.h"
#include "libtc/libtc.h"

#include "ioaux.h"
#include "avilib/avilib.h"
#include "tc.h"

#include <stdint.h>


/* ------------------------------------------------------------
 *
 * rgb extract thread
 *
 * magic: TC_MAGIC_AVI
 *        TC_MAGIC_RAW  <-- default
 *
 * ------------------------------------------------------------*/


void extract_rgb(info_t *ipipe)
{
    uint8_t *video;
    avi_t *avifile = NULL;
    int key, error = 0;
    long frames, bytes, n;

    switch (ipipe->magic) {
      case TC_MAGIC_AVI:
	if (ipipe->nav_seek_file) {
            avifile = AVI_open_indexfd(ipipe->fd_in, 0, ipipe->nav_seek_file);
        } else {
            avifile = AVI_open_fd(ipipe->fd_in, 1);
        }
        if (NULL == avifile) {
            AVI_print_error("AVI open");
            import_exit(1);
        }

        frames = AVI_video_frames(avifile);
        if (ipipe->frame_limit[1] < frames) {
            frames = ipipe->frame_limit[1];
        }

        if (ipipe->verbose & TC_STATS) {
            tc_log_msg(__FILE__, "%ld video frames", frames);
        }

        video = tc_bufalloc(SIZE_RGB_FRAME);
        if (!video) {
            error = 1;
            break;
        }

        AVI_set_video_position(avifile, ipipe->frame_limit[0]);
        /* FIXME: should this be < rather than <= ? */
        for (n = ipipe->frame_limit[0]; n <= frames; n++) {
            bytes = AVI_read_frame(avifile, video, &key);
            if (bytes < 0) {
                error = 1;
                break;
            }
            if (tc_pwrite(ipipe->fd_out, video, bytes) != bytes) {
                error = 1;
                break;
            }
        }

        tc_buffree(video);
        break;

      case TC_MAGIC_RAW: /* fallthrough */
      default:
        if (ipipe->magic == TC_MAGIC_UNKNOWN) {
            tc_log_warn(__FILE__, "no file type specified, assuming %s",
			            filetype(TC_MAGIC_RAW));

            error = tc_preadwrite(ipipe->fd_in, ipipe->fd_out);
            break;
        }
    }

    if (error) {
        tc_log_perror(__FILE__, "error while writing data");
      	import_exit(error);
    }
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
