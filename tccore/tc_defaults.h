/*
 *  tc_defaults.h
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>

#ifndef TC_DEFAULTS_H
#define TC_DEFAULTS_H

#define TC_DEFAULT_IN_FILE  "/dev/zero"
#define TC_DEFAULT_OUT_FILE "/dev/null"

#define TC_PAD_AUD_FRAMES 10
#define TC_MAX_SEEK_BYTES (1<<20)

// DivX/MPEG-4 encoder defaults
#define VBITRATE            1800
#define VKEYFRAMES           250
#define VCRISPNESS           100

#define VMULTIPASS             0
#define VQUALITY               5

#define VMINQUANTIZER          2
#define VMAXQUANTIZER         31

#define VQUANTIZER            10

#define RC_PERIOD           2000
#define RC_REACTION_PERIOD    10
#define RC_REACTION_RATIO     20

//----------------------------------

#define ABITRATE       128
#define AQUALITY         5
#define AVBR             0
#define AMODE            0

//import/export/filter frame buffer status flag
typedef enum tcmediatype_ TCMediaType;
enum tcmediatype_ {
    TC_NONE     = 0,
    TC_VIDEO    = 1,
    TC_AUDIO    = 2,
    TC_SUBEX    = 4,
    TC_RESERVED = 8,
    TC_EXTRA    = 16
};

#define TC_FILTER_INIT          16
#define TC_PRE_S_PROCESS        32
#define TC_PRE_M_PROCESS        64
#define TC_INT_M_PROCESS       128
#define TC_POST_M_PROCESS      256
#define TC_POST_S_PROCESS      512
#define TC_PREVIEW            1024
#define TC_FILTER_CLOSE       2048
#define TC_FILTER_GET_CONFIG  4096

#define TC_IMPORT             8192
#define TC_EXPORT            16384

#define TC_DELAY_MAX         40000
#define TC_DELAY_MIN         10000

#define TC_DEFAULT_IMPORT_AUDIO "null"
#define TC_DEFAULT_IMPORT_VIDEO "null"
#define TC_DEFAULT_EXPORT_AUDIO "null"
#define TC_DEFAULT_EXPORT_VIDEO "null"
#define TC_DEFAULT_EXPORT_MPLEX "null"

#define TC_FRAME_BUFFER        10
#define TC_FRAME_THREADS        1
#define TC_FRAME_THREADS_MAX   32

#define TC_FRAME_FIRST          0
#define TC_FRAME_LAST     INT_MAX

#define TC_MAX_AUD_TRACKS      32

//--------------------------------------------------

#define TC_INFO_NO_DEMUX        1
#define TC_INFO_MPEG_PS         2
#define TC_INFO_MPEG_ES         4
#define TC_INFO_MPEG_PES        8

#define TC_FRAME_DV_PAL     144000
#define TC_FRAME_DV_NTSC    120000

#define TC_SUBTITLE_HDRMAGIC 0x00030001

#define TC_DEFAULT_AAWEIGHT (1.0f/3.0f)
#define TC_DEFAULT_AABIAS   (0.5f)

#define TC_A52_DRC_OFF    1
#define TC_A52_DEMUX      2
#define TC_A52_DOLBY_OFF  4

#define AVI_FILE_LIMIT 2048

#define M2V_REQUANT_FACTOR  1.00f

/* 
 * flags used in modules for supporting export profiles (--export_prof)
 * if one of those flag is set, then use the value provided by the user.
 * otherwise use the ones the export modules suggests.
 */
typedef enum tcexportattribute_ TCExportAttribute;
enum tcexportattribute_ {
    TC_EXPORT_ATTRIBUTE_NONE     = (    0),
    TC_EXPORT_ATTRIBUTE_VBITRATE = (1<< 1), /* -w */
    TC_EXPORT_ATTRIBUTE_ABITRATE = (1<< 2), /* -b */
    TC_EXPORT_ATTRIBUTE_FIELDS   = (1<< 3), /* --encode_fields */
    TC_EXPORT_ATTRIBUTE_VMODULE  = (1<< 4), /* -y X,* */
    TC_EXPORT_ATTRIBUTE_AMODULE  = (1<< 5), /* -y *,X */
    TC_EXPORT_ATTRIBUTE_FRC      = (1<< 6), /* --export_fps *,X */
    TC_EXPORT_ATTRIBUTE_FPS      = (1<< 7), /* --export_fps X,* */
    TC_EXPORT_ATTRIBUTE_VCODEC   = (1<< 8), /* -F */
    TC_EXPORT_ATTRIBUTE_ACODEC   = (1<< 9), /* -N */
    TC_EXPORT_ATTRIBUTE_ARATE    = (1<<10), /* -E X,*,* */
    TC_EXPORT_ATTRIBUTE_ABITS    = (1<<11), /* -E *,X,* */
    TC_EXPORT_ATTRIBUTE_ACHANS   = (1<<12), /* -E *,*,X */
    TC_EXPORT_ATTRIBUTE_ASR      = (1<<13), /* --export_asr */
    TC_EXPORT_ATTRIBUTE_PAR      = (1<<14), /* --export_par */
    TC_EXPORT_ATTRIBUTE_GOP      = (1<<15), /* key frames */
};

#endif /* TC_DEFAULTS_H */
