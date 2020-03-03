/*
 * test-export-profile.c -- testsuite for new export profiles code;
 *                          everyone feel free to add more tests and improve
 *                          existing ones.
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

#include "src/transcode.h"
#include "libtcexport/export_profile.h"
#include "libtc/libtc.h"

#define VIDEO_LOG_FILE       "mpeg4.log"
#define AUDIO_LOG_FILE       "pcm.log"

#define VIDEO_CODEC          "yuv420p"
#define AUDIO_CODEC          "pcm"

int verbose = TC_STATS;

static vob_t vob = {
    .verbose = TC_STATS,

    .has_video = 1,
    .has_audio = 1,

    /* some sane settings, mostly identical to transcode's ones */
    .fps = PAL_FPS,
    .ex_fps = PAL_FPS,
    .im_v_width = PAL_W,
    .ex_v_width = PAL_W,
    .im_v_height= PAL_H,
    .ex_v_height= PAL_H,

    .im_v_codec = TC_CODEC_YUV420P,
    .im_a_codec = TC_CODEC_PCM,
    .ex_v_codec = TC_CODEC_YUV420P,
    .ex_a_codec = TC_CODEC_PCM,

    .im_frc = 3,
    .ex_frc = 3,

    .a_rate = RATE,
    .a_chan = CHANNELS,
    .a_bits = BITS,
    .a_vbr = AVBR,

    .video_in_file = "/dev/zero",
    .audio_in_file = "/dev/zero",
    .video_out_file = "/dev/null",
    .audio_out_file = "/dev/null",
    .audiologfile = AUDIO_LOG_FILE,

    .mp3bitrate = ABITRATE,
    .mp3quality = AQUALITY,
    .mp3mode = AMODE,
    .mp3frequency = RATE,

    .divxlogfile = VIDEO_LOG_FILE,
    .divxmultipass = VMULTIPASS,
    .divxbitrate = VBITRATE,
    .divxkeyframes = VKEYFRAMES,
    .divxcrispness = VCRISPNESS,

    .a_leap_frame = TC_LEAP_FRAME,
    .a_leap_bytes = 0,

    .export_attributes= TC_EXPORT_ATTRIBUTE_NONE,
};

#define PRINT(field,fmt) \
    printf("    %s=" fmt "\n", #field, vob.field);

#define GET_MODULE(mod) ((mod) != NULL) ?(mod) :"null"

int main(int argc, char *argv[])
{
    const TCExportInfo *info = NULL;
    char *amod = "null";
    char *vmod = "null";
    char *mmod = "null";
    int ret = 0;
  
    ret = libtc_init(&argc, &argv);
    if (ret != TC_OK) {
        exit(2);
    }

    ret = tc_export_profile_setup_from_cmdline(&argc, &argv);
    if (ret < 0) {
        /* error, so bail out */
        return 1;
    }

    ret = tc_export_profile_count();
    if (ret > 0) {
        info = tc_export_profile_load_all();
        if (info) {
            amod = GET_MODULE(info->audio.module.name);
            vmod = GET_MODULE(info->video.module.name);
            mmod = GET_MODULE(info->mplex.module.name);
            tc_export_profile_to_job(info, &vob);

            PRINT(divxbitrate, "%i");
            PRINT(video_max_bitrate, "%i");
            PRINT(mp3bitrate, "%i");
            PRINT(mp3frequency, "%i");
            PRINT(divxkeyframes, "%i");
            PRINT(encode_fields, "%i");
            PRINT(ex_frc, "%i");
            PRINT(ex_v_codec, "%x");
            PRINT(ex_a_codec, "%x");
            PRINT(zoom_width, "%i");
            PRINT(zoom_height, "%i");
    
            printf("video module=%s\n", vmod);
            printf("audio module=%s\n", amod);
            printf("mplex module=%s\n", mmod);
        }
    }

    tc_export_profile_cleanup();
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
