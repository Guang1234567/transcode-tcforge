/*
 *  extract_pcm.c
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

#include <sys/types.h>
#include <sys/mman.h>

#include "ioaux.h"
#include "avilib/wavlib.h"
#include "tc.h"

#define MAX_BUF 4096
char audio[MAX_BUF];

#define BUFFER_SIZE 262144
static uint8_t buffer[BUFFER_SIZE];
static FILE *in_file, *out_file;

static unsigned int track_code;


static void pes_lpcm_loop (void)
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

    const unsigned *extract_order = 0;
    unsigned extract_size = 0;
    unsigned i;
    unsigned left_over;

#ifdef WORDS_BIGENDIAN
    static const unsigned lpcm_bebe16[4] = { 0,1, 2,3 };
    static const unsigned lpcm_bebe24[12] = { 0,1,8, 2,3,9 ,4,5,10, 6,7,11 };
#else
    static const unsigned lpcm_bele16[4] = { 1,0, 3,2 };
    static const unsigned lpcm_bele24[12] = { 8,1,0, 9,3,2 ,10,5,4, 11,7,6 };
#endif

    complain_loudly = 1;
    buf = buffer;
    left_over = 0;

    do {
      end = buf + fread (buf, 1, buffer + BUFFER_SIZE - buf, in_file);
      buf = buffer;

      //scan buffer
      while (buf + 4 <= end) {

	// check for valid start code
	if (buf[0] || buf[1] || (buf[2] != 0x01)) {
	  if (complain_loudly && (verbose & TC_DEBUG)) {
	    tc_log_warn(__FILE__, "missing start code at %#lx",
			ftell (in_file) - (end - buf));
	    if ((buf[0] == 0) && (buf[1] == 0) && (buf[2] == 0))
	      tc_log_warn(__FILE__, "incorrect zero-byte padding detected - ignored");
	    complain_loudly = 0;
	  }
	  buf++;
	  continue;
	}// check for valid start code

	if(verbose & TC_STATS)
	  tc_log_msg(__FILE__, "packet code 0x%x", buf[3]);

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


	case 0xbd:	/* private stream 1 */
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

	  if(verbose & TC_STATS)
	    tc_log_msg(__FILE__, "track code 0x%x", *tmp1);

	  if (*tmp1 == track_code) {

	    tmp1++;

	    /*
	     * Additional audio header consists of:
	     *   number of frames
	     *   offset to frame start (high byte)
	     *   offset to frame start (low byte)
	     *
	     * followed by LPCM header:
	     *   emphasis:1, mute:1, rvd:1, frame number:5
	     *   quantization:2, freq:2, rvd:1, channels:3
	     *   dynamic range control (0x80=off)
	     */

	    tmp1 += 3;

	    switch ((tmp1[1] >> 6) & 3) {
	    case 0: extract_size = 4;
#ifdef WORDS_BIGENDIAN
		    extract_order = lpcm_bebe16;
#else
		    extract_order = lpcm_bele16;
#endif
		    break;
            case 2: extract_size = 12;
#ifdef WORDS_BIGENDIAN
		    extract_order = lpcm_bebe24;
#else
		    extract_order = lpcm_bele24;
#endif
		    break;
            default: tc_log_error(__FILE__, "unsupported LPCM quantization");
		     import_exit (1);
            }

	    tmp1 += 3;

	    if (left_over) {
	      while (left_over < extract_size && tmp1 != tmp2)
		audio[left_over++] = *tmp1++;
	      if (left_over == extract_size) {
		for (i = 0; i < extract_size; i++)
		  fputc (audio[extract_order[i]], out_file);
		left_over = 0;
		}
	    }

	    while ((tmp2 - tmp1) >= extract_size) {
	      for (i = 0; i < extract_size; i++)
		fputc (tmp1[extract_order[i]], out_file);
	      tmp1 += extract_size;
	      }

	    while (tmp1 != tmp2)
	      audio[left_over++] = *tmp1++;
	  }

	  buf = tmp2;
	  break;

	default:
	  if (buf[3] < 0xb9) {
	    tc_log_error(__FILE__, "looks like a video stream, not program stream");
	    import_exit(1);
	  }

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


extern void import_exit(int ret);

/* ------------------------------------------------------------
 *
 * pcm extract thread
 *
 * magic: TC_MAGIC_AVI
 *        TC_MAGIC_RAW  <-- default
 *        TC_MAGIC_WAW
 *        TC_MAGIC_VOB
 *
 * ------------------------------------------------------------*/


void extract_pcm(info_t *ipipe)
{

  avi_t *avifile;

  unsigned long frames, bytes, padding, n;

  int error=0;

  WAV wav = NULL;


  /* ------------------------------------------------------------
   *
   * AVI
   *
   * ------------------------------------------------------------*/

  // AVI

  switch (ipipe->magic) {

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

    // get total audio size
   bytes=ipipe->frame_limit[1] - ipipe->frame_limit[0];
   if (ipipe->frame_limit[1] ==LONG_MAX)
   {
     bytes = AVI_audio_bytes(avifile);
   }
   AVI_set_audio_position(avifile,ipipe->frame_limit[0]);

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

  /* ------------------------------------------------------------
   *
   * WAV
   *
   * ------------------------------------------------------------*/

  // WAV

  case TC_MAGIC_WAV:

    wav = wav_fdopen(ipipe->fd_in, WAV_READ|WAV_PIPE, NULL);
    if (wav == NULL) {
      error=1;
      break;
    }

    do {
      bytes = wav_read_data(wav, audio, MAX_BUF);
      if(bytes != MAX_BUF) error=1;
      if(tc_pwrite(ipipe->fd_out, audio, bytes)!= bytes) error=1;
    } while(!error);

    wav_close(wav);

    break;

    /* ------------------------------------------------------------
     *
     * VOB
     *
     * ------------------------------------------------------------*/

    // VOB

  case TC_MAGIC_VOB:

      in_file = fdopen(ipipe->fd_in, "r");
      out_file = fdopen(ipipe->fd_out, "w");

      track_code = 0xA0 + ipipe->track;
      pes_lpcm_loop();

      fclose(in_file);
      fclose(out_file);

    break;


    /* ------------------------------------------------------------
     *
     * RAW
     *
     * ------------------------------------------------------------*/

    // RAW

  case TC_MAGIC_RAW:

  default:

      if(ipipe->magic == TC_MAGIC_UNKNOWN)
	  tc_log_warn(__FILE__, "no file type specified, assuming %s",
		      filetype(TC_MAGIC_RAW));

   	bytes=ipipe->frame_limit[1] - ipipe->frame_limit[0];
   	//skip the first ipipe->frame_limit[0] bytes
	if (ipipe->frame_limit[0]!=0)
		if (lseek(ipipe->fd_in,ipipe->frame_limit[0],SEEK_SET) !=0)
		{
			error=1;
			break;
		}
   	if (ipipe->frame_limit[1] ==LONG_MAX)
   	{
    		error=tc_preadwrite(ipipe->fd_in, ipipe->fd_out);
	}
	else
   	{
   		padding = bytes % MAX_BUF;
   		frames = bytes / MAX_BUF;
   		for (n=0; n<frames; ++n)
  		{
      			if(tc_pread(ipipe->fd_in, audio, MAX_BUF)!= MAX_BUF)
      			{
				error=1;
				break;
      			}
			if(tc_pwrite(ipipe->fd_out, audio, MAX_BUF)!= MAX_BUF)
			{
				error=1;
				break;
      			}
    		}
   		if (padding !=0)
		{
      			if(tc_pread(ipipe->fd_in, audio, padding)!= padding)
      			{
				error=1;
				break;
      			}
			if(tc_pwrite(ipipe->fd_out, audio, padding)!= padding)
			{
				error=1;
				break;
      			}
		}
	}

      break;
  }

  if(error) {
    tc_log_perror(__FILE__, "error while writing data");
  	import_exit(error);
  }
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
