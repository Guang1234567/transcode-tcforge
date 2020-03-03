/*
 *  ioaux.h
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

#ifndef _IOAUX_H
#define _IOAUX_H

#include "transcode.h"
#include "tcinfo.h"
#include "magic.h"


/* this exit is provided by the import module or frontend */
extern void import_exit(int ret);

/* fileinfo.c */
long fileinfo(int fd, int skipy);
long streaminfo(int fd);
const char *filetype(uint32_t magic);

/* scan_pes.c */
void scan_pes(int verbose, FILE *fd);
void probe_pes(info_t *ipipe);

/* ioaux.c */
unsigned int stream_read_int16(const unsigned char *s);
unsigned int stream_read_int32(const unsigned char *s);
double read_time_stamp(const unsigned char *s);
long read_time_stamp_long(const unsigned char *s);

/* ts_reader.c */
void probe_ts(info_t *ipipe);
int ts_read(int fd_in, int fd_out, int demux_pid);

#define VOB_PACKET_SIZE   0x800
#define VOB_PACKET_OFFSET    22

//packet type
#define P_ID_AC3  0xbd
#define P_ID_MP3  0xbc
#define P_ID_MPEG 0xe0
#define P_ID_PROG 0xbb
#define P_ID_PADD 0xbe

//stream type
#define TC_STYPE_ERROR        0xFFFFFFFF
#define TC_STYPE_UNKNOWN      0x00000000
#define TC_STYPE_FILE         0x00000001
#define TC_STYPE_STDIN        0x00000002
#define TC_STYPE_X11          0x00000004

#define ERROR_END_OF_STREAM        1
#define ERROR_INVALID_FRAME        2
#define ERROR_INVALID_FRAME_SIZE   3
#define ERROR_INVALID_HEADER       4
#endif   /* _IOAUX_H */
