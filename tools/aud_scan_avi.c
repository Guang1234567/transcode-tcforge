/*
 *  aud_scan_avi.c
 *
 *  Scans the audio track - AVI specific functions
 *
 *  Copyright (C) Tilmann Bitterberg - June 2003
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
# include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libtcutil/memutils.h>

#include "aud_scan_avi.h"
#include "aud_scan.h"

// ------------------------
// You must set the requested audio before entering this function
// the AVI file out must be filled with correct values.
// ------------------------

typedef struct avidata_ AVIData;
struct avidata_ {
    int     vbr;
    int     mp3rate;
    int     format;
    int     chan;
    long    rate;
    int     bits;

    uint8_t data[48000 * 16 * 4];
};

static void init_avi_data(AVIData *data, avi_t *AVI)
{
    if (AVI && data) {
        data->vbr     = AVI_get_audio_vbr(AVI);
        data->mp3rate = AVI_audio_mp3rate(AVI);
        data->format  = AVI_audio_format(AVI);
        data->chan    = AVI_audio_channels(AVI);
        data->rate    = AVI_audio_rate(AVI);
        data->bits    = AVI_audio_bits(AVI);

        data->bits = (data->bits == 0) ?16 :data->bits;

        if (data->format == 0x1) { /* FIXME */
            data->mp3rate = data->rate * data->chan * data->bits;
        } else {
            data->mp3rate *= 1000; /* FIXME */
        }
    }
    return;
}

static AVIData *new_avi_data(avi_t *AVI)
{
    AVIData *data = tc_zalloc(sizeof(AVIData));
    if (data) {
        init_avi_data(data, AVI);
    }
    return data;
}

/* for supported audio formats */
static int AV_synch_avi2avi(AVIData *data,
                            double vid_ms, double *aud_ms,
                            avi_t *in, avi_t *out)
{
    long bytes = 0;

    while (*aud_ms < vid_ms) {
        bytes = AVI_read_audio_chunk(in, data->data);
        if (bytes < 0) {
            AVI_print_error("AVI audio read frame");
            return -2;
        }

        if (out) {
            if (AVI_write_audio(out, data->data, bytes) < 0) {
                AVI_print_error("AVI write audio frame");
                return -1;
            }
        }

        // pass-through null frames (!?)
        if (bytes == 0) {
            *aud_ms = vid_ms;
            break; // !?
        }

        if (data->vbr
         && tc_get_audio_header(data->data, bytes, data->format,
                                NULL, NULL, &(data->mp3rate)) < 0) {
            // if this is the last frame of the file, slurp in audio chunks
            //if (n == frames-1) continue;
            *aud_ms = vid_ms;
        } else  {
            if (data->vbr)
                data->mp3rate *= 1000;
            *aud_ms += (bytes*8.0*1000.0)/(double)data->mp3rate;
        }
    }
    return 0;
}

/* for UNsupported audio formats */
static int AV_synch_avi2avi_raw(AVIData *data,
                                avi_t *in, avi_t *out)
{
    long bytes = 0;

    do {
        bytes = AVI_read_audio_chunk(in, data->data);
        if (bytes < 0) {
            AVI_print_error("AVI audio read frame");
            return -2;
        }

        if (out) {
            if (AVI_write_audio(out, data->data, bytes) < 0) {
                AVI_print_error("AVI write audio frame");
                return -1;
            }
        }
    } while (AVI_can_read_audio(in));

    return 0;
}

int sync_audio_video_avi2avi(double vid_ms, double *aud_ms, avi_t *in, avi_t *out)
{
    int ret = 0;
    static AVIData *data = NULL;
    if (!data) {
        data = new_avi_data(out);
    } else {
        init_avi_data(data, out);
    }

    if (tc_format_ms_supported(data->format)) {
        ret = AV_synch_avi2avi(data, vid_ms, aud_ms, in, out);
    } else { // fallback for not supported audio format
        ret = AV_synch_avi2avi_raw(data, in, out);
    }

    return ret;
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
