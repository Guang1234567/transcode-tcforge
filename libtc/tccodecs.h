/*
 *  tccodecs.h
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  ripped from 'magic.h' by Francesco Romani - November 2005
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

#ifndef TC_CODECS_H
#define TC_CODECS_H

/*
 * codecs identifiers.
 * (WARNING: avilib relies on these _values_, at least for audio)
 */

typedef enum {
    /* Pixel Formats */
    TC_CODEC_RGB24      = 0x00000024,
    TC_CODEC_YV12       = 0x32315659,
    TC_CODEC_YUV420P    = 0x30323449,
    TC_CODEC_YUV422P    = 0x42323459,
    TC_CODEC_UYVY       = 0x59565955,
    TC_CODEC_YUV2       = 0x32565559,
    TC_CODEC_YUY2       = 0x32595559,

    /* ok, now the real codecs */
    TC_CODEC_PCM        = 0x00000001, /* incidental */
    TC_CODEC_LPCM       = 0x00010001,
    TC_CODEC_ULAW       = 0x00000007, /* incidental */

    TC_CODEC_AC3        = 0x00002000, /* incidental */
    TC_CODEC_DTS        = 0x0001000f,
    TC_CODEC_MP3        = 0x00000055, /* incidental */
    TC_CODEC_MP2        = 0x00000050, /* incidental */
    TC_CODEC_AAC        = 0x000000FF,
    TC_CODEC_VORBIS     = 0x0000FFFE,
    TC_CODEC_FLAC,
    TC_CODEC_SPEEX,

    /* this group should be probably removed or changed */
    TC_CODEC_M2V        = 0x000001b3,
    TC_CODEC_MPEG       = 0x01000000,
    TC_CODEC_MPEG1      = 0x00100000,
    TC_CODEC_MPEG2      = 0x00010000,
    TC_CODEC_PS1        = 0x00007001,
    TC_CODEC_PS2        = 0x00007002,
    TC_CODEC_SUB        = 0xA0000011,

    /* we really need a specific value for those? */
    TC_CODEC_DV         = 0x00001000,
    TC_CODEC_VAG        = 0x0000FEED,
    TC_CODEC_PV3        = 0x50563301,
    /* no special meaning, just enumeration from here */
    TC_CODEC_DIVX3      = 0xFFFE0001,
    TC_CODEC_MP42,
    TC_CODEC_MP43,
    TC_CODEC_DIVX4,
    TC_CODEC_DIVX5,
    TC_CODEC_XVID,
    TC_CODEC_H264,
    TC_CODEC_MJPEG,
    TC_CODEC_MPG1,
    TC_CODEC_NUV,
    TC_CODEC_LZO1,
    TC_CODEC_RV10,
    TC_CODEC_SVQ1,
    TC_CODEC_SVQ3,
    TC_CODEC_VP3,
    TC_CODEC_4XM,
    TC_CODEC_WMV1,
    TC_CODEC_WMV2,
    TC_CODEC_HUFFYUV,
    TC_CODEC_INDEO3,
    TC_CODEC_H263P,
    TC_CODEC_H263I,
    TC_CODEC_LZO2,
    TC_CODEC_FRAPS,
    TC_CODEC_FFV1,
    TC_CODEC_ASV1,
    TC_CODEC_ASV2,
    TC_CODEC_THEORA,
    TC_CODEC_MPEG1VIDEO,
    TC_CODEC_MPEG2VIDEO,
    TC_CODEC_MPEG4VIDEO,
    TC_CODEC_LJPEG,	/* lossless (motion) JPEG */
    TC_CODEC_VP6,
    TC_CODEC_YUV4MPEG,
    /* image formats (IM modules) */
    TC_CODEC_JPEG,
    TC_CODEC_TIFF,
    TC_CODEC_PNG,
    TC_CODEC_PPM,
    TC_CODEC_PGM,
    TC_CODEC_GIF,

    /* special (pseudo)codecs */
    TC_CODEC_UNKNOWN    = 0x00000000,
    TC_CODEC_RAW        = 0xFEFEFEFE,
    TC_CODEC_ANY        = 0x7FFFFFFE,
    /* this one MUST be the last */
    TC_CODEC_ERROR      = 0xFFFFFFFF
} TCCodecID;

#endif // TC_CODECS_H
