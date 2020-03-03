/*
 *  probe_mov.c
 *
 *  Copyright (C) Thomas Oestreich - Januray 2002
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
#include "ioaux.h"
#include "tc.h"
#include "libtc/libtc.h"
#include "libtc/ratiocodes.h"

#ifdef HAVE_LIBQUICKTIME

extern int binary_dump;

#include <quicktime.h>

void probe_mov(info_t *ipipe)
{

  quicktime_t *qt_file=NULL;
  char *codec=NULL;

  int j, tracks;

  /* open movie for video probe */
  if(qt_file==NULL)
    if(NULL == (qt_file = quicktime_open((char *)ipipe->name,1,0))){
      tc_log_error(__FILE__,"can't open quicktime!");
      ipipe->error=1;
      return;
    }

  // extract audio parameters
  tracks=quicktime_audio_tracks(qt_file);

  if(tracks>TC_MAX_AUD_TRACKS) {
    tc_log_warn(__FILE__, "only %d of %d audio tracks scanned",
                TC_MAX_AUD_TRACKS, tracks);
    tracks=TC_MAX_AUD_TRACKS;
  }

  for(j=0; j<tracks; ++j) {

    ipipe->probe_info->track[j].samplerate = quicktime_sample_rate(qt_file, j);
    ipipe->probe_info->track[j].chan = quicktime_track_channels(qt_file, j);
    ipipe->probe_info->track[j].bits = quicktime_audio_bits(qt_file, j);

    codec  = quicktime_audio_compressor(qt_file, j);

    if(strcasecmp(codec,QUICKTIME_RAW)==0 || strcasecmp(codec,QUICKTIME_TWOS)==0)
      ipipe->probe_info->track[j].format = TC_CODEC_PCM;
    else
      /* XXX not right but works */
      ipipe->probe_info->track[j].format = TC_CODEC_PCM;

    if (! binary_dump)
    	tc_log_info(__FILE__, "audio codec=%s", codec);

    if(ipipe->probe_info->track[j].chan>0) ++ipipe->probe_info->num_tracks;
  }


  // read all video parameter from input file
  ipipe->probe_info->width  =  quicktime_video_width(qt_file, 0);
  ipipe->probe_info->height =  quicktime_video_height(qt_file, 0);
  ipipe->probe_info->fps = quicktime_frame_rate(qt_file, 0);

  ipipe->probe_info->frames = quicktime_video_length(qt_file, 0);

  codec  =  quicktime_video_compressor(qt_file, 0);

  //check for supported codecs

  if(codec!=NULL) {

    if(strlen(codec)==0) {
      ipipe->probe_info->codec=TC_CODEC_RGB24;
    } else {

      if(strcasecmp(codec,QUICKTIME_DV)==0)
	ipipe->probe_info->codec=TC_CODEC_DV;

      if(strcasecmp(codec,"dvsd")==0)
	ipipe->probe_info->codec=TC_CODEC_DV;

      if(strcasecmp(codec,"DIV3")==0)
	ipipe->probe_info->codec=TC_CODEC_DIVX3;

      if(strcasecmp(codec,"DIVX")==0)
	ipipe->probe_info->codec=TC_CODEC_DIVX4;

      if(strcasecmp(codec,"DX50")==0)
	ipipe->probe_info->codec=TC_CODEC_DIVX5;

      if(strcasecmp(codec,"MJPG")==0 || strcasecmp(codec,"JPEG")==0)
	ipipe->probe_info->codec=TC_CODEC_MJPEG;

      if(strcasecmp(codec,"YUV2")==0)
	ipipe->probe_info->codec=TC_CODEC_YUV2;

      if(strcasecmp(codec,"SVQ1")==0)
	ipipe->probe_info->codec=TC_CODEC_SVQ1;

      if(strcasecmp(codec,"SVQ3")==0)
	ipipe->probe_info->codec=TC_CODEC_SVQ3;
    }
  } else
    ipipe->probe_info->codec=TC_CODEC_UNKNOWN;

  if (! binary_dump)
  	tc_log_info(__FILE__, "video codec=%s", codec);
  ipipe->probe_info->magic=TC_MAGIC_MOV;
  tc_frc_code_from_value(&(ipipe->probe_info->frc),
                         ipipe->probe_info->fps);

  return;
}

#else   // HAVE_LIBQUICKTIME

void probe_mov(info_t *ipipe)
{
	tc_log_error(__FILE__, "no support for Quicktime compiled - exit.");
	ipipe->error=1;
	return;
}

#endif

