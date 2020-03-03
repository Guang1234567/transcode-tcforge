/*
 *  scan_pack.c
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

#include "src/transcode.h"
#include "ioaux.h"
#include "aux_pes.h"
#include "seqinfo.h"
#include "demuxer.h"
#include "packets.h"

/* ------------------------------------------------------------
 *
 * auxiliary routines
 *
 * ------------------------------------------------------------*/

static char *picture_structure_str[4] = {
    "Invalid Picture Structure",
    "Top field",
    "Bottom field",
    "Frame Picture"
};

static int _cmp_32_bits(char *buf, long x)
{

    if(0) {
	tc_log_msg(__FILE__, "MAGIC: 0x%02lx 0x%02lx 0x%02lx 0x%02lx", (x >> 24) & 0xff, ((x >> 16) & 0xff), ((x >>  8) & 0xff), ((x      ) & 0xff));
	tc_log_msg(__FILE__, " FILE: 0x%02x 0x%02x 0x%02x 0x%02x", buf[0] & 0xff, buf[1] & 0xff, buf[2] & 0xff, buf[3] & 0xff);
    }

    if ((buf[0]& 0xff) != ((x >> 24) & 0xff))
	return 0;
    if ((buf[1]& 0xff) != ((x >> 16) & 0xff))
	return 0;
    if ((buf[2] & 0xff)!= ((x >>  8) & 0xff))
	return 0;
    if ((buf[3]& 0xff) != ((x      ) & 0xff))
	return 0;

  // OK found it
  return 1;
}


static int _cmp_16_bits(char *buf, long x)
{
  if(0) {
    tc_log_msg(__FILE__, "MAGIC: 0x%02lx 0x%02lx 0x%02lx 0x%02lx %s", (x >> 24) & 0xff, ((x >> 16) & 0xff), ((x >>  8) & 0xff), ((x      ) & 0xff), filetype(x));
    tc_log_msg(__FILE__, " FILE: 0x%02x 0x%02x 0x%02x 0x%02x", buf[2] & 0xff, buf[3] & 0xff, buf[0] & 0xff, buf[1] & 0xff);
  }

    if ((uint8_t)buf[0] != ((x >>  8) & 0xff))
	return 0;
    if ((uint8_t)buf[1] != ((x      ) & 0xff))
	return 0;

  // OK found it
  return 1;
}

static int pack_scan_16(char *video, long magic)
{
    int k, off = (video[VOB_PACKET_OFFSET] & 0xff) + VOB_PACKET_OFFSET + 1;

    for(k=off; k<=VOB_PACKET_SIZE-2; ++k) {
	if(_cmp_16_bits(video+k, magic)) return(k);
    }// scan buffer
    return(-1);
}


static int pack_scan_32(char *video, long magic)
{
    int k, off = (video[VOB_PACKET_OFFSET] & 0xff) + VOB_PACKET_OFFSET + 1;

    for(k=off; k<=VOB_PACKET_SIZE-4; ++k) {
	if(_cmp_32_bits(video+k, magic)) return(k);
    }// scan buffer
    return(-1);
}

#if 0  // unused
static unsigned long read_ts(char *_s)
{

  unsigned long pts;

  char *buffer=_s;

  unsigned int ptr=0;

  pts = (buffer[ptr++] >> 1) & 7;  //low 4 bits (7==1111)
  pts <<= 15;
  pts |= (stream_read_int16(&buffer[ptr]) >> 1);
  ptr+=2;
  pts <<= 15;
  pts |= (stream_read_int16(&buffer[ptr]) >> 1);

  return pts;
}
#endif

#define BUF_WARN_COUNT 20

static int probe_picext(uint8_t *buffer, size_t buflen)
{

  //  static char *picture_structure_str[4] = {
  //  "Invalid Picture Structure",
  //  "Top field",
  //  "Bottom field",
  //  "Frame Picture"
  //};
  if(buflen < 3) {
#ifdef PROBE_DEBUG  
    static int buf_small_count = 0;
    if(buf_small_count == 0
      || (buf_small_count % BUF_WARN_COUNT) == 0) {
        tc_log_warn(__FILE__, "not enough buffer to probe picture extension "
                          "(buflen=%lu) [happened at least %i times]",
                          (unsigned long)buflen, buf_small_count);
    }
    buf_small_count++;
#endif
    return(-1); /* failed probe */
  }
  return(buffer[2] & 3);
}

static const char *probe_group(uint8_t *buffer, size_t buflen)
{
    static char retbuf[32];
    if(buflen < 5) {
#ifdef PROBE_DEBUG  
        static int buf_small_count = 0;
        if(buf_small_count == 0
          || (buf_small_count % BUF_WARN_COUNT) == 0) {
            tc_log_warn(__FILE__, "not enough buffer to probe picture group "
                             "(buflen=%lu) [happened at least %i times]",
                             (unsigned long)buflen, buf_small_count);
        }
        buf_small_count++;
#endif
	*retbuf = 0;
    } else {
	tc_snprintf(retbuf, sizeof(retbuf), "%s%s",
		    (buffer[4] & 0x40) ? " closed_gop" : "",
		    (buffer[4] & 0x20) ? " broken_link" : "");
    }
    return retbuf;
}

int flag1=0, flag2=0, flag3=0;

int scan_pack_pics(char *video)
{

   int k, off = (video[VOB_PACKET_OFFSET] & 0xff) + VOB_PACKET_OFFSET + 1;

   int ctr=0;


   if(flag1) if( (video[off] & 0xff) == 0) ++ctr;
   if(flag2) if( (video[off] & 0xff) == 1  &&  (video[off+1] & 0xff) == 0) ++ctr;
   if(flag3) if( (video[off] & 0xff) == 0  && (video[off+1] & 0xff) == 1  &&  (video[off+2] & 0xff) == 0) ++ctr;

//   tc_log_msg(__FILE__, "off=%d byte=0x%x byte=0x%x ctr=%d", off , (video[off] & 0xff), (video[VOB_PACKET_SIZE-4] & 0xff), ctr);

   if(ctr)
     tc_debug(TC_DEBUG_PRIVATE, "split PIC code detected");

   flag1=flag2=flag3=0;

   for(k=off; k<=VOB_PACKET_SIZE-4; ++k) {
     if(_cmp_32_bits(video+k, MPEG_PICTURE_START_CODE)) ++ctr;
   }

   if( (video[VOB_PACKET_SIZE-1] & 0xff) == 0) flag3=1;
   if( (video[VOB_PACKET_SIZE-2] & 0xff) == 0 && (video[VOB_PACKET_SIZE-1] & 0xff) == 0) flag2=1;
   if( (video[VOB_PACKET_SIZE-3] & 0xff) == 0 && (video[VOB_PACKET_SIZE-2] & 0xff) == 0 && (video[VOB_PACKET_SIZE-1] & 0xff) == 1) flag1=1;

//   tc_log_msg(__FILE__, "ctr= %d | f1=%d, f2=%d, f3=%d", ctr, flag1, flag2, flag3);

   return(ctr);
}

int scan_pack_ext(char *buf)
{

  int n, ret_code=-1;

  for(n=0; n<VOB_PACKET_SIZE-4; ++n) {

      if(_cmp_32_bits(buf+n, TC_MAGIC_PICEXT) && ((uint8_t) buf[n+4]>>4)==8){
	  ret_code = probe_picext(buf+n+4, VOB_PACKET_SIZE-4-n);
      }
  } // probe extension header

  return(ret_code);
}


void scan_pack_payload(char *video, size_t size, int n, int verbose)
{

    int k;
    char buf[256];
    unsigned long i_pts, i_dts;

    int aud_tag, vid_tag;

    int len;

    double pts;

    seq_info_t si;

    // scan payload

    // time stamp:
    ac_memcpy(buf, &video[4], 6);
    pts = read_time_stamp(buf);

    //    tc_log_msg(__FILE__, "PTS=%ld %d %f %ld",  read_time_stamp_long(buf),read_ts(buf), read_ts(buf)/90000., parse_pts(buf, 2));

    // payload length
    len = stream_read_int16(&video[18]);

    tc_log_msg(__FILE__, "[%06d] id=0x%x SCR=%12.8f size=%4d", n, (video[17] & 0xff), pts, len);


    if((video[17] & 0xff) == P_ID_MPEG) {

	if((k=pack_scan_32(video, TC_MAGIC_M2V))!=-1) {

	    tc_log_msg(__FILE__, "    MPEG SEQ start code found in packet %d, offset %4d", n, k);


	    //read packet header
	    ac_memcpy(buf, &video[20], 16);
	    get_pts_dts(buf, &i_pts, &i_dts);

	    tc_log_msg(__FILE__, "    PTS=%f DTS=%f", (double) i_pts / 90000., (double) i_dts / 90000.);

	    stats_sequence(&video[k+4], &si);

	}

	if((k=pack_scan_32(video, MPEG_SEQUENCE_END_CODE))!=-1)
	    tc_log_msg(__FILE__, "    MPEG SEQ   end code found in packet %d, offset %4d", n, k);

	if((k=pack_scan_32(video, MPEG_EXT_START_CODE))!=-1) {

	    if(((uint8_t)video[k+4]>>4)==8) {
		    int mode = probe_picext(&video[k+4], size - (size_t)k);
            if(mode > 0)
                tc_log_msg(__FILE__, "    MPEG EXT start code found in packet %d, offset %4d, %s", n, k, picture_structure_str[mode]);
            else
                tc_log_msg(__FILE__, "    MPEG EXT start code found INCOMPLETE in packet %d, offset %4d", n, k);
	    } else
		    tc_log_msg(__FILE__, "    MPEG EXT start code found in packet %d, offset %4d", n, k);
	}

	if((k=pack_scan_32(video, MPEG_GOP_START_CODE))!=-1) {
	    tc_log_msg(__FILE__, "    MPEG GOP start code found in packet %d, offset %4d, gop [%03d]%s",
		       n, k, gop_cnt,
		       probe_group((uint8_t*) &video[k+4], size - (size_t)k));
	    gop_pts=pts;
	    ++gop_cnt;
	    gop=1;
	}

	if((k=pack_scan_32(video, MPEG_PICTURE_START_CODE))!=-1)
	    tc_log_msg(__FILE__, "    MPEG PIC start code found in packet %d, offset %4d", n, k);

	if((k=pack_scan_32(video, MPEG_SYSTEM_START_CODE))!=-1)
	    tc_log_msg(__FILE__, "    MPEG SYS start code found in packet %d, offset %4d", n, k);

	if((k=pack_scan_32(video, MPEG_PADDING_START_CODE))!=-1)
	    tc_log_msg(__FILE__, "    MPEG PAD start code found in packet %d, offset %4d", n, k);
    }

    if((video[17] & 0xff) == P_ID_AC3) {

	      //position of track code
	      uint8_t *ibuf=video+14;
	      uint8_t *tmp=ibuf + 9 + ibuf[8];

	      //read packet header
	      ac_memcpy(buf, &video[20], 16);
	      get_pts_dts(buf, &i_pts, &i_dts);

	      tc_log_msg(__FILE__, "    substream PTS=%f [0x%x]", (double) i_pts / 90000., *tmp);

	      if((k=pack_scan_16(video, TC_MAGIC_AC3))!=-1) {
		if(gop) {

		  tc_log_msg(__FILE__, "    AC3 sync frame, packet %6d, offset %3d, gop [%03d], A-V %.3f", n, k, gop_cnt-1, pts-gop_pts);
		  gop=0;

	    } else
	      tc_log_msg(__FILE__, "    AC3 sync frame found in packet %d, offset %d", n, k);
	}

	if((k=pack_scan_32(video, MPEG_PADDING_START_CODE))!=-1)
	    tc_log_msg(__FILE__, "    MPEG PAD start code found in packet %d, offset %4d", n, k);

    }

    if((video[17] & 0xff) >= 0xc0 && (video[17] & 0xff) <= 0xdf) {

      //read packet header
      ac_memcpy(buf, &video[20], 16);
      get_pts_dts(buf, &i_pts, &i_dts);

      tc_log_msg(__FILE__, "    MPEG audio PTS=%f [0x%x]", (double) i_pts / 90000., (video[17] & 0xff));
    }

    if((video[17] & 0xff) == P_ID_PROG) {

	aud_tag = (video[23]>>2) & 0x3f;
	vid_tag = video[24] & 0x1f;

	tc_log_msg(__FILE__, "    MPEG PRG start code found in packet %d, A=%d, V=%d", n, aud_tag, vid_tag);

    }// check for sync packet

    return;
}

int scan_pack_header(char *buf, long x)
{

    int ret = _cmp_32_bits(buf, x);
    if(0) tc_log_msg(__FILE__, "scan_pack_header() ret=%d", ret);
    return(ret);
}
