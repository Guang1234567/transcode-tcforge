/*
 *  import_dv.c
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

#define MOD_NAME    "import_dv.so"
#define MOD_VERSION "v0.3.1 (2003-10-14)"
#define MOD_CODEC   "(video) DV | (audio) PCM"

#include "src/transcode.h"
#include "libtc/libtc.h"
#include "libtcutil/xio.h"
#include "libtcvideo/tcvideo.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB | TC_CAP_YUV | TC_CAP_DV |
    TC_CAP_PCM | TC_CAP_VID | TC_CAP_YUV422;

#define MOD_PRE dv
#include "import_def.h"


char import_cmd_buf[TC_BUF_MAX];

static int frame_size=0;
static FILE *fd=NULL;
static uint8_t *tmpbuf = NULL;
static int yuv422_mode = 0, width, height;
static TCVHandle tcvhandle = 0;

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{

  char cat_buf[TC_BUF_MAX];
  char yuv_buf[16];
  long sret;

  if(param->flag == TC_VIDEO) {

    //directory mode?
    sret = tc_file_check(vob->video_in_file);
    if (sret < 0) {
        return(TC_IMPORT_ERROR);
    }
    if (sret == 1) {
        tc_snprintf(cat_buf, TC_BUF_MAX, "%s", TCCAT_EXE);
    } else {
        if(vob->im_v_string) {
            tc_snprintf(cat_buf, TC_BUF_MAX, "%s -x dv %s",
                        TCEXTRACT_EXE,
			            vob->im_v_string);
        } else {
            tc_snprintf(cat_buf, TC_BUF_MAX, "%s -x dv", TCEXTRACT_EXE);
        }
    }

    //yuy2 mode?
    (vob->dv_yuy2_mode) ?
        tc_snprintf(yuv_buf, 16, "-y yuv420p -Y") :
        tc_snprintf(yuv_buf, 16, "-y yuv420p");

    param->fd = NULL;
    yuv422_mode = 0;

    switch(vob->im_v_codec) {

    case TC_CODEC_RGB24:

      sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
                      "%s -i \"%s\" -d %d | %s -x dv -y rgb -d %d -Q %d",
                      cat_buf, vob->video_in_file, vob->verbose,
                      TCDECODE_EXE, vob->verbose, vob->quality);
      if (sret < 0)
          return(TC_IMPORT_ERROR);

      // popen
      if((param->fd = popen(import_cmd_buf, "r"))== NULL) {
	return(TC_IMPORT_ERROR);
      }

      break;

    case TC_CODEC_YUV420P:

      sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			 "%s -i \"%s\" -d %d | %s -x dv %s -d %d -Q %d",
			 cat_buf, vob->video_in_file, vob->verbose,
             TCDECODE_EXE, yuv_buf, vob->verbose, vob->quality);
      if (sret < 0)
	return(TC_IMPORT_ERROR);

      // for reading
      frame_size = (vob->im_v_width * vob->im_v_height * 3)/2;

      param->fd = NULL;

      // popen
      if((fd = popen(import_cmd_buf, "r"))== NULL) {
	return(TC_IMPORT_ERROR);
      }

      break;

    case TC_CODEC_YUV422P:

      sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			 "%s -i \"%s\" -d %d |"
			 " %s -x dv -y yuy2 -d %d -Q %d",
			 cat_buf, vob->video_in_file, vob->verbose,
             TCDECODE_EXE, vob->verbose, vob->quality);
      if (sret < 0)
        return(TC_IMPORT_ERROR);

      // for reading
      frame_size = vob->im_v_width * vob->im_v_height * 2;

      tmpbuf = tc_malloc(frame_size);
      if (!tmpbuf) {
    	tc_log_error(MOD_NAME, "out of memory");
	    return(TC_IMPORT_ERROR);
      }

      tcvhandle = tcv_init();
      if (!tcvhandle) {
	tc_log_error(MOD_NAME, "tcv_init() failed");
	return(TC_IMPORT_ERROR);
      }

      yuv422_mode = 1;
      width = vob->im_v_width;
      height = vob->im_v_height;

      param->fd = NULL;

      // popen
      if((fd = popen(import_cmd_buf, "r"))== NULL) {
	return(TC_IMPORT_ERROR);
      }

      break;


    case TC_CODEC_RAW:

      sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX, "%s -i \"%s\" -d %d",
			 cat_buf, vob->video_in_file, vob->verbose);
      if (sret < 0)
	return(TC_IMPORT_ERROR);

      // for reading
      frame_size = (vob->im_v_height==PAL_H) ?
          TC_FRAME_DV_PAL : TC_FRAME_DV_NTSC;

      param->fd = NULL;

      // popen
      if((fd = popen(import_cmd_buf, "r"))== NULL) {
	return(TC_IMPORT_ERROR);
      }

      break;


    default:
      tc_log_warn(MOD_NAME, "invalid import codec request 0x%x",
		      vob->im_v_codec);
      return(TC_IMPORT_ERROR);

    }

    // print out
    if(verbose_flag) tc_log_info(MOD_NAME, "%s", import_cmd_buf);

    return(TC_IMPORT_OK);
  }

  if(param->flag == TC_AUDIO) {

    //directory mode?
    if(tc_file_check(vob->audio_in_file)) {
        tc_snprintf(cat_buf, TC_BUF_MAX, "%s", TCCAT_EXE);
    } else {
        if(vob->im_a_string) {
            tc_snprintf(cat_buf, TC_BUF_MAX, "%s -x dv %s",
                        TCEXTRACT_EXE, vob->im_a_string);
        } else {
            tc_snprintf(cat_buf, TC_BUF_MAX, "%s -x dv", TCEXTRACT_EXE);
        }
    }

    sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
		       "%s -i \"%s\" -d %d | %s -x dv -y pcm -d %d",
               cat_buf, vob->audio_in_file, vob->verbose,
               TCDECODE_EXE, vob->verbose);
    if (sret < 0)
      return(TC_IMPORT_ERROR);

    // print out
    if(verbose_flag) tc_log_info(MOD_NAME, "%s", import_cmd_buf);

    param->fd = NULL;

    // popen
    if((param->fd = popen(import_cmd_buf, "r"))== NULL) {
	tc_log_perror(MOD_NAME, "popen PCM stream");
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

    if(param->flag == TC_AUDIO) return(TC_IMPORT_OK);

    // video and YUV only
    if(param->flag == TC_VIDEO && frame_size==0) return(TC_IMPORT_ERROR);

    // return true yuv frame size as physical size of video data
    param->size = frame_size;

    if (yuv422_mode) {
        if (fread(tmpbuf, frame_size, 1, fd) !=1)
            return(TC_IMPORT_ERROR);
	tcv_convert(tcvhandle, tmpbuf, param->buffer, width, height,
		    IMG_YUY2, IMG_YUV422P);
    } else {
        if (fread(param->buffer, frame_size, 1, fd) !=1)
            return(TC_IMPORT_ERROR);
    }

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

  if(param->flag == TC_AUDIO) return(TC_IMPORT_OK);

  if(param->flag == TC_VIDEO) {

    if(fd) pclose(fd);
    fd=NULL;

    if (tcvhandle)
      tcv_free(tcvhandle);
    tcvhandle=0;

    free(tmpbuf);
    tmpbuf=NULL;

    return(TC_IMPORT_OK);

  }

  return(TC_IMPORT_ERROR);
}

