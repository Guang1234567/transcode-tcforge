/*
 *  import_lzo.c
 *
 *  Copyright (C) Thomas Oestreich - October 2002
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

#define MOD_NAME    "import_lzo.so"
#define MOD_VERSION "v0.1.0 (2005-10-16)"
#define MOD_CODEC   "(video) LZO"

#include "src/transcode.h"
#include "libtcext/tc_lzo.h"
#include "magic.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_PCM | TC_CAP_YUV | TC_CAP_RGB |
                             TC_CAP_AUD | TC_CAP_VID;

#define MOD_PRE lzo
#include "import_def.h"


static avi_t *avifile1=NULL;
static avi_t *avifile2=NULL;

static uint32_t video_codec;
static int audio_codec;
static int aframe_count=0, vframe_count=0;

#define BUFFER_SIZE SIZE_RGB_FRAME<<1

static int r;
static lzo_byte *out;
static lzo_byte *wrkmem;
static lzo_uint out_len;

static int done_seek=0;

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
  int width=0, height=0;
  double fps=0;
  char *codec=NULL;

  param->fd = NULL;

  if(param->flag == TC_AUDIO) return(TC_IMPORT_ERROR);

  if(param->flag == TC_VIDEO) {

    param->fd = NULL;

    if(avifile2==NULL) {
      if(vob->nav_seek_file) {
	if(NULL == (avifile2 = AVI_open_input_indexfile(vob->video_in_file,
                                                      0,vob->nav_seek_file))){
	  AVI_print_error("avi open error");
	  return(TC_IMPORT_ERROR);
	}
      } else {
	if(NULL == (avifile2 = AVI_open_input_file(vob->video_in_file,1))){
	  AVI_print_error("avi open error");
	  return(TC_IMPORT_ERROR);
	}
      }
    }

    // vob->offset contains the last keyframe
    if (!done_seek && vob->vob_offset>0) {
	AVI_set_video_position(avifile2, vob->vob_offset);
	done_seek=1;
    }

    //read all video parameter from input file
    width  =  AVI_video_width(avifile2);
    height =  AVI_video_height(avifile2);

    fps    =  AVI_frame_rate(avifile2);
    codec  =  AVI_video_compressor(avifile2);

    if (strcmp(codec,"LZO1") == 0) {
      video_codec = TC_CODEC_LZO1;
    } else if (strcmp(codec,"LZO2") == 0) {
      video_codec = TC_CODEC_LZO2;
    } else {
      tc_log_warn(MOD_NAME, "Unsupported video codec %s", codec);
      return(TC_IMPORT_ERROR);
    }

    tc_log_info(MOD_NAME, "codec=%s, fps=%6.3f, width=%d, height=%d",
		codec, fps, width, height);

    /*
     * Step 1: initialize the LZO library
     */

    if (lzo_init() != LZO_E_OK) {
      tc_log_warn(MOD_NAME, "lzo_init() failed");
      return(TC_IMPORT_ERROR);
    }

    wrkmem = (lzo_bytep) lzo_malloc(LZO1X_1_MEM_COMPRESS);
    out = (lzo_bytep) lzo_malloc(BUFFER_SIZE);

    if (wrkmem == NULL || out == NULL) {
      tc_log_warn(MOD_NAME, "out of memory");
      return(TC_IMPORT_ERROR);
    }

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

  int key;
  lzo_uint size;
  long bytes_read=0;

  if(param->flag == TC_VIDEO) {
    // If we are using tccat, then do nothing here
    if (param->fd != NULL) {
      return(TC_IMPORT_OK);
    }

    out_len = AVI_read_frame(avifile2, out, &key);

    if(verbose & TC_STATS && key)
      tc_log_info(MOD_NAME, "keyframe %d", vframe_count);

    if(out_len<=0) {
      if(verbose & TC_DEBUG) AVI_print_error("AVI read video frame");
      return(TC_IMPORT_ERROR);
    }

    if (video_codec == TC_CODEC_LZO1) {
      r = lzo1x_decompress(out, out_len, param->buffer, &size, wrkmem);
    } else {
      tc_lzo_header_t *h = (tc_lzo_header_t *)out;
      uint8_t *compdata = out + sizeof(*h);
      int compsize = out_len - sizeof(*h);
      if (h->magic != video_codec) {
          tc_log_warn(MOD_NAME, "frame with invalid magic 0x%08X", h->magic);
	      return (TC_IMPORT_ERROR);
      }
      if (h->flags & TC_LZO_NOT_COMPRESSIBLE) {
          ac_memcpy(param->buffer, compdata, compsize);
	      size = compsize;
	      r = LZO_E_OK;
      } else {
	      r = lzo1x_decompress(compdata, compsize, param->buffer, &size, wrkmem);
      }
    }

    if (r == LZO_E_OK) {
      if(verbose & TC_DEBUG)
	      tc_log_info(MOD_NAME, "decompressed %lu bytes into %lu bytes",
		              (long) out_len, (long) param->size);
    } else {
      /* this should NEVER happen */
      tc_log_warn(MOD_NAME, "internal error - decompression failed: %d", r);
      return(TC_IMPORT_ERROR);
    }

    param->size = size;
    //transcode v.0.5.0-pre8 addition
    if(key) param->attributes |= TC_FRAME_IS_KEYFRAME;

    ++vframe_count;

    return(TC_IMPORT_OK);
  }

  if(param->flag == TC_AUDIO) {

    switch(audio_codec) {

    case TC_CODEC_RAW:

      bytes_read = AVI_audio_size(avifile1, aframe_count);

      if(bytes_read<=0) {
	if(verbose & TC_DEBUG) AVI_print_error("AVI audio read frame");
	return(TC_IMPORT_ERROR);
      }

      if(AVI_read_audio(avifile1, param->buffer, bytes_read) < 0) {
	AVI_print_error("AVI audio read frame");
	return(TC_IMPORT_ERROR);
      }

      param->size = bytes_read;
      ++aframe_count;

      break;

    default:

      bytes_read = AVI_read_audio(avifile1, param->buffer, param->size);

      if(bytes_read<0) {
	if(verbose & TC_DEBUG) AVI_print_error("AVI audio read frame");
	return(TC_IMPORT_ERROR);
      }

      if(bytes_read < param->size) param->size=bytes_read;
    }

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

  if(param->fd != NULL) pclose(param->fd);

  if(param->flag == TC_AUDIO) return(TC_IMPORT_ERROR);

  if(param->flag == TC_VIDEO) {

    lzo_free(wrkmem);
    lzo_free(out);

    if(avifile2!=NULL) {
      AVI_close(avifile2);
      avifile2=NULL;
    }
    done_seek = 0;
    return(TC_IMPORT_OK);
  }

  return(TC_IMPORT_ERROR);
}
