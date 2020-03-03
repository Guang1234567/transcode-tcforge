/*
 *  buffer.c
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

#include "buffer.h"

#include <stdlib.h>
#include <unistd.h>

buffer_list_t *buffer_list_head;
buffer_list_t *buffer_list_tail;

buffer_list_t *buffer_register(int id)

{

  /* objectives:
     ===========

     register new buffer

     allocate space for new buffer and establish backward reference


  */

  buffer_list_t *ptr;

  // retrive a valid pointer from the pool

  if((ptr = malloc(sizeof(buffer_list_t))) == NULL) return(NULL);

  if((ptr->data = (char *) malloc(MAX_PCM_BUFFER)) == NULL) return(NULL);

  ptr->status = BUFFER_EMPTY;

  ptr->next = NULL;
  ptr->prev = NULL;

  ptr->id  = id;

  if(buffer_list_tail != NULL)
  {
      buffer_list_tail->next = ptr;
      ptr->prev = buffer_list_tail;
  }

  buffer_list_tail = ptr;

  /* first buffer registered must set buffer_list_head */

  if(buffer_list_head == NULL) buffer_list_head = ptr;

  return(ptr);

}


/* ------------------------------------------------------------------ */


void buffer_remove(buffer_list_t *ptr)

{

  /* objectives:
     ===========

     remove buffer from chained list

  */


  if(ptr == NULL) return;         // do nothing if null pointer

  if(ptr->prev != NULL) (ptr->prev)->next = ptr->next;
  if(ptr->next != NULL) (ptr->next)->prev = ptr->prev;

  if(ptr == buffer_list_tail) buffer_list_tail = ptr->prev;
  if(ptr == buffer_list_head) buffer_list_head = ptr->next;

  // release valid pointer to pool
  ptr->status = BUFFER_EMPTY;

  free(ptr->data);
  free(ptr);
  ptr=NULL;

}


/* ------------------------------------------------------------------ */


buffer_list_t *buffer_retrieve()

{

  /* objectives:
     ===========

     get pointer to next full buffer

  */

  buffer_list_t *ptr;

  ptr = buffer_list_head;

  /* move along the chain and check for status */

  while(ptr != NULL)
    {
      if(ptr->status == BUFFER_READY)
	{

	  return(ptr);
	}
      ptr = ptr->next;
    }

  return(NULL);
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
