/*
 *  magic.h
 *
 *  Copyright (C) Thomas Oestreich - June 2001
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

#ifndef _MAGIC_H
#define _MAGIC_H

#include "libtc/tccodecs.h"

// file/device magic:
#define TC_MAGIC_ERROR                 0xFFFFFFFF
#define TC_MAGIC_UNKNOWN               0x00000000
#define TC_MAGIC_MPLAYER               0x00FFFFFF
#define TC_MAGIC_PIPE                  0x0000FFFF
#define TC_MAGIC_DIR                   0x000000FF
#define TC_MAGIC_RAW                   0x00000001

#define TC_MAGIC_WAV                   0x00000016
#define TC_MAGIC_AVI                   0x00000017
#define TC_MAGIC_ASF                   0x00000018
#define TC_MAGIC_MOV                   0x00000019
#define TC_MAGIC_CDXA                  0x00000020
#define TC_MAGIC_VDR                   0x00000021
#define TC_MAGIC_XML                   0x00000022
#define TC_MAGIC_OGG                   0x00000024
#define TC_MAGIC_VNC                   0x00000026
#define TC_MAGIC_MXF                   0x00000027

#define TC_MAGIC_VOB                   0x000001ba
#define TC_MAGIC_DVD                   0xF0F0F0F0
#define TC_MAGIC_DVD_PAL               0xF0F0F0F1
#define TC_MAGIC_DVD_NTSC              0xF0F0F0F2

#define TC_MAGIC_V4L_VIDEO             0xF0F0F0F3
#define TC_MAGIC_V4L_AUDIO             0xF0F0F0F4
#define TC_MAGIC_V4L2_VIDEO            0xF0F0F0F5
#define TC_MAGIC_V4L2_AUDIO            0xF0F0F0F6

#define TC_MAGIC_OSS_AUDIO	       0xF0F0F0F7

#define TC_MAGIC_BKTR_VIDEO	       0xB5D00001
#define TC_MAGIC_SUNAU_AUDIO	       0xB5D00002

#define TC_MAGIC_BSDAV                 0xB5D00003

#define TC_MAGIC_X11                   0x1100FEED

//MPEG streams
#define TC_MAGIC_MPEG_ES               0x1EEE00F0
#define TC_MAGIC_MPEG_PS               0x1EEE00F1
#define TC_MAGIC_MPEG_PES              0x1EEE00F2

//raw streams concatenated frames:
#define TC_MAGIC_M2V                   0x000001b3
#define TC_MAGIC_PICEXT                0x000001b5
#define TC_MAGIC_MPEG                  0x000001e0
#define TC_MAGIC_TS                    0x00000047
#define TC_MAGIC_YUV4MPEG              0x00000300
#define TC_MAGIC_DV_PAL                0x1f0700bf
#define TC_MAGIC_DV_NTSC               0x1f07003f
#define TC_MAGIC_AC3                   0x00000b77
#define TC_MAGIC_DTS                   0x7ffe8001
#define TC_MAGIC_LPCM                  0x00000180
#define TC_MAGIC_MP3                   0x0000FFFB
#define TC_MAGIC_MP2_FC                0x0000FFFC
#define TC_MAGIC_MP2                   0x0000FFFD
#define TC_MAGIC_MP3_2_5               0x0000FFE3
#define TC_MAGIC_MP3_2                 0x0000FFF3
#define TC_MAGIC_NUV                   0x4e757070
#define TC_MAGIC_TIFF1                 0x00004D4D
#define TC_MAGIC_TIFF2                 0x00004949
#define TC_MAGIC_JPEG                  0xFFD8FFE0
#define TC_MAGIC_BMP                   0x0000424D
#define TC_MAGIC_SGI                   0x000001DA
#define TC_MAGIC_PNG                   0x89504e47
#define TC_MAGIC_GIF                   0x00474946
#define TC_MAGIC_PPM                   0x00005036
#define TC_MAGIC_PGM                   0x00005035
#define TC_MAGIC_ID3                   0x49443303
#define TC_MAGIC_PV3                   0x50563301
#define TC_MAGIC_PVN                   0x50563460  // PV[456][abdf] & FFFFFCF8
#define TC_MAGIC_FLV                   0x464c5600  // FLV\0

//movie types:
#define TC_MAGIC_PAL                   0x000000F1
#define TC_MAGIC_NTSC                  0x000000F2
#define TC_MAGIC_RMF                   0x000000F4

#define MPEG_PACK_START_CODE           0x000001ba
#define MPEG_SEQUENCE_START_CODE       0x000001b3
#define MPEG_SEQUENCE_END_CODE         0x000001b7
#define MPEG_SYSTEM_START_CODE         0x000001bb
#define MPEG_PADDING_START_CODE        0x000001be
#define MPEG_GOP_START_CODE            0x000001b8
#define MPEG_PROGRAM_END_CODE          0x000001b9
#define MPEG_PICTURE_START_CODE        0x00000100
#define MPEG_EXT_START_CODE            0x000001b5
#define MPEG_USER_START_CODE           0x000001b2
#define MPEG_VIDEO                     0x000001e0
#define MPEG_AUDIO                     0x000001c0
#define MPEG_AC3                       0x000001d0

#endif
