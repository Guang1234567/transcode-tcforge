/*
 *  split.c
 *
 *  Copyright (C) Thomas Oestreich - January 2002
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

#include "transcode.h"
#include "split.h"

#define PMAX_BUF 1024
char split_cmd_buf[PMAX_BUF];

static int entries;

typedef struct seq_s {

  int unit;
  long frame;
  int seq;
  int pseq;
  long offset;
  int foffset;
} seq_t;

static seq_t **seq=NULL;

#define MAX_UNITS 128

static long uframe[MAX_UNITS];
static long unit_offset[MAX_UNITS];

#define debug_return {return(-1);}

static int split_stream_core(const char *file, const char *source)
{

    FILE *fd;
    FILE *tmp;
    char buffer[256];

    int n=-1;

    if(file == NULL) {

	if(tc_snprintf(split_cmd_buf, PMAX_BUF,
                   "%s -i %s | %s -W 2>/dev/null",
                   TCCAT_EXE, TCDEMUX_EXE, source)<0)
        debug_return;
	// popen
	if((fd = popen(split_cmd_buf, "r"))== NULL) debug_return;

	tc_log_info(__FILE__, "generating auto-split information from file \"%s\"", source);

	// open temp. file
	tmp = tmpfile();

	while ( fgets (buffer, 256, fd) ) {
	    ++n;
	    if ( fputs (buffer, tmp) < 0 ) debug_return;
	}

	pclose(fd);
	fd = tmp;

    } else {

	if((fd = fopen(file, "r"))==NULL) debug_return;

	tc_log_info(__FILE__, "reading auto-split information from file \"%s\"", file);

	// determine number of lines

	while (fgets (buffer, 256, fd)) { ++n;  }
    }

    // need n+2 pointers????
    seq = (seq_t **) malloc( (n+2) * sizeof(seq_t*));
    if ( seq == NULL ) debug_return;

    n=-1;
    fseek (fd, 0, SEEK_SET);

    do {
	++n;

	if((seq[n] = (seq_t *) malloc(sizeof(seq_t))) == NULL) debug_return;

    } while((fscanf(fd, "%d %ld %d %d %ld %d", &seq[n]->unit, &seq[n]->frame, &seq[n]->seq, &seq[n]->pseq, &seq[n]->offset, &seq[n]->foffset)!=EOF));

    entries=n;

    if(seq[n]!=NULL) seq[n] = (seq_t *) malloc(sizeof(seq_t));

    //add fake closing entry
    seq[n]->unit=seq[n-1]->unit+1;
    seq[n]->frame=seq[n-1]->frame=0;
    seq[n]->seq=seq[n-1]->seq+1;

    fclose(fd);

    return(0);
}


static long get_frame_index(int unit, long frame_inc)
{
    //first entry is unique

    long n;
    int m;

    if(frame_inc==0) return(unit_offset[unit]);

    n=unit_offset[unit] + frame_inc;
    m=seq[n]->seq;

    while(seq[n]->unit == unit && n < entries && seq[n]->seq == m ) ++n;

    return(n);
}

//----------------------------------------------
//
// main routine
//
//----------------------------------------------


int split_stream(vob_t *vob, const char *file, int this_unit, int *fa, int *fb, int opt_flag)
{

  int n, unit_ctr=-1, last_unit=-1;

  int s1, s2, video=1;

  long max_frames=-1;
  int unit=0, foff=0;

  long _fa, _fb;

  long _n, frame_inc=0, poff=0;

  int startc, chunks;

  if(split_stream_core(file, ((vob->vob_chunk == vob->vob_chunk_max) ? vob->audio_in_file:vob->video_in_file))<0) {
    tc_log_error(__FILE__, "failed to read VOB navigation file %s", file);
    return(-1);
  }

  tc_log_info(__FILE__, "done reading %d entries", entries);

  //analyze data:

  for(n=0; n<MAX_UNITS; ++n) uframe[n]=0;

  // (I) determine presentation units and number of frames

  for(n=0; n<entries; ++n) {

    if(last_unit != seq[n]->unit) {
	last_unit=seq[n]->unit;
	++unit_ctr;
	unit_offset[unit_ctr]=n;
    }

    ++uframe[unit_ctr];
  }

  for(n=0; n<=unit_ctr; ++n) {
      if(max_frames<=uframe[n]) {
	  unit = n;
	  max_frames = uframe[n];
      }

      if(this_unit > unit_ctr) {
	if(verbose >= TC_DEBUG) tc_log_msg(__FILE__, "invalid PSU %s", file);
	return(-1);
      }

      if(-1 < this_unit) unit = this_unit;

      if(verbose >= TC_DEBUG) tc_log_msg(__FILE__, "unit=%d, frames=%ld, offset=%ld (%d)", n, uframe[n], unit_offset[n], vob->ps_unit);
  }

  // (II) determine largest (main) presentation unit

  if(verbose >= TC_DEBUG) tc_log_msg(__FILE__, "selecting unit %d, frames=%ld, offset=%ld", unit, uframe[unit], unit_offset[unit]);


  // video or audio mode ?

  if((vob->vob_percentage) ? (vob->vob_chunk==vob->vob_chunk_max && vob->vob_chunk_max==100 ) : (vob->vob_chunk == vob->vob_chunk_max)) video=0;

  //check user error
  if(vob->vob_chunk_num2>vob->vob_chunk_max) vob->vob_chunk_num2=vob->vob_chunk_max;
  if(vob->vob_chunk_num1>vob->vob_chunk_max) vob->vob_chunk_num1=0;

  // determine number of chunks to process: (0.6.0pre5)

  if(video) {
      chunks = (vob->vob_percentage || vob->vob_chunk_num2==0) ? 1:vob->vob_chunk_num2-vob->vob_chunk_num1;
      startc = (vob->vob_percentage || vob->vob_chunk_num2==0) ? vob->vob_chunk:vob->vob_chunk_num1;
  } else {
      chunks = (vob->vob_percentage || vob->vob_chunk_num2==0) ? vob->vob_chunk_max:vob->vob_chunk_num2-vob->vob_chunk_num1;
      startc = (vob->vob_percentage || vob->vob_chunk_num2==0) ? 0:vob->vob_chunk_num1;
  }

  //---------------------------------------------------------------------

  // (III) determine pack offset for given number of VOB chunks
  // Modified by CARON Dominique <domi@lpm.univ-monp2.fr> to optimize the
  // cluster mode
  // vob->vob_chunk = percentage of frames to skip before frames to encode
  // vob->vob_chunk_max = percentage of frames to encode


  frame_inc = (vob->vob_percentage) ? (long) ((startc * uframe[unit])/100) : (long) ((startc * uframe[unit])/vob->vob_chunk_max);

  if(verbose >= TC_DEBUG) tc_log_msg(__FILE__, "estimated chunk offset = %ld", frame_inc);

  _n = get_frame_index(unit, frame_inc);

  poff = seq[_n]->offset;
  foff = seq[_n]->foffset;

  _fa = seq[_n]->frame;

  // parameter for option "-c"
  *fa = foff;
  *fb = foff - seq[_n]->frame;

  s1 = seq[_n]->seq;

  if(verbose >= TC_DEBUG) tc_log_msg(__FILE__, "chunk %d starts at frame %ld, pack offset %ld, finc=%d", startc, _n, poff, foff);


  // (IV) determine end of chunk(s)
  // Modified by CARON Dominique <domi@lpm.univ-monp2.fr> to optimize the
  // cluster mode

  frame_inc = (vob->vob_percentage) ? (long) (((vob->vob_chunk+vob->vob_chunk_max) * uframe[unit])/100) : (long) (((startc+chunks) * uframe[unit])/vob->vob_chunk_max);

  _n = get_frame_index(unit, frame_inc);

  _fb = seq[_n]->frame;

  s2 = seq[_n]->seq;

  if(_fb==0) {
    _fb = uframe[unit];
    *fb += uframe[unit];
  } else {
    *fb += seq[_n]->frame;
  }

  // (V) set vob parameter

  vob->vob_offset = poff;
  vob->ps_unit = 0;

  vob->ps_seq1 = 0;
  vob->ps_seq2 = (s2==0 && _n) ? seq[_n-1]->seq-s1+3 : s2-s1+2;

  tc_log_msg(__FILE__, "chunk %d/%d PU=%d (-L 0 -c %ld-%ld) mapped onto (-L %ld -c %d-%d)", vob->vob_chunk, vob->vob_chunk_max-1, unit, _fa, _fb, poff, *fa, *fb);


  //---------------------------------------------------------------------

  if(opt_flag==1) { //cluster mode

    if(video) {

      // no sound
      vob->amod_probed="null";
      vob->has_audio=0;

      tc_log_info(__FILE__, "video mode");

    } else {

      // no video
      vob->vmod_probed="null";
      vob->has_video=0;

      vob->vob_offset = poff;
      vob->ps_unit = 0;

      tc_log_info(__FILE__, "audio mode");
    }
  }

  //---------------------------------------------------------------------

  if(seq != NULL) {

    // seq array not needed anymore
    for (n=0; n < entries+1; ++n) {
      free(seq[n]);
    }

    free(seq);
  }

  return(0);
}
