/*
 *  extract_mpeg2.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Copyright (C) 1999-2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 *  This file is part of transcode, a video stream  processing tool
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

#include "ioaux.h"
#include "avilib/avilib.h"
#include "tc.h"

#define BUFFER_SIZE 262144
static uint8_t buffer[BUFFER_SIZE];
static FILE *in_file, *out_file;


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

    /* video stream code: 1110???? */
	case 0xe0:	/* video */
	case 0xe1:	/* video */
	case 0xe2:	/* video */
	case 0xe3:	/* video */
	case 0xe4:	/* video */
	case 0xe5:	/* video */
	case 0xe6:	/* video */
	case 0xe7:	/* video */
	case 0xe8:	/* video */
	case 0xe9:	/* video */
	case 0xea:	/* video */
	case 0xeb:	/* video */
	case 0xec:	/* video */
	case 0xed:	/* video */
	case 0xee:	/* video */
	case 0xef:	/* video */

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

	  if (tmp1 < tmp2) {
        TC_PIPE_WRITE(fileno(out_file), tmp1, tmp2-tmp1);
        /* yeah, I know that's ugly -- FR */
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


/* ------------------------------------------------------------
 *
 * mpeg2 extract thread
 *
 * magic: TC_MAGIC_VOB
 *        TC_MAGIC_RAW  <-- default
 *        TC_MAGIC_M2V
 *        TC_MAGIC_CDXA
 *
 * ------------------------------------------------------------*/


void extract_mpeg2(info_t *ipipe)
{

    int error=0;

    verbose = ipipe->verbose;

    switch(ipipe->magic) {

    case TC_MAGIC_VOB:

      in_file = fdopen(ipipe->fd_in, "r");
      out_file = fdopen(ipipe->fd_out, "w");

      ps_loop();

      fclose(in_file);
      fclose(out_file);

      break;

    case TC_MAGIC_CDXA:

      AVI_dump(ipipe->name, 2);

      break;

    case TC_MAGIC_M2V:
    case TC_MAGIC_RAW:
    default:

      if(ipipe->magic == TC_MAGIC_UNKNOWN)
	tc_log_warn(__FILE__, "no file type specified, assuming %s",
		    filetype(TC_MAGIC_RAW));


      error=tc_preadwrite(ipipe->fd_in, ipipe->fd_out);

      break;
    }

    import_exit(error);

}
