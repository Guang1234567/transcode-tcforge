/*
 *  subtitle_buffer.h
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

#include "config.h"

#include <pthread.h>

#include "src/transcode.h"
#include "tccore/tc_defaults.h"

#ifndef _SUBTITLE_BUFFER_H
#define _SUBTITLE_BUFFER_H

#define FRAME_NULL  -1
#define FRAME_EMPTY  0
#define FRAME_READY  1
#define FRAME_LOCKED 2
#define FRAME_WAIT   3

#define TC_BUFFER_EMPTY  0
#define TC_BUFFER_FULL   1
#define TC_BUFFER_READY  2
#define TC_BUFFER_LOCKED 3

#define SUB_BUFFER_SIZE 2048

typedef struct sframe_list {

  int bufid;     // buffer id
  int tag;       // unused

  int id;        // frame number
  int status;    // frame status

  int attributes;

  double pts;

  int video_size;

  struct sframe_list *next;
  struct sframe_list *prev;

#ifdef STATBUFFER
  char *video_buf;
#else
  char video_buf[SUB_BUFFER_SIZE];
#endif

} sframe_list_t;

sframe_list_t *sframe_register(int id);
void sframe_remove(sframe_list_t *ptr);
sframe_list_t *sframe_retrieve(void);
sframe_list_t *sframe_retrieve_status(int old_status, int new_status);
void sframe_set_status(sframe_list_t *ptr, int status);
int sframe_alloc(int num, FILE *fd);
void sframe_free(void);
void sframe_flush(void);
int sframe_fill_level(int status);

extern pthread_mutex_t sframe_list_lock;
extern pthread_cond_t sframe_list_full_cv;
extern pthread_cond_t sframe_list_empty_cv;
extern sframe_list_t *sframe_list_head;
extern sframe_list_t *sframe_list_tail;

void subtitle_reader(void);

#endif
