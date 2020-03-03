/*
 * probe_wav.c - WAV probing code using wavlib.
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


#include "tccore/tcinfo.h"
#include "src/transcode.h"
#include "ioaux.h"
#include "tc.h"
#include "avilib/wavlib.h"
#include "libtc/libtc.h"

void probe_wav(info_t *ipipe)
{
    WAVError err = 0;
    WAV wav = NULL;
    int chans = 0;

    wav = wav_fdopen(ipipe->fd_in, WAV_READ, &err);
    if (!wav) {
        tc_log_error(__FILE__, "%s", wav_strerror(err));
        ipipe->error = 1;
        return;
    }

    chans = wav_get_channels(wav);
    ipipe->probe_info->track[0].chan = chans;
    ipipe->probe_info->track[0].samplerate = wav_get_rate(wav);
    ipipe->probe_info->track[0].bits = wav_get_bits(wav);
    ipipe->probe_info->track[0].bitrate = wav_get_bitrate(wav);
    ipipe->probe_info->track[0].format = 0x1; /* XXX */

    ipipe->probe_info->magic = TC_MAGIC_WAV;
    ipipe->probe_info->codec = TC_CODEC_PCM;

    if (ipipe->probe_info->track[0].chan > 0) {
        ipipe->probe_info->num_tracks = 1;
    }
    return;
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
