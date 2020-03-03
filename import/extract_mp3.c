/*
 *  extract_mp3.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Copyright (C) 1999-2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
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

#include "avilib/avilib.h"
#include "ioaux.h"
#include "tc.h"


#define BUFFER_SIZE 262144
static uint8_t buffer[BUFFER_SIZE];
static FILE *in_file, *out_file;

static int demux_track=0xc0;

static void ps_loop (void)
{
    static int mpeg1_skip_table[16] = {
	     1, 0xffff,      5,     10, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
    };

    uint8_t * buf;
    uint8_t * end;
    uint8_t * tmp1=NULL;
    uint8_t * tmp2=NULL;
    int complain_loudly;

    complain_loudly = 1;
    buf = buffer;

    do {
      end = buf + fread (buf, 1, buffer + BUFFER_SIZE - buf, in_file);
      buf = buffer;

      //scan buffer
      while (buf + 4 <= end) {

	// check for valid start code
	if (buf[0] || buf[1] || (buf[2] != 0x01)) {
	  if (complain_loudly) {

	    tc_log_warn(__FILE__, "missing start code at %#lx",
			ftell (in_file) - (end - buf));
	    if ((buf[0] == 0) && (buf[1] == 0) && (buf[2] == 0))
	      tc_log_warn(__FILE__, "incorrect zero-byte padding detected - ignored");

	    complain_loudly = 0;
	  }
	  buf++;
	  continue;
	}// check for valid start code


	switch (buf[3]) {

	case 0xb9:	/* program end code */
	  return;

	case 0xba:	/* pack header */

	  /* skip */
	  if ((buf[4] & 0xc0) == 0x40)	        /* mpeg2 */
	    tmp1 = buf + 14 + (buf[13] & 7);
	  else if ((buf[4] & 0xf0) == 0x20)	/* mpeg1 */
	    tmp1 = buf + 12;
	  else if (buf + 5 > end)
	    goto copy;
	  else {
	    tc_log_error(__FILE__, "weird pack header");
	    import_exit(1);
	  }

	  if (tmp1 > end)
	    goto copy;
	  buf = tmp1;
	  break;

	  //MPEG audio

	case 0xc0:
	case 0xc1:
	case 0xc2:
	case 0xc3:
	case 0xc4:
	case 0xc5:
	case 0xc6:
	case 0xc7:
	case 0xc8:
	case 0xc9:
	case 0xca:
	case 0xcb:
	case 0xcc:
	case 0xcd:
	case 0xce:
	case 0xcf:
	case 0xd0:
	case 0xd1:
	case 0xd2:
	case 0xd3:
	case 0xd4:
	case 0xd5:
	case 0xd6:
	case 0xd7:
	case 0xd8:
	case 0xd9:
	case 0xda:
	case 0xdb:
	case 0xdc:
	case 0xdd:
	case 0xde:
	case 0xdf:

	  tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	  if (tmp2 > end)
	    goto copy;
	  if ((buf[6] & 0xc0) == 0x80)	/* mpeg2 */
	    tmp1 = buf + 9 + buf[8];
	  else {	/* mpeg1 */
	    for (tmp1 = buf + 6; *tmp1 == 0xff; tmp1++)
	      if (tmp1 == buf + 6 + 16) {
		tc_log_warn(__FILE__, "too much stuffing");
		buf = tmp2;
		break;
	      }
	    if ((*tmp1 & 0xc0) == 0x40)
	      tmp1 += 2;
	    tmp1 += mpeg1_skip_table [*tmp1 >> 4];
	  }

	  if((buf[3] & 0xff) == demux_track) {
	      if (tmp1 < tmp2) {
            TC_PIPE_WRITE(fileno(out_file), tmp1, tmp2-tmp1);
            /* yeah, I know that's ugly -- FR */
          }
	  }
	  buf = tmp2;

	  break;

	default:
	  if (buf[3] < 0xb9)
	    tc_log_warn(__FILE__, "broken stream - skipping data");

	  /* skip */
	  tmp1 = buf + 6 + (buf[4] << 8) + buf[5];
	  if (tmp1 > end)
	    goto copy;
	  buf = tmp1;
	  break;

	} //start code selection
      } //scan buffer

      if (buf < end) {
      copy:
	/* we only pass here for mpeg1 ps streams */
	memmove (buffer, buf, end - buf);
      }
      buf = buffer + (end - buf);

    } while (end == buffer + BUFFER_SIZE);
}

static int mp3scan(int infd, int outfd)
{

  int j=0, i=0, s=0;

  unsigned long k=0;


  char *buffer = tc_malloc (SIZE_PCM_FRAME);

  uint16_t sync_word = 0;

  // need to find syncbyte:

  if (!buffer) {
      tc_log_error(__FILE__, "cannot malloc memory");
      return 1;
  }

  for(;;) {

    if (tc_pread(infd, &buffer[s], 1) !=1) {
      //mp3 sync byte scan failed
      return(ERROR_INVALID_HEADER);
    }

    sync_word = (sync_word << 8) + (uint8_t) buffer[s];

    s = (s+1)%2;

    ++i;
    ++k;

    if(sync_word == 0xfffc || sync_word == 0xfffb || sync_word == 0xfffd) break;

    if(k>(1<<20)) {
      tc_log_error(__FILE__, "no MP3 sync byte found within 1024 kB of stream");
      free (buffer);
      return(1);
    }
  }

  i=i-2;

  if(verbose & TC_DEBUG)
    tc_log_msg(__FILE__, "found sync frame at offset %d (%d)", i, j);

  // dump the rest

  tc_pwrite(outfd, buffer, 2);
  tc_preadwrite(infd, outfd);

  free (buffer);
  return(1);
}

#define MAX_BUF 4096
char audio[MAX_BUF];

/* ------------------------------------------------------------
 *
 * mp3 extract thread
 *
 * magic: TC_MAGIC_VOB
 *        TC_MAGIC_AVI
 *        TC_MAGIC_RAW  <-- default
 *
 * ------------------------------------------------------------*/


void extract_mp3(info_t *ipipe)
{

    int error=0;

    avi_t *avifile;

    long frames, padding, n;
    off_t bytes;
    //off_t fpos;

    verbose = ipipe->verbose;

    switch(ipipe->magic) {

    case TC_MAGIC_VOB:

	in_file = fdopen(ipipe->fd_in, "r");
	out_file = fdopen(ipipe->fd_out, "w");

	demux_track = 0xc0 + ipipe->track;

	ps_loop();

	fclose(in_file);
	fclose(out_file);

	break;

   case TC_MAGIC_AVI:

      if(ipipe->stype == TC_STYPE_STDIN){
	tc_log_error(__FILE__, "invalid magic/stype - exit");
	error=1;
	break;
      }

      // scan file
      if (ipipe->nav_seek_file) {
	if(NULL == (avifile = AVI_open_indexfd(ipipe->fd_in,0,ipipe->nav_seek_file))) {
	  AVI_print_error("AVI open");
	  break;
	}
      } else {
	if(NULL == (avifile = AVI_open_fd(ipipe->fd_in,1))) {
	  AVI_print_error("AVI open");
	  break;
	}
      }

      //set selected for multi-audio AVI-files
      AVI_set_audio_track(avifile, ipipe->track);

      bytes = (off_t)AVI_audio_bytes(avifile);
      padding = bytes % MAX_BUF;
      frames = bytes / MAX_BUF;

      for (n=0; n<frames; ++n) {

	if(AVI_read_audio(avifile, audio, MAX_BUF)<0) {
	  error=1;
	  break;
	}

	if(tc_pwrite(ipipe->fd_out, audio, MAX_BUF)!= MAX_BUF) {
	  error=1;
	  break;
	}
      }

      if((bytes = AVI_read_audio(avifile, audio, padding)) < padding)
	error=1;

      if(tc_pwrite(ipipe->fd_out, audio, bytes)!= bytes) error=1;

      break;


    case TC_MAGIC_RAW:
    default:

	if(ipipe->magic == TC_MAGIC_UNKNOWN)
	    tc_log_warn(__FILE__, "no file type specified, assuming %s",
			filetype(TC_MAGIC_RAW));


	error=mp3scan(ipipe->fd_in, ipipe->fd_out);

     break;
    }

    import_exit(error);

}
