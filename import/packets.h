/*
 *  packets.h
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

#ifndef _PACKETS_H
#define _PACKETS_H

#include "transcode.h"
#include "ioaux.h"

#define PACKET_NULL  -1
#define PACKET_EMPTY  0
#define PACKET_READY  1

typedef struct packet_list {

  int id;        // packet number
  int status;    // packet status

  int bufid;

  int size;

  struct packet_list *next;
  struct packet_list *prev;

  char buffer[2048];

} packet_list_t;

packet_list_t *packet_register(int id);
void packet_remove(packet_list_t *ptr);
packet_list_t *packet_retrieve(void);
int packet_buffer_flush(void);

extern pthread_mutex_t packet_list_lock;

extern packet_list_t *packet_list_head;
extern packet_list_t *packet_list_tail;

int sbuf_alloc(int num);
void sbuf_free(void);
packet_list_t *sbuf_retrieve(void);
int sbuf_release(packet_list_t *ptr);

int flush_buffer_write(int fd, char*buffer, int size);
int flush_buffer_close(void);
int flush_buffer_empty(void);
int flush_buffer_init(int fd, int verb);

#endif
