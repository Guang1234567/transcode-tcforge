/*
 * test-framealloc.c -- testsuite for frame allocation functions.
 *                      everyone feel free to add more tests and improve
 *                      existing ones.
 * (C) 2006-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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


#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "libtc/libtc.h"
#include "libtc/tccodecs.h"
#include "libtc/tcframes.h"
#include "tccore/tc_defaults.h"
#include "src/framebuffer.h"

#ifndef PACKAGE
#define PACKAGE __FILE__
#endif

static int format[] = { TC_CODEC_RGB24, TC_CODEC_YUV422P, TC_CODEC_YUV420P };
static const char *strfmt[] = { "rgb24", "yuv422p", "yuv420p" };

static int test_alloc_vid(int w, int h, int fmtid, int part)
{
    int ret = 0;
    int fmt = format[fmtid];
    vframe_list_t *vptr = tc_new_video_frame(w, h, fmt, part);

    if (vptr != NULL && vptr->video_size >= (w * h * 3 / 2)) {
        ret = 1;
    }
    tc_del_video_frame(vptr);

    if (ret) {
        tc_info("testing frame (simple): width=%i height=%i format=%s partial=%s -> OK",
                w, h, strfmt[fmtid], (part) ?"yes" :"no");
    } else {
        tc_warn("testing frame (simple): width=%i height=%i format=%s partial=%s -> FAILED",
                w, h, strfmt[fmtid], (part) ?"yes" :"no");
    }
    return ret;
}

static int test_alloc_aud(double rate, double fps, int chans, int bits)
{
    int ret = 0;
    aframe_list_t *aptr = tc_new_audio_frame(rate/fps, chans, bits);

    if (aptr != NULL) {
        int as = ((rate/fps) * chans * bits/8);
        if (aptr->audio_size >=  as - 2) {
            ret = 1;
        }
    }
    tc_del_audio_frame(aptr);

    if (ret) {
        tc_info("testing frame (simple): samples=%.0f/%.3f channels=%i bits=%i -> OK",
                rate, fps, chans, bits);
    } else {
        tc_warn("testing frame (simple): samples=%.0f/%.3f channels=%i bits=%i -> FAILED",
                rate, fps, chans, bits);
    }
    return ret;
}


static int test_alloc_memset_vid(int w, int h, int fmtid, int part)
{
    int ret = 0;
    int fmt = format[fmtid];
    vframe_list_t *vptr = tc_new_video_frame(w, h, fmt, part);

    if (vptr != NULL && vptr->video_size >= (w * h * 3 / 2)) {
        memset(vptr->video_buf, 'A', vptr->video_size);
        if (!part) {
            memset(vptr->video_buf2, 'B', vptr->video_size);
        }
        ret = 1;
    }
    tc_del_video_frame(vptr);

    if (ret) {
        tc_info("testing frame (memset): width=%i height=%i format=%s partial=%s -> OK",
                w, h, strfmt[fmtid], (part) ?"yes" :"no");
    } else {
        tc_warn("testing frame (memset): width=%i height=%i format=%s partial=%s -> FAILED",
                w, h, strfmt[fmtid], (part) ?"yes" :"no");
    }
    return ret;
}

static int test_alloc_memset_aud(double rate, double fps, int chans, int bits)
{
    int ret = 0;
    aframe_list_t *aptr = tc_new_audio_frame(rate/fps, chans, bits);

    if (aptr != NULL) {
        int as = ((rate/fps) * chans * bits/8);
        if (aptr->audio_size >=  as - 2) {
            memset(aptr->audio_buf, 'A', aptr->audio_size);
            ret = 1;
        }
    }
    tc_del_audio_frame(aptr);

    if (ret) {
        tc_info("testing frame (memset): samples=%.0f/%.3f channels=%i bits=%i -> OK",
                rate, fps, chans, bits);
    } else {
        tc_warn("testing frame (memset): samples=%.0f/%.3f channels=%i bits=%i -> FAILED",
                rate, fps, chans, bits);
    }
    return ret;
}


#define LEN(a)  (sizeof(a)/sizeof((a)[0]))

int main(int argc, char *argv[])
{
    int width[] = { 128, 320, 576, 640, 960, 1024, 1280, 2048 };
    int height[] = { 96, 240, 240, 480, 560, 768, 800, 1536 };
    int w, h, f;

    double fps[] = { 24000.0/1001.0, 24000.0/1000.0, 25000.0/1000.0,
                     30000.0/1001.0, 30000.0/1000.0, 50000.0/1000.0 };
    double rate[] = { 16000.0, 22500.0, 24000.0, 32000.0, 44100.0, 48000.0 };
    int channels[] = { 1, 2 };
    int bits[] = { 8, 16 };
    int F, r, c, b;

    int runned = 0, succesfull = 0;

    libtc_init(&argc, &argv);

    for (f = 0; f < LEN(format); f++) {
        for (w = 0; w < LEN(width); w++) {
            for (h = 0; h < LEN(height); h++) {
                succesfull += test_alloc_vid(width[w], height[h], f, 0);
                succesfull += test_alloc_vid(width[w], height[h], f, 1);
                runned += 2;
            }
        }
    }

    for (F = 0; F < LEN(fps); F++) {
        for (r = 0; r < LEN(rate); r++) {
            for (c = 0; c < LEN(channels); c++) {
                for (b = 0; b < LEN(bits); b++) {
                   succesfull += test_alloc_aud(rate[r], fps[F], channels[c], bits[b]);
                   runned++;
                }
            }
        }
    }

    for (f = 0; f < LEN(format); f++) {
        for (w = 0; w < LEN(width); w++) {
            for (h = 0; h < LEN(height); h++) {
                succesfull += test_alloc_memset_vid(width[w], height[h], f, 0);
                succesfull += test_alloc_memset_vid(width[w], height[h], f, 1);
                runned += 2;
            }
        }
    }

    for (F = 0; F < LEN(fps); F++) {
        for (r = 0; r < LEN(rate); r++) {
            for (c = 0; c < LEN(channels); c++) {
                for (b = 0; b < LEN(bits); b++) {
                    succesfull += test_alloc_memset_aud(rate[r], fps[F], channels[c], bits[b]);
                    runned++;
                }
            }
        }
    }

    tc_info("test summary: %i tests runned, %i succesfully",
            runned, succesfull);
    return 0;
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
