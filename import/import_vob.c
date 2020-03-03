/*
 *  import_vob.c
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

#define MOD_NAME    "import_vob.so"
#define MOD_VERSION "v0.6.1 (2006-05-02)"
#define MOD_CODEC   "(video) MPEG-2 | (audio) MPEG/AC3/PCM | (subtitle)"

#include "src/transcode.h"

#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

/*%*
 *%* DESCRIPTION 
 *%*   This module imports audio/video from VOB files. If you need direct
 *%*   DVD access, use import_dvd module.
 *%*
 *%* #BUILD-DEPENDS
 *%*
 *%* #DEPENDS
 *%*
 *%* PROCESSING
 *%*   import/demuxer
 *%*
 *%* MEDIA
 *%*   video, audio
 *%*
 *%* #INPUT
 *%*
 *%* OUTPUT
 *%*   YUV420P*, RGB24, PCM, AC3
 *%*
 *%* OPTION
 *%*   nodemux (flag)
 *%*     skip demuxing processing stage. This sometimes improves A/V sync.
 *%*/

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_VID | TC_CAP_RGB | TC_CAP_YUV | TC_CAP_PCM | TC_CAP_AC3;

#define MOD_PRE vob
#include "import_def.h"

#include "ac3scan.h"
#include "demuxer.h"
#include "clone.h"



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

static int codec, syncf=0;
static int pseudo_frame_size=0, real_frame_size=0, effective_frame_size=0;
static int ac3_bytes_to_go=0;
static FILE *fd;

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

#define CMD_BUF 256
MOD_open
{
  const char *tag = "";
  const char *logfile = "sync.log";
  /* command buffers */
  char seq_buf[CMD_BUF];
  char demux_buf[CMD_BUF];
  char input_buf[TC_BUF_MAX];
  char import_cmd_buf[TC_BUF_MAX];

  int off=0x80;

  /* common both to all audio and video pipelines */
  if(vob->ps_seq1 != 0 || vob->ps_seq2 != TC_FRAME_LAST) {
      tc_snprintf(seq_buf, sizeof(seq_buf), "-S %d,%d-%d",
                  vob->ps_unit, vob->ps_seq1, vob->ps_seq2);
  } else {
      strlcpy(seq_buf, "-S 0", 256);
  }

  if(param->flag == TC_AUDIO) {
    /* common part */
    if(tc_snprintf(input_buf, sizeof(input_buf), 
                   "%s -i \"%s\" -t vob -d %d -S %d",
                   TCCAT_EXE, vob->audio_in_file, vob->verbose, vob->vob_offset) < 0) {
      tc_log_perror(MOD_NAME, "command buffer overflow (input)");
      return(TC_IMPORT_ERROR);
    }

    if(vob->demuxer == TC_DEMUX_OFF
     || (vob->im_a_string && optstr_lookup(vob->im_a_string, "nodemux"))) {
      demux_buf[0] = '\0';
    } else { /* build tcdemux part of pipeline */
      const char *codec = "raw";
      /* select demuxer codec. Ugh. */
      if (vob->im_a_codec == TC_CODEC_AC3) {
        codec = "ac3";
      } else { /* vob->im_a_codec == CODEC_PCM */
        if (vob->a_codec_flag == TC_CODEC_AC3) {
          codec = "ac3";
        } else if (vob->a_codec_flag == TC_CODEC_MP3
                || vob->a_codec_flag == TC_CODEC_MP2) {
          codec = "mp3";
        } else if (vob->a_codec_flag == TC_CODEC_PCM
                || vob->a_codec_flag == TC_CODEC_LPCM) {
          codec = "pcm";
        }
      }
      if(tc_snprintf(demux_buf, sizeof(demux_buf),
                    "| %s -M %d -a %d -x %s %s -d %d",
                    TCDEMUX_EXE,
                    vob->demuxer, vob->a_track, codec, seq_buf,
                    vob->verbose) < 0) {
        tc_log_perror(MOD_NAME, "command buffer overflow (demux)");
        return(TC_IMPORT_ERROR);
      }
    }

    codec = vob->im_a_codec;
    syncf = vob->sync;

    switch(codec) {
    case TC_CODEC_AC3:
      if (tc_snprintf(import_cmd_buf, sizeof(import_cmd_buf),
                      "%s %s"
                      " | %s -t vob -a %d -x ac3 -d %d"
                      " | %s -t raw -x ac3 -d %d",
                      input_buf, demux_buf,
                      TCEXTRACT_EXE, vob->verbose, vob->a_track, vob->verbose,
                      TCEXTRACT_EXE, vob->verbose) < 0) {
        tc_log_perror(MOD_NAME, "command buffer overflow");
        return(TC_IMPORT_ERROR);
      }
      if(verbose_flag & TC_DEBUG) tc_log_info(MOD_NAME, "AC3->AC3");
      break;
    
    case TC_CODEC_PCM:
      if(vob->a_codec_flag==TC_CODEC_AC3) {
        if(tc_snprintf(import_cmd_buf, sizeof(import_cmd_buf),
                       "%s %s"
                       " | %s -t vob -a %d -x ac3 -d %d"
                       " | %s -x ac3 -d %d -s %f,%f,%f -A %d",
                       input_buf, demux_buf,
                       TCEXTRACT_EXE, vob->a_track, vob->verbose,
                       TCDECODE_EXE, vob->verbose,
                       vob->ac3_gain[0], vob->ac3_gain[1], vob->ac3_gain[2],
                       vob->a52_mode) < 0) {
          tc_log_perror(MOD_NAME, "command buffer overflow");
	      return(TC_IMPORT_ERROR);
        }
        if(verbose_flag & TC_DEBUG) tag = "AC3->PCM : ";
      }
      
      if(vob->a_codec_flag==TC_CODEC_MP3) {
        if(tc_snprintf(import_cmd_buf, sizeof(import_cmd_buf),
                       "%s %s"
                       " | %s -t vob -a %d -x mp3 -d %d"
                       " | %s -x mp3 -d %d",
                       input_buf, demux_buf,
                       TCEXTRACT_EXE, vob->a_track, vob->verbose,
                       TCDECODE_EXE, vob->verbose) < 0) {
          tc_log_perror(MOD_NAME, "command buffer overflow");
          return(TC_IMPORT_ERROR);
        }
        if(verbose_flag & TC_DEBUG) tag = "MP3->PCM : ";
      }

      if(vob->a_codec_flag==TC_CODEC_MP2) {
        if(tc_snprintf(import_cmd_buf, sizeof(import_cmd_buf),
                       "%s %s"
                       " | %s -t vob -a %d -x mp2 -d %d"
                       " | %s -x mp2 -d %d",
                       input_buf, demux_buf,
                       TCEXTRACT_EXE, vob->a_track, vob->verbose,
                       TCDECODE_EXE, vob->verbose) < 0) {
          tc_log_perror(MOD_NAME, "command buffer overflow");
	      return(TC_IMPORT_ERROR);
        }
        if(verbose_flag & TC_DEBUG) tag = "MP2->PCM : ";
      }

      if(vob->a_codec_flag==TC_CODEC_PCM || vob->a_codec_flag==TC_CODEC_LPCM) {
        if(tc_snprintf(import_cmd_buf, sizeof(import_cmd_buf),
                       "%s %s"
                       " | %s -t vob -a %d -x pcm -d %d",
                       input_buf, demux_buf,
                       TCEXTRACT_EXE, vob->a_track, vob->verbose) < 0) {
          tc_log_perror(MOD_NAME, "command buffer overflow");
          return(TC_IMPORT_ERROR);
        }
        if(verbose_flag & TC_DEBUG) tag = "LPCM->PCM : ";
      }
      break;

    default:
      tc_log_warn(MOD_NAME, "invalid import codec request 0x%x", codec);
      return(TC_IMPORT_ERROR);
    }

    // print out
    if(verbose_flag) tc_log_info(MOD_NAME, "%s%s", tag, import_cmd_buf);

    // set to NULL if we handle read
    param->fd = NULL;

    // popen
    if((fd = popen(import_cmd_buf, "r"))== NULL) {
      tc_log_perror(MOD_NAME, "popen PCM stream");
      return(TC_IMPORT_ERROR);
    }
    return(TC_IMPORT_OK);
  }

  if(param->flag == TC_SUBEX) {

    tc_snprintf(demux_buf, sizeof(demux_buf), "-M %d", vob->demuxer);

    codec = vob->im_a_codec;
    syncf = vob->sync;

    if(tc_snprintf(import_cmd_buf, TC_BUF_MAX,
                   "%s -i \"%s\" -t vob -d %d -S %d"
                   " | %s -a %d -x ps1 %s %s -d %d"
                   " | %s -t vob -a 0x%x -x ps1 -d %d",
      TCCAT_EXE, vob->audio_in_file, vob->verbose, vob->vob_offset,
      TCDEMUX_EXE, vob->s_track, seq_buf, demux_buf, vob->verbose,
      TCEXTRACT_EXE, (vob->s_track+0x20), vob->verbose) < 0) {
        tc_log_perror(MOD_NAME, "command buffer overflow");
	    return(TC_IMPORT_ERROR);
    }

    if(verbose_flag & TC_DEBUG) tc_log_info(MOD_NAME, "subtitle extraction");

    // print out
    if(verbose_flag) tc_log_info(MOD_NAME, "%s", import_cmd_buf);

    // popen
    if((param->fd = popen(import_cmd_buf, "r"))== NULL) {
      tc_log_perror(MOD_NAME, "popen subtitle stream");
      return(TC_IMPORT_ERROR);
    }

    return(0);
  }

  if(param->flag == TC_VIDEO) {

      char requant_buf[256];

      if (vob->demuxer==TC_DEMUX_SEQ_FSYNC || vob->demuxer==TC_DEMUX_SEQ_FSYNC2) {

	if((logfile=clone_fifo())==NULL) {
	  tc_log_warn(MOD_NAME, "failed to create a temporary pipe");
	  return(TC_IMPORT_ERROR);
	}
	tc_snprintf(demux_buf, sizeof(demux_buf), "-M %d -f %f -P %s %s %s", vob->demuxer, vob->fps, logfile, ((vob->vob_chunk==0)? "": "-O"),
		((vob->hard_fps_flag==1)?"-H":""));
      } else tc_snprintf(demux_buf, sizeof(demux_buf), "-M %d", vob->demuxer);

      //determine subtream id for sync adjustment
      //default is off=0x80

      off=0x80;

      if(vob->a_codec_flag==TC_CODEC_PCM || vob->a_codec_flag==TC_CODEC_LPCM)
	off=0xA0;
      if(vob->a_codec_flag==TC_CODEC_MP3 || vob->a_codec_flag==TC_CODEC_MP2)
	off=0xC0;

      switch(vob->im_v_codec) {

      case TC_CODEC_RAW:

	memset(requant_buf, 0, sizeof (requant_buf));
	if (vob->m2v_requant > M2V_REQUANT_FACTOR) {
	  tc_snprintf (requant_buf, 256, " | tcrequant -d %d -f %f ", vob->verbose, vob->m2v_requant);
	}
	m2v_passthru=1;

	if (tc_snprintf(import_cmd_buf, TC_BUF_MAX,
		"%s -i \"%s\" -t vob -d %d -S %d"
		" | %s -s 0x%x -x mpeg2 %s %s -d %d"
		" | %s -t vob -a %d -x mpeg2 -d %d"
		"%s",
		TCCAT_EXE, vob->video_in_file, vob->verbose, vob->vob_offset,
        TCDEMUX_EXE,
		(vob->a_track+off), seq_buf, demux_buf, vob->verbose,
		TCEXTRACT_EXE, vob->v_track, vob->verbose,
		requant_buf) < 0) {
	  tc_log_perror(MOD_NAME, "command buffer overflow");
	  return(TC_IMPORT_ERROR);
	}
	break;
      case TC_CODEC_RGB24:

	if (tc_snprintf(import_cmd_buf, TC_BUF_MAX,
                    "%s -i \"%s\" -t vob -d %d -S %d"
                    " | %s -s 0x%x -x mpeg2 %s %s -d %d"
                    " | %s -t vob -a %d -x mpeg2 -d %d"
                    " | %s -x mpeg2 -d %d",
                    TCCAT_EXE, vob->video_in_file, vob->verbose, vob->vob_offset,
                    TCDEMUX_EXE, (vob->a_track+off), seq_buf, demux_buf, vob->verbose,
                    TCEXTRACT_EXE, vob->v_track, vob->verbose,
                    TCDECODE_EXE, vob->verbose) < 0) {
	  tc_log_perror(MOD_NAME, "command buffer overflow");
	  return(TC_IMPORT_ERROR);
	}

	break;

      case TC_CODEC_YUV420P:

	if (tc_snprintf(import_cmd_buf, TC_BUF_MAX,
                    "%s -i \"%s\" -t vob -d %d -S %d"
                    " | %s -s 0x%x -x mpeg2 %s %s -d %d"
                    " | %s -t vob -a %d -x mpeg2 -d %d"
                    " | %s -x mpeg2 -d %d -y yuv420p",
                    TCCAT_EXE, vob->video_in_file, vob->verbose, vob->vob_offset,
                    TCDEMUX_EXE, (vob->a_track+off), seq_buf, demux_buf, vob->verbose,
                    TCEXTRACT_EXE, vob->v_track, vob->verbose,
                    TCDECODE_EXE, vob->verbose) < 0) {
	  tc_log_perror(MOD_NAME, "command buffer overflow");
	  return(TC_IMPORT_ERROR);
	}

	break;

      default:

	tc_log_warn(MOD_NAME, "Don't know anything about Codec 0x%x", vob->im_v_codec);
	if (tc_snprintf(import_cmd_buf, TC_BUF_MAX, "cat /dev/null") < 0) {
	  tc_log_perror(MOD_NAME, "command buffer overflow");
	  return(TC_IMPORT_ERROR);
	}

      }

      // print out
      if(verbose_flag) tc_log_info(MOD_NAME, "%s", import_cmd_buf);

      param->fd = NULL;

      // popen
      if((param->fd = popen(import_cmd_buf, "r"))== NULL) {
	tc_log_perror(MOD_NAME, "popen RGB stream");
	return(TC_IMPORT_ERROR);
      }

      if (!m2v_passthru &&
	  (vob->demuxer==TC_DEMUX_SEQ_FSYNC || vob->demuxer==TC_DEMUX_SEQ_FSYNC2)) {

	if(clone_init(param->fd)<0) {
	  if(verbose_flag) tc_log_warn(MOD_NAME, "failed to init stream sync mode");
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

	if ( (tbuf.len = fread(tbuf.d, 1, tbuf.len, f))<0) return -1;

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

      return(0);
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

    if (!m2v_passthru && (vob->demuxer==TC_DEMUX_SEQ_FSYNC || vob->demuxer==TC_DEMUX_SEQ_FSYNC2)) {

      if(clone_frame(param->buffer, param->size)<0) {
	if(verbose_flag & TC_DEBUG) tc_log_warn(MOD_NAME, "end of stream - failed to sync video frame");
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
		 	 tbuf.d[0]&0xff, tbuf.d[1]&0xff, tbuf.d[2]&0xff, tbuf.d[3]&0xff);
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

    return(0);
  }

  if(param->flag == TC_SUBEX) return(0);

  if(param->flag == TC_AUDIO) {

    switch(codec) {

    case TC_CODEC_AC3:

      // determine frame size at the very beginning of the stream

      if(pseudo_frame_size==0) {

	if(ac3scan(fd, param->buffer, param->size, &ac_off, &ac_bytes, &pseudo_frame_size, &real_frame_size, verbose)!=0) return(TC_IMPORT_ERROR);

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
	  tc_log_info(MOD_NAME, "pseudo=%d, real=%d, frames=%d, effective=%d offset=%d",
		  ac_bytes, real_frame_size, num_frames, effective_frame_size, ac_off);

      // adjust
      ac_bytes=effective_frame_size;

#if 0
      if(syncf>0) {
	//dump an ac3 frame, instead of a pcm frame
	ac_bytes = real_frame_size-ac_off;
	param->size = real_frame_size;
	--syncf;
      }
#endif

      break;

    case TC_CODEC_PCM:

      //default:
      ac_off   = 0;
      ac_bytes = param->size;
      break;

    default:
      tc_log_warn(MOD_NAME, "invalid import codec request 0x%x",codec);
      return(TC_IMPORT_ERROR);

    }

    if (fread(param->buffer+ac_off, ac_bytes-ac_off, 1, fd) !=1)
      return(TC_IMPORT_ERROR);


    return(0);
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

    if(param->fd) {
	pclose(param->fd);
    }
    param->fd = NULL;

    if (f) {
      pclose (f);
    }
    f = NULL;

    syncf = 0;

    if(param->flag == TC_VIDEO) {

	//safe
      clone_close();

      return(0);
    }

    if(param->flag == TC_SUBEX) return(0);

    if(param->flag == TC_AUDIO) {

      if(fd) pclose(fd);
      fd=NULL;

      return(0);

    }
    return(TC_IMPORT_ERROR);
}


