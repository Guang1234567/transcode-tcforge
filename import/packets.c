/*
 *  packets.c
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

#include "src/transcode.h"
#include "libtc/libtc.h"

#include "packets.h"

#define FLUSH_BUFFER_MAX (1024<<4)


pthread_mutex_t packet_list_lock=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t pack_ctr_lock=PTHREAD_MUTEX_INITIALIZER;

packet_list_t *packet_list_head;
packet_list_t *packet_list_tail;

static int sbuf_max =  0;
static int sbuf_next=  0;

static packet_list_t **sbuf_ptr; char *sbuf_mem;

//important internal parameter and counter
static int verbose_flag=TC_QUIET;
static int pack_ctr, pack_fill_ctr=0;
static int ifd=0;

pthread_cond_t packet_pop_cv=PTHREAD_COND_INITIALIZER;
pthread_cond_t packet_push_cv=PTHREAD_COND_INITIALIZER;
static pthread_t packet_thread;

extern int seq_info_delay(void);

packet_list_t *packet_register(int id)

{

  /* objectives:
     ===========

     register new packet

     allocate space for packet buffer and establish backward reference

     requirements:
     =============

     thread-safe

     global mutex: packet_list_lock

  */

  packet_list_t *ptr;

  pthread_mutex_lock(&packet_list_lock);

  // retrieve a valid pointer from the pool

#ifdef STATBUFFER
  tc_debug(TC_DEBUG_FLIST, "packet id=%d", id);
  if((ptr = sbuf_retrieve()) == NULL) {
    pthread_mutex_unlock(&packet_list_lock);
    return(NULL);
  }
#else

  if((ptr = tc_malloc(sizeof(packet_list_t))) == NULL) {
    pthread_mutex_unlock(&packet_list_lock);
    return(NULL);
  }
#endif

  ptr->status = PACKET_EMPTY;

  ptr->next = NULL;
  ptr->prev = NULL;

  ptr->id  = id;

 if(packet_list_tail != NULL)
    {
      packet_list_tail->next = ptr;
      ptr->prev = packet_list_tail;
    }

  packet_list_tail = ptr;

  /* first packet registered must set packet_list_head */

  if(packet_list_head == NULL) packet_list_head = ptr;

  pthread_mutex_unlock(&packet_list_lock);

  return(ptr);

}


/* ------------------------------------------------------------------ */


void packet_remove(packet_list_t *ptr)

{

  /* objectives:
     ===========

     remove packet from chained list

     requirements:
     =============

     thread-safe

  */


  if(ptr == NULL) return;         // do nothing if null pointer

  pthread_mutex_lock(&packet_list_lock);

  if(ptr->prev != NULL) (ptr->prev)->next = ptr->next;
  if(ptr->next != NULL) (ptr->next)->prev = ptr->prev;

  if(ptr == packet_list_tail) packet_list_tail = ptr->prev;
  if(ptr == packet_list_head) packet_list_head = ptr->next;

  // release valid pointer to pool
  ptr->status = PACKET_EMPTY;

#ifdef STATBUFFER
      sbuf_release(ptr);
#else
      free(ptr);
#endif

  pthread_mutex_unlock(&packet_list_lock);

}


/* ------------------------------------------------------------------ */


packet_list_t *packet_retrieve()

{

  /* objectives:
     ===========

     get pointer to next packet for rendering

     requirements:
     =============

     thread-safe

  */

  packet_list_t *ptr;

  pthread_mutex_lock(&packet_list_lock);

  ptr = packet_list_head;

  /* move along the chain and check for status */

  while(ptr != NULL)
    {
      if(ptr->status == PACKET_READY)
	{
	  pthread_mutex_unlock(&packet_list_lock);
	  return(ptr);
	}
      ptr = ptr->next;
    }

  pthread_mutex_unlock(&packet_list_lock);

  return(NULL);
}

/* ------------------------------------------------------------------ */

int sbuf_alloc(int ex_num)
{

    /* objectives:
       ===========

       allocate memory for ringbuffer structure
       return -1 on failure, 0 on success

    */

    int n, num;

    if(ex_num < 0) return(-1);

    num = ex_num + 2; //alloc some more because

    if((sbuf_ptr = tc_zalloc(num * sizeof(packet_list_t *)))==NULL) {
	tc_log_perror(__FILE__, "out of memory");
	return(-1);
    }

    if((sbuf_mem = tc_zalloc(num * sizeof(packet_list_t)))==NULL) {
	tc_log_perror(__FILE__, "out of memory");
	return(-1);
    }

    // init ringbuffer
    for (n=0; n<num; ++n) {
	sbuf_ptr[n] = (packet_list_t *) (sbuf_mem + n * sizeof(packet_list_t));

	sbuf_ptr[n]->status = PACKET_NULL;
	sbuf_ptr[n]->bufid = n;
    }

    // assign to static
    sbuf_max = num;

    return(0);
}

/* ------------------------------------------------------------------ */

void sbuf_free()
{

    /* objectives:
       ===========

       free memory for ringbuffer structure

    */

    if(sbuf_max > 0) {
	free(sbuf_ptr);
	free(sbuf_mem);
    }
}



/* ------------------------------------------------------------------ */

packet_list_t *sbuf_retrieve()
{

    /* objectives:
       ===========

       retrieve a valid pointer to a packet_list_t structure
       return NULL on failure, valid pointer on success

       thread safe

    */

    packet_list_t *ptr;

    ptr = sbuf_ptr[sbuf_next];

    // check, if this structure is really free to reuse

    if(ptr->status != PACKET_NULL) return(NULL);

    // ok

    tc_debug(TC_DEBUG_FLIST, "alloc  =%d [%d]", sbuf_next, ptr->bufid);
    ++sbuf_next;
    sbuf_next %= sbuf_max;

    return(ptr);
}



/* ------------------------------------------------------------------ */

int sbuf_release(packet_list_t *ptr)
{

    /* objectives:
       ===========

       release a valid pointer to a packet_list_t structure
       return -1 on failure, 0 on success

       thread safe

    */

    // instead of freeing the memory and setting the pointer
    // to NULL we only change a flag

    if(ptr == NULL) return(-1);

    if(ptr->status != PACKET_EMPTY) {
	return(-1);
    } else {

    tc_debug(TC_DEBUG_FLIST, "release=%d [%d]", sbuf_next, ptr->bufid);
	ptr->status = PACKET_NULL;

    }

    return(0);
}

/* ------------------------------------------------------------------
 *
 * packet buffer management
 *
 * API:   flush_buffer_init
 *        flush_buffer_write
 *        flush_buffer_close
 *
 *-------------------------------------------------------------------*/

int packet_buffer_flush()

{

  /* objectives:
     ===========

     flush a packet and release memory

     requirements:
     =============

     thread-safe

  */

  packet_list_t *ptr;

  size_t n = 0;

  ptr = packet_retrieve();

  pthread_mutex_lock(&pack_ctr_lock);

  //info:
  tc_debug(TC_DEBUG_SYNC, "packet buffer status (%03d/%03d) [%.1f%%]",
           pack_ctr, pack_fill_ctr, (double) 100*pack_fill_ctr/FLUSH_BUFFER_MAX);
  pthread_mutex_unlock(&pack_ctr_lock);

  if(ptr==NULL) {
      //Ooops, shouldn't happen
      return(-1);
  }

  n = tc_pwrite(ifd, ptr->buffer, ptr->size);

  tc_debug(TC_DEBUG_SYNC,
           "done writing packet (%d/%03d)", ptr->id, pack_ctr);

  // no release, if not set!
  ptr->status = PACKET_EMPTY;

  packet_remove(ptr);

  return(0);
}

/* ------------------------------------------------------------------ */

static void flush_buffer_thread(void)
{

    int tt;

    for (;;) {

	pthread_mutex_lock(&pack_ctr_lock);

	while(pack_fill_ctr==0) {
	    pthread_cond_wait(&packet_pop_cv, &pack_ctr_lock);
#ifdef BROKEN_PTHREADS // Used to be MacOSX specific; kernel 2.6 as well?
	    pthread_testcancel();
#endif
	}

	pthread_mutex_unlock(&pack_ctr_lock);

	if(packet_buffer_flush()==0) {

	    pthread_mutex_lock(&pack_ctr_lock);
	    tt=pack_fill_ctr;
	    --pack_fill_ctr;
	    pthread_mutex_unlock(&pack_ctr_lock);

	    //notify submissions
	    if(tt==FLUSH_BUFFER_MAX) pthread_cond_signal(&packet_push_cv);
	}
    }

    return;
}

/* ------------------------------------------------------------------ */

int flush_buffer_init(int _ifd, int _verbose)
{

    ifd=_ifd;
    verbose_flag = _verbose;

    pack_fill_ctr=0;

#ifdef STATBUFFER
  // allocate buffer
  if(verbose_flag & TC_DEBUG)
    tc_log_msg(__FILE__, "allocating %d framebuffer (static)", FLUSH_BUFFER_MAX);
  if(sbuf_alloc(FLUSH_BUFFER_MAX)<0) {
    tc_log_error(__FILE__, "static framebuffer allocation failed");
    exit(1);
  }
#else
  if(verbose_flag & TC_DEBUG)
    tc_log_msg(__FILE__, "%d framebuffer (dynamic) requested", FLUSH_BUFFER_MAX);
#endif


  // start the flush thread
  if(pthread_create(&packet_thread, NULL, (void *) flush_buffer_thread, NULL)!=0) {
    tc_log_error(__FILE__, "failed to start packet flush thread");
    return(-1);
  } else {
    tc_debug(TC_DEBUG_SYNC, "flush buffer thread started");
  }

  return(0);
}

/* ------------------------------------------------------------------ */

int flush_buffer_write(int fd_out, char*buffer, int packet_size)
{

    packet_list_t *pack_ptr=NULL;


    pthread_mutex_lock(&pack_ctr_lock);

    while(pack_fill_ctr == FLUSH_BUFFER_MAX) {
      pthread_cond_wait(&packet_push_cv, &pack_ctr_lock);
#ifdef BROKEN_PTHREADS // Used to be MacOSX specific; kernel 2.6 as well?
      pthread_testcancel();
#endif
    }

    pthread_mutex_unlock(&pack_ctr_lock);

    pack_ptr = packet_register(pack_ctr);

    ac_memcpy(pack_ptr->buffer, buffer, packet_size);

    pack_ptr->size = packet_size;
    pack_ptr->status = PACKET_READY; //packet ready to go


    //total processed packets
    ++pack_ctr;

    pthread_mutex_lock(&pack_ctr_lock);

    ++pack_fill_ctr;

    //info: buffer status
    tc_debug(TC_DEBUG_SYNC,
             "packet submitted to flush buffer (%03d/%03d) [%.1f%%]",
             pack_ctr, pack_fill_ctr, (double) 100*pack_fill_ctr/FLUSH_BUFFER_MAX);

    pthread_mutex_unlock(&pack_ctr_lock);

    //notify write thread
    pthread_cond_signal(&packet_pop_cv);

    return(packet_size);
}

/* ------------------------------------------------------------------ */

int flush_buffer_close()
{

    pthread_mutex_lock(&pack_ctr_lock);

    while(pack_fill_ctr>0) {
      pthread_mutex_unlock(&pack_ctr_lock);
      usleep(TC_DELAY_MAX);
      pthread_mutex_lock(&pack_ctr_lock);
    }

    pthread_mutex_unlock(&pack_ctr_lock);

    return(0);
}

