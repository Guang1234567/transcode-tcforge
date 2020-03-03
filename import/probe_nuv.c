/*
 *  probe_nuv.c
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
#include "ioaux.h"
#include "tc.h"
#include "libtc/libtc.h"
#include "nuv/nuppelvideo.h"

void probe_nuv(info_t *ipipe)
{

  int bytes;
  struct rtfileheader *rtf;

  if((rtf = tc_zalloc(sizeof(rtfileheader)))==NULL) {
    tc_log_error(__FILE__, "out of memory");
    ipipe->error=1;
    return;
  }

  // read min frame (NTSC)
  if((bytes=tc_pread(ipipe->fd_in, (uint8_t*) rtf, sizeof(rtfileheader)))
     != sizeof(rtfileheader)) {
    tc_log_error(__FILE__, "end of stream");
    ipipe->error=1;
    return;
  }


  ipipe->probe_info->width  = rtf->width;
  ipipe->probe_info->height = rtf->height;
  ipipe->probe_info->fps = rtf->fps;

  ipipe->probe_info->track[0].samplerate = 44100;
  ipipe->probe_info->track[0].chan = 2;
  ipipe->probe_info->track[0].bits = 16;
  ipipe->probe_info->track[0].format = 0x1;

  ipipe->probe_info->magic = TC_MAGIC_NUV;
  ipipe->probe_info->codec = TC_CODEC_NUV;

  if(ipipe->probe_info->track[0].chan>0) ipipe->probe_info->num_tracks=1;

  free(rtf);

  return;
}
