/*
 *  demuxer.c
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

#include "tccore/tcinfo.h"

#include "src/transcode.h"
#include "libtc/libtc.h"

#include "ioaux.h"
#include "aux_pes.h"
#include "seqinfo.h"
#include "demuxer.h"
#include "packets.h"

#include <math.h>

static int demux_mode=TC_DEMUX_SEQ_ADJUST;

int gop, gop_pts, gop_cnt;

typedef struct timecode_struc	/* Time_code Struktur laut MPEG		*/
{   unsigned long msb;		/* fuer SCR, DTS, PTS			*/
    unsigned long lsb;
    unsigned long reference_ext;
    unsigned long negative;     /* for delays when doing multiple files */
} Timecode_struc;

#define MAX_FFFFFFFF 4294967295.0 	/* = 0xffffffff in hex.	*/

/* ------------------------------------------------------------
 *
 * support code (scr_rewrite()) moved from aux_pes.c
 *
 * ------------------------------------------------------------*/

static void make_timecode (double timestamp, Timecode_struc *pointer)
{
    double temp_ts;

    if (timestamp < 0.0) {
      pointer->negative = 1;
      timestamp = -timestamp;
    } else
      pointer->negative = 0;

    temp_ts = floor(timestamp / 300.0);

    if (temp_ts > MAX_FFFFFFFF) {
      pointer->msb=1;
      temp_ts -= MAX_FFFFFFFF;
      pointer->lsb=(unsigned long)temp_ts;
    } else {
      pointer->msb=0;
      pointer->lsb=(unsigned long)temp_ts;
    }

    pointer->reference_ext = (unsigned long)(timestamp - (floor(timestamp / 300.0) * 300.0));

}

#define MPEG2_MARKER_SCR	1		/* MPEG2 Marker SCR	*/

/*************************************************************************
    Kopiert einen TimeCode in einen Bytebuffer. Dabei wird er nach
    MPEG-Verfahren in bits aufgesplittet.

    Makes a Copy of a TimeCode in a Buffer, splitting it into bitfields
    according to MPEG-System
*************************************************************************/

static void buffer_timecode_scr (Timecode_struc *pointer, unsigned char **buffer)
{

  unsigned char temp;
  unsigned char marker=MPEG2_MARKER_SCR;


  temp = (marker << 6) | (pointer->msb << 5) |
    ((pointer->lsb >> 27) & 0x18) | 0x4 | ((pointer->lsb >> 28) & 0x3);
  *((*buffer)++)=temp;
  temp = (pointer->lsb & 0x0ff00000) >> 20;
  *((*buffer)++)=temp;
  temp = ((pointer->lsb & 0x000f8000) >> 12) | 0x4 |
    ((pointer->lsb & 0x00006000) >> 13);
  *((*buffer)++)=temp;
  temp = (pointer->lsb & 0x00001fe0) >> 5;
  *((*buffer)++)=temp;
  temp = ((pointer->lsb & 0x0000001f) << 3) | 0x4 |
    ((pointer->reference_ext & 0x00000180) >> 7);
  *((*buffer)++)=temp;
  temp = ((pointer->reference_ext & 0x0000007F) << 1) | 1;
  *((*buffer)++)=temp;

}

static void scr_rewrite(char *buf, uint32_t pts)
{
  Timecode_struc timecode;
  unsigned char * ucbuf = (unsigned char *)buf;

  timecode.msb = 0;
  timecode.lsb = 0;
  timecode.reference_ext = 0;
  timecode.negative = 0;

  make_timecode((double) pts, &timecode);

  buffer_timecode_scr(&timecode, &ucbuf);
}

/* ------------------------------------------------------------
 *
 * demuxer / synchronization thread
 *
 * ------------------------------------------------------------*/

void tcdemux_thread(info_t *ipipe)
{

    int k, id=0, zz=0;

    int j, i, bytes, filesize;
    char *buffer=NULL;

    int payload_id=0, select=PACKAGE_ALL;

    double pts=0.0f, ref_pts=0.0f, resync_pts=-1.0f, pts_diff=0.0f, track_initial_pts=0.0f;
    double av_fine_pts1=-1.0f, av_fine_pts2=-1.0f, av_fine_diff=0.0f;

    uint32_t frame_based_lpts=0;

    int unit_seek=0, unit, track=0, is_track=0;

    int resync_seq1=0, resync_seq2=INT_MAX, seq_dump, seq_seek;
    int keep_seq = 0;
    int hard_fps = 0;

    int has_pts_dts=0, demux_video=0, demux_audio=0;

    int flag_flush        = 0;
    int flag_force        = 0;
    int flag_eos          = 0;
    int flag_has_audio    = 0;
    int flag_append_audio = 0;
    int flag_notify       = 1;
    int flag_avsync       = 1;
    int flag_skip         = 0;
    int flag_sync_reset   = 0;
    int flag_sync_active  = 0;
    int flag_loop_all     = 0;
    int flag_av_fine_tune = 0;
    int flag_rewrite_scr  = 0;
    int flag_field_encoded= 0;

    //for demux_mode=2
    int seq_picture_ctr=0, pack_picture_ctr=0, sequence_ctr=0, packet_ctr=0;

    char buf[256];
    const char *logfile;
    unsigned long i_pts, i_dts;
    unsigned int packet_size=VOB_PACKET_SIZE;
    seq_list_t *ptr=NULL;
    double fps;


    // allocate space
    if((buffer = tc_zalloc(packet_size))==NULL) {
      tc_log_perror(__FILE__, "out of memory");
      exit(1);
    }

    // copy info parameter to local variables

    unit_seek   = ipipe->ps_unit;
    resync_seq1 = ipipe->ps_seq1;
    resync_seq2 = ipipe->ps_seq2;
    track       = ipipe->track;

    //map track on substream id

    switch(ipipe->codec) {

    case TC_CODEC_SUB:
      track+=0x20;
      flag_rewrite_scr=1;
      break;
    case TC_CODEC_AC3:
      track+=0x80;
      break;
    case TC_CODEC_PCM:
      track+=0xA0;
      break;
    case TC_CODEC_MP3:
      track+=0xC0;
      break;
    case TC_CODEC_MPEG2:
      // for MPEG2 video, use this substream id for sync adjustment
      track=ipipe->subid;
      demux_video=1;
      break;
    }

    //0.6.0pre4
    if(demux_video==0) {

	if(ipipe->demux == TC_DEMUX_SEQ_FSYNC)
	    ipipe->demux=TC_DEMUX_SEQ_ADJUST;

	if(ipipe->demux == TC_DEMUX_SEQ_FSYNC2)
	    ipipe->demux=TC_DEMUX_SEQ_ADJUST2;

	//substream synchonized with video in "force frame rate" mode
	demux_audio=1;
    }

    if(ipipe->demux == TC_DEMUX_SEQ_FSYNC || ipipe->demux == TC_DEMUX_SEQ_FSYNC2 || ipipe->demux == TC_DEMUX_SEQ_LIST) {

      //allocate buffer
      if(flush_buffer_init(ipipe->fd_out, ipipe->verbose)<0) {
	tc_log_error(__FILE__, "flush buffer facility init failed");
	exit(1);
      }

      //need to open the logfile
      if(seq_init(ipipe->name, ipipe->fd_log, ipipe->fps, ipipe->verbose)<0) {
	tc_log_error(__FILE__, "sync mode init failed");
	exit(1);
      }
    }


    //new default behaving:
    //    if(unit_seek==0 && resync_seq1==0 && resync_seq2 == INT_MAX) flag_loop_all=1;
    //changes 0.6.0pre3:
    //tcprobe selects start unit --> switch to flag_loop_all always true
    // for any given unit
    if(resync_seq1==0 && resync_seq2 == INT_MAX) flag_loop_all=1;

    demux_mode  = ipipe->demux;
    select      = ipipe->select;
    fps         = ipipe->fps;
    logfile     = ipipe->name;
    keep_seq    = ipipe->keep_seq;
    hard_fps    = ipipe->hard_fps_flag;

    j=0;  //packet counter
    i=0;  //skipped packets counter
    k=0;  //unit counter

    // will be switched on as soon start of sequences to flush is reached
    flag_flush=0;

    flag_notify=1;

    flag_avsync=0;
    flag_append_audio=0;

    if(keep_seq) flag_sync_active=1;

    unit=unit_seek;

    seq_seek = resync_seq1;
    seq_dump = resync_seq2 - resync_seq1;

    ++unit_seek;
    ++seq_seek;

    if(!flag_loop_all) {
      tc_log_msg(__FILE__, "seeking to sequence %d:%d ...", unit, resync_seq1);
    }

    filesize = 0;
    for(;;) {

      /* ------------------------------------------------------------
       *
       * (I) read a 2048k block
       *
       * ------------------------------------------------------------*/

      if((bytes=tc_pread(ipipe->fd_in, buffer, packet_size)) != packet_size) {

	//program end code?
	if(bytes==4) {
	  if(scan_pack_header(buffer, MPEG_PROGRAM_END_CODE)) {
	    if(ipipe->verbose & TC_DEBUG)
	      tc_log_msg(__FILE__, "(pid=%d) program stream end code detected",
			 getpid());
	    break;
	  }
	}

	if(bytes)
	  tc_log_warn(__FILE__, "invalid program stream packet size (%d/%d)",
		      bytes, packet_size);

	break;
      }
      filesize += bytes;

      // do not make any tests in pass-through mode
      if(demux_mode==TC_DEMUX_OFF) goto flush_packet;

      /* ------------------------------------------------------------
       *
       * (II) packet header ok?
       *
       * ------------------------------------------------------------*/


      if(!scan_pack_header(buffer, TC_MAGIC_VOB)) {

	if(flag_notify && (ipipe->verbose & TC_DEBUG))
	  tc_log_warn(__FILE__, "(pid=%d) invalid packet header detected",
		      getpid());

	// something else?

	if(scan_pack_header(buffer, MPEG_VIDEO) | scan_pack_header(buffer, MPEG_AUDIO)) {

	    if(ipipe->verbose & TC_STATS)
		tc_log_msg(__FILE__, "(pid=%d) MPEG system stream detected",
			   getpid());

	    if(scan_pack_header(buffer, MPEG_VIDEO)) payload_id=PACKAGE_VIDEO;
	    if(scan_pack_header(buffer, MPEG_AUDIO)) payload_id=PACKAGE_AUDIO_MP3;

	    // no further processing
	    goto flush_packet;
	} else {

	    tc_log_warn(__FILE__, "(pid=%d) '0x%02x%02x%02x%02x' not yet supported",
			getpid(), buffer[0] & 0xff, buffer[1] & 0xff,
			buffer[2] & 0xff, buffer[3] & 0xff);
	    break;
	}
      } else {

	//MPEG1?
	if ((buffer[4] & 0xf0) == 0x20) {

	  payload_id=PACKAGE_MPEG1;
	  flag_flush=1;

	  if(ipipe->verbose & TC_STATS)
	    tc_log_msg(__FILE__, "(pid=%d) MPEG-1 video stream detected",
		       getpid());

	  // no further processing
	  goto flush_packet;
	}
      }


      /* ------------------------------------------------------------
       *
       * (III) analyze packet contents
       *
       * ------------------------------------------------------------*/


      // proceed with a valid package, assume defaults

      flag_skip=0;          //do not skip
      has_pts_dts=0;        //no pts_dts stamp
      payload_id=0;         //payload unknown
      flag_sync_reset=0;    //no reset of syncinfo

      id = buffer[17] & 0xff;  //payload id byte

      //MPEG 2?
      if ((buffer[4] & 0xc0) == 0x40) {

	// do not change any flags

	if(ipipe->verbose & TC_STATS) {
	  //display info only once
	  tc_log_msg(__FILE__, "(pid=%d) MPEG-2 video stream detected",
		     getpid());
	}
      } else {

	//MPEG1
	if ((buffer[4] & 0xf0) == 0x20) {

	  payload_id=PACKAGE_MPEG1;

	  if(ipipe->verbose & TC_STATS) {
	    //display info only once
	    tc_log_msg(__FILE__, "(pid=%d) MPEG-1 video stream detected",
		       getpid());
	  }
	} else {

	  payload_id=PACKAGE_PASS;

	  if(ipipe->verbose & TC_DEBUG)
	    tc_log_warn(__FILE__, "(pid=%d) unknown stream packet id detected",
			getpid());
	}

	//flush all MPEG1 stuff
	goto flush_packet;
      }

      /* ------------------------------------------------------------
       *
       * (IV) audio payload
       *
       * ------------------------------------------------------------*/

      // check payload id
      // process this audio packet?

      // sync to AC3 audio mode?
      if(id == P_ID_AC3) payload_id = PACKAGE_PRIVATE_STREAM;

      // sync to MP3 audio mode?
      if(id >= 0xc0 && id <= 0xdf) payload_id = PACKAGE_AUDIO_MP3;

      // are we dealing with the right track?
      // check here:

      is_track=0;

      if(payload_id == PACKAGE_PRIVATE_STREAM) {

	//position of track code
	uint8_t *_buf=buffer+14;
	uint8_t *_tmp=_buf + 9 + _buf[8];

	is_track = ((*_tmp) == track) ? 1:0;

	if(ipipe->verbose & TC_STATS)
	  tc_log_msg(__FILE__, "substream [0x%x] %d", *_tmp, is_track);

	if(is_track==0) {
	  flag_skip=1;  //drop this packet
	} else {
	  flag_skip=0;
	  goto sync_track;
	}
      }

      if(payload_id & PACKAGE_AUDIO_MP3) {

	is_track = ((id) == track) ? 1:0;
	if(is_track==0) flag_skip=1;  //drop this packet

	if(ipipe->verbose & TC_STATS)
	  tc_log_msg(__FILE__, "MPEG audio track [0x%x] %d", id, is_track);
      }

    sync_track:

      if(is_track) {


	if(flag_sync_active==0) {

	  //first valid audio packet!

	  // get pts time stamp:
	  ac_memcpy(buf, &buffer[20], 16);
	  has_pts_dts=get_pts_dts(buf, &i_pts, &i_dts);

	  if(has_pts_dts) {
	    track_initial_pts=(double)i_pts/90000.;
	  } else {
	    //fallback to scr time stamp:
	    ac_memcpy(buf, &buffer[4], 6);
	    track_initial_pts = read_time_stamp(buf);
	  }

	  if(resync_pts<0) {
	    pts_diff = track_initial_pts;
	  } else {
	    pts_diff = track_initial_pts - resync_pts;
	  }

	  //pts_diff<0 is OK, the packets will be dropped to establish sync
	  //pts_diff>0 is not so simple, since we need to drop video
	  //packets, already submitted to flush buffer

	  //enable sync mode, check for PTS giant leap????
	  if(pts_diff < TC_DEMUX_MIN_PTS || pts_diff > TC_DEMUX_CRIT_PTS)
	    flag_sync_active = 1;

	  //0.6.0pre5: frame dropping handled by transcode
	  flag_sync_active=1;

	  //unless this flag is on, the video sequence is dropped
	  //in TC_DEMUX_SEQ_FSYNC mode
	}


	// sync now, if requested:

	if(!unit_seek) flag_has_audio=1; //unit has audio packets


	// need to find the time difference of two audio packets
	// for fine-tuning AV sync.

	if(flag_av_fine_tune == 0) {
	  // get pts time stamp:
	  ac_memcpy(buf, &buffer[20], 16);
	  has_pts_dts=get_pts_dts(buf, &i_pts, &i_dts);

	  if(av_fine_pts1<0) {
	    av_fine_pts1 = (double)i_pts/90000.;
	  } else {
	    av_fine_pts2 = (double)i_pts/90000.;
	    flag_av_fine_tune=1;
	  }

	  //diff:
	  if(flag_av_fine_tune==1) {
	    av_fine_diff=av_fine_pts2-av_fine_pts1;
	    if(ipipe->verbose & TC_DEBUG)
	      tc_log_msg(__FILE__, "AV fine-tuning: %d ms",
			 (int)(av_fine_diff*1000));
	  }

	  //sanity check:
	  if(av_fine_diff<0) av_fine_diff=0.0f;

	}

	//Pre-processing: check if we need to re-sync?

	if(demux_mode == TC_DEMUX_SEQ_FSYNC2 || demux_mode == TC_DEMUX_SEQ_ADJUST2) {
	    //new demux modes let transcode handle audio sync shift
	    flag_avsync=0;
	    flag_skip=0;
	}

	if(flag_avsync) {

	  // get pts time stamp:
	  ac_memcpy(buf, &buffer[20], 16);
	  has_pts_dts=get_pts_dts(buf, &i_pts, &i_dts);

	  if(has_pts_dts) {
	    pts=(double)i_pts/90000.;
	  } else {
	    //fallback to scr time stamp:
	    ac_memcpy(buf, &buffer[4], 6);
	    pts = read_time_stamp(buf);
	  }

	  pts_diff = pts - resync_pts;

	  //correction
	  //FIXME
	  pts_diff += av_fine_diff;

	  if(pts_diff<0) {
	    flag_skip=1;

	    if(ipipe->verbose & TC_DEBUG)
	      tc_log_msg(__FILE__, "(pid=%d) audio packet %06d for PU [%d] skipped (%.4f)",
			 getpid(), j, ((k==0)? 0:k-1), pts-resync_pts);
	  } else {
	    //reset
	    flag_skip=0;
	    flag_avsync=0;
	    if(ipipe->verbose)
	      tc_log_msg(__FILE__, "(pid=%d) AV sync established for PU [%d] at PTS=%.4f (%.4f)",
			 getpid(), k-1, pts, pts-resync_pts);
	  }
	}

	//Post-processing: more audio packets?

	if(flag_append_audio) {

	  //need to flush a few audio packets, if video ahead

	  // get pts time stamp:
	  ac_memcpy(buf, &buffer[20], 16);
	  has_pts_dts=get_pts_dts(buf, &i_pts, &i_dts);

	  if(has_pts_dts) {
	    pts=(double)i_pts/90000.;
	  } else {
	    //fallback to scr time stamp:
	    ac_memcpy(buf, &buffer[4], 6);
	    pts = read_time_stamp(buf);
	  }

	  pts_diff = pts - resync_pts;

	  if(pts_diff<0) {

	    //append this packet
	    flag_skip=0;

	    if(ipipe->verbose & TC_DEBUG)
	      tc_log_msg(__FILE__, "(pid=%d) audio packet %06d for PU [%d] appended (%.4f)",
			 getpid(), j, ((k==0)? 0:k-1), pts-resync_pts);
	  } else {
	    //abort - all done
	    flag_eos=1;
	    if(ipipe->verbose)
	      tc_log_msg(__FILE__, "(pid=%d) AV sync abandoned for PU [%d] at PTS=%.4f (%.4f)",
			 getpid(), k-1, pts, pts-resync_pts);
	  }
	}
      }

      //only go for audio in this phase
      if(flag_append_audio) goto flush_packet;

      /* ------------------------------------------------------------
       *
       * (V) misc payload
       *
       * ------------------------------------------------------------*/


      if(id == P_ID_PROG || id == P_ID_PADD)  {

	payload_id=PACKAGE_NAV;

	// get pts time stamp:
	ac_memcpy(buf, &buffer[20], 16);
	has_pts_dts=get_pts_dts(buf, &i_pts, &i_dts);

	if(has_pts_dts) {
	  pts=(double)i_pts/90000.;
	} else {
	  //fallback to scr time stamp:
	  ac_memcpy(buf, &buffer[4], 6);
	  pts = read_time_stamp(buf);
	}

	//do not dump this packet
	flag_skip=1;
      }

      /* ------------------------------------------------------------
       *
       * (VI) video payload
       *
       * ------------------------------------------------------------*/


      if(id == P_ID_MPEG) {

	payload_id = PACKAGE_VIDEO;

	// get pts time stamp:
	ac_memcpy(buf, &buffer[4], 6);
	pts = read_time_stamp(buf);

	//read full packet header
	ac_memcpy(buf, &buffer[20], 16);
	has_pts_dts=get_pts_dts(buf, &i_pts, &i_dts);

	//need frame/field encoding information
	zz=scan_pack_ext(buffer);
	if(zz>0) flag_field_encoded=zz;

	//need precise number of pics in this sequence
	pack_picture_ctr = scan_pack_pics(buffer);

	seq_picture_ctr += pack_picture_ctr;

	frame_based_lpts = (seq_picture_ctr-1);

	//need this pack for subtitle PTS information
	if(flag_rewrite_scr && has_pts_dts) flag_force=1;

	// only process packets with pts/dts time stamp, since
	// they (all?) have a sequence start code

	if(has_pts_dts) {

	  if (ipipe->verbose & TC_STATS)
	    tc_log_msg(__FILE__, "(pid=%d) PTS-DTS detected in packet [%06d]",
		       getpid(), j);

	  // default first(=0) unit ?
	  if(k==0) {

	    --unit_seek;
	    flag_sync_reset=1;

	    if(ipipe->verbose & TC_DEBUG)
	      tc_log_msg(__FILE__, "(pid=%d) MPEG sequence start code in packet %06d for PU [0]",
			 getpid(), j);

	    k++;
	  }


	  if(pts<ref_pts) {

	    --unit_seek;
	    flag_sync_reset=1;

	    // past next unit - abort
	    // or process all following units?

	    if(unit_seek<0 && flag_loop_all==0) flag_eos=1;

	    //experimental: try to resync
	    //flag_avsync = 1;

	    if(ipipe->verbose & TC_DEBUG)
	      tc_log_msg(__FILE__, "(pid=%d) PTS reset (%.3f->%.3f) in packet %06d for PU [%d]",
			 getpid(), ref_pts, pts, j, k);

	    k++;
	  }


	  // only decrement sequence counter in right unit, i.e.
	  // unit_seek=0;
	  if(!unit_seek) --seq_seek;

	  // flush all sequences until the end of the unit
	  // or end of stream
	  // or resync_seq2 is reached

	  if(seq_seek==0) {

	    //re read packet header
	    ac_memcpy(buf, &buffer[20], 16);
	    get_pts_dts(buf, &i_pts, &i_dts);

	    resync_pts = (double) i_pts / 90000;

	    if(!flag_flush) {

	      // need to dump requested seq_dump sequences
	      seq_seek=seq_dump;
	      flag_flush = 1;

	      flag_avsync = 1;

	      // may be a useful info for the user
	      if(ipipe->verbose)
		tc_log_msg(__FILE__, "(pid=%d) processing PU [%d], on at PTS=%.4f sec",
			   getpid(), k-1, resync_pts);

	    } else {
				// finished, all sequences flushed, switch
			        // to audio packets post processing
	      flag_append_audio = 1;
	      flag_skip = 1;  //flush mode on, but do not write this one

	      // may be a useful info for the user
	      if(ipipe->verbose)
		tc_log_msg(__FILE__, "(pid=%d) processing PU [%d], off at PTS=%.4f sec",
			   getpid(), k-1, resync_pts);
	    }
	  }

	  //---------------------------------------------------
	  //gather information on the sequences before flushing
	  //---------------------------------------------------

	  if((demux_mode == TC_DEMUX_SEQ_FSYNC || demux_mode == TC_DEMUX_SEQ_FSYNC2) && flag_flush) {

	    ptr = seq_register(sequence_ctr);

	    zz=(flag_field_encoded==3)?(seq_picture_ctr-pack_picture_ctr):
		(seq_picture_ctr-pack_picture_ctr)/2;

	    if(sequence_ctr)  seq_update(ptr->prev, i_pts, zz, packet_ctr, flag_sync_active, hard_fps);

	    //init sequence information structure for current sequence

	    ptr->pts = i_pts;
	    ptr->dts = i_dts;
	    ptr->pics_first_packet = pack_picture_ctr;
	    ptr->sync_reset = flag_sync_reset;

	    //reset/update
	    seq_picture_ctr = 0;
	    packet_ctr = 0;
	    ++sequence_ctr;

	    //shift resync_pts, since sequence is dropped
	    if(flag_sync_active==0) resync_pts=(double) i_pts / 90000;

	  } //end TC_DEMUX_SEQ_FSYNC mode

	  //---------------------------------------------------
	  //print out sequence information for frame navigation
	  //---------------------------------------------------

	  if((demux_mode == TC_DEMUX_SEQ_LIST) && flag_flush) {

	    ptr = seq_register(sequence_ctr);

	    zz=(flag_field_encoded==3)?(seq_picture_ctr-pack_picture_ctr):
		(seq_picture_ctr-pack_picture_ctr)/2;

	    if(sequence_ctr)  seq_list(ptr->prev, i_pts, zz, packet_ctr, flag_sync_active);

	    //init sequence information structure for current sequence

	    ptr->pts = i_pts;
	    ptr->dts = i_dts;
	    ptr->pics_first_packet = pack_picture_ctr;
	    ptr->sync_reset = flag_sync_reset;
	    ptr->packet_ctr=j;

	    //reset/update
	    seq_picture_ctr = 0;
	    packet_ctr = 0;
	    ++sequence_ctr;

	    //shift resync_pts, since sequence is dropped
	    if(flag_sync_active==0) resync_pts=(double) i_pts / 90000;

	  } //end TC_DEMUX_SEQ_LIST mode

	  //reset sync pts, if no audio/substream has been found (0.6.0pre4)
	  if(flag_sync_active==0 && demux_audio) {
	    resync_pts=(double) i_pts / 90000;
	    if(ipipe->verbose & TC_DEBUG)
	      tc_log_msg(__FILE__, "new initial PTS=%f", resync_pts);
	  }

	}// PTS-DTS flag yes

	ref_pts=pts;

      }// MPEG video packet

      /* ------------------------------------------------------------
       *
       * (VII) evaluate scan results - flush packet
       *
       * ------------------------------------------------------------*/

    flush_packet:


      if(ipipe->verbose & TC_STATS)
	tc_log_msg(__FILE__, "INFO: j=%05d, i=%05d, skip=%d, flush=%d, force=%d, pay=%3d, sid=0x%02x, eos=%d",
		   j, i, flag_skip, flag_flush, flag_force, payload_id, id, flag_eos);

      //need to rewrite SCR pack header entry based on
      //frame_based_pts information for transcode:
      if(flag_rewrite_scr) scr_rewrite(&buffer[4], ((flag_field_encoded==3)?frame_based_lpts:frame_based_lpts/2));

      //flush here:

      switch(demux_mode) {

      case TC_DEMUX_DEBUG:
	if((flag_flush && !flag_skip && (payload_id & select))
	   || flag_force) scan_pack_payload(buffer, packet_size, j, ipipe->verbose);
	break;

      case TC_DEMUX_DEBUG_ALL:
	scan_pack_payload(buffer, packet_size, j, ipipe->verbose);
	break;

      case TC_DEMUX_SEQ_FSYNC:
      case TC_DEMUX_SEQ_FSYNC2:

	if((flag_flush && !flag_skip && (payload_id & select)) || flag_force) {

	  //count current sequence packets to be flushed
	  ++packet_ctr;

	  if(ipipe->verbose & TC_STATS)
	    tc_log_msg(__FILE__, "flushing packet (%d/%d)", sequence_ctr, j);

	  if(flush_buffer_write(ipipe->fd_out, buffer, packet_size) != packet_size) {
	    tc_log_perror(__FILE__, "write program stream packet");
	    exit(1);
	  }

	  //reset
	  flag_force=0;
	}

	break;

      case TC_DEMUX_SEQ_ADJUST:
      case TC_DEMUX_SEQ_ADJUST2:

	if((flag_flush && !flag_skip && (payload_id & select)) || flag_force) {

	  if(tc_pwrite(ipipe->fd_out, buffer, packet_size) != packet_size) {
	    tc_log_perror(__FILE__, "write program stream packet");
	    exit(1);
	  }

	  //reset
	  flag_force=0;

	  if(ipipe->verbose & TC_STATS)
	    tc_log_msg(__FILE__, "writing packet %d", j);

	} else {

	  ++i;
	  if(ipipe->verbose & TC_STATS)
	    tc_log_msg(__FILE__, "skipping packet %d", j);
	}

	break;


      case TC_DEMUX_SEQ_LIST:

	//count packs
	++packet_ctr;

	// nothing to do
	break;

      case TC_DEMUX_OFF:

	if(tc_pwrite(ipipe->fd_out, buffer, packet_size) != packet_size) {
	  tc_log_perror(__FILE__, "write program stream packet");
	  exit(1);
	}

	if(ipipe->verbose & TC_STATS)
	  tc_log_msg(__FILE__, "writing packet %d", j);

	break;
      }

      //aborting?
      if(flag_eos) break;

      //total packs (2k each) counter
      ++j;

    } // process next packet/block

    if(ipipe->verbose >= TC_DEBUG)
      tc_log_msg(__FILE__, "EOS - flushing packet buffer");

    //post processing

    if(demux_mode == TC_DEMUX_SEQ_FSYNC || demux_mode == TC_DEMUX_SEQ_FSYNC2) {
      seq_close();
      flush_buffer_close();
    }

    if(demux_mode == TC_DEMUX_SEQ_LIST) {

      ptr = seq_register(sequence_ctr);

      zz=(flag_field_encoded==3)?(seq_picture_ctr-pack_picture_ctr):
	  (seq_picture_ctr-pack_picture_ctr)/2;

      if(ptr!=NULL && ptr->id)  seq_list(ptr->prev, ref_pts, zz, packet_ctr, flag_sync_active);

      fflush(stdout);

      //summary
      seq_list_frames();

      seq_close();
      flush_buffer_close();

    }

    //summary
    if(ipipe->verbose & TC_DEBUG)
      tc_log_msg(__FILE__, "(pid=%d) %d/%d packets discarded", getpid(), i, j);

    if(buffer!=NULL) free(buffer);

    return;
}

