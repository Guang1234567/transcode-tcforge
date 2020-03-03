/*
 *  sub_proc.c
 *
 *  Copyright (C) Thomas Oestreich - February 2002
 *
 *  code based on ExtSub - A subtitle extractor for VOB files Version 1.00
 *  by Sham Gardner (risctaker@risctaker.de)
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

#include "subproc.h"

#define MAXDATA (1024*100)

//#define DEBUG(args...) tc_log_msg(__FILE__ , ## args)
//#define NDEBUG(args...) tc_log_message(__FILE__ , ## args)
#define ERROR(args...) tc_log_error(__FILE__ , ## args)

#define NDEBUG(args...)
#define DEBUG(args...)

typedef struct
{
  unsigned int last ;
  unsigned int forcedisplay ; // flag

  unsigned int startdisplay ; // flag } only one of these
  unsigned int stopdisplay ;  // flag } should be set

  unsigned int time ; // in centiseconds?
  struct
  {
    unsigned char colour[4] ; // palette entries
    unsigned  int used ; //flag
  } palette ;
  struct
  {
    unsigned char colour[4] ;
    unsigned  int used ;
  } alpha ; // alpha channel info
  struct
  {
    unsigned int x0, y0, x1, y1 ;
    unsigned int size[2] ;
    unsigned int used ;
  } dimensions ;
  struct
  {
    unsigned int line0, line1 ; // graphics are interlaced
    unsigned int used ;
  } linestart ;
} parsed_ctrl_sequence ;

static struct
{
  FILE *convertscript ;
  char *subprefix ;
  int subtitles ;
  unsigned short id ; // which subtitle stream to extract (max 5 bits (31))

  sub_info_t sub;

} config ;

static int counter=0 ;

static unsigned short read_short(unsigned char *data)
{
  return (data[0] << 8) | data[1] ;
}


static unsigned int read_nibble(unsigned char *data, unsigned int offset/*in nibbles*/)
{
  if (offset & 1)
  {
    //    NDEBUG("Got nibble %x from offset %d (%x) odd\n", (data[offset/2] & 0x0F), offset, data[offset/2]) ;
    return (data[offset/2] & 0x0F) ;
  }
  else
  {
    //NDEBUG("Got nibble %x from offset %d (%x) even\n", (data[offset/2] & 0xF0) >> 4, offset, data[offset/2]) ;
    return (data[offset/2] & 0xF0) >> 4 ;
  }
}

#if 0 // UNUSED, EMS
static void show_nibbles(unsigned char *data, unsigned int offset, unsigned int number, FILE *stream)
{
  int n ;
  for (n=0; n <number; n++)
  {
    fprintf(stream, "%x", read_nibble(data, offset+n)) ;
  }
}
#endif
/*
static void display_ctrl_sequence(FILE *file, parsed_ctrl_sequence *seq)
{
  int n=0 ;
  do
  {
    fprintf(file, "  sequence: %d\n", n) ;
    fprintf(file, "  time:  %d\n", seq[n].time) ;
    fprintf(file, "  start: %d\n", seq[n].startdisplay) ;
    fprintf(file, "  stop:  %d\n", seq[n].stopdisplay) ;
    if (seq[n].palette.used)
    {
      fprintf(file, "  palette: %d, %d, %d, %d\n", seq[n].palette.colour[0],
                                                   seq[n].palette.colour[1],
                                                   seq[n].palette.colour[2],
                                                   seq[n].palette.colour[3]) ;
    }
    if (seq[n].alpha.used)
    {
      fprintf(file, "  alpha: %d, %d, %d, %d\n", seq[n].alpha.colour[0],
                                               seq[n].alpha.colour[1],
                                               seq[n].alpha.colour[2],
                                               seq[n].alpha.colour[3]) ;
    }
    if (seq[n].dimensions.used)
    {
      fprintf(file, "  dimensions: x0=%d, x1=%d, y0=%d, y1=%d, width=%d, height=%d\n",
                       seq[n].dimensions.x0,
                       seq[n].dimensions.x1,
                       seq[n].dimensions.y0,
                       seq[n].dimensions.y1,
                       seq[n].dimensions.size[0],
                       seq[n].dimensions.size[1]) ;
    }
    if (seq[n].linestart.used)
    {
      fprintf(file, "  linestart: line0=%d, line1=%d\n",
                       seq[n].linestart.line0, seq[n].linestart.line1) ;
    }
    if (seq[n].last==0)
    {
      fprintf(file, "\n") ;
    }
    n++ ;
  } while (seq[n-1].last==0) ;
  fprintf(file, "End subtitle control sequence\n") ;
}
*/

static void parse_data_sequence(unsigned char *data, parsed_ctrl_sequence *parsed)
{
  int pixcounter=0 ;
  unsigned int start[2] = {parsed->linestart.line0, parsed->linestart.line1} ;
  unsigned int offset[2]={0,0} ; // in nibbles
  unsigned int x=0, y=0 ;
  unsigned int chunk ;
  unsigned int n ;
  unsigned int len ;
  signed int colour ;
  unsigned int width=parsed->dimensions.size[0] ;
  int linestart=0;
  //  FILE *file ;
  //char filename[16];

  //buffer for plugin
  unsigned char *picture;

  picture = config.sub.frame;

  memset(picture, 0, parsed->dimensions.size[0]*parsed->dimensions.size[1]);

  /*
  snprintf(filename, sizeof(filename), "%s_%d.raw", config.subprefix, counter) ;

  if ((file=fopen(filename, "w"))==0)
  {
    DEBUG("Unable to open output file %s\n", filename) ;
    return ;
  }

  fprintf(file, "P5\n");
  fprintf(file, "%d %d 2\n", parsed->dimensions.size[0], parsed->dimensions.size[1]);
  */
  //NDEBUG("Start=%d\n", start[0]) ;


  while (y < parsed->dimensions.size[1]) {

    int parity = y&1 ;

    if (x==0) linestart=offset[parity] ;
    chunk=read_nibble(data+start[parity], offset[parity]) ;
    offset[parity]++ ;

    //    DEBUG("chunk=%x, offset=%d (1)\n", chunk, offset[parity]) ;

    if (chunk < 0x4) {
      chunk = (chunk << 4) | read_nibble(data+start[parity], offset[parity]) ;
      offset[parity]++ ;
      //      DEBUG("chunk=%x, offset=%d (2)\n", chunk, offset[parity]) ;
      if (chunk < 0x10) {
        chunk = (chunk << 4) | read_nibble(data+start[parity], offset[parity]) ;
        offset[parity]++ ;
	//        DEBUG("chunk=%x, offset=%d (3)\n", chunk, offset[parity]) ;
        if (chunk < 0x040) {
          chunk = (chunk << 4) | read_nibble(data+start[parity], offset[parity]) ;
          offset[parity]++ ;
          if (chunk != 0 && chunk < 0x0100) {
	    //            DEBUG("ERK! Illegal quad nibble %x\n", chunk) ;
          }
	  //          DEBUG("chunk=%x, offset=%d (4)\n", chunk, offset[parity]) ;
        }
      }
    }
    colour=chunk & 3 ;
    //    colour=enhance_colour(colour) ;

    if (chunk < 4) {
      // EOL
      len=width-x ;
      //tc_log_msg(__FILE__, "Writing %d pixels (EOL)", parsed->dimensions.size[0]-x) ;
    } else {
      len=chunk >> 2 ;
      //tc_log_msg(__FILE__, "Writing %d pixels (chunk=%x)", len, chunk) ;

      if (x+len > width) {
	//DEBUG("ERK! Line overrun by %d\n", (x+number)-width) ;
      }
    }

    for (n=0; n < len; n++) {
      picture[parsed->dimensions.size[0]*y + x + n]=colour& 0xff;
      //      fputc(colour, file);
      pixcounter++ ;
    }
    x += len ;

    if (x >= width) {
      // the > case shouldn't really happen

      x=0 ;
      y++ ;

      if (offset[parity] & 1) {
	offset[parity]++ ;
	//DEBUG("Realigned at EOL\n") ;
      }
      //tc_log_msg(__FILE__, "Written line %d with %d pixels from %d nibbles:",
      //           y-1, pixcounter, offset[parity]-linestart) ;
      //show_nibbles(data+start[parity], linestart, offset[parity]-linestart, stderr);
      NDEBUG("\n") ;
      pixcounter=0 ;
    }
  }

  //fclose(file) ;

  counter++ ;
}

// returns the size of the ctrl sequence
static int parse_ctrl_sequence(unsigned char *data,
                               unsigned int start_offset, // required because offsets are used to determine the final sequence
                               parsed_ctrl_sequence *parsed)
{
  int offset=0 ;
  int next ;
  int n=0 ;
  int current_offset ;

  do
  {
    memset(&parsed[n], 0, sizeof(parsed_ctrl_sequence)) ;
    current_offset = start_offset+offset ;
    parsed[n].time = read_short(data+offset) ;

    config.sub.time = (parsed[n].time) ? parsed[n].time : 500;
    next = read_short(data+offset+2) ;

    //tc_log_msg(__FILE__, "sequence %d, time=%d, next=%d, current_offset=%d", n, parsed[n].time, next, current_offset) ;
    offset+=4 ;

    //DEBUG("ctrlseq: ") ; show_nibbles(data, 0, 80, stderr) ;DEBUG("\n") ;
    while (data[offset] != 0xFF )
    {
      switch (data[offset])
      {
        case 0x00:
          parsed[n].forcedisplay=1 ;
          config.sub.forced=1 ;
          offset++ ;
          break ;

        case 0x01:
          parsed[n].startdisplay=1 ;
          DEBUG("Start\n") ;
          offset++ ;
          break ;

        case 0x02:
          parsed[n].stopdisplay=1 ;
          offset++ ;
          break ;

        case 0x03:
          parsed[n].palette.colour[0] = (data[offset+1] & 0xF0) >> 4 ;
	  config.sub.colour[0]=parsed[n].palette.colour[0];
          parsed[n].palette.colour[1] = (data[offset+1] & 0x0F) ;
	  config.sub.colour[1]=parsed[n].palette.colour[1];
	  parsed[n].palette.colour[2] = (data[offset+2] & 0xF0) >> 4 ;
	  config.sub.colour[2]=parsed[n].palette.colour[2];
          parsed[n].palette.colour[3] = (data[offset+2] & 0x0F) ;
	  config.sub.colour[3]=parsed[n].palette.colour[3];
          parsed[n].palette.used=1 ;
          DEBUG("Colour\n") ;
          offset += 3 ;
          break ;

        case 0x04:
          parsed[n].alpha.colour[0] = (data[offset+1] & 0xF0) >> 4 ;
	  config.sub.alpha[0]=parsed[n].alpha.colour[0];
          parsed[n].alpha.colour[1] = (data[offset+1] & 0x0F) ;
	  config.sub.alpha[1]=parsed[n].alpha.colour[0];
          parsed[n].alpha.colour[2] = (data[offset+2] & 0xF0) >> 4 ;
	  config.sub.alpha[2]=parsed[n].alpha.colour[0];
          parsed[n].alpha.colour[3] = (data[offset+2] & 0x0F) ;
	  config.sub.alpha[3]=parsed[n].alpha.colour[0];
          parsed[n].alpha.used=1 ;
          offset += 3 ;
          break ;

        case 0x05:
          parsed[n].dimensions.x0 =  (data[offset+1] << 4)         | (data[offset+2] >> 4) ;

	  config.sub.x = parsed[n].dimensions.x0;

          parsed[n].dimensions.x1 = ((data[offset+2] & 0x0F) << 8) |  data[offset+3] ;
          parsed[n].dimensions.y0 =  (data[offset+4] << 4)         | (data[offset+5] >> 4) ;

	  config.sub.y = parsed[n].dimensions.y0;

          parsed[n].dimensions.y1 = ((data[offset+5] & 0x0F) << 8) |  data[offset+6] ;
          parsed[n].dimensions.size[0]=parsed[n].dimensions.x1 - parsed[n].dimensions.x0+1 ; // +1 because it's inclusive

	  config.sub.w = parsed[n].dimensions.size[0];

          parsed[n].dimensions.size[1]=parsed[n].dimensions.y1 - parsed[n].dimensions.y0+1 ; // +1 because it's inclusive

	  config.sub.h = parsed[n].dimensions.size[1];

          parsed[n].dimensions.used=1 ;
          offset += 7 ;
          break ;

        case 0x06:
          parsed[n].linestart.line0=read_short(data+offset+1) ;
          parsed[n].linestart.line1=read_short(data+offset+3) ;
          parsed[n].linestart.used=1 ;
          offset += 5 ;
          break ;

        case 0x07:
          // CHG_COLCON, see http://dvd.sourceforge.net/dvdinfo/spu.html
          // To implement this, I would suggest to extend the size of the
          // picture to the combined size of the subpicture and the
          // size/coordinates defined within this control, merging
          // both datas.
          offset += read_short(data+offset+1)+1;
          break;

        default:
          tc_log_warn(__FILE__, "unknown ctrl sequence 0x%x", data[offset]) ;
	  ++offset;
          break;
      }
    }
    offset++ ;
    n++ ;
    NDEBUG("next=%d, currentoff=%d\n", next, current_offset) ;
  } while (next != current_offset) ;
  NDEBUG("terminated parsing ctrl\n") ;
  parsed[n-1].last=1 ;
  return offset ;
}

static void process_title(unsigned char *data, unsigned int size, unsigned int data_size, double pts)
{
  unsigned int ctrl_offset=data_size ;
  unsigned int ctrl_size ;
  parsed_ctrl_sequence parsed[10] ;

  memset(parsed, 0, sizeof(parsed)) ;
  ctrl_size=parse_ctrl_sequence(data+ctrl_offset, ctrl_offset, parsed) ;
  parse_data_sequence(data, parsed) ;
}


static int process_sub(unsigned char *data, unsigned int size, int block, unsigned int id, double pts)
{
  static int queued=0 ;

  static struct
  {
    double pts ;
    unsigned char data[65536];
    unsigned int size;
    unsigned short total_size ;
    unsigned short data_size ;
  } buffer ;

  unsigned short ts;

  if (queued==0) {
      buffer.total_size = (data[0] << 8) | data[1];

      ts=buffer.total_size;

      buffer.data_size = ntohs(*((unsigned short *) &data[2])) ;
      buffer.size=0 ;
  }

  ac_memcpy(buffer.data+buffer.size, data, size) ;
  buffer.size += size ;

  buffer.pts=pts;

  NDEBUG("total size=%d, data size=%d\n", buffer.total_size, buffer.data_size);

  if (buffer.total_size > buffer.size) {
      queued=1 ;
  } else {
      queued=0 ;
  }

  if(queued) {
      NDEBUG("Packet overflow, queued\n");
      return(-1);
  } else {
      NDEBUG("Processing packet of size %d\n", buffer.total_size) ;
      process_title(buffer.data, buffer.size, buffer.data_size, buffer.pts);
  }

  return(0);
}

//-----------------------
//
// API
//
//-----------------------

int subproc_init(char *scriptfile, char *prefix, int subtitles, unsigned short id)
{
    config.subprefix=prefix ;
    config.subtitles=subtitles ;
    config.id=id ;

    if (id > 31) {
	tc_log_error(__FILE__, "illegal subtitle stream id %d", id) ;
	return(-1);
    }

    tc_log_info(__FILE__, "extracting subtitle stream %d", config.id) ;
    return(0);
}


int subproc_feedme(void *_data, unsigned  int size, int block, double pts, sub_info_t *sub)
{
  unsigned char *data=_data ;
  unsigned int type=data[0] ;

  int n;

  memset(&config.sub, 0, sizeof(config.sub));
  config.sub.frame = sub->frame;

  if(process_sub(data+1, size-1, block, type & 0x1F, pts)<0) return(-1);

  sub->time  = config.sub.time;
  sub->forced = config.sub.forced;
  sub->x     = config.sub.x;
  sub->y     = config.sub.y;
  sub->w     = config.sub.w;
  sub->h     = config.sub.h;
  sub->frame = config.sub.frame;

  for(n=0; n<4; ++n) {
    sub->colour[n] = config.sub.colour[n];
    sub->alpha[n] = config.sub.alpha[n];
  }

  return(0);

}

