/*
 *  a52_decore.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  changes by Joerg Sauer <js-mail@gmx.net> for liba52-0.7.3
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

#include <limits.h>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
# ifdef OS_DARWIN
#  include "libdldarwin/dlfcn.h"
# endif
#endif

#include <a52dec/a52.h>
#include <a52dec/mm_accel.h>
#include "magic.h"

#define FRAME_SIZE 3840
#define HEADER_LEN    8
#define A52_BLOCKS    6

static inline int16_t convert (int32_t i)
{

  if (i > 0x43c07fff)
    return 32767;
  else if (i < 0x43bf8000)
    return -32768;
  else
    return i - 0x43c00000;
}

static void float2s16_2 (float * _f, int16_t * s16)
{
  int i;
  int32_t * f = (int32_t *) _f;

  //interleave l/r channels

  for (i = 0; i < 256; i++) {
    s16[2*i] = convert (f[i]);
    s16[2*i+1] = convert (f[i+256]);
  }
}

static void float2s16 (float * _f, int16_t * s16)
{
  int i;
  int32_t * f = (int32_t *) _f;

  for (i = 0; i < 256*A52_BLOCKS; i++) s16[i] = convert (f[i]);
}

static unsigned char buf[FRAME_SIZE];

int a52_decore(decode_t *decode);  /* avoid a missing-prototype warning */

int a52_decore(decode_t *decode) {

  int i, s=0, pcm_size, frame_size;
  int k, n, bytes_read, bytes_wrote, sample_rate, bit_rate, flags;
  unsigned short sync_word = 0;
  int pass_through = decode->format==TC_CODEC_RAW?1:0;
  a52_state_t *state;
  sample_t level=1, bias=384;
  sample_t *samples;
  int chans = -1;
  int16_t pcm_buf[256 * A52_BLOCKS];
  uint32_t accel = MM_ACCEL_DJBFFT;


#ifdef HAVE_ASM_MMX
  if (decode->accel & AC_MMX)
    accel |= MM_ACCEL_X86_MMX;
#endif

#ifdef HAVE_ASM_3DNOW
  if (decode->accel & AC_3DNOW)
    accel |= MM_ACCEL_X86_3DNOW;
#endif

  state = a52_init(accel);

  n=0;

  for (;;) {

    // check for next AC3 sync bytes

    k=0;
    memset(buf, 0, HEADER_LEN);
    s=0;
    sync_word = 0;
    bytes_read = 0;
    bytes_wrote = 0;

    for (;;) {

      if (tc_pread(decode->fd_in, &buf[s], 1) !=1) {
	//ac3 sync frame scan failed
	return(-1);
      }

      sync_word = (sync_word << 8) + (unsigned char) buf[s];

      s = (s+1)%2;

      ++k;

      if(sync_word == 0x0b77) break;

      if(k>(1<<20)) {
	tc_log_error(__FILE__, "no AC3 sync frame found within 1024 kB of stream");
	return(-1);
      }
    }

    // found, read rest of frame header

    buf[0] = (sync_word >> 8) & 0xff;
    buf[1] = (sync_word) & 0xff;

    bytes_read=tc_pread(decode->fd_in, &buf[2], HEADER_LEN-2);

    if(bytes_read< HEADER_LEN-2) {
      if(decode->verbose & TC_DEBUG)
	tc_log_msg(__FILE__, "read error (%d/%d)", bytes_read, HEADER_LEN-2);
      return(-1);
    }

    // FIXME:
    // save header
    // ac_memcpy(header, &buf[2], 5);

    // valid AC3 frame?

    frame_size = a52_syncinfo(buf, &flags, &sample_rate, &bit_rate);

    if(frame_size==0 || frame_size>= FRAME_SIZE) {
      tc_log_msg(__FILE__, "frame size = %d (%d %d)", frame_size, sample_rate, bit_rate);
      goto skip_frame;
    }

    // read the rest of the frame
    if((bytes_read=tc_pread(decode->fd_in, &buf[HEADER_LEN], frame_size-HEADER_LEN)) < frame_size-HEADER_LEN) {
      if(decode->verbose & TC_DEBUG)
	tc_log_msg(__FILE__, "read error (%d/%d)", bytes_read, frame_size-HEADER_LEN);
      return(-1);
    }

    // decoder start
    flags = (decode->a52_mode & TC_A52_DOLBY_OFF) ? A52_STEREO:A52_DOLBY;
    flags = (decode->a52_mode & TC_A52_DEMUX) ? (A52_3F2R | A52_LFE) : flags;

    a52_frame(state, buf, &flags, &level, bias);
    if(decode->a52_mode & TC_A52_DRC_OFF) a52_dynrng (state, NULL, NULL);

    flags &= A52_CHANNEL_MASK | A52_LFE;

    if (flags & A52_LFE)
	chans = 6;
    else if (flags & 1)	/* center channel */
	chans = 5;
    else switch (flags) {
    case A52_2F2R:
	chans = 4;
	break;
    case A52_CHANNEL:
    case A52_STEREO:
    case A52_DOLBY:
	chans = 2;
	break;
    default:
	return 1;
    }

    // decode frame
   if (!pass_through) {
    for(i=0; i<A52_BLOCKS; ++i) {

      a52_block(state);

      // output pcm data

      samples = a52_samples(state);

      pcm_size = 256 * sizeof (int16_t)*chans;

      //tc_log_msg(__FILE__, "write (%d) bytes", pcm_size);
      (decode->a52_mode & TC_A52_DEMUX) ? float2s16((float *)samples, (int16_t *)&pcm_buf) : float2s16_2((float *)samples, (int16_t *)&pcm_buf);

	if((bytes_wrote=tc_pwrite(decode->fd_out, (uint8_t*)pcm_buf, pcm_size)) < pcm_size) {
	  if(decode->verbose & TC_DEBUG)
	    tc_log_error(__FILE__, "write error (%d/%d)", bytes_wrote, pcm_size);
	  return(-1);
	}
    } //end pcm data output
   } else {
    // pass through
    for(i=0; i<A52_BLOCKS; ++i) {

      a52_block(state);

      // output pcm data

      samples = a52_samples(state);

      pcm_size = 256 * sizeof (int16_t)*chans;

      //tc_log_msg(__FILE__, "write (%d) bytes", pcm_size);
      (decode->a52_mode & TC_A52_DEMUX) ? float2s16((float *)samples, (int16_t *)&pcm_buf) : float2s16_2((float *)samples, (int16_t *)&pcm_buf);
    } //end pcm data output
    if((bytes_wrote=tc_pwrite(decode->fd_out, buf, bytes_read+HEADER_LEN)) < bytes_read+HEADER_LEN) {
	if(decode->verbose & TC_DEBUG)
	  tc_log_error(__FILE__, "write error (%d/%d)", bytes_wrote, bytes_read+HEADER_LEN);
	return(-1);
    }
   }

  skip_frame:
    continue;

  } //end frame processing

  // should not get here
  return 0;
}
