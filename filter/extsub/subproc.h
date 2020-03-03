/*
 *  sub_proc.h
 *
 *  Copyright (C) Thomas Oestreich - February 2002
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

#ifndef _SUB_PROC_H
#define _SUB_PROC_H

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>

typedef struct sub_info_s {

  int time;
  int forced;

  int x, y;
  int w, h;

  char *frame;

  int colour[4];
  int alpha[4];

} sub_info_t;

int subproc_init(char *convertscript, char *prefix, int subtitles, unsigned short id) ;
int subproc_feedme(void *buffer, unsigned  int size, int block, double pts, sub_info_t *sub);

#endif
