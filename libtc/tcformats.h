/*
 *  tcformats.h
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  ripped from 'magic.h' by Francesco Romani - November 2006
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

#ifndef TC_FORMATS_H
#define TC_FORMATS_H

/*
 * formats identifiers.
 */

typedef enum {
    /* audio only */
    TC_FORMAT_WAV,
    TC_FORMAT_CDXA,

    /* video only */
    TC_FORMAT_YUV4MPEG,
    TC_FORMAT_PVN,

    /* audio + video */
    TC_FORMAT_AVI,
    TC_FORMAT_ASF,
    TC_FORMAT_MOV,
    TC_FORMAT_OGG,
    TC_FORMAT_MPEG, /* generic, should not be used directly */
    TC_FORMAT_MPEG_ES,
    TC_FORMAT_MPEG_PS,
    TC_FORMAT_MPEG_TS,
    TC_FORMAT_MPEG_PES,
    TC_FORMAT_MPEG_VOB,
    TC_FORMAT_MPEG_VDR,
    TC_FORMAT_MPEG_MP4,
    TC_FORMAT_MXF,
    TC_FORMAT_PV3,
    TC_FORMAT_VAG,
    TC_FORMAT_NUV,
    TC_FORMAT_FLV,
    TC_FORMAT_MKV,

    /* special */
    TC_FORMAT_RAW,      /* no container */
    TC_FORMAT_ALSA,
    TC_FORMAT_X11,
    TC_FORMAT_XML,
    TC_FORMAT_VIDEO4LINUX,
    TC_FORMAT_OSS,
    TC_FORMAT_BKTR,
    TC_FORMAT_VNC,
    TC_FORMAT_DVD,
    TC_FORMAT_DVD_PAL,  /* temporary */
    TC_FORMAT_DVD_NTSC, /* temporary */

    /* special (pseudo)formats */
    TC_FORMAT_UNKNOWN    = 0x00000000,
    TC_FORMAT_NULL       = 0xFFFFFF00, /* drop content */
    TC_FORMAT_ANY        = 0xFFFFFFFE,
    /* this one MUST be the last */
    TC_FORMAT_ERROR      = 0xFFFFFFFF
} TCFormatID;

#endif // TC_FORMATS_H
