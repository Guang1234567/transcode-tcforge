/*
 *  decode_mp3.c
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
#include "tc.h"

#include <stdint.h>

#ifdef HAVE_LAME

#include "mpg123.h"

#define MP3_PCM_SIZE 1152
static uint16_t buffer[MP3_PCM_SIZE<<2];
static uint16_t ch1[MP3_PCM_SIZE], ch2[MP3_PCM_SIZE];

#endif  // HAVE_LAME

#define MP3_AUDIO_ID    0x55
#define MP2_AUDIO_ID    0x50

/* 
 * About MP2/3 handling differences:
 * It is possible that lame_decode_initfile() when looking for an MP3 syncbyte
 * finds an invalid one (esp. in broken mp3 streams). Thats why we use the
 * format argument to decide which syncword detection is to be done. The
 * syncword detection for mp2 also finds mp3 sync bytes but NOT the other way round.
 */

/* ------------------------------------------------------------
 *
 * decoder thread
 *
 * ------------------------------------------------------------*/


static void decode_mpaudio(decode_t *decode, int format)
{
#ifdef HAVE_LAME
    int samples = 0, j, bytes, channels = 0, i, padding = 0;
    int verbose;

    mp3data_struct *mp3data = NULL;
    FILE *in_file = NULL;

    if (format != MP2_AUDIO_ID && format != MP3_AUDIO_ID) {
        tc_log_error(__FILE__, "wrong mpeg audio format: 0x%x", format);
        exit(1);
    }

    verbose = decode->verbose;

    // init decoder

    mp3data = tc_zalloc(sizeof(mp3data_struct));
    if (mp3data == NULL) {
        tc_log_error(__FILE__, "out of memory");
        exit(1);
    }

    if (lame_decode_init() < 0) {
        tc_log_error(__FILE__, "failed to init decoder");
        exit(1);
    }

    in_file = fdopen(decode->fd_in, "r");

    if (format == MP3_AUDIO_ID) {
        int c = 0;
        /* padding detection */
        while (!(c = fgetc(in_file))) 
            padding++;
        if (c != EOF)
            ungetc(c, in_file);
    }

    samples = lame_decode_initfile(in_file, mp3data, format);

    if (verbose) {
        tc_log_info(__FILE__, "channels=%d, samplerate=%d Hz, bitrate=%d kbps, (%d)",
		            mp3data->stereo, mp3data->samplerate, mp3data->bitrate,
            		mp3data->framesize);
    }

    if (format == MP3_AUDIO_ID) {
        if (decode->padrate > 0) {
            padding = (int)((double)padding / (double)decode->padrate * mp3data->samplerate)
                      * mp3data->stereo * 2;
            memset(buffer, 0, sizeof(buffer));
            while (padding >= sizeof(buffer)) {
                if (tc_pwrite(decode->fd_out, (uint8_t *)buffer, sizeof(buffer)) != sizeof(buffer)) {
                    tc_log_error(__FILE__, "error while writing padding output data");
                    import_exit(1);
                }
                padding -= sizeof(buffer);
            }
            if (padding && tc_pwrite(decode->fd_out, (uint8_t *)buffer, padding) != padding) {
                tc_log_error(__FILE__, "error while writing final padding output data");
                import_exit(1);
            }
        }
    }

    // decoder loop
    channels = mp3data->stereo;

    while ((samples=lame_decode_fromfile(in_file, ch1, ch2, mp3data)) > 0) {
        // interleave data
        j = 0;
        switch (channels) {
          case 1: // mono
            ac_memcpy (buffer, ch1, samples * sizeof(uint16_t));
            break;
          case 2: // stereo
            for (i=0; i < samples; i++) {
                *(buffer+j+0) = ch1[i];
                *(buffer+j+1) = ch2[i];
                j+=2;
            }
            break;
        }

        bytes = samples * channels * sizeof(uint16_t);

        if (tc_pwrite(decode->fd_out, (uint8_t *) buffer, bytes) != bytes) {
            tc_log_error(__FILE__, "error while writing output data");
            import_exit(1);
            break; /* broken pipe */
        }
    }

    import_exit(0);

#else  // HAVE_LAME
    tc_log_error(__FILE__, "no lame support available");
    import_exit(1);
#endif
}

void decode_mp3(decode_t *decode)
{
    decode_mpaudio(decode, MP3_AUDIO_ID);
}


void decode_mp2(decode_t *decode)
{
    decode_mpaudio(decode, MP2_AUDIO_ID);
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
