/*
 *  extract_avi.c
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
#include "libtc/ratiocodes.h"
#include "ioaux.h"
#include "avilib/avilib.h"
#include "tc.h"

/* ------------------------------------------------------------
 *
 * avi extract thread
 *
 * ------------------------------------------------------------*/

void extract_avi(info_t *ipipe)
{
  // seek to vob_offset?
  AVI_dump(ipipe->name, ipipe->select);
}

#define FORMAT_ULAW 0x07

void probe_avi(info_t *ipipe)
{
    avi_t *avifile=NULL;
    char *codec=NULL;
    int j, tracks;

    // scan file
    if (ipipe->nav_seek_file) {
	if(NULL == (avifile = AVI_open_indexfd(ipipe->fd_in,0,ipipe->nav_seek_file))) {
	    AVI_print_error("AVI open");
	    return;
	}
    } else {
	if(NULL == (avifile = AVI_open_fd(ipipe->fd_in,1))) {
	    AVI_print_error("AVI open");
	    return;
	}
    }

    ipipe->probe_info->frames = AVI_video_frames(avifile);

    ipipe->probe_info->width  =  AVI_video_width(avifile);
    ipipe->probe_info->height =  AVI_video_height(avifile);
    ipipe->probe_info->fps    =  AVI_frame_rate(avifile);

    tracks = AVI_audio_tracks(avifile);

    //FIXME: check for max tracks suported

    for(j=0; j<tracks; ++j) {

      AVI_set_audio_track(avifile, j);

      ipipe->probe_info->track[j].samplerate = AVI_audio_rate(avifile);
      ipipe->probe_info->track[j].chan = AVI_audio_channels(avifile);
      ipipe->probe_info->track[j].bits = AVI_audio_bits(avifile);
      ipipe->probe_info->track[j].format = AVI_audio_format(avifile);
      ipipe->probe_info->track[j].bitrate = AVI_audio_mp3rate(avifile);
      ipipe->probe_info->track[j].padrate = AVI_audio_padrate(avifile);

      ipipe->probe_info->track[j].tid=j;
      
      /*
       * this should probably go somewhere else
       * uLaw in avi seems to store the samplesize of the compressed data instead of the
       * that of the uncompressed data; uLaw is always 16 bit
       */
      if(ipipe->probe_info->track[j].format == FORMAT_ULAW)
        ipipe->probe_info->track[j].bits = 16;

      if(ipipe->probe_info->track[j].chan>0) ++ipipe->probe_info->num_tracks;
    }

    codec = AVI_video_compressor(avifile);

    //check for supported codecs

    if(codec!=NULL) {

      if(strlen(codec)==0) {
	ipipe->probe_info->codec=TC_CODEC_RGB24;
      } else {

    /* FIXME: switch to a table or, better, use tccodecs.c facilities */
	if(strcasecmp(codec,"dvsd")==0)
	  ipipe->probe_info->codec=TC_CODEC_DV;

	if(strcasecmp(codec,"UYVY")==0)
	  ipipe->probe_info->codec=TC_CODEC_UYVY;

	if(strcasecmp(codec,"DIV3")==0)
	  ipipe->probe_info->codec=TC_CODEC_DIVX3;

	if(strcasecmp(codec,"MP42")==0)
	  ipipe->probe_info->codec=TC_CODEC_MP42;

	if(strcasecmp(codec,"MP43")==0)
	  ipipe->probe_info->codec=TC_CODEC_MP43;

	if(strcasecmp(codec,"DIVX")==0)
	  ipipe->probe_info->codec=TC_CODEC_DIVX4;

	if(strcasecmp(codec,"DX50")==0)
	  ipipe->probe_info->codec=TC_CODEC_DIVX5;

	if(strcasecmp(codec,"XVID")==0)
	  ipipe->probe_info->codec=TC_CODEC_XVID;

	if(strcasecmp(codec,"MJPG")==0)
	  ipipe->probe_info->codec=TC_CODEC_MJPEG;

	if(strcasecmp(codec,"RV10")==0)
	  ipipe->probe_info->codec=TC_CODEC_RV10;

	if(strcasecmp(codec,"MPG1")==0)
	  ipipe->probe_info->codec=TC_CODEC_MPEG1VIDEO;

	if(strcasecmp(codec,"LZO1")==0)
	  ipipe->probe_info->codec=TC_CODEC_LZO1;

	if(strcasecmp(codec,"LZO2")==0)
	  ipipe->probe_info->codec=TC_CODEC_LZO2;

	if(strcasecmp(codec,"FPS1")==0)
	  ipipe->probe_info->codec=TC_CODEC_FRAPS;

	if(strcasecmp(codec,"ASV1")==0)
	  ipipe->probe_info->codec=TC_CODEC_ASV1;

	if(strcasecmp(codec,"ASV2")==0)
	  ipipe->probe_info->codec=TC_CODEC_ASV2;

	if(strcasecmp(codec,"FFV1")==0)
	  ipipe->probe_info->codec=TC_CODEC_FFV1;

	if(strcasecmp(codec,"H264")==0
        || strcasecmp(codec,"X264")==0
        || strcasecmp(codec,"avc1")==0)
	  ipipe->probe_info->codec=TC_CODEC_H264;

      }
    } else
      ipipe->probe_info->codec=TC_CODEC_UNKNOWN;

    ipipe->probe_info->magic=TC_MAGIC_AVI;

    tc_frc_code_from_value(&(ipipe->probe_info->frc),
                           ipipe->probe_info->fps);
}
