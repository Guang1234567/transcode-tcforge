/*
 *  import_ac3.c
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

#define MOD_NAME    "import_ac3.so"
#define MOD_VERSION "v0.3.2 (2002-02-15)"
#define MOD_CODEC   "(audio) AC3"

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_PCM | TC_CAP_AC3;

#define MOD_PRE ac3
#include "import_def.h"

#include "ac3scan.h"


char import_cmd_buf[TC_BUF_MAX];

static FILE *fd;

static int codec, syncf=0;
static int pseudo_frame_size=0, real_frame_size=0, effective_frame_size=0;
static int ac3_bytes_to_go=0;


/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
    const char *tag = "";
    long sret;

    // audio only
    if(param->flag != TC_AUDIO) return(TC_IMPORT_ERROR);

    codec = vob->im_a_codec;
    syncf = vob->sync;

    switch(codec) {

    case TC_CODEC_AC3:

	// produce a clean sequence of AC3 frames
	sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
		"%s -a %d -i \"%s\" -x ac3 -d %d |"
		" %s -t raw -x ac3 -d %d",
        TCEXTRACT_EXE, vob->a_track, vob->audio_in_file, vob->verbose,
        TCEXTRACT_EXE, vob->verbose);
        if (sret < 0)
    	    return(TC_IMPORT_ERROR);

	if(verbose_flag) tc_log_info(MOD_NAME, "AC3->AC3");

	break;

    case TC_CODEC_PCM:

	if(vob->a_codec_flag==TC_CODEC_AC3) {

	    sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			"%s -a %d -i \"%s\" -x ac3 -d %d |"
			" %s -x ac3 -d %d -s %f,%f,%f -A %d",
            TCEXTRACT_EXE, vob->a_track, vob->audio_in_file, vob->verbose,
            TCDECODE_EXE, vob->verbose, vob->ac3_gain[0], vob->ac3_gain[1],
			vob->ac3_gain[2], vob->a52_mode);
            if (sret < 0)
    	        return(TC_IMPORT_ERROR);

	    if (verbose_flag)
            tag = "AC3->PCM : ";
	}

	break;

    default:
	tc_log_warn(MOD_NAME, "invalid import codec request 0x%x", codec);
	return(TC_IMPORT_ERROR);

    }

    // print out
    if(verbose_flag)
        tc_log_info(MOD_NAME, "%s%s", tag, import_cmd_buf);

    // set to NULL if we handle read
    param->fd = NULL;

    // popen
    if((fd = popen(import_cmd_buf, "r"))== NULL) {
	    tc_log_perror(MOD_NAME, "popen pcm stream");
    	return(TC_IMPORT_ERROR);
    }

    return(TC_IMPORT_OK);
}

/* ------------------------------------------------------------
 *
 * decode stream
 *
 * ------------------------------------------------------------*/

MOD_decode
{

  int ac_bytes=0, ac_off=0;
  int num_frames;

  // audio only
  if(param->flag != TC_AUDIO) return(TC_IMPORT_ERROR);

  switch(codec) {

  case TC_CODEC_AC3:

      // determine frame size at the very beginning of the stream

      if (pseudo_frame_size == 0) {

	  if (ac3scan(fd, param->buffer, param->size, &ac_off, &ac_bytes,
			&pseudo_frame_size, &real_frame_size, verbose) != 0)
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

      if (verbose_flag & TC_STATS)
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

    //default:
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

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
  if(param->fd != NULL) pclose(param->fd);

  return(TC_IMPORT_OK);
}
