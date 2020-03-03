/*
 * probe_ffmpeg.c -- libavcodec/libavformat based probing code
 * (C) 2006-2010 Francesco Romani <fromani -at- gmail -dot- com>
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

#include "tccore/tcinfo.h"
#include "src/transcode.h"
#include "ioaux.h"
#include "tc.h"
#include "libtc/libtc.h"
#include "libtc/ratiocodes.h"


#ifdef HAVE_FFMPEG

#include "libtcext/tc_avcodec.h"




static void translate_info(const AVFormatContext *ctx, ProbeInfo *info)
{
    AVStream *st = NULL;
    int i = 0, j = 0;

    if (ctx == NULL || info == NULL) {
        return;
    }

    /* first of all, video */
    for (i = 0; i < ctx->nb_streams; i++) {
        st = ctx->streams[i];

        if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
            info->bitrate = st->codec->bit_rate / 1000;
            info->width = st->codec->width;
            info->height = st->codec->height;
            if (st->r_frame_rate.num > 0 && st->r_frame_rate.den > 0) {
                info->fps = av_q2d(st->r_frame_rate);
            } else {
                /* watch out here */
                info->fps = 1.0/av_q2d(st->codec->time_base);
            }
            tc_frc_code_from_value(&info->frc, info->fps);
            break;
        }
    }
    /* then audio track(s) */
    for (i = 0; i < ctx->nb_streams; i++) {
        st = ctx->streams[i];

        if (st->codec->codec_type == CODEC_TYPE_AUDIO
         && j < TC_MAX_AUD_TRACKS) {
            info->track[j].format = 0x1; /* known wrong */
            info->track[j].chan = st->codec->channels;
            info->track[j].samplerate = st->codec->sample_rate;
            info->track[j].bitrate = st->codec->bit_rate / 1000;
            /* XXX: known wrong for PCM */

            info->track[j].bits = BITS;
            /* 
             * XXX where libavcodec/libaformat provide this?
             * Should be inferred by CODEC_ID used?
             * */
            info->track[j].pts_start = 0; /* XXX: ?!? */

            j++;
        }
    }

    info->num_tracks = j;
}



void probe_ffmpeg(info_t *ipipe)
{
    /* to be completed */
    AVFormatContext *lavf_dmx_context = NULL;
    int ret = 0;

    close(ipipe->fd_in);

    TC_LOCK_LIBAVCODEC;
    av_register_all();
    avcodec_init();
    avcodec_register_all();
    TC_UNLOCK_LIBAVCODEC;

    ret = av_open_input_file(&lavf_dmx_context, ipipe->name,
                             NULL, 0, NULL);
    if (ret != 0) {
        tc_log_error(__FILE__, "unable to open '%s'"
                               " (libavformat failure)",
                     ipipe->name);
        ipipe->error = 1;
        return;
    }

    ret = av_find_stream_info(lavf_dmx_context);
    if (ret < 0) {
        tc_log_error(__FILE__, "unable to fetch informations from '%s'"
                               " (libavformat failure)",
                     ipipe->name);
        ipipe->error = 1;
        return;
    }

    translate_info(lavf_dmx_context, ipipe->probe_info);

    av_close_input_file(lavf_dmx_context);
    return;
}

#else   // HAVE_FFMPEG

void probe_ffmpeg(info_t *ipipe)
{
	tc_log_error(__FILE__, "no support for FFmpeg compiled - exit.");
	ipipe->error = 1;
	return;
}

#endif // HAVE_FFMPEG


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
