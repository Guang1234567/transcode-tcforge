/*
 *  aux_pes.c
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
#include "libtc/libtc.h"
#include "ioaux.h"
#include "aux_pes.h"


static char * aspect_ratio_information_str[16] = {
  "Invalid Aspect Ratio",
  "1:1",
  "4:3",
  "16:9",
  "2.21:1",
  "Invalid Aspect Ratio",
  "Invalid Aspect Ratio",
  "Invalid Aspect Ratio",
  "4:3",
  "Invalid Aspect Ratio",
  "Invalid Aspect Ratio",
  "4:3",
  "Invalid Aspect Ratio",
  "Invalid Aspect Ratio",
  "Invalid Aspect Ratio"
};

static char * frame_rate_str[16] = {
  "Invalid frame_rate_code",
  "23.976", "24", "25" , "29.97",
  "30" , "50", "59.94", "60" ,
  "1", "5", "10", "12", "15",   //libmpeg3 only
  "Invalid frame_rate_code",
  "Invalid frame_rate_code"
};


int stats_sequence_silent(uint8_t * buffer, seq_info_t *seq_info)
{

  int horizontal_size;
  int vertical_size;
  int aspect_ratio_information;
  int frame_rate_code;
  int bit_rate_value;

  vertical_size = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
  horizontal_size = ((vertical_size >> 12) + 15) & ~15;
  vertical_size = ((vertical_size & 0xfff) + 15) & ~15;

  aspect_ratio_information = buffer[3] >> 4;
  frame_rate_code = buffer[3] & 15;
  bit_rate_value = (buffer[4] << 10) | (buffer[5] << 2) | (buffer[6] >> 6);
  if(aspect_ratio_information < 0 || aspect_ratio_information>15) {
    tc_log_error(__FILE__, "****** invalid MPEG sequence header detected (%d/%d|%d/%d) ******",
		 aspect_ratio_information, 16, frame_rate_code, 16);
    return(-1);
  }

  if(frame_rate_code < 0 || frame_rate_code>15) {
    tc_log_error(__FILE__, "****** invalid MPEG sequence header detected (%d/%d|%d/%d) ******",
		 frame_rate_code, 16, aspect_ratio_information, 8);
    return(-1);
  }

  //fill out user structure

  seq_info->w = horizontal_size;
  seq_info->h = vertical_size;
  seq_info->ari = aspect_ratio_information;
  seq_info->frc = frame_rate_code;
  seq_info->brv = bit_rate_value;

  return(0);

}
int stats_sequence(uint8_t * buffer, seq_info_t *seq_info)
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

  if(aspect_ratio_information < 0 || aspect_ratio_information>15) {
    tc_log_error(__FILE__, "****** invalid MPEG sequence header detected (%d/%d|%d/%d) ******",
		 aspect_ratio_information, 16, frame_rate_code, 16);
    return(-1);
  }

  if(frame_rate_code < 0 || frame_rate_code>15) {
    tc_log_error(__FILE__, "****** invalid MPEG sequence header detected (%d/%d|%d/%d) ******",
		 frame_rate_code, 16, aspect_ratio_information, 8);
    return(-1);
  }

  tc_log_msg(__FILE__,
	     "sequence: %dx%d %s, %s fps, %5.0f kbps, VBV %d kB%s%s%s",
	     horizontal_size, vertical_size,
	     aspect_ratio_information_str [aspect_ratio_information],
	     frame_rate_str [frame_rate_code],
	     bit_rate_value * 400.0 / 1000.0,
	     2 * vbv_buffer_size_value,
	     constrained_parameters_flag ? " , CP":"",
	     load_intra_quantizer_matrix ? " , Custom Intra Matrix":"",
	     load_non_intra_quantizer_matrix ? " , Custom Non-Intra Matrix":"");


  //fill out user structure

  seq_info->w = horizontal_size;
  seq_info->h = vertical_size;
  seq_info->ari = aspect_ratio_information;
  seq_info->frc = frame_rate_code;
  seq_info->brv = bit_rate_value;

  return(0);

}

int get_pts_dts(char *buffer, unsigned long *pts, unsigned long *dts)
{
  unsigned int pes_header_bytes = 0;
  unsigned int pts_dts_flags;
  int pes_header_data_length;

  int has_pts_dts=0;

  unsigned int ptr=0;

  /* drop first 8 bits */
  ++ptr;
  pts_dts_flags = (buffer[ptr++] >> 6) & 0x3;
  pes_header_data_length = buffer[ptr++];

  switch(pts_dts_flags)

    {

    case 2:

      *pts = (buffer[ptr++] >> 1) & 7;  //low 4 bits (7==1111)
      *pts <<= 15;
      *pts |= (stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;
      *pts <<= 15;
      *pts |= (stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;

      pes_header_bytes += 5;

      has_pts_dts=1;

      break;

    case 3:

      *pts = (buffer[ptr++] >> 1) & 7;  //low 4 bits (7==1111)
      *pts <<= 15;
      *pts |= (stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;
      *pts <<= 15;
      *pts |= (stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;

      *dts = (buffer[ptr++] >> 1) & 7;
      *dts <<= 15;
      *dts |= (stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;
      *dts <<= 15;
      *dts |= (stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;

      pes_header_bytes += 10;

      has_pts_dts=1;

      break;

    default:

      has_pts_dts=0;
      *dts=*pts=0;
      break;
    }

  return(has_pts_dts);
}
