/*
 *  seqinfo.c
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
#include "seqinfo.h"

pthread_mutex_t seq_list_lock=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t seq_ctr_lock=PTHREAD_MUTEX_INITIALIZER;

seq_list_t *seq_list_head=NULL;
seq_list_t *seq_list_tail=NULL;

static int _sfd=0;

static double fps;
static int seq_ctr=0, drop_ctr=0;

seq_list_t *seq_register(int id)
{

  /* objectives:
     ===========

     register new seq

     allocate space for new seq and establish backward reference


  */

  seq_list_t *ptr;

  pthread_mutex_lock(&seq_list_lock);

  // retrive a valid pointer from the pool

  if((ptr = tc_malloc(sizeof(seq_list_t))) == NULL) {
    pthread_mutex_unlock(&seq_list_lock);
    return(NULL);
  }

  memset(ptr, 0, sizeof(seq_list_t));

  ptr->status = BUFFER_EMPTY;

  ptr->next = NULL;
  ptr->prev = NULL;

  ptr->id  = id;

  if(seq_list_tail != NULL)
  {
      seq_list_tail->next = ptr;
      ptr->prev = seq_list_tail;
  }

  seq_list_tail = ptr;

  /* first seq registered must set seq_list_head */

  if(seq_list_head == NULL) seq_list_head = ptr;

  pthread_mutex_unlock(&seq_list_lock);

  return(ptr);

}


/* ------------------------------------------------------------------ */


void seq_remove(seq_list_t *ptr)

{

  /* objectives:
     ===========

     remove seq from chained list

  */


  if(ptr == NULL) return;         // do nothing if null pointer

  pthread_mutex_lock(&seq_list_lock);

  if(ptr->prev != NULL) (ptr->prev)->next = ptr->next;
  if(ptr->next != NULL) (ptr->next)->prev = ptr->prev;

  if(ptr == seq_list_tail) seq_list_tail = ptr->prev;
  if(ptr == seq_list_head) seq_list_head = ptr->next;

  free(ptr);
  ptr=NULL;

  pthread_mutex_unlock(&seq_list_lock);

}


/* ------------------------------------------------------------------ */


seq_list_t *seq_retrieve()

{

  /* objectives:
     ===========

     get pointer to next full seq

  */

  seq_list_t *ptr;

  pthread_mutex_lock(&seq_list_lock);

  ptr = seq_list_head;

  /* move along the chain and check for status */

  while(ptr != NULL)
    {
      if(ptr->status == BUFFER_READY)
	{
	  pthread_mutex_unlock(&seq_list_lock);
	  return(ptr);
	}
      ptr = ptr->next;
    }

  pthread_mutex_unlock(&seq_list_lock);

  return(NULL);
}
/* ------------------------------------------------------------------ */


static void seq_flush_thread(void)
{

  seq_list_t *ptr, *tmp;

  ptr = seq_retrieve();

  if(ptr!=NULL) {

      tc_debug(TC_DEBUG_SYNC, "syncinfo write (%d)", ptr->id);

      seq_write(ptr);

      // release valid pointer to pool
      ptr->status = BUFFER_EMPTY;

      tmp=ptr->prev;
      seq_remove(tmp);

      pthread_mutex_lock(&seq_ctr_lock);
      --seq_ctr;
      pthread_mutex_unlock(&seq_ctr_lock);

  } else
     tc_log_error(__FILE__, "called but no work to do - this shouldn't happen");

  return;
}


static long frame_ctr=0;
static long check_ctr=0;


/* ------------------------------------------------------------------ */

void seq_write(seq_list_t *ptr)
{

  int i, k, clone[256];

  sync_info_t sync_info;

  int tmp;

  int inc=0;

  double ftot_pts=0.0f;

  // -----------------
  //
  // write to log file
  //
  // -----------------

  // set clone flag for each frame:
  // 0 = drop this frame
  // 1 = unique frame
  // N = use N copies of this frame


  //default
  for(i=0; i<ptr->enc_pics; ++i) clone[i]=1;

  if(ptr->adj_pics<0) {

      tmp =  ptr->adj_pics;
      inc = -ptr->enc_pics/ptr->adj_pics;

      while(-tmp>0) {
	k=(-tmp*inc)%ptr->enc_pics;
        clone[k]=0;

	++tmp;
      }
  }
  if(ptr->adj_pics>0) {

      tmp =  ptr->adj_pics;
      inc = (ptr->adj_pics<ptr->enc_pics) ? (int) ptr->enc_pics/ptr->adj_pics : 1;

      while(tmp>0) {

	  k=(tmp*inc)%ptr->enc_pics;
	  ++clone[k];
	  --tmp;
      }
  }

  //write out

  for(i=0; i<ptr->enc_pics; ++i) {

      //check for pulldown flag
      sync_info.pulldown = ptr->pulldown;

      //master flag makes final decision
      if(ptr->sync_active == 0) {
	  clone[i]=0;
	  sync_info.drop_seq=1;
	  sync_info.pulldown=0;
      } else
	  sync_info.drop_seq=0;

      tc_debug(TC_DEBUG_PRIVATE, "[%ld] %d %d %d %ld",
               frame_ctr, ptr->id, i, clone[i], check_ctr);

      drop_ctr += (int) (clone[i]-1);

      sync_info.sequence = ptr->id;

      sync_info.enc_frame = (long) frame_ctr++;
      sync_info.adj_frame = (long) clone[i];

      ftot_pts=(double) ptr->tot_pts/90000;
      sync_info.dec_fps = (double) (ptr->tot_dec_pics)/ftot_pts;
      sync_info.enc_fps = (ptr->ptime) ? (double) (ptr->enc_pics*90000)/ptr->ptime:0.0f;

      sync_info.pts = (double) ptr->tot_pts/90000;

      if((tmp=tc_pwrite(_sfd, (uint8_t *) &sync_info, sizeof(sync_info_t)))!= sizeof(sync_info_t)) {
          tc_log_warn(__FILE__, "syncinfo write error (%d): %s",
                      tmp, strerror(errno));
      }
      check_ctr += clone[i];

      if(i==ptr->enc_pics-1) {
          tc_debug(TC_DEBUG_SYNC, "sync data for sequence %d flushed [%ld]",
                   ptr->id, sync_info.enc_frame);
      }
  }

   tc_debug(TC_DEBUG_PRIVATE, 
           "frames=%6ld seq=%4ld adj=%4d AV=%8.4f [fps] ratio= %.4f PTS= %.2f",
           sync_info.enc_frame, sync_info.sequence, drop_ctr,
           sync_info.dec_fps-fps, sync_info.enc_fps/fps, sync_info.pts);

  return;

}


/* ------------------------------------------------------------------ */

void seq_update(seq_list_t *ptr, int end_pts, int pictures, int packets, int flag, int hard_fps)
{

  int tmp;

  long int adj=0;

  long int request_pics=0, delay=0;

  double ftot_pts=0.0;

  //set basic parameter from inout variables

  ptr->seq_pics    = pictures + ptr->pics_first_packet;
  ptr->enc_pics    = ptr->seq_pics;
  ptr->packet_ctr  = packets;
  ptr->ptime       = end_pts - ptr->pts;
  ptr->sync_active = flag;  //master flag

  if(ptr->ptime<=0 || ptr->id == 0) {
    adj=0;
    delay=0;
    goto skip;
  }

  if(ptr->id && ptr->sync_reset==0) {

      // (1) calculate total encoded frames:
      ptr->tot_enc_pics = ptr->prev->tot_enc_pics + ptr->enc_pics;

      // (2) calculate total decoded frames:
      ptr->tot_dec_pics = ptr->prev->tot_dec_pics + ptr->seq_pics;

      // (3) total number of packets:
      ptr->tot_packet_ctr = ptr->prev->tot_packet_ctr + ptr->packet_ctr;

      // (4) total time since first sequence
      ptr->tot_pts = ptr->prev->tot_pts + ptr->ptime;

  } else {

      // first sequence
      ptr->tot_enc_pics = ptr->enc_pics;
      ptr->tot_dec_pics = ptr->seq_pics;
      ptr->tot_packet_ctr = ptr->packet_ctr;
      ptr->tot_pts = ptr->ptime;
  }

  // (5) total frames as requested by transcode
  ftot_pts=(double) ptr->tot_pts/90000.;
  request_pics = (long) (fps * ftot_pts);

  // (6) drop or clone frames of this sequence?
  delay = request_pics - ptr->tot_dec_pics;

  adj=0;

  if(delay>0) { //frame cloning no limit yet

      if(delay<ptr->seq_pics) {
	  adj = delay;
      } else {
	  adj = delay - (delay%ptr->seq_pics);
      }
  }

  if(delay<0) { //frame dropping, maximum seq_pics/2

      tmp=-delay;

      if ( (-delay) >= (ptr->seq_pics/2)) {
	  adj = -(ptr->seq_pics/2);
      } else {
	  adj = delay;
      }
  }

  //3:2 pulldown?

  // disable smooth dropping, pulldown, etc. This makes sense for variable
  // framerates when the fps changes back and forth from 29.9 to 23.9 fps. The
  // smooth dropping will work very badly then.
 if (hard_fps == 0) {

  ptr->pulldown=0;

  if(adj == -3 && ptr->ptime == 45045 && ptr->seq_pics==15) ptr->pulldown = 1;
  if(adj == -4 && ptr->ptime == 45045 && ptr->seq_pics==15) ptr->pulldown = 2;
  if(adj == -2 && ptr->ptime ==  6006 && ptr->seq_pics== 4) ptr->pulldown = 3;
  if(adj == -1 && ptr->ptime == 39039 && ptr->seq_pics==11) ptr->pulldown = 4;

  //smooth drop/copy algorithm 2002-08-21
  if(ptr->pulldown==0) {
    if(adj==-1 || adj==1 || adj==2) adj=0;
    if(adj== 3) adj=1;
  }
 }

 skip:

  if (verbose >= TC_DEBUG) {

    tc_log_msg(__FILE__, "---------------------------------------------------------");
    tc_log_msg(__FILE__, "MPEG sequence: %d (reset=%d)", ptr->id, ptr->sync_reset);
    tc_log_msg(__FILE__, "2k packets: %d (%d) | stream size %.2f MB", ptr->packet_ctr, ptr->tot_packet_ctr, (double) 2*ptr->tot_packet_ctr/(1<<10));
    tc_log_msg(__FILE__, "PTS: %f (abs) --> runtime=%f (sec)", (double) ptr->pts/90000, ftot_pts);
    tc_log_msg(__FILE__, "sequence length: %f | ftime: %.4f (sec)", (double) ptr->ptime/90000, (double) ptr->ptime/90000/ptr->seq_pics);
    tc_log_msg(__FILE__, "sequence frames: %2d (current=%.3f fps) %ld (average=%.3f fps)", ptr->seq_pics, (double) ptr->seq_pics*90000/ptr->ptime, ptr->ptime, (double) ptr->tot_dec_pics/ftot_pts);
    tc_log_msg(__FILE__, "3:2 pulldown flag: %d (%f) | master_flag = %d", ptr->pulldown, fps * ftot_pts - ptr->tot_dec_pics, flag);
    tc_log_msg(__FILE__, "total frames (encoded in sequence 0-%d): %d (requested=%ld) %ld --> adjust: %ld", ptr->id, ptr->tot_enc_pics, request_pics, delay, adj);

  }

  //save the adjustment:
  ptr->tot_dec_pics +=adj;
  ptr->seq_pics     +=adj;
  ptr->adj_pics      =adj;

  // A-V shift at end of this sequence
  ptr->av_sync = (ptr->tot_dec_pics - request_pics)/fps;


  if(verbose >= TC_DEBUG) {

    tc_log_msg(__FILE__, "adjusted frames (decoded in sequence 0-%d): %d --> A-V: %.4f", ptr->id, ptr->tot_dec_pics, ptr->av_sync);
    tc_log_msg(__FILE__, "---------------------------------------------------------");
  }


  // -----------------
  //
  // write to log file
  //
  // -----------------

  ptr->status = BUFFER_READY;

  pthread_mutex_lock(&seq_ctr_lock);
  ++seq_ctr;
  pthread_mutex_unlock(&seq_ctr_lock);

  seq_flush_thread();

  return;

}

/* ------------------------------------------------------------------ */

/********
 * FIXME: these two functions, and the printf()s below, are only used
 * with the undocumented TC_DEMUX_SEQ_LIST (-M 5) mode.  Is printf()
 * appropriate, or for that matter is this even needed at all?  --AC
 ********/

static int seq_offset=0, unit_ctr=-1;

void seq_list_frames()
{
  if(unit_ctr==-1) return;
  tc_log_info(__FILE__, "%8ld video frame(s) in unit %d detected",
              (long) frame_ctr, unit_ctr);
}

/* ------------------------------------------------------------------ */

void seq_list(seq_list_t *ptr, int end_pts, int pictures, int packets, int flag)
{

  int n, id;

  int tmp;

  long int adj=0;

  long int request_pics=0, delay=0;

  double ftot_pts=0.0;

  //set basic parameter from inout variables

  //-------------------------------------------------------
  //for NTSC film
  //we need to recalculate ptr->seq_pics
  //-------------------------------------------------------

  id=ptr->id-seq_offset;

  ptr->seq_pics    = pictures + ptr->pics_first_packet;
  ptr->enc_pics    = ptr->seq_pics;

  ptr->ptime       = end_pts - ptr->pts;
  ptr->sync_active = flag;  //master flag

  //first sequence timing info may be wrong
  if(ptr->ptime<=0 || id == 0 || ptr->sync_reset) {
    adj=0;
    delay=0;
    goto skip;
  }

  if(ptr->id && ptr->sync_reset==0) {

      // (1) calculate total encoded frames:
      ptr->tot_enc_pics = ptr->prev->tot_enc_pics + ptr->enc_pics;

      // (2) calculate total decoded frames:
      ptr->tot_dec_pics = ptr->prev->tot_dec_pics + ptr->seq_pics;

      // (3) total number of packets:
      ptr->tot_packet_ctr = ptr->prev->tot_packet_ctr + ptr->packet_ctr;

      // (4) total time since first sequence
      ptr->tot_pts = ptr->prev->tot_pts + ptr->ptime;

  } else {

      ptr->tot_enc_pics = ptr->enc_pics;
      ptr->tot_dec_pics = ptr->seq_pics;
      ptr->tot_packet_ctr = ptr->packet_ctr;
      ptr->tot_pts = ptr->ptime;
  }

  // (5) total frames as requested by transcode
  ftot_pts=(double) ptr->tot_pts/90000;
  request_pics = (long) (fps * ftot_pts);

  // (6) drop or clone frames of this sequence?
  delay = request_pics - ptr->tot_dec_pics;

  adj=0;

  if(delay>0) { //frame cloning no limit yet

      if(delay<ptr->seq_pics) {
	  adj = delay;
      } else {
	  adj = delay - (delay%ptr->seq_pics);
      }
  }

  if(delay<0) { //frame dropping, maximum seq_pics/2

      tmp=-delay;

      if ( (-delay) >= (ptr->seq_pics/2)) {
	  adj = -(ptr->seq_pics/2);
      } else {
	  adj = delay;
      }
  }

  ptr->pulldown=0;

  //smooth drop/copy algorithm 2002-06-04
  if((adj==1 || adj == -1 || adj==2 || adj== -2 || adj== -3 || adj== 3)
     && ptr->pulldown==0) adj=0;
  if(adj >  3 && ptr->pulldown==0) adj= 1;
  if(adj < -3 && ptr->pulldown==0) adj=-1;

  //FIXME: drop all NTSC stuff, let transcode handle frame count
  adj=0;

 skip:

  //save the adjustment:
  ptr->tot_dec_pics +=adj;
  ptr->seq_pics     +=adj;
  ptr->adj_pics      =adj;

  // A-V shift at end of this sequence
  ptr->av_sync = (ptr->tot_dec_pics - request_pics)/fps;


  // -----------------
  //
  // write to log file
  //
  // -----------------

  ptr->status = BUFFER_READY;

  pthread_mutex_lock(&seq_ctr_lock);
  ++seq_ctr;
  pthread_mutex_unlock(&seq_ctr_lock);

  //print frame navigation information:

  if(ptr->sync_reset) {

    seq_list_frames();

    frame_ctr=0;
    seq_offset=ptr->id;
    ++unit_ctr;

    id=0;

  }

  //  tc_log_msg(__FILE__, "--- %d %d %d %d %d %d", delay, adj, ptr->seq_pics , ptr->enc_pics, ptr->pics_first_packet, pictures);

  //
  // first sequence of stream or new unit
  //

  if(id==0 || ptr->sync_reset) {

    for(n=0; n<ptr->enc_pics; ++n)
      printf("%2d %6ld %5d %5d %6ld %3d\n", unit_ctr, (long) frame_ctr++, id, id, (long) ptr->packet_ctr, n);

    return;
  }

  //
  // regular sequence
  //

  for(n=0; n<ptr->enc_pics; ++n) {

    if(n==0 || n==1) {
      printf("%2d %6ld %5d %5d %6ld %3d\n", unit_ctr, (long) frame_ctr++, id, id-1, (long) ptr->prev->packet_ctr, ptr->prev->seq_pics+n);
    } else {
      printf("%2d %6ld %5d %5d %6ld %3d\n", unit_ctr, (long) frame_ctr++, id, id, (long) ptr->packet_ctr, n);
    }
  }
  return;
}

/* ------------------------------------------------------------------ */

int seq_init(const char *logfile, int _ext_sfd, double _fps, int _verbose)
{

  //need to open the file

  if(logfile != NULL) {
    if((_sfd = open(logfile, O_WRONLY|O_CREAT, 0666))<0) {
      tc_log_error(__FILE__, "open logfile: %s", strerror(errno));
      return(-1);
    }
  }

  fps = _fps;

  //done

  if(_verbose & TC_DEBUG)
      tc_log_msg(__FILE__, "open %s for frame sync information", logfile);

  return(0);
}

/* ------------------------------------------------------------------ */

void seq_close()
{

  if(_sfd != 0) close(_sfd);
  _sfd=0;

  return;
}

