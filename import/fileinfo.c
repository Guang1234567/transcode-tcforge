/*
 *  fileinfo.c
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "libtc/libtc.h"
#include "libtcutil/xio.h"
#include "ioaux.h"
#include "tc.h"

#ifdef HAVE_LIBDV
#include <libdv/dv.h>
#endif

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

/* forward declaration */ 
static int scan_header_dv(const char *buf);

unsigned char asfhdrguid[16]={0x30,0x26,0xB2,0x75,0x8E,0x66,0xCF,0x11,0xA6,0xD9,0x00,0xAA,0x00,0x62,0xCE,0x6C};

unsigned char mxfmagic[]={0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01};

unsigned char zero_pad[4]={0,0,0,0};

static int cmp_32_bits(char *buf, long x)
{

  if(0) {
    tc_log_msg(__FILE__, "MAGIC: 0x%02lx 0x%02lx 0x%02lx 0x%02lx %s", (x >> 24) & 0xff, ((x >> 16) & 0xff), ((x >>  8) & 0xff), ((x      ) & 0xff), filetype(x));
    tc_log_msg(__FILE__, " FILE: 0x%02x 0x%02x 0x%02x 0x%02x", buf[0] & 0xff, buf[1] & 0xff, buf[2] & 0xff, buf[3] & 0xff);
  }

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

static int cmp_28_bits(char *buf, long x)
{

  if(0) {
    tc_log_msg(__FILE__, "MAGIC: 0x%02lx 0x%02lx 0x%02lx 0x%02lx %s", (x >> 24) & 0xff, ((x >> 16) & 0xff), ((x >>  8) & 0xff), ((x      ) & 0xff), filetype(x));
    tc_log_msg(__FILE__, " FILE: 0x%02x 0x%02x 0x%02x 0x%02x", buf[0] & 0xff, buf[1] & 0xff, buf[2] & 0xff, buf[3] & 0xff);
  }

  if ((uint8_t)buf[0] != ((x >> 24) & 0xff))
    return 0;
  if ((uint8_t)buf[1] != ((x >> 16) & 0xff))
    return 0;
  if ((uint8_t)buf[2] != ((x >>  8) & 0xff))
    return 0;
  if ((uint8_t)(buf[3] & 0xf0) != ((x      ) & 0xff))
    return 0;

  // OK found it
  return 1;
}


static int cmp_16_bits(char *buf, long x)
{

  int16_t sync_word=0;

  if(0) {
    tc_log_msg(__FILE__, "MAGIC: 0x%02lx 0x%02lx 0x%02lx 0x%02lx %s", (x >> 24) & 0xff, ((x >> 16) & 0xff), ((x >>  8) & 0xff), ((x      ) & 0xff), filetype(x));
    tc_log_msg(__FILE__, " FILE: 0x%02x 0x%02x 0x%02x 0x%02x", buf[0] & 0xff, buf[1] & 0xff, buf[2] & 0xff, buf[3] & 0xff);
  }

  sync_word = (sync_word << 8) + (uint8_t) buf[0];
  sync_word = (sync_word << 8) + (uint8_t) buf[1];

  if(sync_word == (int16_t) x) return 1;

  // not found;
  return 0;
}

static int save_read(char *buf, int bytes, off_t offset, int fdes)
{

  // returns 0 if ok, 1 on failure to read first bytes

  // rewind
  if(xio_lseek(fdes, offset, SEEK_SET)<0) {
    tc_log_warn(__FILE__, "[%s:%d] file seek error: %s",
		__FILE__, __LINE__, strerror(errno));
    return(1);
  }

  if(xio_read(fdes, buf, bytes)<bytes) {
    tc_log_warn(__FILE__, "[%s:%d] file read error: %s",
		__FILE__, __LINE__, strerror(errno));
    return(1);
  }

  return(0);
}

#define MAX_PROBE_BYTES 4096

long fileinfo(int fdes, int skip)
{

  char buf[MAX_PROBE_BYTES];

  off_t off=0;

  int cc=0;

  long id=TC_MAGIC_UNKNOWN, offset;

  // assume this is a valid file descriptor

  // are we at offset defined by skip?
  if((offset = xio_lseek(fdes, skip, SEEK_CUR)) < 0) {
    if(errno==ESPIPE) return(TC_MAGIC_PIPE);
    return(TC_MAGIC_ERROR);
  }

  // refuse to work with a file not at offset 0
  if(offset != skip) {
    tc_log_error(__FILE__, "file pointer not at requested offset %d - exit", skip);
    return(TC_MAGIC_ERROR);
  }

  off +=skip;

  /* -------------------------------------------------------------------
   *
   * zero padding detection
   *
   *-------------------------------------------------------------------*/

  if(save_read(buf, 4, off, fdes)) goto exit;

  while(memcmp(buf, zero_pad, 4)==0) {
    off +=4;  //preserves byte order
    if(off> TC_MAX_SEEK_BYTES) goto exit;
    if(save_read(buf, 4, off, fdes)) goto exit;
  }

  if(off<0) goto exit;

  //tc_log_msg(__FILE__, "off=%d '%c' '%c' '%c' '%c'", off, buf[0], buf[1], buf[2], buf[3]);


  /* -------------------------------------------------------------------
   *
   * 2 byte section, read 4 bytes
   *
   *-------------------------------------------------------------------*/

  if(save_read(buf, 4, off, fdes)) goto exit;

  // AC3

  if(cmp_16_bits(buf, TC_MAGIC_AC3)) {
    id = TC_MAGIC_AC3;
    goto exit;
  }

  // MP3 audio

  if(cmp_16_bits(buf, TC_MAGIC_MP3)) {
    id = TC_MAGIC_MP3;
    goto exit;
  }

  if(cmp_16_bits(buf, TC_MAGIC_MP3_2_5)) {
    id = TC_MAGIC_MP3_2_5;
    goto exit;
  }

  if(cmp_16_bits(buf, TC_MAGIC_MP3_2)) {
    id = TC_MAGIC_MP3_2;
    goto exit;
  }

  // MP2 audio

  if(cmp_16_bits(buf, TC_MAGIC_MP2) || cmp_16_bits(buf, TC_MAGIC_MP2_FC)) {
    id = TC_MAGIC_MP2;
    goto exit;
  }

  if ( ((((buf[0]<<8)&0xff00)|buf[1])&0xfff8) == 0xfff0) {
      if ( (buf[1]&0x02) == 0x02) {
	  id = TC_MAGIC_MP3;
	  goto exit;
      }
      if ( (buf[1]&0x01) == 0x01) {
	  id = TC_MAGIC_MP2;
	  goto exit;
      }
  }

  // TIFF image

  if (cmp_16_bits(buf, TC_MAGIC_TIFF1)) {
    id = TC_MAGIC_TIFF1;
    goto exit;
  }
  if (cmp_16_bits(buf, TC_MAGIC_TIFF2)) {
    id = TC_MAGIC_TIFF2;
    goto exit;
  }

  // BMP image

  if (cmp_16_bits(buf, TC_MAGIC_BMP)) {
    id = TC_MAGIC_BMP;
    goto exit;
  }

  // SGI image

  if (cmp_16_bits(buf, TC_MAGIC_SGI)) {
    id = TC_MAGIC_SGI;
    goto exit;
  }

  // PPM image

  if (strncmp (buf, "P6", 2)==0) {
      id = TC_MAGIC_PPM;
      goto exit;
  }

  // PGM image

  if (strncmp (buf, "P5", 2)==0) {
      id = TC_MAGIC_PGM;
      goto exit;
  }

  // SGI image

  if (cmp_16_bits(buf, TC_MAGIC_SGI)) {
      id = TC_MAGIC_SGI;
      goto exit;
  }


  // transport stream

  if (buf[0] == (uint8_t) TC_MAGIC_TS) {
    id = TC_MAGIC_TS;
    goto exit;
  }


  /* -------------------------------------------------------------------
   *
   * 4 byte section
   *
   *-------------------------------------------------------------------*/

  if(save_read(buf, 4, off, fdes)) goto exit;


  // DTS

  if(cmp_32_bits(buf, TC_MAGIC_DTS)) {
    id = TC_MAGIC_DTS;
    goto exit;
  }

  // VOB

  if(cmp_32_bits(buf, TC_MAGIC_VOB)) {
    id = TC_MAGIC_VOB;
    goto exit;
  }

  // MPEG Video / .VDR

  if(cmp_28_bits(buf, TC_MAGIC_MPEG)) {
    id = TC_MAGIC_MPEG; /* FIXME: it's PES? */
    goto exit;
  }

  // DV

  if(cmp_32_bits(buf, TC_MAGIC_DV_NTSC)) {
    id = TC_MAGIC_DV_NTSC;
    goto exit;
  }

  // DV

  if(cmp_32_bits(buf, TC_MAGIC_DV_PAL)) {
    id = TC_MAGIC_DV_PAL;
    goto exit;
  }

  // OGG stream

  if (strncmp (buf, "OggS", 4)==0) {
    id = TC_MAGIC_OGG;
    goto exit;
  }

  // M2V

  if(cmp_32_bits(buf, TC_MAGIC_M2V)) {
    id = TC_MAGIC_MPEG_ES;
    goto exit;
  }

  // NUV

  if(cmp_32_bits(buf, TC_MAGIC_NUV)) {
    id = TC_MAGIC_NUV;
    goto exit;
  }

  // OGG

  if (strncasecmp(buf, "OggS", 4) == 0) {
    id = TC_MAGIC_OGG;
    goto exit;
  }

  // Real Media
  if(strncasecmp(buf,".RMF", 4)==0) {
    id = TC_MAGIC_RMF;
    goto exit;
  }

  // PV3
  if (memcmp(buf, "PV3\1", 4) == 0 || memcmp(buf, "PV3\2", 4) == 0) {
    id = TC_MAGIC_PV3;
    goto exit;
  }

  // PVN
  if (buf[0]=='P' && buf[1]=='V'
   && (buf[2]>='4' && buf[2]<='6')
   && (buf[3]=='a' || buf[3]=='b' || buf[3]=='d' || buf[3]=='f')
  ) {
    id = TC_MAGIC_PVN;
    goto exit;
  }


  // MP3 audio + odd 0 padding

  if(cmp_16_bits(buf+1, TC_MAGIC_MP3)) {
    id = TC_MAGIC_MP3;
    goto exit;
  }

  if(cmp_16_bits(buf+1, TC_MAGIC_MP3_2_5)) {
    id = TC_MAGIC_MP3_2_5;
    goto exit;
  }

  if(cmp_16_bits(buf+1, TC_MAGIC_MP3_2)) {
    id = TC_MAGIC_MP3_2;
    goto exit;
  }

  if(cmp_16_bits(buf+2, TC_MAGIC_MP3)) {
    id = TC_MAGIC_MP3;
    goto exit;
  }

  if(cmp_16_bits(buf+2, TC_MAGIC_MP3_2_5)) {
    id = TC_MAGIC_MP3_2_5;
    goto exit;
  }

  if(cmp_16_bits(buf+2, TC_MAGIC_MP3_2)) {
    id = TC_MAGIC_MP3_2;
    goto exit;
  }

  if(cmp_32_bits(buf, TC_MAGIC_ID3)) {
    id = TC_MAGIC_ID3;
    goto exit;
  }

  // iTunes sets an ID3 header that way at the beginning. We search for an
  // syncword first so it should just work.
  if (buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3' && buf[3] == 0x02) {
    id = TC_MAGIC_MP3;
    goto exit;
  }

  /* -------------------------------------------------------------------
   *
   * 8 byte section
   *
   *-------------------------------------------------------------------*/

  if(save_read(buf, 8, off, fdes)) goto exit;

  // YUV4MPEG

  if (strncmp (buf, "YUV4MPEG", 8)==0) {
    id = TC_MAGIC_YUV4MPEG;
    goto exit;
  }

  // BSDAV

  if (strncmp (buf, "BSDAV", 5)==0) {
    id = TC_MAGIC_BSDAV;
    goto exit;
  }

  // MOV

  if(strncasecmp(buf+4,"moov", 4) ==0 ||
     strncasecmp(buf+4,"cmov", 4) ==0 ||
     strncasecmp(buf+4,"mdat", 4) ==0 ||
     strncasecmp(buf+4,"ftyp", 4) ==0 ||
     strncasecmp(buf+4,"pnot", 4) ==0) {
    id = TC_MAGIC_MOV;
    goto exit;
  }

  // PNG

  if (cmp_32_bits(buf, TC_MAGIC_PNG) &&
      cmp_32_bits(buf+4, 0x0D0A1A0A)) {
    id = TC_MAGIC_PNG;
    goto exit;
  }

  // GIF

  if (strncasecmp(buf, "GIF87a", 6) == 0 ||
      strncasecmp(buf, "GIF89a", 6) == 0) {
    id = TC_MAGIC_GIF;
    goto exit;
  }

  // XML

  if(strncasecmp(buf,"<?xml", 5) ==0) {
    id = TC_MAGIC_XML;
    goto exit;
  }

  // vncrec log file

  if(strncasecmp(buf,"vncLog",6) ==0 ) {
    id = TC_MAGIC_VNC;
    goto exit;
  }

  if (buf[0] == 'F' && buf[1] == 'L' && buf[2] == 'V' && buf[5]==0) {
    id = TC_MAGIC_FLV;
    goto exit;
  }

  /* -------------------------------------------------------------------
   *
   * 12 byte section
   *
   *-------------------------------------------------------------------*/


  if(save_read(buf, 12, off, fdes)) goto exit;

  // YUV4MPEG2

  if (strncmp (buf, "YUV4MPEG2", 9)==0) {
    id = TC_MAGIC_YUV4MPEG;
    goto exit;
  }

  // AVI
  if(strncasecmp(buf  ,"RIFF",4) ==0 &&
     strncasecmp(buf+8,"AVI ",4) ==0 ) {
    id = TC_MAGIC_AVI;
    goto exit;
  }

  // JPEG
  if (cmp_32_bits(buf, TC_MAGIC_JPEG) &&
      strncasecmp(buf+6, "JFIF", 4) == 0) {
    id = TC_MAGIC_JPEG;
    goto exit;
  }
  if (cmp_16_bits(buf, 0xFFD8)) {
    id = TC_MAGIC_JPEG;
    goto exit;
  }

  // WAVE
  if(strncasecmp(buf  ,"RIFF",4) ==0 &&
     strncasecmp(buf+8,"WAVE",4) ==0 ) {
    id = TC_MAGIC_WAV;
    goto exit;
  }

  // CDXA
  if(strncasecmp(buf  ,"RIFF",4) ==0 &&
     strncasecmp(buf+8,"CDXA",4) ==0 ) {
    id = TC_MAGIC_CDXA;
    goto exit;
  }


  /* -------------------------------------------------------------------
   *
   * 16 byte section
   *
   *-------------------------------------------------------------------*/

  if(save_read(buf, 16, off, fdes)) goto exit;

  //ASF
  if(memcmp(asfhdrguid,buf,16)==0) {
    id = TC_MAGIC_ASF;
    goto exit;
  }

  //MXF
  if(memcmp(mxfmagic,buf,sizeof(mxfmagic))==0) {
    id = TC_MAGIC_MXF;
    goto exit;
  }

  // MOV

  if(strncasecmp(buf+12,"moov", 4) ==0 ||
     strncasecmp(buf+12,"cmov", 4) ==0 ||
     strncasecmp(buf+12,"mdat", 4) ==0 ||
     strncasecmp(buf+12,"pnot", 4) ==0) {
    id = TC_MAGIC_MOV;
    goto exit;
  }

  /* -------------------------------------------------------------------
   *
   * more tests
   *
   *-------------------------------------------------------------------*/

  if(save_read(buf, MAX_PROBE_BYTES, off, fdes)) goto exit;

  //DV
  cc=scan_header_dv(buf);

  if(cc==1) {
      id = TC_MAGIC_DV_PAL;
      goto exit;
  }

  if(cc==2) {
      id = TC_MAGIC_DV_NTSC;
      goto exit;
  }

  /* -------------------------------------------------------------------
   *
   * exit
   *
   *-------------------------------------------------------------------*/

 exit:
  // reset file pointer
  xio_lseek(fdes, 0, SEEK_SET);
  return(id);
}

long streaminfo(int fdes)
{

  char buf[64];

  long id=TC_MAGIC_UNKNOWN;

  // assume this is a valid file descriptor

  int bytes=16, ret=0;

  if( (ret = tc_pread(fdes, buf, bytes))<bytes) {
    if (ret)
      tc_log_error(__FILE__, "File too short (must be 16 bytes at least)");
    else
      tc_log_error(__FILE__, "stream read error: %s", strerror(errno));
    return(TC_MAGIC_ERROR);
  }

  /* -------------------------------------------------------------------
   *
   * 2 byte section
   *
   *-------------------------------------------------------------------*/

  // AC3

  if(cmp_16_bits(buf, TC_MAGIC_AC3)) {
    id = TC_MAGIC_AC3;
    goto exit;
  }

  // MPEG audio

  if(cmp_16_bits(buf, TC_MAGIC_MP3)) {
    id = TC_MAGIC_MP3;
    goto exit;
  }

  if(cmp_16_bits(buf, TC_MAGIC_MP3_2_5)) {
    id = TC_MAGIC_MP3_2_5;
    goto exit;
  }

  if(cmp_16_bits(buf, TC_MAGIC_MP3_2)) {
    id = TC_MAGIC_MP3_2;
    goto exit;
  }

  // transport stream

  if (buf[0] == (uint8_t) TC_MAGIC_TS) {
    id = TC_MAGIC_TS;
    goto exit;
  }

  /* -------------------------------------------------------------------
   *
   * 4 byte section
   *
   *-------------------------------------------------------------------*/

  // DTS

  if(cmp_32_bits(buf, TC_MAGIC_DTS)) {
    id = TC_MAGIC_DTS;
    goto exit;
  }

  // VOB

  if(cmp_32_bits(buf, TC_MAGIC_VOB)) {
    id = TC_MAGIC_VOB;
    goto exit;
  }

 // DV

  if(cmp_32_bits(buf, TC_MAGIC_DV_NTSC)) {
    id = TC_MAGIC_DV_NTSC;
    goto exit;
  }

  // DV

  if(cmp_32_bits(buf, TC_MAGIC_DV_PAL)) {
    id = TC_MAGIC_DV_PAL;
    goto exit;
  }

  // M2V

  if(cmp_32_bits(buf, TC_MAGIC_M2V)) {
    id = TC_MAGIC_MPEG_ES;
    goto exit;
  }

  // MPEG Video / .VDR

  if(cmp_32_bits(buf, TC_MAGIC_MPEG)) {
    id = TC_MAGIC_MPEG; /* FIXME: it's PES? */
    goto exit;
  }

  // NUV

  if(cmp_32_bits(buf, TC_MAGIC_NUV)) {
    id = TC_MAGIC_NUV;
    goto exit;
  }

 // MP3 audio + odd 0 padding

  if(cmp_16_bits(buf+1, TC_MAGIC_MP3)) {
    id = TC_MAGIC_MP3;
    goto exit;
  }

  if(cmp_16_bits(buf+1, TC_MAGIC_MP3_2_5)) {
    id = TC_MAGIC_MP3_2_5;
    goto exit;
  }

  if(cmp_16_bits(buf+1, TC_MAGIC_MP3_2)) {
    id = TC_MAGIC_MP3_2;
    goto exit;
  }

  if(cmp_16_bits(buf+2, TC_MAGIC_MP3)) {
    id = TC_MAGIC_MP3;
    goto exit;
  }

  if(cmp_16_bits(buf+2, TC_MAGIC_MP3_2_5)) {
    id = TC_MAGIC_MP3_2_5;
    goto exit;
  }

  if(cmp_16_bits(buf+2, TC_MAGIC_MP3_2)) {
    id = TC_MAGIC_MP3_2;
    goto exit;
  }

  // transport stream

  if (cmp_16_bits(buf, TC_MAGIC_TS)) {
    id = TC_MAGIC_TS;
    goto exit;
  }


  /* -------------------------------------------------------------------
   *
   * 8 byte section
   *
   *-------------------------------------------------------------------*/

  // YUV4MPEG

  if (strncmp (buf, "YUV4MPEG", 8)==0) {
    id = TC_MAGIC_YUV4MPEG;
    goto exit;
  }

  // MOV

  if(strncasecmp(buf+4,"moov", 4) ==0 ||
     strncasecmp(buf+4,"cmov", 4) ==0 ||
     strncasecmp(buf+4,"mdat", 4) ==0 ||
     strncasecmp(buf+4,"pnot", 4) ==0) {
    id = TC_MAGIC_MOV;
    goto exit;
  }

  if (buf[0] == 'F' && buf[1] == 'L' && buf[2] == 'V' && buf[5]==0) {
    id = TC_MAGIC_FLV;
    goto exit;
  }

  /* -------------------------------------------------------------------
   *
   * 12 byte section
   *
   *-------------------------------------------------------------------*/

  // WAVE
  if(strncasecmp(buf  ,"RIFF",4) ==0 &&
     strncasecmp(buf+8,"WAVE",4) ==0 ) {
    id = TC_MAGIC_WAV;
    goto exit;
  }

  // OGG

  if (strncasecmp(buf, "OggS", 4) == 0) {
    id = TC_MAGIC_OGG;
    goto exit;
  }

  /* -------------------------------------------------------------------
   *
   * 16 byte section
   *
   *-------------------------------------------------------------------*/

  if(memcmp(asfhdrguid,buf,16)==0) {
    id = TC_MAGIC_ASF;
    goto exit;
  }

  //MXF
  if(memcmp(mxfmagic,buf,sizeof(mxfmagic))==0) {
    id = TC_MAGIC_MXF;
    goto exit;
  }

  /* -------------------------------------------------------------------
   *
   * exit
   *
   *-------------------------------------------------------------------*/

 exit:

  return(id);
}

const char *filetype(uint32_t magic)
{

  switch(magic) {

  case TC_MAGIC_TS:           return("MPEG transport stream (TS)");
  case TC_MAGIC_MPEG_PS:      /* fallthrough */
  case TC_MAGIC_VOB:          return("MPEG program stream (PS)");
  case TC_MAGIC_MPEG_ES:      /* fallthrough */
  case TC_MAGIC_M2V:          return("MPEG elementary stream (ES)");
  case TC_MAGIC_MPEG_PES:     /* fallthrough */
  case TC_MAGIC_MPEG:         return("MPEG packetized elementary stream (PES)");
  case TC_MAGIC_AVI:          return("RIFF data, AVI video");
  case TC_MAGIC_WAV:          return("RIFF data, WAVE audio");
  case TC_MAGIC_CDXA:         return("RIFF data, CDXA");
  case TC_MAGIC_MOV:          return("Apple QuickTime movie file");
  case TC_MAGIC_ASF:          return("advanced streaming format ASF");
  case TC_MAGIC_TIFF1:
  case TC_MAGIC_TIFF2:        return("TIFF image");
  case TC_MAGIC_JPEG:         return("JPEG image");
  case TC_MAGIC_BMP:          return("BMP image");
  case TC_MAGIC_PNG:          return("PNG image");
  case TC_MAGIC_GIF:          return("GIF image");
  case TC_MAGIC_PPM:          return("PPM image");
  case TC_MAGIC_PGM:          return("PGM image");
  case TC_MAGIC_SGI:          return("SGI image");
  case TC_MAGIC_RMF:          return("Real Media");
  case TC_MAGIC_XML:          return("XML file, need to analyze the content");
  case TC_MAGIC_MXF:          return("The Material eXchange Format");
  case TC_MAGIC_OGG:          return("OGG Multimedia Container");

  case TC_MAGIC_RAW:          return("RAW stream");
  case TC_MAGIC_AC3:          return("AC3 stream");
  case TC_MAGIC_DTS:          return("DTS stream");
  case TC_MAGIC_MP3:          return("MPEG-1 layer-3 stream");
  case TC_MAGIC_MP3_2:        return("MPEG-2 layer-3 stream");
  case TC_MAGIC_MP3_2_5:      return("MPEG-2.5 layer-3 stream");
  case TC_MAGIC_MP2:          return("MP2 stream");
  case TC_MAGIC_ID3:          return("MPEG audio ID3 tag");

  case TC_MAGIC_DV_NTSC:      return("Digital Video (NTSC)");
  case TC_MAGIC_DV_PAL:       return("Digital Video (PAL)");
  case TC_MAGIC_DVD:          return("DVD image/device");
  case TC_MAGIC_DVD_PAL:      return("PAL DVD image/device");
  case TC_MAGIC_DVD_NTSC:     return("NTSC DVD image/device");
  case TC_MAGIC_YUV4MPEG:     return("YUV4MPEG stream");
  case TC_MAGIC_NUV:          return("NuppelVideo stream");
  case TC_MAGIC_VNC:          return("VNCrec logfile");
  case TC_MAGIC_PV3:          return("PV3 video");
  case TC_MAGIC_FLV:          return("FLV stream");

  case TC_MAGIC_V4L_AUDIO:    return("V4L audio device");
  case TC_MAGIC_V4L_VIDEO:    return("V4L video device");
  case TC_MAGIC_BKTR_VIDEO:   return("bktr video device");
  case TC_MAGIC_SUNAU_AUDIO:  return("sunau audio device");
  case TC_MAGIC_BSDAV:        return("bsdav stream");
  case TC_MAGIC_OSS_AUDIO:    return("OSS audio device");
  case TC_MAGIC_PIPE:         return("pipe/fifo (not seekable)");
  case TC_MAGIC_MPLAYER:      return("probed by mplayer");
  case TC_MAGIC_X11:          return("X11 display source");
  case TC_MAGIC_ERROR:        return("error");
  case TC_MAGIC_UNKNOWN:
  default:                    return("unknown file type");
  }
}

static int scan_header_dv(const char *buf)
{
    int cc = -1;
#ifdef HAVE_LIBDV
    dv_decoder_t *dv_decoder = dv_decoder_new(TRUE, FALSE, FALSE);

    // Initialize DV decoder

    if (dv_decoder == NULL) {
    	tc_log_error(__FILE__, "dv decoder init failed");
	    return(-1);
    }

    dv_decoder->prev_frame_decoded = 0;
    cc = dv_parse_header(dv_decoder, buf);

    dv_decoder_free(dv_decoder);
#endif
    return(cc);
}

