/*
 *  buffer.h
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

#ifndef _BUFFER_H
#define _BUFFER_H

#define BUFFER_NULL  -1
#define BUFFER_EMPTY  0
#define BUFFER_READY  1

#define MAX_PCM_BUFFER  (SIZE_PCM_FRAME<<2)


typedef struct buffer_list {

  int id;      // buffer number
  int status;  // buffer status

  struct buffer_list *next;
  struct buffer_list *prev;

  int size;

  char *data;

} buffer_list_t;

buffer_list_t *buffer_register(int id);
void buffer_remove(buffer_list_t *ptr);
buffer_list_t *buffer_retrieve(void);

extern buffer_list_t *buffer_list_head;
extern buffer_list_t *buffer_list_tail;

#endif
