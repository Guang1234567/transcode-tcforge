/*
 *  demuxer.h
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

#ifndef _DEMUXER_H
#define _DEMUXER_H

#include "config.h"

#define SYNC_LOGFILE "sync.log"

#define TC_DEMUX_CRIT_PTS 300.0f
#define TC_DEMUX_MIN_PTS  0.040f

#define TC_DEMUX_OFF         0
#define TC_DEMUX_SEQ_ADJUST  1
#define TC_DEMUX_SEQ_FSYNC   2
#define TC_DEMUX_SEQ_ADJUST2 3
#define TC_DEMUX_SEQ_FSYNC2  4
#define TC_DEMUX_SEQ_LIST    5
#define TC_DEMUX_DEBUG       6
#define TC_DEMUX_DEBUG_ALL   7
#define TC_DEMUX_MAX_OPTS    8


#define PACKAGE_AUDIO_AC3    1
#define PACKAGE_VIDEO        2
#define PACKAGE_NAV          4
#define PACKAGE_MPEG1        8
#define PACKAGE_PASS        16
#define PACKAGE_AUDIO_MP3   32
#define PACKAGE_AUDIO_PCM   64
#define PACKAGE_SUBTITLE   128
#define PACKAGE_AUDIO_DTS  256
#define PACKAGE_AUDIO_VAG  512
#define PACKAGE_PRIVATE_STREAM (PACKAGE_AUDIO_AC3|PACKAGE_AUDIO_PCM|PACKAGE_SUBTITLE|PACKAGE_AUDIO_DTS|PACKAGE_AUDIO_VAG)
#define PACKAGE_ALL         -1

extern int gop, gop_pts, gop_cnt;

int scan_pack_header(char *buf, long x);
int scan_pack_pics(char *video);
int scan_pack_ext(char *video);
void scan_pack_payload(char *video, size_t size, int n, int verb);

void tcdemux_pass_through(info_t *ipipe, int *pass, int npass);

void tcdemux_thread(info_t *ipipe);

#endif
