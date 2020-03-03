/*
 *  frame_info.h
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

#include <pthread.h>
#include <stdlib.h>

#ifndef _FRAME_INFO_H
#define _FRAME_INFO_H

#define FRAME_INFO_NULL  -1
#define FRAME_INFO_EMPTY  0
#define FRAME_INFO_READY  1
#define FRAME_INFO_LOCKED 2
#define FRAME_INFO_WAIT   3


typedef struct frame_info_list {

  int id;        // frame number
  int status;    // frame status

  struct sync_info_s *sync_info;

  struct frame_info_list *next;
  struct frame_info_list *prev;

  } frame_info_list_t;

frame_info_list_t *frame_info_register(int id);
void frame_info_remove(frame_info_list_t *ptr);
frame_info_list_t *frame_info_retrieve(void);
frame_info_list_t *frame_info_retrieve_status(int old_status, int new_status);
void frame_info_set_status(frame_info_list_t *ptr, int status);

extern pthread_mutex_t frame_info_list_lock;

extern frame_info_list_t *frame_info_list_head;
extern frame_info_list_t *frame_info_list_tail;


#endif
