/*
 *  clone.c
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

//undef if you experience lock ups!
#define USE_FIFO_LOGFILE 1

#include "src/transcode.h"
#include "libtc/libtc.h"
#include "libtcexport/export.h"
#include "clone.h"
#include "seqinfo.h"  /* for sync_type_t */
#include "ivtc.h"

#include "frame_info.h"

static FILE *pfd=NULL;

static int clone_ctr=0, sync_disabled_flag=0;

static int width, height, vcodec;

static char *video_buffer=NULL, *pulldown_buffer=NULL;

static int sync_ctr=0, frame_ctr=0, drop_ctr=0, seq_dis=-1;

static char *logfile;

static int sfd=0;

static double fps;

static pthread_t thread=(pthread_t)0;

static int clone_read_thread_flag=0;

static pthread_mutex_t buffer_fill_lock=PTHREAD_MUTEX_INITIALIZER;
static int buffer_fill_ctr;
static pthread_cond_t buffer_fill_cv=PTHREAD_COND_INITIALIZER;


int clone_init(FILE *fd)
{

  vob_t *vob;

  // copy file pointer
  pfd=fd;

  vob = tc_get_vob();
  fps = vob->fps;
  width = vob->im_v_width;
  height = vob->im_v_height;
  vcodec = vob->im_v_codec;

  //sync log file

  if((sfd = open(logfile, O_RDONLY, 0666))<0) {
    tc_log_perror(__FILE__, "open file");
    return(-1);
  }

  if(verbose & TC_DEBUG)
    tc_log_msg(__FILE__, "reading video frame sync data from %s", logfile);

  // allocate space, assume max buffer size

  if((video_buffer = tc_zalloc(width*height*3))==NULL) {
    tc_log_error(__FILE__, "out of memory");
    sync_disabled_flag=1;
    return(-1);
  }

  // allocate space, assume max buffer size

  if((pulldown_buffer = tc_zalloc(width*height*3))==NULL) {
    tc_log_error(__FILE__, "out of memory");
    sync_disabled_flag=1;
    return(-1);
  }

  //basic operational flags
  clone_read_thread_flag=1;
  sync_disabled_flag=0;

  if(pthread_create(&thread, NULL, (void *) clone_read_thread, NULL)!=0) {
      tc_log_error(__FILE__, "failed to start frame processing thread");
      sync_disabled_flag=1;
      return(-1);
  }

  return(0);
}


static frame_info_list_t *fiptr=NULL;

static int buffered_p_read(char *s)
{

    pthread_mutex_lock(&buffer_fill_lock);

    //exit
    if(buffer_fill_ctr <= 0 && clone_read_thread_flag==0) {
	pthread_mutex_unlock(&buffer_fill_lock);
	return(0);
    }

    tc_debug(TC_DEBUG_SYNC, "WAIT (%d)", buffer_fill_ctr);

    while(buffer_fill_ctr == 0) {
      pthread_cond_wait(&buffer_fill_cv, &buffer_fill_lock);
#ifdef BROKEN_PTHREADS // Used to be MacOSX specific; kernel 2.6 as well?
      pthread_testcancel();
#endif
    }

    --buffer_fill_ctr;

    pthread_mutex_unlock(&buffer_fill_lock);

    fiptr=frame_info_retrieve();

    ac_memcpy(s, fiptr->sync_info, sizeof(sync_info_t));

    return(sizeof(sync_info_t));
}

static int get_next_frame(char *buffer, int size)
{

  int clone_flag;

  int i, ret=0;

  double drift=0;

  sync_info_t ptr;

  // read next log file entry:

  //default
  clone_flag=1;

  if(sync_disabled_flag) goto read_only;

  tc_debug(TC_DEBUG_SYNC, "----------------- reading syncinfo (%d)", sync_ctr);

  if((i=buffered_p_read((char *) &ptr)) != sizeof(sync_info_t)) {

      if(verbose & TC_DEBUG) {
	  tc_log_msg(__FILE__, "read error (%d/%ld)", i, (long)sizeof(sync_info_t));
      }

      //no more frames?
      sync_disabled_flag=1;

      return(-1);
  }

  //only relevant information
  clone_flag = ptr.adj_frame;

  // infos:

  if(verbose >= TC_DEBUG) {
      if(ptr.sequence != seq_dis) {

	  drift = ptr.dec_fps - fps;

	  tc_log_msg(__FILE__, "frame=%6ld seq=%4ld adj=%4d AV=%8.4f [fps] ratio= %.4f PTS= %.2f",
		     ptr.enc_frame, ptr.sequence, drop_ctr, drift,
		     ((fps>0)?ptr.enc_fps/fps:0.0f), ptr.pts);
	  if(ptr.drop_seq) {
	      tc_log_msg(__FILE__, "MPEG sequence (%ld) dropped for AV sync correction",
			 ptr.sequence);
	  }
	  seq_dis=ptr.sequence;
      }
  }

  drop_ctr += (clone_flag-1);
  tc_update_frames_dropped(clone_flag-1);

  ++sync_ctr;

  read_only:

  tc_debug(TC_DEBUG_SYNC, "reading frame (%d)", frame_ctr);
  ret = fread(buffer, size, 1, pfd);

  if(ret!=1) {
    sync_disabled_flag=1;
    return(-1);
  }
  ++frame_ctr;


  // this number determines the number of frame copies, including master
  // frame

  // ----------------------------------------------------------------
  //
  // new: reverse 3:2 pulldown (inverse telecine)
  // currently, only a few number of hardcoded sequences
  // indicated by pulldown flag, are supported, s. below:

  if(ptr.pulldown > 0)
    ivtc(&clone_flag, ptr.pulldown, buffer, pulldown_buffer, width, height, size, vcodec, verbose);

  //free frame info buffer
  frame_info_remove(fiptr);
  fiptr=NULL;

  return(clone_flag);
}

//import API
int clone_frame(char *buffer, int size)
{

  int i=0;

  if(clone_ctr) {

    //copy already buffered frame

    ac_memcpy(buffer, video_buffer, size);

    --clone_ctr;
    return(0);
  }

  //we loop until we have a valid frame;

  for (;;) {

    i=get_next_frame(buffer, size);

    //error or eos
    if(i==-1) return(-1);

    if(i==1) return(0); //unique frame, already loaded in buffer

    if(i>1) {

      //frame will be cloned. We need to get a copy first

      ac_memcpy(video_buffer, buffer, size);

      clone_ctr=i-1;
      return(0);
    }
    //frame dropped, get next one
  }

  return(0);
}

void clone_close()
{
    void *status;

    // cancel the thread
    if (thread) {
      pthread_cancel(thread);
#ifdef BROKEN_PTHREADS // Used to be MacOSX specific; kernel 2.6 as well?
      pthread_cond_signal(&buffer_fill_cv);
#endif
      pthread_join(thread, &status);
      thread = (pthread_t)0;
    }

    //reentrance safe

    if(video_buffer != NULL) free(video_buffer);
    video_buffer=NULL;

    if(pulldown_buffer != NULL) free(pulldown_buffer);
    pulldown_buffer=NULL;

    if(sfd>0) {

	close(sfd);
	unlink(logfile);

	free(logfile);
	sfd=0;
    }

    if (pfd) pclose(pfd);
    pfd = NULL;
}

char *clone_fifo()
{

  char *name, *a, b[PATH_MAX];

  //need to create a pipe here
  if ((a = getenv("TMPDIR")) != NULL)
      tc_snprintf(b, PATH_MAX, "%s/%s", a, "fileXXXXXX");
  else
      tc_snprintf(b, PATH_MAX, "%s/%s", "/tmp", "fileXXXXXX");

  name = mktemp(b);

  logfile=tc_strdup(name);

#ifdef USE_FIFO_LOGFILE
  if(mkfifo(logfile, 0666)<0) {
    tc_log_perror(__FILE__, "create FIFO");
    return(NULL);
  }
#endif

  return(logfile);
}

void clone_read_thread()
{
    frame_info_list_t *ptr = NULL;

    int i=0, j=0;

    for(;;) {

	if((ptr = frame_info_register(i))==NULL) {

	    tc_log_error(__FILE__, "could not allocate a frame info buffer");

	    pthread_mutex_lock(&buffer_fill_lock);
	    clone_read_thread_flag=0;
	    pthread_mutex_unlock(&buffer_fill_lock);
	    pthread_exit(0);
	    return;
	}

	if((ptr->sync_info = tc_zalloc(sizeof(sync_info_t)))==NULL) {
	    tc_log_error(__FILE__, "out of memory");
	    pthread_mutex_lock(&buffer_fill_lock);
	    clone_read_thread_flag=0;
	    pthread_mutex_unlock(&buffer_fill_lock);
	    pthread_exit(0);
	}

    tc_debug(TC_DEBUG_SYNC, "READ (%d)", i);

	if((j=tc_pread(sfd, (uint8_t *) ptr->sync_info, sizeof(sync_info_t))) != sizeof(sync_info_t)) {

	    if(verbose & TC_DEBUG)
		tc_log_msg(__FILE__, "tc_pread error (%d/%ld)", j, (long)sizeof(sync_info_t));
	    pthread_mutex_lock(&buffer_fill_lock);
	    clone_read_thread_flag=0;
	    pthread_mutex_unlock(&buffer_fill_lock);
	    pthread_exit(0);
	}

	// ready for encoding
	frame_info_set_status(ptr, FRAME_INFO_READY);

	pthread_mutex_lock(&buffer_fill_lock);
	++buffer_fill_ctr;
	//tc_log_msg(__FILE__, "fill (%d)", buffer_fill_ctr);
	//notify import thread
	pthread_cond_signal(&buffer_fill_cv);
	pthread_mutex_unlock(&buffer_fill_lock);


	++i;
    }

    return;
}

