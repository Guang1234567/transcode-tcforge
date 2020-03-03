/*
 *  import_dvd.c
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

#define MOD_NAME    "import_dvd.so"
#define MOD_VERSION "v0.4.1 (2007-07-15)"
#define MOD_CODEC   "(video) DVD | (audio) MPEG/AC3/PCM"

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB | TC_CAP_YUV | TC_CAP_AC3 | TC_CAP_PCM;

#define MOD_PRE dvd
#include "import_def.h"

#include "ac3scan.h"
#include "dvd_reader.h"
#include "demuxer.h"
#include "clone.h"

#include "libtcutil/optstr.h"

/*%*
 *%* DESCRIPTION 
 *%*   This module provides access to DVD content using libdvdread,
 *%*   directly from DVD device.
 *%*   (e.g. on-the-fly operation, no intermediate disk storage needed).
 *%*
 *%* BUILD-DEPENDS
 *%*   libdvdread >= 0.9.3
 *%*
 *%* DEPENDS
 *%*   libdvdread >= 0.9.3
 *%*
 *%* PROCESSING
 *%*   import/demuxer
 *%*
 *%* MEDIA
 *%*   video, audio, extra
 *%*
 *%* #INPUT
 *%*
 *%* OUTPUT
 *%*   YUV420P*, YUV422P, RGB24, PCM*
 *%*
 *%* OPTION
 *%*   delay (integer)
 *%*     set device access delay (seconds).
 *%*/

#define DVD_ACCESS_DELAY    3
/* seconds */

static char import_cmd_buf[TC_BUF_MAX];

//#define ACCESS_DELAY 3

typedef struct tbuf_t {
	int off;
	int len;
	char *d;
} tbuf_t;

// m2v passthru
static int can_read = 1;
static tbuf_t tbuf;
static int m2v_passthru=0;
static FILE *f; // video fd

static int query=0;

static int codec, syncf=0;
static int pseudo_frame_size=0, real_frame_size=0, effective_frame_size=0;
static int ac3_bytes_to_go=0;
static FILE *fd=NULL;

static int dvd_access_delay = DVD_ACCESS_DELAY;

// avoid to much messages for DVD chapter mode
int a_re_entry=0, v_re_entry=0;

#define TMP_BUF_SIZE 256
static char seq_buf[TMP_BUF_SIZE], dem_buf[TMP_BUF_SIZE],
            cha_buf[TMP_BUF_SIZE];

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
  const char *tag = "";
  char *logfile = "sync.log";
  long sret;

  int off = 0;

  (vob->ps_seq1 != 0 || vob->ps_seq2 != INT_MAX) ?
      tc_snprintf(seq_buf, TMP_BUF_SIZE, "-S %d,%d-%d", vob->ps_unit,
		  vob->ps_seq1, vob->ps_seq2) :
      tc_snprintf(seq_buf, TMP_BUF_SIZE, "-S %d", vob->ps_unit);

  //new chapter range feature
  (vob->dvd_chapter2 == -1) ?
      tc_snprintf(cha_buf, TMP_BUF_SIZE, "%d,%d,%d", vob->dvd_title,
		  vob->dvd_chapter1,  vob->dvd_angle) :
      tc_snprintf(cha_buf, TMP_BUF_SIZE, "%d,%d-%d,%d", vob->dvd_title,
		  vob->dvd_chapter1, vob->dvd_chapter2, vob->dvd_angle);

  if(param->flag == TC_AUDIO) {

    if(query==0) {
      // query DVD first:

      int max_titles, max_chapters, max_angles;

      if(dvd_init(vob->audio_in_file, &max_titles, verbose_flag)<0) {
	tc_log_warn(MOD_NAME, "failed to open DVD %s",
			vob->video_in_file);
	return(TC_IMPORT_ERROR);
      }

      if(dvd_query(vob->dvd_title, &max_chapters, &max_angles)<0) {
	tc_log_warn(MOD_NAME, "failed to read DVD information");
	dvd_close();
	return(TC_IMPORT_ERROR);
      } else {

	dvd_close();
	// transcode need this information
	vob->dvd_max_chapters = max_chapters;
      }
      query=1;
    }

    tc_snprintf(dem_buf, TMP_BUF_SIZE, "-M %d", vob->demuxer);

    codec = vob->im_a_codec;
    syncf = vob->sync;

    switch(codec) {

    case TC_CODEC_AC3:

      sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			 "%s -T %s -i \"%s\" -t dvd -d %d |"
			 " %s -a %d -x ac3 %s %s -d %d |"
			 " %s -t vob -x ac3 -a %d -d %d |"
			 " %s -t raw -x ac3 -d %d",
			 TCCAT_EXE, cha_buf, vob->audio_in_file, vob->verbose,
             TCDEMUX_EXE, vob->a_track, seq_buf, dem_buf, vob->verbose,
             TCEXTRACT_EXE, vob->a_track, vob->verbose,
             TCEXTRACT_EXE, vob->verbose);
      if (sret < 0)
        return(TC_IMPORT_ERROR);

      if(verbose_flag & TC_DEBUG && !a_re_entry)
        tc_log_info(MOD_NAME, "AC3->AC3");

      break;

    case TC_CODEC_PCM:

      if(vob->a_codec_flag==TC_CODEC_AC3) {

	sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			   "%s -T %s -i \"%s\" -t dvd -d %d |"
			   " %s -a %d -x ac3 %s %s -d %d |"
			   " %s -t vob -x ac3 -a %d -d %d |"
			   " %s -x ac3 -d %d -s %f,%f,%f -A %d",
			   TCCAT_EXE, cha_buf, vob->audio_in_file, vob->verbose,
               TCDEMUX_EXE, vob->a_track, seq_buf, dem_buf, vob->verbose,
               TCEXTRACT_EXE, vob->a_track, vob->verbose,
			   TCDECODE_EXE, vob->verbose, vob->ac3_gain[0], vob->ac3_gain[1],
			   vob->ac3_gain[2], vob->a52_mode);
	if (sret < 0)
	  return(TC_IMPORT_ERROR);

	if(verbose_flag & TC_DEBUG && !a_re_entry)
          tag = "AC3->PCM : ";
      }

      if(vob->a_codec_flag==TC_CODEC_MP3) {

        sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			   "%s -T %s -i \"%s\" -t dvd -d %d |"
			   " %s -a %d -x mp3 %s %s -d %d |"
			   " %s -t vob -x mp3 -a %d -d %d |"
			   " tcdecode -x mp3 -d %d",
			   TCCAT_EXE, cha_buf, vob->audio_in_file, vob->verbose,
               TCDEMUX_EXE, vob->a_track, seq_buf, dem_buf, vob->verbose,
               TCEXTRACT_EXE, vob->a_track, vob->verbose,
			   TCDECODE_EXE, vob->verbose);
	if (sret < 0)
	  return(TC_IMPORT_ERROR);

	if(verbose_flag & TC_DEBUG && !a_re_entry)
          tag = "MP3->PCM : ";
      }

      if(vob->a_codec_flag==TC_CODEC_MP2) {

	sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			   "%s -T %s -i \"%s\" -t dvd -d %d |"
			   " %s -a %d -x mp3 %s %s -d %d |"
			   " %s -t vob -x mp2 -a %d -d %d |"
			   " %s -x mp2 -d %d",
			   TCCAT_EXE, cha_buf, vob->audio_in_file, vob->verbose,
               TCDEMUX_EXE, vob->a_track, seq_buf, dem_buf, vob->verbose,
               TCEXTRACT_EXE, vob->a_track, vob->verbose,
			   TCDECODE_EXE, vob->verbose);
	if (sret < 0)
	  return(TC_IMPORT_ERROR);

	if(verbose_flag & TC_DEBUG && !a_re_entry)
          tag = "MP2->PCM : ";
      }

      if(vob->a_codec_flag==TC_CODEC_PCM || vob->a_codec_flag==TC_CODEC_LPCM) {

	sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			   "%s -T %s -i \"%s\" -t dvd -d %d |"
			   " %s -a %d -x pcm %s %s -d %d |"
			   " %s -t vob -x pcm -a %d -d %d",
			   TCCAT_EXE, cha_buf, vob->audio_in_file, vob->verbose,
               TCDEMUX_EXE, vob->a_track, seq_buf, dem_buf, vob->verbose,
               TCEXTRACT_EXE, vob->a_track, vob->verbose);
	if (sret < 0)
	  return(TC_IMPORT_ERROR);

	if(verbose_flag & TC_DEBUG && !a_re_entry)
          tag = "LPCM->PCM : ";
      }

      break;

    default:
      tc_log_warn(MOD_NAME, "invalid import codec request 0x%x", codec);
      return(TC_IMPORT_ERROR);

    }

    // print out
    if(verbose_flag && !a_re_entry)
      tc_log_info(MOD_NAME, "%s%s", tag, import_cmd_buf);

    // set to NULL if we handle read
    param->fd = NULL;

    // popen
    if((fd = popen(import_cmd_buf, "r"))== NULL) {
      tc_log_perror(MOD_NAME, "popen PCM stream");
      return(TC_IMPORT_ERROR);
    }

    a_re_entry=1;

    return(TC_IMPORT_OK);
  }

  if(param->flag == TC_SUBEX) {

    sret = snprintf(dem_buf, TMP_BUF_SIZE, "-M %d", vob->demuxer);

    codec = vob->im_a_codec;
    syncf = vob->sync;
    sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
		       "%s -T %s -i \"%s\" -t dvd -d %d -S %d |"
		       " %s -a %d -x ps1 %s %s -d %d |"
		       " %s -t vob -a 0x%x -x ps1 -d %d",
		       TCCAT_EXE, cha_buf, vob->audio_in_file, vob->verbose, vob->vob_offset,
               TCDEMUX_EXE, vob->s_track, seq_buf, dem_buf, vob->verbose,
               TCEXTRACT_EXE, (vob->s_track + 0x20), vob->verbose);
    if (sret < 0)
      return(TC_IMPORT_ERROR);

    if(verbose_flag & TC_DEBUG) tc_log_info(MOD_NAME, "subtitle extraction");

    // print out
    if(verbose_flag) tc_log_info(MOD_NAME, "%s", import_cmd_buf);

    // popen
    if((param->fd = popen(import_cmd_buf, "r"))== NULL) {
      tc_log_perror(MOD_NAME, "popen subtitle stream");
      return(TC_IMPORT_ERROR);
    }

    return(TC_IMPORT_OK);
  }

  if(param->flag == TC_VIDEO) {
    char requant_buf[TMP_BUF_SIZE];

    if(query==0) {
      // query DVD first:

      int max_titles, max_chapters, max_angles;

      if(dvd_init(vob->video_in_file, &max_titles, verbose_flag)<0) {
	tc_log_warn(MOD_NAME, "failed to open DVD %s",
                         vob->video_in_file);
	return(TC_IMPORT_ERROR);
      }

      if(dvd_query(vob->dvd_title, &max_chapters, &max_angles)<0) {
	tc_log_warn(MOD_NAME, "failed to read DVD information");
	dvd_close();
	return(TC_IMPORT_ERROR);
      } else {

	dvd_close();
	// transcode need this information
	vob->dvd_max_chapters = max_chapters;
      }
      query=1;
    }

    if (vob->demuxer==TC_DEMUX_SEQ_FSYNC || vob->demuxer==TC_DEMUX_SEQ_FSYNC2) {

      if((logfile=clone_fifo())==NULL) {
	tc_log_warn(MOD_NAME, "failed to create a temporary pipe");
	return(TC_IMPORT_ERROR);
      }
      tc_snprintf(dem_buf, TMP_BUF_SIZE, "-M %d -f %f -P %s",
		  vob->demuxer, vob->fps, logfile);
    } else
        tc_snprintf(dem_buf, TMP_BUF_SIZE, "-M %d", vob->demuxer);

    //determine subtream id for sync adjustment
    //default is off=0x80

    off=0x80;

    if(vob->a_codec_flag==TC_CODEC_PCM || vob->a_codec_flag==TC_CODEC_LPCM)
      off=0xA0;
    if(vob->a_codec_flag==TC_CODEC_MP3 || vob->a_codec_flag==TC_CODEC_MP2)
      off=0xC0;


    // construct command line

    switch(vob->im_v_codec) {

    case TC_CODEC_RAW:

      memset(requant_buf, 0, sizeof (requant_buf));
      if (vob->m2v_requant > M2V_REQUANT_FACTOR) {
	tc_snprintf (requant_buf, TMP_BUF_SIZE, " | tcrequant -d %d -f %f ",
		     vob->verbose, vob->m2v_requant);
      }
      m2v_passthru=1;

      sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			 "%s -T %s -i \"%s\" -t dvd -d %d"
			 " | %s -s 0x%x -x mpeg2 %s %s -d %d"
			 " | %s -t vob -a %d -x mpeg2 -d %d%s",
			 TCCAT_EXE, cha_buf, vob->video_in_file, vob->verbose,
             TCDEMUX_EXE, (vob->a_track + off), seq_buf, dem_buf, vob->verbose,
			 TCEXTRACT_EXE, vob->v_track, vob->verbose, requant_buf);
      if (sret < 0)
	  return(TC_IMPORT_ERROR);

      break;

    case TC_CODEC_RGB24:

      sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			 "%s -T %s -i \"%s\" -t dvd -d %d |"
			 " %s -s 0x%x -x mpeg2 %s %s -d %d |"
			 " %s -t vob -a %d -x mpeg2 -d %d |"
			 " %s -x mpeg2 -d %d",
			 TCCAT_EXE, cha_buf, vob->video_in_file, vob->verbose,
             TCDEMUX_EXE, (vob->a_track + off), seq_buf, dem_buf, vob->verbose,
             TCEXTRACT_EXE, vob->v_track, vob->verbose,
			 TCDECODE_EXE, vob->verbose);
      if (sret < 0)
	return(TC_IMPORT_ERROR);

      break;

    case TC_CODEC_YUV420P:

      sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			 "%s -T %s -i \"%s\" -t dvd -d %d |"
			 " %s -s 0x%x -x mpeg2 %s %s -d %d |"
			 " %s -t vob -a %d -x mpeg2 -d %d |"
			 " %s -x mpeg2 -d %d -y yuv420p",
			 TCCAT_EXE, cha_buf, vob->video_in_file, vob->verbose,
             TCDEMUX_EXE, (vob->a_track + off), seq_buf, dem_buf, vob->verbose,
             TCEXTRACT_EXE, vob->v_track, vob->verbose,
			 TCDECODE_EXE, vob->verbose);
      if (sret < 0)
	return(TC_IMPORT_ERROR);

      break;
    }


    // print out
    if(verbose_flag && !v_re_entry)
      tc_log_info(MOD_NAME, "%s", import_cmd_buf);

    param->fd = NULL;

    if (vob->im_v_string != NULL) {
        optstr_get(vob->im_v_string, "delay", "%i", &dvd_access_delay);
        if (dvd_access_delay < 0) {
            tc_log_error(MOD_NAME, "invalid value for DVD access delay,"
                                   "reset to defaults");
            dvd_access_delay = DVD_ACCESS_DELAY;
        }
    }
 
    if (dvd_access_delay > 0) {
      if(verbose_flag && !v_re_entry)
        tc_log_info(MOD_NAME, "delaying DVD access by %d second%s",
		            dvd_access_delay, (dvd_access_delay > 1) ?"s" :"");

      while (dvd_access_delay--) {
    	if (verbose_flag)
            tc_log_info(MOD_NAME, "waiting...");
	    fflush(stdout);
    	sleep(1);
      }
    }

    // popen
    if((param->fd = popen(import_cmd_buf, "r"))== NULL) {
      tc_log_perror(MOD_NAME, "popen RGB stream");
      return(TC_IMPORT_ERROR);
    }

    if (!m2v_passthru &&
	(vob->demuxer==TC_DEMUX_SEQ_FSYNC || vob->demuxer==TC_DEMUX_SEQ_FSYNC2)) {

      if(clone_init(param->fd)<0) {
	tc_log_warn(MOD_NAME, "failed to init stream sync mode");
	return(TC_IMPORT_ERROR);
      } else param->fd = NULL;
    }

    // we handle the read;
    if (m2v_passthru) {
      f = param->fd;
      param->fd = NULL;

      tbuf.d = tc_malloc (SIZE_RGB_FRAME);
      tbuf.len = SIZE_RGB_FRAME;
      tbuf.off = 0;

      if ((tbuf.len = fread(tbuf.d, 1, tbuf.len, f)) < 0)
        return(TC_IMPORT_ERROR);

      // find a sync word
      while (tbuf.off+4<tbuf.len) {
	if (tbuf.d[tbuf.off+0]==0x0 && tbuf.d[tbuf.off+1]==0x0 &&
	    tbuf.d[tbuf.off+2]==0x1 &&
	    (unsigned char)tbuf.d[tbuf.off+3]==0xb3) break;
	else tbuf.off++;
      }
      if (tbuf.off+4>=tbuf.len)  {
	tc_log_warn(MOD_NAME, "Internal Error. No sync word");
	return (TC_IMPORT_ERROR);
      }

    }

    v_re_entry=1;

    return(TC_IMPORT_OK);
  }

  return(TC_IMPORT_ERROR);

}

/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/


MOD_decode
{

  int ac_bytes=0, ac_off=0;
  int num_frames;

  if(param->flag == TC_VIDEO) {

    if (!m2v_passthru &&
          (vob->demuxer==TC_DEMUX_SEQ_FSYNC ||
           vob->demuxer==TC_DEMUX_SEQ_FSYNC2)) {

      if(clone_frame(param->buffer, param->size)<0) {
	if(verbose_flag & TC_DEBUG)
          tc_log_warn(MOD_NAME, "end of stream - failed to sync video frame");
	return(TC_IMPORT_ERROR);
      }
    }

    // ---------------------------------------------------
    // This code splits the MPEG2 elementary stream
    // into packets. It sets the type of the packet
    // as an frame attribute.
    // I frames (== Key frames) are not only I frames,
    // they also carry the sequence headers in the packet.
    // ---------------------------------------------------

    if (m2v_passthru) {
      int ID, start_seq, start_pic, pic_type;

      ID = tbuf.d[tbuf.off+3]&0xff;

      switch (ID) {
	case 0xb3: // sequence
	  start_seq = tbuf.off;

	  // look for pic header
	  while (tbuf.off+6<tbuf.len) {

	    if (tbuf.d[tbuf.off+0]==0x0 && tbuf.d[tbuf.off+1]==0x0 &&
		tbuf.d[tbuf.off+2]==0x1 && tbuf.d[tbuf.off+3]==0x0 &&
		((tbuf.d[tbuf.off+5]>>3)&0x7)>1 &&
		((tbuf.d[tbuf.off+5]>>3)&0x7)<4) {
	      if (verbose & TC_DEBUG)
                tc_log_info(MOD_NAME, "Completed a sequence + I frame from %d -> %d",
		        start_seq, tbuf.off);

	      param->attributes |= TC_FRAME_IS_KEYFRAME;
	      param->size = tbuf.off-start_seq;

	      // spit frame out
	      ac_memcpy(param->buffer, tbuf.d+start_seq, param->size);
	      memmove(tbuf.d, tbuf.d+param->size, tbuf.len-param->size);
	      tbuf.off = 0;
	      tbuf.len -= param->size;

	      if (verbose & TC_DEBUG)
                  tc_log_info(MOD_NAME, "%02x %02x %02x %02x",
                          tbuf.d[0]&0xff, tbuf.d[1]&0xff,
                          tbuf.d[2]&0xff, tbuf.d[3]&0xff);

	      return TC_IMPORT_OK;
	    }
	    else tbuf.off++;
	  }

	  // not enough data.
	  if (tbuf.off+6 >= tbuf.len) {

	    if (verbose & TC_DEBUG) tc_log_info(MOD_NAME, "Fetching in Sequence");
	    memmove (tbuf.d, tbuf.d+start_seq, tbuf.len - start_seq);
	    tbuf.len -= start_seq;
	    tbuf.off = 0;

	    if (can_read>0) {
	      can_read = fread (tbuf.d+tbuf.len, SIZE_RGB_FRAME-tbuf.len, 1, f);
	      tbuf.len += (SIZE_RGB_FRAME-tbuf.len);
	    } else {
		tc_log_info(MOD_NAME, "No 1 Read %d", can_read);
	      /* XXX: Flush buffers */
	      return TC_IMPORT_ERROR;
	    }
	  }
	  break;

	case 0x00: // pic header

	  start_pic = tbuf.off;
	  pic_type = (tbuf.d[start_pic+5] >> 3) & 0x7;
	  tbuf.off++;

	  while (tbuf.off+6<tbuf.len) {
	    if (tbuf.d[tbuf.off+0]==0x0 && tbuf.d[tbuf.off+1]==0x0 &&
		tbuf.d[tbuf.off+2]==0x1 &&
		(unsigned char)tbuf.d[tbuf.off+3]==0xb3) {
	      if (verbose & TC_DEBUG)
                  tc_log_info(MOD_NAME, "found a last P or B frame %d -> %d",
		          start_pic, tbuf.off);

	      param->size = tbuf.off - start_pic;

	      ac_memcpy(param->buffer, tbuf.d+start_pic, param->size);
	      memmove(tbuf.d, tbuf.d+param->size, tbuf.len-param->size);
	      tbuf.off = 0;
	      tbuf.len -= param->size;

	      return TC_IMPORT_OK;

	    } else if // P or B frame
	       (tbuf.d[tbuf.off+0]==0x0 && tbuf.d[tbuf.off+1]==0x0 &&
		tbuf.d[tbuf.off+2]==0x1 && tbuf.d[tbuf.off+3]==0x0 &&
		((tbuf.d[tbuf.off+5]>>3)&0x7)>1 &&
		((tbuf.d[tbuf.off+5]>>3)&0x7)<4) {
		 if (verbose & TC_DEBUG)
                     tc_log_info(MOD_NAME, "found a P or B frame from %d -> %d",
		             start_pic, tbuf.off);

		 param->size = tbuf.off - start_pic;

		 ac_memcpy(param->buffer, tbuf.d+start_pic, param->size);
		 memmove(tbuf.d, tbuf.d+param->size, tbuf.len-param->size);
		 tbuf.off = 0;
		 tbuf.len -= param->size;

		 return TC_IMPORT_OK;

	       } else tbuf.off++;

	    // not enough data.
	    if (tbuf.off+6 >= tbuf.len) {

	      memmove (tbuf.d, tbuf.d+start_pic, tbuf.len - start_pic);
	      tbuf.len -= start_pic;
	      tbuf.off = 0;

	      if (can_read>0) {
		can_read = fread (tbuf.d+tbuf.len, SIZE_RGB_FRAME-tbuf.len, 1, f);
		tbuf.len += (SIZE_RGB_FRAME-tbuf.len);
	      } else {
		tc_log_info(MOD_NAME, "No 1 Read %d", can_read);
		/* XXX: Flush buffers */
		return TC_IMPORT_ERROR;
	      }
	    }
	  }
	  break;
	default:
	  // should not get here
	  tc_log_warn(MOD_NAME, "Default case");
	  tbuf.off++;
	  break;
      }

    }

    return(TC_IMPORT_OK);
  }

  if (param->flag == TC_SUBEX) return(TC_IMPORT_OK);

  if(param->flag == TC_AUDIO) {

    switch(codec) {

    case TC_CODEC_AC3:

      // determine frame size at the very beginning of the stream

      if(pseudo_frame_size==0) {

	if(ac3scan(fd, param->buffer, param->size, &ac_off,
                   &ac_bytes, &pseudo_frame_size, &real_frame_size, verbose)!=0)
          return(TC_IMPORT_ERROR);

      } else {
	ac_off = 0;
	ac_bytes = pseudo_frame_size;
      }

      // switch to entire frames:
      // bytes_to_go is the difference between requested bytes and
      // delivered bytes
      //
      // pseudo_frame_size = average bytes per audio frame
      // real_frame_size = real AC3 frame size in bytes

      num_frames = (ac_bytes + ac3_bytes_to_go) / real_frame_size;

      effective_frame_size = num_frames * real_frame_size;
      ac3_bytes_to_go = ac_bytes + ac3_bytes_to_go - effective_frame_size;

      // return effective_frame_size as physical size of audio data
      param->size = effective_frame_size;

      if(verbose_flag & TC_STATS)
        tc_log_info(MOD_NAME, "pseudo=%d, real=%d, frames=%d, effective=%d",
                ac_bytes, real_frame_size, num_frames,
                effective_frame_size);

      // adjust
      ac_bytes=effective_frame_size;


      if(syncf>0) {
	//dump an ac3 frame, instead of a pcm frame
	ac_bytes = real_frame_size-ac_off;
	param->size = real_frame_size;
	--syncf;
      }

      break;

    case TC_CODEC_PCM:

      ac_off   = 0;
      ac_bytes = param->size;
      break;

    default:
      tc_log_warn(MOD_NAME, "invalid import codec request 0x%x", codec);
      return(TC_IMPORT_ERROR);

    }

    if (fread(param->buffer+ac_off, ac_bytes-ac_off, 1, fd) !=1)
      return(TC_IMPORT_ERROR);

    return(TC_IMPORT_OK);
  }

  return(TC_IMPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
    if(param->fd != NULL) pclose(param->fd); param->fd = NULL;
    if (f) pclose (f); f=NULL;

    if(param->flag == TC_VIDEO) {

	//safe
	clone_close();

	return(TC_IMPORT_OK);
    }

    if(param->flag == TC_AUDIO) {

      if(fd) pclose(fd);
      fd=NULL;

      return(TC_IMPORT_OK);

    }

    return(TC_IMPORT_ERROR);
}
