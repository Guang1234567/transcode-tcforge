/*
 *  frame_info.c
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
# include "config.h"
#endif

#include "libtc/libtc.h"
#include "frame_info.h"

pthread_mutex_t frame_info_list_lock=PTHREAD_MUTEX_INITIALIZER;

frame_info_list_t *frame_info_list_head;
frame_info_list_t *frame_info_list_tail;

frame_info_list_t *frame_info_register(int id)

{

  /* objectives:
     ===========

     register new frame

     allocate space for frame buffer and establish backward reference

     requirements:
     =============

     thread-safe

     global mutex: frame_info_list_lock

  */

  frame_info_list_t *ptr;

  pthread_mutex_lock(&frame_info_list_lock);

  // retrive a valid pointer from the pool


  if((ptr = tc_malloc(sizeof(frame_info_list_t))) == NULL) {
      pthread_mutex_unlock(&frame_info_list_lock);
      return(NULL);
  }

  ptr->status = FRAME_INFO_EMPTY;

  ptr->next = NULL;
  ptr->prev = NULL;

  ptr->id  = id;

 if(frame_info_list_tail != NULL)
    {
      frame_info_list_tail->next = ptr;
      ptr->prev = frame_info_list_tail;
    }

  frame_info_list_tail = ptr;

  /* first frame registered must set frame_info_list_head */

  if(frame_info_list_head == NULL) frame_info_list_head = ptr;

  pthread_mutex_unlock(&frame_info_list_lock);

  return(ptr);

}


/* ------------------------------------------------------------------ */


void frame_info_remove(frame_info_list_t *ptr)

{

  /* objectives:
     ===========

     remove frame from chained list

     requirements:
     =============

     thread-safe

  */


  if(ptr == NULL) return;         // do nothing if null pointer

  pthread_mutex_lock(&frame_info_list_lock);

  if(ptr->prev != NULL) (ptr->prev)->next = ptr->next;
  if(ptr->next != NULL) (ptr->next)->prev = ptr->prev;

  if(ptr == frame_info_list_tail) frame_info_list_tail = ptr->prev;
  if(ptr == frame_info_list_head) frame_info_list_head = ptr->next;

  // release valid pointer to pool
  ptr->status = FRAME_INFO_EMPTY;

  free(ptr->sync_info);

  free(ptr);

  pthread_mutex_unlock(&frame_info_list_lock);

}


/* ------------------------------------------------------------------ */


frame_info_list_t *frame_info_retrieve()

{

  /* objectives:
     ===========

     get pointer to next frame for rendering

     requirements:
     =============

     thread-safe

  */

  frame_info_list_t *ptr;

  pthread_mutex_lock(&frame_info_list_lock);

  ptr = frame_info_list_head;

  /* move along the chain and check for status */

  while(ptr != NULL)
    {
      if(ptr->status == FRAME_INFO_READY)
	{
	  pthread_mutex_unlock(&frame_info_list_lock);
	  return(ptr);
	}
      ptr = ptr->next;
    }

  pthread_mutex_unlock(&frame_info_list_lock);

  return(NULL);
}

/* ------------------------------------------------------------------ */


frame_info_list_t *frame_info_retrieve_status(int old_status, int new_status)

{

  /* objectives:
     ===========

     get pointer to next frame for rendering

     requirements:
     =============

     thread-safe

  */

  frame_info_list_t *ptr;

  pthread_mutex_lock(&frame_info_list_lock);

  ptr = frame_info_list_head;

  /* move along the chain and check for status */

  while(ptr != NULL)
    {
      if(ptr->status == old_status)
	{

	    // found matching frame

	    ptr->status = new_status;

	    pthread_mutex_unlock(&frame_info_list_lock);

	    return(ptr);
	}
      ptr = ptr->next;
    }

  pthread_mutex_unlock(&frame_info_list_lock);

  return(NULL);
}


/* ------------------------------------------------------------------ */


void frame_info_set_status(frame_info_list_t *ptr, int status)

{

  /* objectives:
     ===========

     get pointer to next frame for rendering

     requirements:
     =============

     thread-safe

  */

    if(ptr == NULL) return;

    pthread_mutex_lock(&frame_info_list_lock);

    ptr->status = status;

    pthread_mutex_unlock(&frame_info_list_lock);

    return;
}

