/*
 *  subtitle_buffer.c
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

#include "subtitle_buffer.h"
#include "libtc/libtc.h"

pthread_mutex_t sframe_list_lock=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t sframe_list_empty_cv=PTHREAD_COND_INITIALIZER;
pthread_cond_t sframe_list_full_cv=PTHREAD_COND_INITIALIZER;

sframe_list_t *sframe_list_head;
sframe_list_t *sframe_list_tail;

static int sub_buf_max = 0;
#ifdef STATBUFFER
static int sub_buf_next = 0;
#endif

static int sub_buf_fill=0;
static int sub_buf_ready=0;

static sframe_list_t **sub_buf_ptr; char *sub_buf_mem, **sub_buf_sub;

/* ------------------------------------------------------------------ */

static int sub_buf_alloc(int ex_num)
{

    /* objectives:
       ===========

       allocate memory for ringbuffer structure
       return -1 on failure, 0 on success

    */

    int n, num;

    if(ex_num < 0) return(-1);

    num = ex_num + 2; //alloc some more because
    //of import threads strange testing code

    if((sub_buf_ptr = (sframe_list_t **) calloc(num, sizeof(sframe_list_t *)))==NULL) {
      tc_log_perror(__FILE__, "out of memory");
      return(-1);
    }

    if((sub_buf_mem = (char *) calloc(num, sizeof(sframe_list_t)))==NULL) {
      tc_log_perror(__FILE__, "out of memory");
      return(-1);
    }

    // init ringbuffer
    for (n=0; n<num; ++n) {
	sub_buf_ptr[n] = (sframe_list_t *) (sub_buf_mem + n * sizeof(sframe_list_t));

	sub_buf_ptr[n]->status = FRAME_NULL;
	sub_buf_ptr[n]->bufid = n;

#ifdef STATBUFFER
	//allocate extra subeo memory:
	if((sub_buf_ptr[n]->video_buf=tc_bufalloc(SUB_BUFFER_SIZE))==NULL) {
	  tc_log_perror(__FILE__, "out of memory");
	  return(-1);
	}
#endif /* STATBUFFER */
    }

    // assign to static
    sub_buf_max = num;

    return(0);
}




/* ------------------------------------------------------------------ */

static void sub_buf_free(void)
{

    /* objectives:
       ===========

       free memory for ringbuffer structure

    */

  int n;

  if(sub_buf_max > 0) {

    for (n=0; n<sub_buf_max; ++n) {
      tc_buffree(sub_buf_ptr[n]->video_buf);
    }
    free(sub_buf_mem);
    free(sub_buf_ptr);
  }
}

/* ------------------------------------------------------------------ */

#ifdef STATBUFFER

static sframe_list_t *sub_buf_retrieve(void)
{

    /* objectives:
       ===========

       retrieve a valid pointer to a sframe_list_t structure
       return NULL on failure, valid pointer on success

       thread safe

    */

    sframe_list_t *ptr;

    ptr = sub_buf_ptr[sub_buf_next];

    // check, if this structure is really free to reuse

    if(ptr->status != FRAME_NULL) return(NULL);

    // ok

    tc_debug(TC_DEBUG_FLIST, "alloc  =%d [%d]", sub_buf_next, ptr->bufid);

    ++sub_buf_next;
    sub_buf_next %= sub_buf_max;

    return(ptr);
}



/* ------------------------------------------------------------------ */

static int sub_buf_release(sframe_list_t *ptr)
{

    /* objectives:
       ===========

       release a valid pointer to a sframe_list_t structure
       return -1 on failure, 0 on success

       thread safe

    */

    // instead of freeing the memory and setting the pointer
    // to NULL we only change a flag

    if(ptr == NULL) return(-1);

    if(ptr->status != FRAME_EMPTY) {
	return(-1);
    } else {

       tc_debug(TC_DEBUG_FLIST, "release=%d [%d]", sub_buf_next, ptr->bufid);
	ptr->status = FRAME_NULL;

    }

    return(0);
}

#endif /* STATBUFFER */

/* ------------------------------------------------------------------ */

static FILE *fd = NULL;

int sframe_alloc(int ex_num, FILE *_fd)
{
  fd=_fd;
  return(sub_buf_alloc(ex_num));
}

void sframe_free()
{
  sub_buf_free();
}

/* ------------------------------------------------------------------ */

sframe_list_t *sframe_register(int id)

{

  /* objectives:
     ===========

     register new frame

     allocate space for frame buffer and establish backward reference

     requirements:
     =============

     thread-safe

     global mutex: sframe_list_lock

  */

  sframe_list_t *ptr;

  pthread_mutex_lock(&sframe_list_lock);

  // retrive a valid pointer from the pool

#ifdef STATBUFFER
  tc_debug(TC_DEBUG_FLIST, "frameid=%d", id);
  if((ptr = sub_buf_retrieve()) == NULL) {
    pthread_mutex_unlock(&sframe_list_lock);
    return(NULL);
  }
#else
  if((ptr = malloc(sizeof(sframe_list_t))) == NULL) {
    pthread_mutex_unlock(&sframe_list_lock);
    return(NULL);
  }
#endif

  ptr->status = FRAME_EMPTY;

  ptr->next = NULL;
  ptr->prev = NULL;

  ptr->id  = id;

 if(sframe_list_tail != NULL)
    {
      sframe_list_tail->next = ptr;
      ptr->prev = sframe_list_tail;
    }

  sframe_list_tail = ptr;

  /* first frame registered must set sframe_list_head */

  if(sframe_list_head == NULL) sframe_list_head = ptr;

  // adjust fill level
  ++sub_buf_fill;

  pthread_mutex_unlock(&sframe_list_lock);

  return(ptr);

}


/* ------------------------------------------------------------------ */


void sframe_remove(sframe_list_t *ptr)

{

  /* objectives:
     ===========

     remove frame from chained list

     requirements:
     =============

     thread-safe

  */


  if(ptr == NULL) return;         // do nothing if null pointer

  pthread_mutex_lock(&sframe_list_lock);

  if(ptr->prev != NULL) (ptr->prev)->next = ptr->next;
  if(ptr->next != NULL) (ptr->next)->prev = ptr->prev;

  if(ptr == sframe_list_tail) sframe_list_tail = ptr->prev;
  if(ptr == sframe_list_head) sframe_list_head = ptr->next;

  if(ptr->status == FRAME_READY) --sub_buf_ready;

  // release valid pointer to pool
  ptr->status = FRAME_EMPTY;

#ifdef STATBUFFER
  sub_buf_release(ptr);
#else
  free(ptr);
#endif

  // adjust fill level
  --sub_buf_fill;

  pthread_mutex_unlock(&sframe_list_lock);

}

/* ------------------------------------------------------------------ */


void sframe_flush()

{

  /* objectives:
     ===========

     remove all frame from chained list

     requirements:
     =============

     thread-safe

  */

  sframe_list_t *ptr;

  while((ptr=sframe_retrieve())!=NULL) {
       tc_log_msg(__FILE__, "flushing buffers");
      sframe_remove(ptr);
  }
  return;

}


/* ------------------------------------------------------------------ */


sframe_list_t *sframe_retrieve()

{

  /* objectives:
     ===========

     get pointer to next frame for rendering

     requirements:
     =============

     thread-safe

  */

  sframe_list_t *ptr;

  pthread_mutex_lock(&sframe_list_lock);

  ptr = sframe_list_head;

  /* move along the chain and check for status */

  while(ptr != NULL)
  {
      // we cannot skip a locked frame, since
      // we have to preserve order in which frames are encoded
      if(ptr->status == FRAME_LOCKED)
      {
	  pthread_mutex_unlock(&sframe_list_lock);
	  return(NULL);
      }

      //this frame is ready to go
      if(ptr->status == FRAME_READY)
      {
	  pthread_mutex_unlock(&sframe_list_lock);
	  return(ptr);
      }
      ptr = ptr->next;
  }

  pthread_mutex_unlock(&sframe_list_lock);

  return(NULL);
}

/* ------------------------------------------------------------------ */


sframe_list_t *sframe_retrieve_status(int old_status, int new_status)

{

  /* objectives:
     ===========

     get pointer to next frame for rendering

     requirements:
     =============

     thread-safe

  */

  sframe_list_t *ptr;

  pthread_mutex_lock(&sframe_list_lock);

  ptr = sframe_list_head;

  /* move along the chain and check for status */

  while(ptr != NULL)
    {
      if(ptr->status == old_status)
	{

	  // found matching frame

	  if(ptr->status==FRAME_READY) --sub_buf_ready;

	  ptr->status = new_status;

	  if(ptr->status==FRAME_READY) ++sub_buf_ready;

	  pthread_mutex_unlock(&sframe_list_lock);

	  return(ptr);
	}
      ptr = ptr->next;
    }

  pthread_mutex_unlock(&sframe_list_lock);

  return(NULL);
}


/* ------------------------------------------------------------------ */


void sframe_set_status(sframe_list_t *ptr, int status)

{

  /* objectives:
     ===========

     get pointer to next frame for rendering

     requirements:
     =============

     thread-safe

  */

    if(ptr == NULL) return;

    pthread_mutex_lock(&sframe_list_lock);

    if(ptr->status==FRAME_READY) --sub_buf_ready;

    ptr->status = status;

    if(ptr->status==FRAME_READY) ++sub_buf_ready;

    pthread_mutex_unlock(&sframe_list_lock);

    return;
}


/* ------------------------------------------------------------------ */


int sframe_fill_level(int status)
{

  if(verbose & TC_STATS)
    tc_log_msg(__FILE__, "(S) fill=%d, ready=%d, request=%d", sub_buf_fill, sub_buf_ready, status);

  //user has to lock sframe_list_lock to obtain a proper result

  if(status==TC_BUFFER_FULL  && sub_buf_fill==sub_buf_max) return(1);
  if(status==TC_BUFFER_READY && sub_buf_ready>0) return(1);
  if(status==TC_BUFFER_EMPTY && sub_buf_fill==0) return(1);

  return(0);
}

//----------------------------------------------------------------

//subtitle read thread

void subtitle_reader()
{
  sframe_list_t *ptr=NULL;
  int i=0;

  subtitle_header_t subtitle_header;
  char *subtitle_header_str="SUBTITLE";

  char *buffer;

  for(;;) {

    pthread_testcancel();

    pthread_mutex_lock(&sframe_list_lock);

    while(sframe_fill_level(TC_BUFFER_FULL)) {
      pthread_cond_wait(&sframe_list_full_cv, &sframe_list_lock);
#ifdef BROKEN_PTHREADS // Used to be MacOSX specific; kernel 2.6 as well?
      pthread_testcancel();
#endif
    }

    pthread_mutex_unlock(&sframe_list_lock);

    pthread_testcancel();

    // buffer start with 0
    if((ptr = sframe_register(i))==NULL) {

	//error
	tc_log_error(__FILE__, "could not allocate subtitle buffer - exit.");
	pthread_exit(0);
    }

    buffer = ptr->video_buf;

    // get a subtitle

    if(fread(buffer, strlen(subtitle_header_str), 1, fd) != 1) {
      tc_log_error(__FILE__, "reading subtitle header string (%d) failed - end of stream", i);
      sframe_remove(ptr);
      pthread_exit(0);
    }

    if(strncmp(buffer, subtitle_header_str, strlen(subtitle_header_str))!=0) {
      tc_log_error(__FILE__, "invalid subtitle header");
      sframe_remove(ptr);
      pthread_exit(0);
    }

    //get subtitle packet length and pts

    if(fread(&subtitle_header, sizeof(subtitle_header_t), 1, fd) != 1) {
	tc_log_error(__FILE__, "error reading subtitle header");
	sframe_remove(ptr);
	pthread_exit(0);
    }

    ptr->video_size=subtitle_header.payload_length;
    ptr->pts=(double)subtitle_header.lpts;

    //OK
    if(verbose & TC_STATS)
      tc_log_msg(__FILE__, "subtitle %d, len=%d, lpts=%u", i, subtitle_header.payload_length, subtitle_header.lpts);

    // read packet payload

    if(fread(buffer, subtitle_header.payload_length, 1, fd) != 1) {
	tc_log_error(__FILE__, "error reading subtitle packet");
	sframe_remove(ptr);
	pthread_exit(0);
    }

    if(verbose & TC_STATS) tc_log_msg(__FILE__, "buffering packet (%d)", ptr->id);

    sframe_set_status(ptr, FRAME_READY);

    ++i;

  }
}
