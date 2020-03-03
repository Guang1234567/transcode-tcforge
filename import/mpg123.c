/*
 *  mpg123.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Copyright (c) 1999 Albert L Faber
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
#include "magic.h"
#include "tc.h"
#include "libtc/libtc.h"

#ifdef HAVE_LAME
#include "mpg123.h"

#define         MAX_U_32_NUM            0xFFFFFFFF

#define MAX_BUF 4096
static char sbuffer[MAX_BUF];

static int fskip(FILE * fp, long offset, int whence)
{
#ifndef PIPE_BUF
    char    buffer[4096];
#else
    char    buffer[PIPE_BUF];
#endif
    int     read;

    if (0 == fseek(fp, offset, whence))
        return 0;

    if (whence != SEEK_CUR || offset < 0) {
        tc_log_warn(__FILE__,
		    "fskip problem: Mostly the return status of functions is not evaluate so it is more secure to polute <stderr>.");
        return -1;
    }

    while (offset > 0) {
        read = offset > sizeof(buffer) ? sizeof(buffer) : offset;
        if ((read = fread(buffer, 1, read, fp)) <= 0)
            return -1;
        offset -= read;
    }

    return 0;
}

static int check_aid(const unsigned char *header)
{
    return 0 == strncmp(header, "AiD\1", 4);
}

/*
 * Please check this and don't kill me if there's a bug
 * This is a (nearly?) complete header analysis for a MPEG-1/2/2.5 Layer I, II or III
 * data stream
 */

static int is_syncword_mp123(const void *const headerptr)
{
    const unsigned char *const p = headerptr;
    static const char abl2[16] =
        { 0, 7, 7, 7, 0, 7, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8 };

    if ((p[0] & 0xFF) != 0xFF)
        return 0;       // first 8 bits must be '1'
    if ((p[1] & 0xE0) != 0xE0)
        return 0;       // next 3 bits are also
    if ((p[1] & 0x18) == 0x08)
        return 0;       // no MPEG-1, -2 or -2.5
    if ((p[1] & 0x06) == 0x00)
        return 0;       // no Layer I, II and III
    if ((p[2] & 0xF0) == 0xF0)
        return 0;       // bad bitrate
    if ((p[2] & 0x0C) == 0x0C)
        return 0;       // no sample frequency with (32,44.1,48)/(1,2,4)
    if ((p[1] & 0x06) == 0x04) // illegal Layer II bitrate/Channel Mode comb
        if (abl2[p[2] >> 4] & (1 << (p[3] >> 6)))
            return 0;
    return 1;
}

static int is_syncword_mp3(const void *const headerptr)
{
    const unsigned char *const p = headerptr;

    if ((p[0] & 0xFF) != 0xFF)
        return 0;       // first 8 bits must be '1'
    if ((p[1] & 0xE0) != 0xE0)
        return 0;       // next 3 bits are also
    if ((p[1] & 0x18) == 0x08)
        return 0;       // no MPEG-1, -2 or -2.5
    if ((p[1] & 0x06) != 0x02)
        return 0;       // no Layer III (can be merged with 'next 3 bits are also' test, but don't do this, this decreases readability)
    if ((p[2] & 0xF0) == 0xF0)
        return 0;       // bad bitrate
    if ((p[2] & 0x0C) == 0x0C)
        return 0;       // no sample frequency with (32,44.1,48)/(1,2,4)
    return 1;
}


int lame_decode_initfile(FILE * fd, mp3data_struct * mp3data, int format)
{
    //  VBRTAGDATA pTagData;
    // int xing_header,len2,num_frames;
#define bufsize 100
    unsigned char buf[bufsize];
    int     ret;
    int     len, aid_header;
    short int pcm_l[1152], pcm_r[1152];

    memset(mp3data, 0, sizeof(mp3data_struct));
    memset(buf, 0, bufsize);

    lame_decode_init();
    if (!format) format = 0x55;

    len = 4;
    if (fread(buf, 1, len, fd) != len)
        return -1;      /* failed */
    aid_header = check_aid(buf);
    if (aid_header) {
        if (fread(buf, 2, 1, fd) != 1)
            return -1;  /* failed */
        aid_header = (unsigned char) buf[0] + 256 * (unsigned char) buf[1];
        tc_log_msg(__FILE__, "Album ID found.  length=%i", aid_header);
        /* skip rest of AID, except for 6 bytes we have already read */
        fskip(fd, aid_header - 6, SEEK_CUR);

        /* read 4 more bytes to set up buffer for MP3 header check */
        len = fread(buf, 1, 4, fd);
    }

    /* look for valid 4 byte MPEG header  */
    if (len < 4)
        return -1;

    if (format == 0x55) {
	while ( !is_syncword_mp3(buf)) {
	    int     i;
	    for (i = 0; i < len - 1; i++)
		buf[i] = buf[i + 1];
	    if (fread(buf + len - 1, 1, 1, fd) != 1)
		return -1;  /* failed */
	}
    } else if (format == 0x50) {
	while ( !is_syncword_mp123(buf)) {
	    int     i;
	    for (i = 0; i < len - 1; i++)
		buf[i] = buf[i + 1];
	    if (fread(buf + len - 1, 1, 1, fd) != 1)
		return -1;  /* failed */
	}
    }

    // now parse the current buffer looking for MP3 headers.
    // (as of 11/00: mpglib modified so that for the first frame where
    // headers are parsed, no data will be decoded.
    // However, for freeformat, we need to decode an entire frame,
    // so mp3data->bitrate will be 0 until we have decoded the first
    // frame.  Cannot decode first frame here because we are not
    // yet prepared to handle the output.

    ret = lame_decode1_headers(buf, len, pcm_l, pcm_r, mp3data);
    if (-1 == ret)
        return -1;

    /* repeat until we decode a valid mp3 header.  */
    while (!mp3data->header_parsed) {
        len = fread(buf, 1, sizeof(buf), fd);
        if (len != sizeof(buf))
            return -1;
        ret = lame_decode1_headers(buf, len, pcm_l, pcm_r, mp3data);
        if (-1 == ret)
            return -1;
    }

    if (mp3data->bitrate==0) {
	tc_log_msg(__FILE__,"Input file is freeformat.");
    }

    if (mp3data->totalframes > 0) {
        /* mpglib found a Xing VBR header and computed nsamp & totalframes */
    }
    else {
	/* set as unknown.  Later, we will take a guess based on file size
	 * ant bitrate */
        mp3data->nsamp = MAX_U_32_NUM;
    }

    return 0;
}

/*
For lame_decode_fromfile:  return code
  -1     error
   n     number of samples output.  either 576 or 1152 depending on MP3 file.


For lame_decode1_headers():  return code
  -1     error
   0     ok, but need more data before outputing any samples
   n     number of samples output.  either 576 or 1152 depending on MP3 file.
*/

int lame_decode_fromfile(FILE * fd, short pcm_l[], short pcm_r[],
			 mp3data_struct * mp3data)
{
    int     ret = 0, len=0;
    unsigned char buf[1024];

    /* first see if we still have data buffered in the decoder: */
    ret = lame_decode1_headers(buf, len, pcm_l, pcm_r, mp3data);
    if (ret!=0) return ret;


    /* read until we get a valid output frame */
    while (1) {
        len = fread(buf, 1, 1024, fd);
        if (len == 0) {
	    /* we are done reading the file, but check for buffered data */
	    ret = lame_decode1_headers(buf, len, pcm_l, pcm_r, mp3data);
	    if (ret<=0) return -1;  // done with file
	    break;
	}

        ret = lame_decode1_headers(buf, len, pcm_l, pcm_r, mp3data);
        if (ret == -1) return -1;
	if (ret >0) break;
    }
    return ret;
}

static int verbose_flag;

int buf_probe_mp3(unsigned char *_buf, int len, ProbeTrackInfo *pcm)
{

  //  VBRTAGDATA pTagData;
  // int xing_header,len2,num_frames;

  char *buf;
  mp3data_struct *mp3data;
  int     i, ret;
  int format=0;
  short int pcm_l[1152], pcm_r[1152];

  int type;

  if((mp3data = tc_zalloc(sizeof(mp3data_struct)))==NULL) {
    tc_log_error(__FILE__, "out of memory");
    exit(1);
  }

  lame_decode_init();

  buf=_buf;

  for (i = 0; i < len - 1; i++) {
    if(is_syncword_mp123(buf)) {
	// catch false positives
	switch(buf[1] & 0xff) {
	    case 0xFD: case 0xFC: format = TC_CODEC_MP2; break;
	    case 0xFB:            format = TC_CODEC_MP3; break;
	}
	if (format) break;
    }
    ++buf;
  }

  type = buf[1] & 0xff;

  ret = lame_decode1_headers(buf, len, pcm_l, pcm_r, mp3data);

  if (-1 == ret) {
    //tc_log_error(__FILE__, "failed to probe mp3 header (%d)", len);
    return -1;
  }

  //copy infos:

  pcm->samplerate = mp3data->samplerate;
  pcm->chan = mp3data->stereo;
  pcm->bits = 16;
  pcm->format = TC_CODEC_MP3;
  pcm->bitrate = mp3data->bitrate;

  if(verbose_flag & TC_DEBUG)
    tc_log_msg(__FILE__, "channels=%d, samplerate=%d Hz, bitrate=%d kbps, (fsize=%d)", mp3data->stereo, mp3data->samplerate, mp3data->bitrate, mp3data->framesize);

  switch(type) {

  case 0xFD:
  case 0xFC:
    pcm->format = TC_CODEC_MP2;
    break;
  case 0xFB:
    pcm->format = TC_CODEC_MP3;
    break;
  }
  return 0;
}

void probe_mp3(info_t *ipipe)
{
    ssize_t ret=0;

    // need to find syncframe:

    if((ret = tc_pread(ipipe->fd_in, sbuffer, MAX_BUF)) != MAX_BUF) {
	if (!ret) {
	    ipipe->error=1;
	    return;
	}
    }

    verbose_flag = ipipe->verbose;

    //for single MP3 stream only
    if(buf_probe_mp3(sbuffer, ret, &ipipe->probe_info->track[0])<0) {
	ipipe->error=1;
	return;
    }

    switch(ipipe->probe_info->track[0].format) {

    case TC_CODEC_MP2:
      ipipe->probe_info->magic = TC_MAGIC_MP2;
      break;
    case TC_CODEC_MP3:
      ipipe->probe_info->magic = TC_MAGIC_MP3;
      break;
    }

    ++ipipe->probe_info->num_tracks;

    return;
}

#else  // HAVE_LAME

void probe_mp3(info_t *ipipe) {

    tc_log_error(__FILE__, "no lame support available");
    return;

}

#endif
