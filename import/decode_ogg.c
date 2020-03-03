/*
 *  decode_ogg.c
 *
 *  Copyright (C) Tilmann Bitterberg
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
#include "tc.h"

#include <stdint.h>

#if (HAVE_OGG && HAVE_VORBIS)
#include <vorbis/vorbisfile.h>

#define TC_OGG_BUF_SIZE 8192

/* ------------------------------------------------------------
 *
 * decoder thread
 *
 * ------------------------------------------------------------*/

static int decode_ogg_file(int fdin, int fdout, int verbose)
{
    FILE *in = NULL;
    OggVorbis_File vf;
    uint8_t buf[TC_OGG_BUF_SIZE];
    unsigned int written = 0;
    ogg_int64_t length = 0;
#if 0    
    ogg_int64_t done = 0;
#endif
    int ret, bs = 0, size = 0, bits = 16, r = 0;
    int seekable = 0, endian = 0, sign = 1;
#if 0    
    int percent = 0;
#endif

    in  = fdopen(fdin,  "rb");

    ret = ov_open(in, &vf, NULL, 0);
    if (ret < 0) {
        tc_log_error(__FILE__, "Failed to open input as vorbis");
        fclose(in);
        return 1;
    }

    if (ov_seekable(&vf)) {
        seekable = 1;
        length = ov_pcm_total(&vf, 0);
        size = bits/8 * ov_info(&vf, 0)->channels;
    }

    while ((r = ov_read(&vf, buf, TC_OGG_BUF_SIZE,
                        endian, bits/8, sign, &bs)) != 0) {
        if (bs != 0) {
            tc_log_error(__FILE__, "Only one logical bitstream currently supported");
            break;
        }

        if (r < 0 && verbose) {
            tc_log_warn(__FILE__, "hole in data");
            continue;
        }

        ret = tc_pwrite(fdout, buf, r);
        if (ret != r) {
            tc_log_perror(__FILE__, "Error writing to file");
            ov_clear(&vf);
            return 1;
        }

        written += ret;
#if 0        
        if(verbose && seekable) {
            done += ret/size;
            if((double)done/(double)length * 200. > (double)percent) {
                percent = (double)done/(double)length *200;
                fprintf(stderr, "\r[%5.1f%%]", (double)percent/2.);
            }
        }
#endif
    }

    ov_clear(&vf);

    return 0;
}
#endif // HAVE_OGG


void decode_ogg(decode_t *decode)
{

#if (HAVE_OGG && HAVE_VORBIS)

  decode_ogg_file(decode->fd_in, decode->fd_out, decode->verbose);
  import_exit(0);

#else

  tc_log_error(__FILE__, "no support for VORBIS decoding configured - exit.");
  import_exit(1);

#endif

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
