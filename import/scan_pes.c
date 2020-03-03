/*
 *  scan_pes.c
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
#include "aux_pes.h"
#include "mpg123.h"
#include "ac3scan.h"
#include "demuxer.h"
#include "libtc/libtc.h"
#include "libtc/ratiocodes.h"

#define BUFFER_SIZE 262144
static uint8_t buffer[BUFFER_SIZE];

static seq_info_t si;

static int mpeg_version=0;

static int unit_ctr=0, seq_ctr=0;

static uint16_t id;

static uint32_t stream[256], track[TC_MAX_AUD_TRACKS], attr[TC_MAX_AUD_TRACKS];

static int tot_seq_ctr=0, tot_unit_ctr=0;
static unsigned int tot_bitrate=0, min_bitrate=(unsigned int)-1, max_bitrate=0;

//count packs for each presntation unit
static uint32_t unit_pack_cnt[256], unit_pack_cnt_index=0;

static int ref_pts=0;

static int show_seq_info=0, show_ext_info=0;

static int cmp_32_bits(char *buf, long x)
{

    if ((uint8_t)buf[0] != ((x >> 24) & 0xff))
	return 0;
    if ((uint8_t)buf[1] != ((x >> 16) & 0xff))
	return 0;
    if ((uint8_t)buf[2] != ((x >>  8) & 0xff))
	return 0;
    if ((uint8_t)buf[3] != ((x      ) & 0xff))
	return 0;

  // OK found it
  return 1;
}

static int probe_sequence(uint8_t *buffer, ProbeInfo *probe_info)
{

  int horizontal_size;
  int vertical_size;
  int aspect_ratio_information;
  int frame_rate_code;
  int bit_rate_value;
  int vbv_buffer_size_value;
  int constrained_parameters_flag;
  int load_intra_quantizer_matrix;
  int load_non_intra_quantizer_matrix;

  vertical_size = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
  horizontal_size = ((vertical_size >> 12) + 15) & ~15;
  vertical_size = ((vertical_size & 0xfff) + 15) & ~15;

  aspect_ratio_information = buffer[3] >> 4;
  frame_rate_code = buffer[3] & 15;
  bit_rate_value = (buffer[4] << 10) | (buffer[5] << 2) | (buffer[6] >> 6);
  vbv_buffer_size_value = ((buffer[6] << 5) | (buffer[7] >> 3)) & 0x3ff;
  constrained_parameters_flag = buffer[7] & 4;
  load_intra_quantizer_matrix = buffer[7] & 2;
  if (load_intra_quantizer_matrix)
    buffer += 64;
  load_non_intra_quantizer_matrix = buffer[7] & 1;

  //set some defaults, if invalid:
  if(aspect_ratio_information < 0 || aspect_ratio_information>15) aspect_ratio_information=0;

  if(frame_rate_code < 0 || frame_rate_code>15) frame_rate_code=0;

  //fill out user structure

  probe_info->width = horizontal_size;
  probe_info->height = vertical_size;
  probe_info->asr = aspect_ratio_information;
  probe_info->frc = frame_rate_code;
  probe_info->bitrate = bit_rate_value * 400.0 / 1000.0;
  tc_frc_code_to_value(frame_rate_code, &probe_info->fps);

  return(0);

}

static int probe_extension(uint8_t *buffer, ProbeInfo *probe_info)
{

    int intra_dc_precision;
    int picture_structure;
    int top_field_first;
    int frame_pred_frame_dct;
    int concealment_motion_vectors;
    int q_scale_type;
    int intra_vlc_format;
    int alternate_scan;
    int repeat_first_field;
    int progressive_frame;

    intra_dc_precision = (buffer[2] >> 2) & 3;
    picture_structure = buffer[2] & 3;
    top_field_first = buffer[3] >> 7;
    frame_pred_frame_dct = (buffer[3] >> 6) & 1;
    concealment_motion_vectors = (buffer[3] >> 5) & 1;
    q_scale_type = (buffer[3] >> 4) & 1;
    intra_vlc_format = (buffer[3] >> 3) & 1;
    alternate_scan = (buffer[3] >> 2) & 1;
    repeat_first_field = (buffer[3] >> 1) & 1;
    progressive_frame = buffer[4] >> 7;

    //get infos
    probe_info->ext_attributes[2] = progressive_frame;
    probe_info->ext_attributes[3] = alternate_scan;

    if(top_field_first == 1 && repeat_first_field == 0) return(1);

  return(0);
}

static void unit_summary(void)
{
    int n;

    int pes_total=0;

    tc_log_msg(__FILE__, "------------- presentation unit [%d] ---------------", unit_ctr);

    for(n=0; n<256; ++n) {
	if(stream[n] && n != 0xba)
	    tc_log_msg(__FILE__, "stream id [0x%x] %6d", n, stream[n]);

	if(n != 0xba) pes_total+=stream[n];
	stream[n]=0; //reset or next unit
    }

    tc_log_msg(__FILE__, "%d packetized elementary stream(s) PES packets found", pes_total);

    tc_log_msg(__FILE__, "presentation unit PU [%d] contains %d MPEG video sequence(s)", unit_ctr, seq_ctr);
    if (seq_ctr) {
    tc_log_msg(__FILE__, "Average Bitrate is %u. Min Bitrate is %u, max is %u (%s)",
	((tot_bitrate*400)/1000)/seq_ctr, min_bitrate*400/1000, max_bitrate*400/1000,
	(max_bitrate==min_bitrate)?"CBR":"VBR");
    }

    ++tot_unit_ctr;
    tot_seq_ctr+=seq_ctr;

    tc_log_msg(__FILE__, "---------------------------------------------------");

    //reset counters
    seq_ctr=0;
    show_seq_info=0;

    fflush(stdout);
}

static int mpeg1_skip_table[16] = {
  1, 0xffff,      5,     10, 0xffff, 0xffff, 0xffff, 0xffff,
  0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
};

/*------------------------------------------------------------------
 *
 * full source scan mode:
 *
 *------------------------------------------------------------------*/

/* 
 * helper. Ok, that's no much more than a crude movement from
 * probe_stream. Isn't really a big leap forward. Yet.
 */
static void adjust_info(info_t *ipipe)
{
    switch(ipipe->magic) {
      case TC_MAGIC_CDXA:
        ipipe->probe_info->attributes |= TC_INFO_NO_DEMUX;
        break;

      case TC_MAGIC_MPEG_PS: /* MPEG Program Stream */
      case TC_MAGIC_VOB:     /* backward compatibility fallback */
        /* NTSC video/film check */
        if (verbose >= TC_DEBUG) {
            tc_log_msg(__FILE__, "att0=%d, att1=%d",
                       ipipe->probe_info->ext_attributes[0],
                       ipipe->probe_info->ext_attributes[1]);
        }
        if (ipipe->probe_info->codec == TC_CODEC_MPEG2
         && ipipe->probe_info->height == 480
         && ipipe->probe_info->width == 720) {
            if (ipipe->probe_info->ext_attributes[0] > 2 * ipipe->probe_info->ext_attributes[1]
             || ipipe->probe_info->ext_attributes[1] == 0) {
                ipipe->probe_info->is_video = 1;
            }

            if (ipipe->probe_info->is_video) {
                ipipe->probe_info->fps = NTSC_VIDEO;
                ipipe->probe_info->frc = 4;
            } else {
                ipipe->probe_info->fps = NTSC_FILM;
                ipipe->probe_info->frc = 1;
            }
        }

        if (ipipe->probe_info->codec == TC_CODEC_MPEG1) {
            ipipe->probe_info->magic = TC_MAGIC_MPEG_PS;
        }

        /*
         * check for need of special import module,
         * that does not rely on 2k packs
         */
        if (ipipe->probe_info->attributes & TC_INFO_NO_DEMUX) {
            ipipe->probe_info->codec = TC_CODEC_MPEG;
            ipipe->probe_info->magic = TC_MAGIC_MPEG_PS; /* XXX: doubtful */
        }
        break;

      case TC_MAGIC_MPEG_ES: /* MPEG Elementary Stream */
      case TC_MAGIC_M2V:     /* backward compatibility fallback */
        /* make sure not to use the demuxer */
        ipipe->probe_info->codec = TC_CODEC_MPEG;
        ipipe->probe_info->magic = TC_MAGIC_MPEG_ES;
        break;

      case TC_MAGIC_MPEG_PES:/* MPEG Packetized Elementary Stream */
      case TC_MAGIC_MPEG:    /* backward compatibility fallback */
        ipipe->probe_info->attributes |= TC_INFO_NO_DEMUX;
        break;
    }
    return;
}




void scan_pes(int verbose, FILE *in_file)
{
  int n, has_pts_dts=0;
  unsigned long i_pts, i_dts;

  uint8_t * buf;
  uint8_t * end;
  uint8_t * tmp1=NULL;
  uint8_t * tmp2=NULL;
  int complain_loudly;

  long int pack_header_last=0, pack_header_ctr=0, pack_header_pos=0, pack_header_inc=0;

  char scan_buf[256];

  complain_loudly = 1;
  buf = buffer;

    for(n=0; n<256; ++n) stream[n]=0;

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


	id = buf[3] &  0xff;


	switch (buf[3]) {

	case 0xb9:	/* program end code */

	    tc_log_msg(__FILE__, "found program end code [0x%x]", buf[3] & 0xff);

	    goto summary;

	case 0xba:	/* pack header */

	    pack_header_pos = ftell (in_file) - (end - buf);
	    pack_header_inc = pack_header_pos - pack_header_last;

	    if (pack_header_inc==0) {
		tc_log_msg(__FILE__, "found first packet header at stream offset 0x%#x", 0);
	    } else {
		if((pack_header_inc-((pack_header_inc>>11)<<11)))
		    tc_log_msg(__FILE__, "pack header out of sequence at %#lx (+%#lx)", pack_header_ctr, pack_header_inc);
	    }

	    pack_header_last=pack_header_pos;
	    ++pack_header_ctr;
	    ++stream[id];

	    /* skip */
	    if ((buf[4] & 0xc0) == 0x40)	        /* mpeg2 */
		tmp1 = buf + 14 + (buf[13] & 7);
	    else if ((buf[4] & 0xf0) == 0x20)	        /* mpeg1 */
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

	  if(!stream[id]) tc_log_msg(__FILE__, "found %s stream [0x%x]", "private_stream_1", buf[3] & 0xff);

	  ++stream[id];

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

	  if(verbose & TC_DEBUG)
	    tc_log_msg(__FILE__, "[0x%x] (sub_id=0x%02x)", buf[3] & 0xff, *tmp1);

	  if((*tmp1-0x80) >= 0 && (*tmp1-0x80)<TC_MAX_AUD_TRACKS && !track[*tmp1-0x80] ) {
	    tc_log_msg(__FILE__, "found AC3 audio track %d [0x%x]", *tmp1-0x80, *tmp1);
	    track[*tmp1-0x80]=1;
	  } else if (*tmp1 == 0xFF && memcmp(tmp1+4,"SShd",4) == 0) {
	    tc_log_msg(__FILE__, "found VAG audio track [0x%x]", *tmp1);
	    track[0]=1;
	  }

	  buf = tmp2;

	  break;

	case 0xbf:

	  if(!stream[id])
	    tc_log_msg(__FILE__, "found %s [0x%x]", "navigation pack", buf[3] & 0xff);

	  ++stream[id];

	  tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	  if (tmp2 > end)
	    goto copy;

	  buf = tmp2;

	  break;

	case 0xbe:

	  if(!stream[id])
	    tc_log_msg(__FILE__, "found %s stream [0x%x]", "padding", buf[3] & 0xff);

	  ++stream[id];

	  tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	  if (tmp2 > end)
	    goto copy;

	  buf = tmp2;

	  break;

	case 0xbb:

	  if(!stream[id])
	    tc_log_msg(__FILE__, "found %s stream [0x%x]", "unknown", buf[3] & 0xff);

	  ++stream[id];

	  tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	  if (tmp2 > end)
	    goto copy;

	  buf = tmp2;

	  break;


	  //MPEG audio, maybe more???

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

	    if(!stream[id])
		tc_log_msg(__FILE__, "found %s track %d [0x%x]", "ISO/IEC 13818-3 or 11172-3 MPEG audio", (buf[3] & 0xff) - 0xc0, buf[3] & 0xff);

	    ++stream[id];

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

	    buf = tmp2;

	    break;

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

	    id = buf[3] &  0xff;

	    tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	    if (tmp2 > end)
		goto copy;
	    if ((buf[6] & 0xc0) == 0x80) {
		/* mpeg2 */
		tmp1 = buf + 9 + buf[8];

		if(!stream[id])
		    tc_log_msg(__FILE__, "found %s stream [0x%x]", "ISO/IEC 13818-2 or 11172-2 MPEG video", buf[3] & 0xff);
		++stream[id];

		mpeg_version=2;

		// get pts time stamp:
		ac_memcpy(scan_buf, &buf[6], 16);
		has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);

		if(has_pts_dts) {

		  if(!show_seq_info) {
		    for(n=0; n<100; ++n) {

		      if(cmp_32_bits(buf+n, TC_MAGIC_M2V)) {
			stats_sequence(buf+n+4, &si);
			show_seq_info=1;
			break;
		      }
		    }
		  }
		  for(n=0; n<100; ++n) {
		    if(cmp_32_bits(buf+n, TC_MAGIC_M2V)) {
		      stats_sequence_silent(buf+n+4, &si);
		      if (si.brv>max_bitrate) max_bitrate=si.brv;
		      if (si.brv<min_bitrate) min_bitrate=si.brv;
		      tot_bitrate += si.brv;
		      break;
		    }
		  }

		  if( ref_pts != 0 && i_pts < ref_pts) {

		    unit_summary();
		    unit_ctr++;
		  }
		  ref_pts=i_pts;
		  ++seq_ctr;
		}

	    } else {
	      /* mpeg1 */

	      if(!stream[id])
		  tc_log_msg(__FILE__, "found %s stream [0x%x]", "MPEG-1 video", buf[3] & 0xff);
	      ++stream[id];

	      mpeg_version=1;

	      if(!show_seq_info) {
		for(n=0; n<100; ++n) {

		  if(cmp_32_bits(buf+n, TC_MAGIC_M2V)) {
		    stats_sequence(buf+n+4, &si);
		    show_seq_info=1;
		  }
		}
	      }

	      // get pts time stamp:
	      ac_memcpy(scan_buf, &buf[6], 16);
	      has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);

	      if(has_pts_dts) {

		if( ref_pts != 0 && i_pts < ref_pts) {

		  //unit_summary();
		  unit_ctr++;
		}
		ref_pts=i_pts;

		++seq_ctr;
	      }

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

	    buf = tmp2;
	    break;


	case 0xb3:

	    tc_log_msg(__FILE__, "found MPEG sequence start code [0x%x]", buf[3] & 0xff);
	    tc_log_warn(__FILE__, "looks like an elementary stream - not program stream");

	    stats_sequence(&buf[4], &si);

	    return;

	    break;


	default:
	    if (buf[3] < 0xb9) {
		tc_log_warn(__FILE__, "looks like an elementary stream - not program stream");

		return;
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

    tc_log_msg(__FILE__, "end of stream reached");

 summary:

    unit_summary();

    tc_log_msg(__FILE__, "(%s) detected a total of %d presentation unit(s) PU and %d sequence(s)", __FILE__, tot_unit_ctr, tot_seq_ctr);

}

/*------------------------------------------------------------------
 *
 * probe only mode:
 *
 *------------------------------------------------------------------*/


void probe_pes(info_t *ipipe)
{

    int n, num, has_pts_dts=0;
    int aid, ret, initial_sync=0, has_audio=0;

    unsigned long i_pts, i_dts;
    long probe_bytes=0, total_bytes=0;

    uint8_t * buf;
    uint8_t * end;
    uint8_t * tmp1=NULL;
    uint8_t * tmp2=NULL;

    long pack_pts_1=0, pack_pts_2=0, pack_pts_3=0;

    long int pack_header_last=0, pack_header_ctr=0, pack_header_pos=0, pack_header_inc=0;

    char scan_buf[256];

    double pack_ppp=0;

    FILE *in_file = fdopen(ipipe->fd_in, "r");

    buf = buffer;

    for(n=0; n<256; ++n) stream[n]=unit_pack_cnt[n]=0;

    do {

      probe_bytes = fread (buf, 1, buffer + BUFFER_SIZE - buf, in_file);

      if(probe_bytes<0) {
	ipipe->error=1;
	return;
      }

      total_bytes += probe_bytes;

      //limit amount of search stream bytes
      if(total_bytes > TC_MAX_SEEK_BYTES * ipipe->factor) return;

      end = buf + probe_bytes;
      buf = buffer;

      //scan buffer
      while (buf + 4 <= end) {

	// check for valid start code
	if (buf[0] || buf[1] || (buf[2] != 0x01)) {

	  if (ipipe->verbose & TC_DEBUG) {
	    tc_log_warn(__FILE__, "missing start code at %#lx",
			ftell (in_file) - (end - buf));
	    if ((buf[0] == 0) && (buf[1] == 0) && (buf[2] == 0))
	      tc_log_warn(__FILE__, "incorrect zero-byte padding detected - ignored");
	  }
	  ipipe->probe_info->attributes=TC_INFO_NO_DEMUX;
	  buf++;
	  continue;
	}// check for valid start code

	id = buf[3] &  0xff;

	switch (buf[3]) {

	  //------------------------------
	  //
	  // packet header start/end code
	  //
	  //------------------------------

	case 0xb9:	/* program end code */
	  return;
	  break;

	case 0xba:	/* pack header */

	    pack_header_pos = ftell (in_file) - (end - buf);
	    pack_header_inc = pack_header_pos - pack_header_last;

	    if((pack_header_inc-((pack_header_inc>>11)<<11)))
	      ipipe->probe_info->attributes=TC_INFO_NO_DEMUX|TC_INFO_MPEG_PS;

	    pack_header_last=pack_header_pos;

	    ++pack_header_ctr;
	    ++stream[id];

	    /* skip */
	    if ((buf[4] & 0xc0) == 0x40) {	                /* mpeg2 */
		tmp1 = buf + 14 + (buf[13] & 7);
		ipipe->probe_info->codec=TC_CODEC_MPEG2;
	    } else if ((buf[4] & 0xf0) == 0x20) {	        /* mpeg1 */
		tmp1 = buf + 12;
		ipipe->probe_info->codec=TC_CODEC_MPEG1;
	    } else if (buf + 5 > end)
		goto copy;
	    else {
		tc_log_error(__FILE__, "weird pack header");
		import_exit(1);
	    }

	    // get PPP - starts

	    ac_memcpy(scan_buf, &buf[4], 16);
	    pack_pts_2 = read_time_stamp_long(scan_buf);
	    pack_ppp = read_time_stamp(scan_buf);

	    if(pack_pts_2 == pack_pts_1)
	      if(ipipe->verbose & TC_DEBUG)
		tc_log_msg(__FILE__, "SCR=%8ld (%8ld) unit=%d @ offset %10.4f (sec)", pack_pts_2, pack_pts_1, ipipe->probe_info->unit_cnt, pack_pts_1/90000.0);

	    if(pack_pts_2 < pack_pts_1) {

	      pack_pts_3 += pack_pts_1;

	      if(ipipe->verbose & TC_DEBUG)
		tc_log_msg(__FILE__, "SCR=%8ld (%8ld) unit=%d @ offset %10.4f (sec)", pack_pts_2, pack_pts_1, ipipe->probe_info->unit_cnt+1, pack_pts_3/90000.0);

	      ++unit_pack_cnt_index;

	      //reset all video/audio information at this point - start

	      memset(ipipe->probe_info, 0, sizeof(ProbeInfo));
	      for(n=0; n<256; ++n) stream[n]=0;
	      for(n=0; n<TC_MAX_AUD_TRACKS; ++n) track[n]=attr[n]=0;
	      show_seq_info=0;
	      //reset - ends

	      ipipe->probe_info->unit_cnt=unit_pack_cnt_index;
	    }

	    ++unit_pack_cnt[unit_pack_cnt_index];
	    pack_pts_1 = pack_pts_2;

	    // get PPP - ends

	    if (tmp1 > end)
		goto copy;
	    buf = tmp1;

	    break;

	    //------------------------
	    //
	    // MPEG video
	    //
	    //------------------------

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

	    id = buf[3] &  0xff;

	    tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	    if (tmp2 > end)
		goto copy;
	    if ((buf[6] & 0xc0) == 0x80) {
		/* mpeg2 */
		tmp1 = buf + 9 + buf[8];

		++stream[id];

		mpeg_version=2;
		ipipe->probe_info->codec=TC_CODEC_MPEG2;

		// get pts time stamp:
		ac_memcpy(scan_buf, &buf[6], 16);
		has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);

		if(has_pts_dts) {

		  /*
		   I have no idea why the has_audio==0 is there. It seems to
		   cause problems at least for:
		   http://lists.exit1.org/pipermail/transcode-devel/2003-October/000004.html
		   I'll remove it until someone complains -- tibit
		   */
#if 0
		  if(ipipe->probe_info->pts_start==0 || has_audio==0) {
#else
		  if(ipipe->probe_info->pts_start==0) {
#endif
		    ipipe->probe_info->pts_start=(double)i_pts/90000.0;
		    initial_sync=1;
		  }

		  if(!show_seq_info) {

		    for(n=0; n<128; ++n) {

		      if(cmp_32_bits(buf+n, TC_MAGIC_M2V)) {
			probe_sequence(buf+n+4, ipipe->probe_info);
			show_seq_info=1;
		      }
		    }
		  } // probe sequence header
		}


		if(!show_ext_info) {

		  int ret_code=-1;
		  int bb=tmp2-tmp1;

		  if(bb<0 || bb>2048) bb=2048;

		  for(n=0; n<bb; ++n) {

		    if(cmp_32_bits(buf+n, TC_MAGIC_PICEXT)
		       && (buf[n+4]>>4)==8) {

		      ret_code = probe_extension(buf+n+4, ipipe->probe_info);

		      //ret_code
		      //-1 = invalid header
		      // 1 = (TFF=1,RFF=0)
		      // 0 else

		      if(ret_code==1) ++ipipe->probe_info->ext_attributes[0];
		      if(ret_code==0) ++ipipe->probe_info->ext_attributes[1];

		    }
		  } // probe extension header

		  ref_pts=i_pts;
		  ++seq_ctr;
		}

	    } else {
	      /* mpeg1 */

	      ++stream[id];

	      mpeg_version=1;

	      //MPEG1 may have audio but no time stamps
	      initial_sync=1;
	      ipipe->probe_info->codec=TC_CODEC_MPEG1;

	      if(!show_seq_info) {
		for(n=0; n<100; ++n) {

		  if(cmp_32_bits(buf+n, TC_MAGIC_M2V)) {
		    probe_sequence(buf+n+4, ipipe->probe_info);
		    show_seq_info=1;
		  }
		}
	      }

	      // get pts time stamp:
	      ac_memcpy(scan_buf, &buf[6], 16);
	      has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);

	      if(has_pts_dts) {

		if( ref_pts != 0 && i_pts < ref_pts) unit_ctr++;

		ref_pts=i_pts;

		++seq_ctr;

		if(ipipe->probe_info->pts_start==0 || has_audio==0) {
		  ipipe->probe_info->pts_start=(double)i_pts/90000.0;
		}

	      }

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

	    buf = tmp2;
	    break;


	    //----------------------------------
	    //
	    // private stream 1
	    //
	    //----------------------------------


	case 0xbd:

	    ++stream[id];

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

	    aid = *tmp1;

	    //-------------
	    //
	    //subtitle
	    //
	    //-------------

	    if((aid >= 0x20) && (aid <= 0x3F)) {

	      num=aid-0x20;

	      if(!(attr[num] & PACKAGE_SUBTITLE)) {

		if(!track[num]) {
		  ++ipipe->probe_info->num_tracks;
		  track[num]=1;
		  ipipe->probe_info->track[num].tid=num;
		}

		if(!(ipipe->probe_info->track[num].attribute &
		     PACKAGE_SUBTITLE)&& initial_sync) {
		  ipipe->probe_info->track[num].attribute |= PACKAGE_SUBTITLE;

		  // get pts time stamp:
		  ac_memcpy(scan_buf, &buf[6], 16);
		  has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);
		}
	      }
	    }

	    //-------------
	    //
	    //AC3 audio
	    //
	    //-------------

	    if(((aid >= 0x80 && aid <= 0x88)) || (aid>=0x90 && aid<=0x9f)) {
	      num=aid-0x80;

	      if(!(attr[num] & PACKAGE_AUDIO_AC3) && initial_sync) {

		if(!track[num]) {
		  ++ipipe->probe_info->num_tracks;
		  track[num]=1;
		  ipipe->probe_info->track[num].tid=num;
		}

		if(!(ipipe->probe_info->track[num].attribute &
		     PACKAGE_AUDIO_AC3)) {

		  tmp1 +=4;

		  //need to scan payload for more AC3 audio info
		  ret = buf_probe_ac3(tmp1, tmp2-tmp1, &ipipe->probe_info->track[num]);
		  if(ret==0) {
		    ipipe->probe_info->track[num].attribute |= PACKAGE_AUDIO_AC3;
		    ac_memcpy(scan_buf, &buf[6], 16);
		    has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);
		    ipipe->probe_info->track[num].pts_start=(double) i_pts/90000.;
		    has_audio=1;
		  }
		}
	      }
	    }

	    //-------------
	    //
	    // DTS audio
	    //
	    //-------------

	    if((aid >= 0x89) && (aid <= 0x8f)) {
	      num=aid-0x80;

	      if(!(attr[num] & PACKAGE_AUDIO_DTS) && initial_sync) {

		if(!track[num]) {
		  ++ipipe->probe_info->num_tracks;
		  track[num]=1;
		  ipipe->probe_info->track[num].tid=num;
		}

		if(!(ipipe->probe_info->track[num].attribute &
		     PACKAGE_AUDIO_DTS)) {

			tmp1+=4;

		  //need to scan payload for more DTS audio info
		  ipipe->probe_info->track[num].attribute |= PACKAGE_AUDIO_DTS;
		  buf_probe_dts(tmp1, tmp2-tmp1, &ipipe->probe_info->track[num]);

		  ac_memcpy(scan_buf, &buf[6], 16);
		  has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);
		  ipipe->probe_info->track[num].pts_start=(double) i_pts/90000.;
		  has_audio=1;
	      }
	    }
		}
	    //-------------
	    //
	    //AC3 audio
	    //
	    //-------------

	    if((aid >= 0x80) && (aid <= 0x88)) {
	      num=aid-0x80;

	      if(!(attr[num] & PACKAGE_AUDIO_AC3) && initial_sync) {

		if(!track[num]) {
		  ++ipipe->probe_info->num_tracks;
		  track[num]=1;
		  ipipe->probe_info->track[num].tid=num;
		}

		if(!(ipipe->probe_info->track[num].attribute &
		     PACKAGE_AUDIO_AC3)) {

		  tmp1 +=4;

		  //need to scan payload for more AC3 audio info
		  ret = buf_probe_ac3(tmp1, tmp2-tmp1, &ipipe->probe_info->track[num]);
		  if(ret==0) {
		    ipipe->probe_info->track[num].attribute |= PACKAGE_AUDIO_AC3;
		    ac_memcpy(scan_buf, &buf[6], 16);
		    has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);
		    ipipe->probe_info->track[num].pts_start=(double) i_pts/90000.;
		    has_audio=1;
		  }
		}
	      }
	    }

	    //-------------
	    //
	    //LPCM audio
	    //
	    //-------------

	    if((aid >= 0xA0) && (aid <= 0xBF)) {
	      num=aid-0xA0;

	      if(!(attr[num] & PACKAGE_AUDIO_PCM) && initial_sync) {

		if(!track[num]) {
		  ++ipipe->probe_info->num_tracks;
		  track[num]=1;
		  ipipe->probe_info->track[num].tid=num;
		}

		if(!(ipipe->probe_info->track[num].attribute &
		     PACKAGE_AUDIO_PCM)) {

		  tmp1 += 4;

		  ipipe->probe_info->track[num].attribute |= PACKAGE_AUDIO_PCM;

		  switch ((tmp1[1] >> 4) & 3) {
		  case 0: ipipe->probe_info->track[num].samplerate = 48000;
			  break;
		  case 1: ipipe->probe_info->track[num].samplerate = 96000;
			  break;
		  case 2: ipipe->probe_info->track[num].samplerate = 44100;
			  break;
		  case 3: ipipe->probe_info->track[num].samplerate = 32000;
			  break;
		  }
		  switch ((tmp1[1] >> 6) & 3) {
		  case 0: ipipe->probe_info->track[num].bits = 16;
			  break;
		  case 1: ipipe->probe_info->track[num].bits = 20;
			  break;
		  case 2: ipipe->probe_info->track[num].bits = 24;
			  break;
		  default: tc_log_error(__FILE__, "unknown LPCM quantization");
			  import_exit (1);
		  }
		  ipipe->probe_info->track[num].chan = 1 + (tmp1[1] & 7);
		  ipipe->probe_info->track[num].bitrate
		    = ipipe->probe_info->track[num].samplerate
		      * ipipe->probe_info->track[num].bits
		      * ipipe->probe_info->track[num].chan / 1000;
		  ipipe->probe_info->track[num].format=TC_CODEC_LPCM;

		  ac_memcpy(scan_buf, &buf[6], 16);
		  has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);
		  ipipe->probe_info->track[num].pts_start=(double) i_pts/90000.;
		  has_audio=1;
		  //ipipe->probe_info->track[num].pts_start=pack_ppp;
		}
	      }
	    }

	    //-------------
	    //
	    //VAG audio
	    //
	    //-------------

	    if(aid == 0xFF && memcmp(tmp1+4,"SShd",4) == 0) {
	      num=0;

	      if(!(attr[num] & PACKAGE_AUDIO_VAG) && initial_sync) {

		if(!track[num]) {
		  ++ipipe->probe_info->num_tracks;
		  track[num]=1;
		  ipipe->probe_info->track[num].tid=num;
		}

		if(!(ipipe->probe_info->track[num].attribute &
		     PACKAGE_AUDIO_VAG)) {

		  tmp1 += 4;  // skip MPEG data
		  tmp1 += 4;  // skip "SShd" header tag
		  tmp1 += 4;  // skip file (data+header) size (int32_le)
		  ipipe->probe_info->track[num].bits = *tmp1;
		  tmp1 += 4;
		  ipipe->probe_info->track[num].samplerate = tmp1[0]|tmp1[1]<<8;
		  tmp1 += 4;
		  ipipe->probe_info->track[num].chan = *tmp1;
		  tmp1 += 4;
		  // next 4 bytes are stereo quantization size
		  // next 8 bytes are unused?
		  // next 8 bytes are data block header "SSbd" and int32_le size

		  ipipe->probe_info->track[num].attribute |= PACKAGE_AUDIO_VAG;
		  ipipe->probe_info->track[num].bitrate
		    = ipipe->probe_info->track[num].samplerate
		      * ipipe->probe_info->track[num].chan
		      * 4        /* bits per sample, encoded */
		      * 16 / 14  /* overhead ratio */
                      / 1000;
		  ipipe->probe_info->track[num].format = TC_CODEC_VAG;

		  ac_memcpy(scan_buf, &buf[6], 16);
		  has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);
		  ipipe->probe_info->track[num].pts_start=(double) i_pts/90000.;
		  has_audio=1;
		  //ipipe->probe_info->track[num].pts_start=pack_ppp;
		}
	      }
	    }

	    buf = tmp2;

	    break;


	    //------------------------
	    //
	    // MPEG audio
	    //
	    //------------------------

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

	  ++stream[id];

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

	  //add found track
	  //need to scan payload for more MPEG audio info

	  num=(buf[3] & 0xff) - 0xc0;

	  if(num >= 0 && !track[num] && num<TC_MAX_AUD_TRACKS && initial_sync) {

	    ++ipipe->probe_info->num_tracks;

#ifdef HAVE_LAME
	    //need to scan payload for more MPEG audio info
	    if(end-buf>0) buf_probe_mp3(buf, end-buf, &ipipe->probe_info->track[num]);
#else
	    //all we know for now
	    ipipe->probe_info->track[num].format=CODEC_MP3;
	    ipipe->probe_info->track[num].tid=num;
#endif

	    ac_memcpy(scan_buf, &buf[6], 16);
	    has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);

	    if(has_pts_dts) {
	      ipipe->probe_info->track[num].pts_start=(double) i_pts/90000.;
	      track[num]=1;
	    }

	    has_audio=1;
	  }

	  buf = tmp2;
	  break;

	case 0xb3:

	  //MPEG video ES
	  probe_sequence(&buf[4], ipipe->probe_info);

	  ipipe->probe_info->codec=TC_CODEC_MPEG;
	  if ((buf[6] & 0xc0) == 0x80) ipipe->probe_info->codec=TC_CODEC_MPEG2;

	  return;
	  break;

	default:
	  if (buf[3] < 0xb9) {
	    tc_log_warn(__FILE__, "looks like an elementary stream - not program stream");
	    ipipe->probe_info->codec=TC_CODEC_MPEG;
	    if ((buf[6] & 0xc0) == 0x80) ipipe->probe_info->codec=TC_CODEC_MPEG2;
	    return;
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

    adjust_info(ipipe);
    return;
}
