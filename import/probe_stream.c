/*
 *  probe_stream.c
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
#include "ioaux.h"
#include "probe_stream.h"

#include "libtc/libtc.h"

/*************************************************************************/


void probe_file(info_t *ipipe)
{
    switch (ipipe->magic) {
      case TC_MAGIC_AVI:
        probe_avi(ipipe);
        break;
#ifdef HAVE_IMAGEMAGICK
      case TC_MAGIC_TIFF1:   /* image formats (multiple fallbacks) */
      case TC_MAGIC_TIFF2:
      case TC_MAGIC_JPEG:
      case TC_MAGIC_BMP:
      case TC_MAGIC_PNG:
      case TC_MAGIC_GIF:
      case TC_MAGIC_PPM:
      case TC_MAGIC_PGM:
      case TC_MAGIC_SGI:
        probe_im(ipipe); /* ImageMagick serve all */
        break;
#endif
      case TC_MAGIC_MXF:
        probe_mxf(ipipe);
        break;
#if HAVE_OGG
      case TC_MAGIC_OGG:
        probe_ogg(ipipe);
        break;
#endif
      case TC_MAGIC_CDXA:
      case TC_MAGIC_MPEG_PS: /* MPEG Program Stream */
      case TC_MAGIC_VOB:     /* backward compatibility fallback */
      case TC_MAGIC_MPEG_ES: /* MPEG Elementary Stream */
      case TC_MAGIC_M2V:     /* backward compatibility fallback */
      case TC_MAGIC_MPEG_PES:/* MPEG Packetized Elementary Stream */
      case TC_MAGIC_MPEG:    /* backward compatibility fallback */
        probe_pes(ipipe);
        break;
#if defined HAVE_MJPEGTOOLS
      case TC_MAGIC_YUV4MPEG:
        probe_yuv(ipipe);
        break;
#endif
      case TC_MAGIC_NUV:
        probe_nuv(ipipe);
        break;
#ifdef HAVE_LIBQUICKTIME
      case TC_MAGIC_MOV:
        probe_mov(ipipe);
        break;
#endif
      case TC_MAGIC_WAV:
        probe_wav(ipipe);
        break;

      case TC_MAGIC_DTS:
        probe_dts(ipipe);
        break;

      case TC_MAGIC_AC3:
        probe_ac3(ipipe);
        break;

      case TC_MAGIC_MP3:
      case TC_MAGIC_MP3_2:
      case TC_MAGIC_MP3_2_5:
      case TC_MAGIC_MP2:
        probe_mp3(ipipe);
        break;
#ifdef HAVE_LIBDV
      case TC_MAGIC_DV_PAL:
      case TC_MAGIC_DV_NTSC:
        probe_dv(ipipe);
        break;
#endif
      case TC_MAGIC_PV3:
        probe_pv3(ipipe);
        break;

      case TC_MAGIC_PVN:
        probe_pvn(ipipe);
        break;

      case TC_MAGIC_FLV:
        probe_ffmpeg(ipipe); /* explicit call */
        break;

      default:
        /* libavcodec/libavformat it's a catchall too */
        probe_ffmpeg(ipipe);
    }
    return;
}


void probe_stream(info_t *ipipe)
{
    static ProbeInfo probe_info;

    verbose = ipipe->verbose;

    ipipe->probe_info = &probe_info;
    ipipe->probe = 1;

    /* data structure will be filled by subroutines */
    memset(&probe_info, 0, sizeof(ProbeInfo));
    probe_info.magic = ipipe->magic;

    /* ------------------------------------------------------------
     * check file type/magic and take action to probe for contents
     * ------------------------------------------------------------*/

    /* not-plain-old-file stuff */
    switch (ipipe->magic) {
      case TC_MAGIC_MPLAYER:
        probe_mplayer(ipipe);
        break;

      case TC_MAGIC_VNC:
        probe_vnc(ipipe);
        break;

      case TC_MAGIC_V4L_VIDEO:
      case TC_MAGIC_V4L_AUDIO:
        probe_v4l(ipipe);
        break;

      case TC_MAGIC_BKTR_VIDEO:
        probe_bktr(ipipe);
        break;

      case TC_MAGIC_SUNAU_AUDIO:
        probe_sunau(ipipe);
        break;

      case TC_MAGIC_BSDAV:
        probe_bsdav(ipipe);
        break;

      case TC_MAGIC_OSS_AUDIO:
        probe_oss(ipipe);
        break;

      case TC_MAGIC_DVD:
      case TC_MAGIC_DVD_PAL:
      case TC_MAGIC_DVD_NTSC:
        probe_dvd(ipipe);
        break;

      case TC_MAGIC_XML:
        probe_xml(ipipe);
        break;

      case TC_MAGIC_X11:
        probe_x11(ipipe);
        break;

      default: /* fallback to P.O.D. file... */
        probe_file(ipipe);
        break; /* for coherency */
    }

    if (ipipe->magic == TC_MAGIC_XML) {
        ipipe->probe_info->magic_xml = TC_MAGIC_XML;
        /*
         * used in transcode to load import_xml and to have
         * the correct type of the video/audio
         */
    } else {
        ipipe->probe_info->magic_xml = ipipe->probe_info->magic;
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
