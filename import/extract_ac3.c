/*
 *  extract_ac3.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Copyright (C) Aaron Holtzman <aholtzma@ess.engr.uvic.ca> - June 1999
 *
 *  Ideas and bitstream syntax info borrowed from code written
 *  by Nathan Laredo <laredo@gnu.org>
 *
 *  Multiple track support by Yuqing Deng <deng@tinker.chem.brown.edu>
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

#undef DDBUG
//#define DDBUG

#include "tccore/tcinfo.h"
#include "src/transcode.h"
#include "libtc/libtc.h"

#include <sys/mman.h>
#include <limits.h>

#include "ioaux.h"
#include "aux_pes.h"
#include "tc.h"


static unsigned int read_tc_time_stamp(const char *s)
{

  unsigned long i, j;
  unsigned long clock_ref=0, clock_ref_ext=0;

  if(s[0] & 0x40) {

    i = stream_read_int32(s);
    j = stream_read_int16(s+4);

    if(i & 0x40000000 || (i >> 28) == 2) {
      clock_ref  = ((i & 0x31000000) << 3);
      clock_ref |= ((i & 0x03fff800) << 4);
      clock_ref |= ((i & 0x000003ff) << 5);
      clock_ref |= ((j & 0xf800) >> 11);
      clock_ref_ext = (j >> 1) & 0x1ff;
    }
  }

  return ((unsigned int) (clock_ref * 300 + clock_ref_ext));
}


#define BUFFER_SIZE 262144
static uint8_t *buffer = NULL;
static FILE *in_file, *out_file;

static unsigned int track_code=0, vdr_work_around=0;

static int get_pts=0;

static subtitle_header_t subtitle_header;
static char *subtitle_header_str="SUBTITLE";

static void pes_ac3_loop (void)
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

    char pack_buf[16];

    unsigned int pack_lpts=0;
    double pack_rpts=0.0f, last_rpts=0.0f, offset_rpts=0.0f, abs_rpts=0.0f;
    double pack_sub_rpts=0.0f, abs_sub_rpts=0.0f;

    int discont=0;

    unsigned long i_pts, i_dts;

    complain_loudly = 1;
    buf = buffer;

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

	  //check for PTS


	case 0xe0:	/* video */

	  tmp2 = buf + 6 + (buf[4] << 8) + buf[5];

	  if (tmp2 > end)
	    goto copy;

	  if ((buf[6] & 0xc0) == 0x80) {
	    /* mpeg2 */
	    tmp1 = buf + 9 + buf[8];
	  } else {
	    /* mpeg1 */
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

	  // get pts time stamp:
	  ac_memcpy(pack_buf, &buf[6], 16);

	  if(get_pts_dts(pack_buf, &i_pts, &i_dts)) {
	    pack_rpts = (double) i_pts/90000.;

	    if(pack_rpts < last_rpts){ // pts resets when a new chapter begins
	      offset_rpts += last_rpts;
	      ++discont;
	    }

	    //default
	    last_rpts=pack_rpts;
	    abs_rpts=pack_rpts + offset_rpts;

	    //tc_log_msg(__FILE__, "PTS=%8.3f ABS=%8.3f", pack_rpts, abs_rpts);
	  }

	  buf = tmp2;
	  break;

	case 0xba:	/* pack header */

	  if(get_pts) {
	    ac_memcpy(pack_buf, &buf[4], 6);
	    pack_lpts = read_tc_time_stamp(pack_buf);
	  }

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

	  if(vdr_work_around) {
	    if (tmp1 < tmp2) {
            TC_PIPE_WRITE(fileno(out_file), tmp1, tmp2-tmp1);
            /* yeah, I know that's ugly -- FR */
        }
	  } else {

	    //subtitle

	    if (*tmp1 == track_code && track_code < 0x40) {

	      if (tmp1 < tmp2) {

		// get pts time stamp:
		  ac_memcpy(pack_buf, &buf[6], 16);

		  if(get_pts_dts(pack_buf, &i_pts, &i_dts)) {
		    pack_sub_rpts = (double) i_pts/90000.;

		    //i suppose there *canNOT* be 2 sub chunks from the
		    //same sub line over a chapter change
		    //let's add the video offset to the subs
		    abs_sub_rpts=pack_sub_rpts + offset_rpts;

		    //tc_log_msg(__FILE__, "sub PTS=%8.3f ABS=%8.3f", pack_rpts, abs_rpts);
		  }

		subtitle_header.lpts = pack_lpts;
		subtitle_header.rpts = abs_sub_rpts;
		subtitle_header.discont_ctr = discont;
		subtitle_header.header_version = TC_SUBTITLE_HDRMAGIC;
		subtitle_header.header_length = sizeof(subtitle_header_t);
		subtitle_header.payload_length=tmp2-tmp1;

		if(verbose & TC_STATS)
		  tc_log_msg(__FILE__, "subtitle=0x%x size=%4d lpts=%d rpts=%f rptsfromvid=%f",
			     track_code, subtitle_header.payload_length,
			     subtitle_header.lpts, subtitle_header.rpts,
			     abs_rpts);

		if(tc_pwrite(STDOUT_FILENO, (uint8_t*) subtitle_header_str, strlen(subtitle_header_str))<0) {
		    tc_log_error(__FILE__, "error writing subtitle: %s",
				 strerror(errno));
		    import_exit(1);
		}
		if(tc_pwrite(STDOUT_FILENO, (uint8_t*) &subtitle_header, sizeof(subtitle_header_t))<0) {
		    tc_log_error(__FILE__, "error writing subtitle: %s",
				 strerror(errno));
		    import_exit(1);
		}
		if(tc_pwrite(STDOUT_FILENO, tmp1, tmp2-tmp1)<0) {
		    tc_log_error(__FILE__, "error writing subtitle: %s",
				 strerror(errno));
		    import_exit(1);
		}
	      }
	    }

	    //ac3 package

	    if (*tmp1 == track_code && track_code >= 0x80) {
    		tmp1 += 4;
#if 0
	    	//test
		    if(0) {
    		    ac_memcpy(pack_buf, &buf[6], 16);
	    	    get_pts_dts(pack_buf, &i_pts, &i_dts);
		        tc_log_msg(__FILE__, "AC3 PTS=%f", (double) i_pts/90000.);
    		}
#endif
    		if (tmp1 < tmp2) {
                TC_PIPE_WRITE(fileno(out_file), tmp1, tmp2-tmp1);
                /* yeah, I know that's ugly -- FR */
            }
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




FILE *fd;

#define MAX_BUF 4096
char audio[MAX_BUF];


/* from ac3scan.c */
static int get_ac3_bitrate(uint8_t *ptr)
{
    static const int bitrates[] = {
	32, 40, 48, 56,
	64, 80, 96, 112,
	128, 160, 192, 224,
	256, 320, 384, 448,
	512, 576, 640
    };
    int ratecode = (ptr[2] & 0x3E) >> 1;
    if (ratecode < sizeof(bitrates)/sizeof(*bitrates))
	return bitrates[ratecode];
    return -1;
}

static int get_ac3_samplerate(uint8_t *ptr)
{
    static const int samplerates[] = {48000, 44100, 32000, -1};
    return samplerates[ptr[2]>>6];
}

static int get_ac3_framesize(uint8_t *ptr)
{
    int bitrate = get_ac3_bitrate(ptr);
    int samplerate = get_ac3_samplerate(ptr);
    if (bitrate < 0 || samplerate < 0)
	return -1;
    return bitrate * 96000 / samplerate + (samplerate==44100 ? ptr[2]&1 : 0);
}


static int ac3scan(int infd, int outfd)
{
    int pseudo_frame_size = 0, j = 0, i = 0, s = 0;
    unsigned long k = 0;
#ifdef DDBUG
    int n = 0;
#endif
    char buffer[SIZE_PCM_FRAME];
    int frame_size, bitrate;
    float rbytes;
    uint16_t sync_word = 0;
    ssize_t bytes_read;

    // need to find syncframe:
    for (;;) {
        k = 0;
        for (;;) {
            bytes_read = tc_pread(infd, &buffer[s], 1);
            if (bytes_read <= 0) {
                // ac3 sync frame scan failed
                if (bytes_read == 0)  /* EOF */
                    return 0;
                else
                    return ERROR_INVALID_HEADER;
            }

            sync_word = (sync_word << 8) + (uint8_t) buffer[s];

            s = (s + 1) % 2;
            ++i;
            ++k;

            if (sync_word == 0x0b77) {
                break;
            }

            if (k > (1 << 20)) {
                tc_log_error(__FILE__, "no AC3 sync frame found within 1024 kB of stream");
	            return 1;
            }
        }
        i = i - 2;
#ifdef DDBUG
        tc_log_msg(__FILE__, "found sync frame at offset %d (%d)", i, j);
#endif
        // read rest of header
        if (tc_pread(infd, &buffer[2], 3) !=3) {
            return ERROR_INVALID_HEADER;
        }

        if ((frame_size = 2 * get_ac3_framesize(&buffer[2])) < 1) {
            tc_log_error(__FILE__, "ac3 framesize %d invalid", frame_size);
            return 1;
        }

        //FIXME: I assume that a single AC3 frame contains 6kB PCM bytes

        rbytes = (float) SIZE_PCM_FRAME/1024/6 * frame_size;
        pseudo_frame_size = (int) rbytes;

        if ((bitrate = get_ac3_bitrate(&buffer[2])) < 1) {
            tc_log_error(__FILE__, "ac3 bitrate invalid");
            return 1;
        }

        // write out frame header

#ifdef DDBUG
        tc_log_msg(__FILE__, "[%05d] %04d bytes, pcm pseudo frame %04d bytes, bitrate %03d kBits/s",
	               n++, frame_size, pseudo_frame_size, bitrate);
#endif

        // s points directly at first byte of syncword
        tc_pwrite(outfd, &buffer[s], 1);
        s = (s + 1) % 2;
        tc_pwrite(outfd, &buffer[s], 1);
        s = (s + 1) % 2;

        // read packet
        tc_pread(infd, &buffer[5], frame_size-5);
        tc_pwrite(outfd, &buffer[2], frame_size-2);

        i += frame_size;
        j = i;
    }
    return 0;
}


/* ------------------------------------------------------------
 *
 * extract thread
 *
 * magic: TC_MAGIC_VOB
 *        TC_MAGIC_AVI
 *        TC_MAGIC_RAW  <-- default
 *
 * ------------------------------------------------------------*/


void extract_ac3(info_t *ipipe)
{

    int error=0;

    avi_t *avifile;

    long frames, bytes, padding, n;

    verbose = ipipe->verbose;

    buffer = tc_malloc (BUFFER_SIZE);

    switch(ipipe->magic) {

    case TC_MAGIC_VDR:

      in_file = fdopen(ipipe->fd_in, "r");
      out_file = fdopen(ipipe->fd_out, "w");

      vdr_work_around=1;

      pes_ac3_loop();

      fclose(in_file);
      fclose(out_file);

      break;

    case TC_MAGIC_VOB:

      in_file = fdopen(ipipe->fd_in, "r");
      out_file = fdopen(ipipe->fd_out, "w");


      if(ipipe->codec==TC_CODEC_PS1) {

	track_code = ipipe->track;
	get_pts=1;

	if(track_code < 0) import_exit(1);

      } else {
	if (ipipe->track < 0 || ipipe->track >= TC_MAX_AUD_TRACKS) {
	  tc_log_error(__FILE__, "invalid track number: %d", ipipe->track);
	  import_exit(1);
	}

	// DTS tracks begin with ID 0x88, ac3 with 0x80
	if (ipipe->codec == TC_CODEC_DTS)
	  track_code = ipipe->track + 0x88;
	else
	  track_code = ipipe->track + 0x80;
      }

      pes_ac3_loop();

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

      // get total audio size
      bytes = AVI_audio_bytes(avifile);

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

      error=ac3scan(ipipe->fd_in, ipipe->fd_out);
      break;
    }

    free (buffer);
    import_exit(error);

}

