/*
 *  ac3scan.h
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

#include "transcode.h"
#include "tcinfo.h"

#ifndef _AC3SCAN_H
#define _AC3SCAN_H

int ac3scan(FILE *fd, uint8_t *buffer, int size, int *ac_off, int *ac_bytes, int *pseudo_size, int *real_size, int verbose);

int buf_probe_ac3(unsigned char *_buf, int len, ProbeTrackInfo *pcm);
int buf_probe_dts(unsigned char *_buf, int len, ProbeTrackInfo *pcm);
void probe_ac3(info_t *ipipe);
void probe_dts(info_t *ipipe);

#endif
