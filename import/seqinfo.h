/*
 *  seqinfo.h
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


#ifndef _SEQINFO_H
#define _SEQINFO_H

#define BUFFER_NULL  -1
#define BUFFER_EMPTY  0
#define BUFFER_READY  1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct seq_list_s {

  int id;          //sequence id
  int tag;

  int status;

  long pts;        //sequence pts
  long dts;        //sequence dts

  int enc_pics;    //encoded frames
  int seq_pics;    //effective frames
  int adj_pics;    //frame correction

  int tot_enc_pics;    //total stream (encoded) number of frames
  int tot_dec_pics;    //total effective (decoded) number of frames

  int pics_first_packet;

  double av_sync;  //encoder av sync

  int tot_pts;     //relative sequence PTS

  int packet_ctr;       // seq packets
  int tot_packet_ctr;   // stream packets

  long ptime;           //encoded sequence presentation time

  int sync_reset;
  int sync_active;

  int pulldown;

  struct seq_list_s *next;
  struct seq_list_s *prev;

} seq_list_t;

seq_list_t *seq_register(int id);
void seq_remove(seq_list_t *ptr);
seq_list_t *seq_retrieve(void);
void seq_update(seq_list_t *ptr, int pts, int pictures, int packets, int flag, int hard_fps);
void seq_list(seq_list_t *ptr, int end_pts, int pictures, int packets, int flag);
void seq_close(void);
int seq_init(const char *logfile, int ext, double fps, int verb);
void seq_write(seq_list_t *ptr);
void seq_list_frames(void);

extern seq_list_t *seq_list_head;
extern seq_list_t *seq_list_tail;


/* Can't decide whether this belongs here or in clone.h, but it certainly
 * doesn't belong in ioaux.h  --AC */
typedef struct sync_info_s {

  long int enc_frame;
  long int adj_frame;

  long int sequence;

  double dec_fps;
  double enc_fps;

  double pts;

  int pulldown;
  int drop_seq;

} sync_info_t;



#endif
