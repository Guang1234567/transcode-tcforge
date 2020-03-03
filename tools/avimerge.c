/*
 *  avimerge.c
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
// TODO: Simplify this code. Immediatly

#include "transcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "aud_scan.h"
#include "aud_scan_avi.h"
#include "avimisc.h"

#define EXE "avimerge"

void version(void)
{
  printf("%s (%s v%s) (C) 2001-2004 Thomas Oestreich, T. Bitterberg"
                        " 2004-2010 Transcode Team\n",
                      EXE, PACKAGE, VERSION);
}

static void usage(int status)
{
    version();
    printf("\nUsage: %s [options]\n", EXE);
    printf("    -o file                   output file name\n");
    printf("    -i file1 [file2 [...]]    input file(s)\n");
    printf("    -p file                   multiplex additional audio track from file\n");
    printf("    -a num                    select audio track number from input file [0]\n");
    printf("    -A num                    select audio track number in output file [next]\n");
    printf("    -b n                      handle vbr audio [autodetect]\n");
    printf("    -c                        drop video frames in case audio is missing [off]\n");
    printf("    -f FILE                   read AVI comments from FILE [off]\n");
    printf("    -x FILE                   read AVI index from FILE [off] (see aviindex(1))\n");
    exit(status);
}

static char data[SIZE_RGB_FRAME];
static char *comfile = NULL;
static char *indexfile = NULL;
long sum_frames = 0;
int is_vbr=1;
int drop_video=0;


static int merger(avi_t *out, char *file)
{
    avi_t *in;
    long frames, n, bytes;
    int key, j, aud_tracks;
    static int init = 0;
    static int vid_chunks = 0;

    double fps;
    static double vid_ms;
    static double aud_ms[AVI_MAX_TRACKS];
    int have_printed=0;
    int do_drop_video=0;

    if (!init) {
	for (j=0; j<AVI_MAX_TRACKS; j++)
	    aud_ms[j] = 0.0;
	vid_ms = 0;
	vid_chunks = 0;
	init = 1;
    }

    if(indexfile)  {
	if (NULL == (in = AVI_open_input_indexfile(file, 0, indexfile))) {
	    AVI_print_error("AVI open with indexfile");
	    return(-1);
	}
    }
    else if(NULL == (in = AVI_open_input_file(file,1))) {
	AVI_print_error("AVI open");
	return(-1);
    }

    AVI_seek_start(in);
    fps    =  AVI_frame_rate(in);
    frames =  AVI_video_frames(in);
    aud_tracks = AVI_audio_tracks(in);

    for (n=0; n<frames; ++n) {

      ++vid_chunks;
      vid_ms = vid_chunks*1000.0/fps;

      // audio
      for(j=0; j<aud_tracks; ++j) {

	  int ret;
	  double old_ms = aud_ms[j];

	  AVI_set_audio_track(in, j);
	  AVI_set_audio_track(out, j);

	  ret = sync_audio_video_avi2avi (vid_ms, &aud_ms[j], in, out);
	  if (ret<0) {
	      if (ret==-2) {
		  if (aud_ms[j] == old_ms) {
		      do_drop_video = 1;
		      if (!have_printed) {
			  fprintf(stderr, "\nNo audiodata left for track %d->%d (%.2f=%.2f) %s ..\n",
			      AVI_get_audio_track(in), AVI_get_audio_track(out),
			      old_ms, aud_ms[j], (do_drop_video && drop_video)?"breaking (-c)":"continuing");
			  have_printed++;
		      }
		  }
	      } else {
		  fprintf(stderr, "\nAn error happend at frame %ld track %d\n", n, j);
	      }
	  }

      }

      if (do_drop_video && drop_video) {
	  fprintf(stderr, "\n[avimerge] Dropping %ld frames\n", frames-n-1);
	  goto out;
      }

      // video
      bytes = AVI_read_frame(in, data, &key);

      if(bytes < 0) {
	AVI_print_error("AVI read video frame");
	return(-1);
      }

      if(AVI_write_frame(out, data, bytes, key)<0) {
	AVI_print_error("AVI write video frame");
	return(-1);
      }

      // progress
      fprintf(stderr, "[%s] (%06ld-%06ld) (%.2f <-> %.2f)\r", file, sum_frames, sum_frames + n, vid_ms, aud_ms[0]);
    }
out:
    fprintf(stderr, "\n");

    AVI_close(in);

    sum_frames += n;

    return(0);
}


int main(int argc, char *argv[])
{
  avi_t *avifile, *avifile1, *avifile2;

  char *outfile=NULL, *infile=NULL, *audfile=NULL;

  long rate, mp3rate;

  int j, ch, cc=0, track_num=0, out_track_num=-1;
  int width, height, format=0, format_add, chan, bits, aud_error=0;

  double fps;

  char *codec;

  long offset, frames, n, bytes, aud_offset=0;

  int key;

  int aud_tracks;

  // for mp3 audio
  FILE *f=NULL;
  int len, headlen, chan_i, rate_i, mp3rate_i;
  unsigned long vid_chunks=0;
  char head[8];
  off_t pos;
  double aud_ms = 0.0, vid_ms = 0.0;
  double aud_ms_w[AVI_MAX_TRACKS];

  ac_init(AC_ALL);

  if(argc==1) usage(EXIT_FAILURE);

  while ((ch = getopt(argc, argv, "A:a:b:ci:o:p:f:x:?hv")) != -1) {

    switch (ch) {

    case 'i':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      infile = optarg;

      break;

    case 'A':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      out_track_num = atoi(optarg);

      if(out_track_num<-1) usage(EXIT_FAILURE);

      break;

    case 'a':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      track_num = atoi(optarg);

      if(track_num<0) usage(EXIT_FAILURE);

      break;

    case 'b':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      is_vbr = atoi(optarg);

      if(is_vbr<0) usage(EXIT_FAILURE);

      break;

    case 'c':

      drop_video = 1;

      break;

    case 'o':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      outfile = optarg;

      break;

    case 'p':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      audfile = optarg;

      break;

    case 'f':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      comfile = optarg;

      break;


    case 'x':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      indexfile = optarg;

      break;

    case 'v':
      version();
      exit(EXIT_SUCCESS);
    case 'h':
      usage(EXIT_SUCCESS);
    default:
      usage(EXIT_FAILURE);
    }
  }

  if(outfile == NULL || infile == NULL) usage(EXIT_FAILURE);

  printf("scanning file %s for video/audio parameter\n", infile);

  // open first file for video/audio info read only
  if(indexfile) {
      if (NULL == (avifile1 = AVI_open_input_indexfile(infile,0,indexfile))) {
	  AVI_print_error("AVI open with index file");
      }
  }
  else if(NULL == (avifile1 = AVI_open_input_file(infile,1))) {
      AVI_print_error("AVI open");
      exit(1);
  }

  AVI_info(avifile1);

  // safety checks

  if(strcmp(infile, outfile)==0) {
    printf("error: output filename conflicts with input filename\n");
    exit(1);
  }

  ch = optind;

  while (ch < argc) {

    if(tc_file_check(argv[ch]) != 0) {
      printf("error: file not found\n");
      exit(1);
    }

    if(strcmp(argv[ch++], outfile)==0) {
      printf("error: output filename conflicts with input filename\n");
      exit(1);
    }
  }

  // open output file
  if(NULL == (avifile = AVI_open_output_file(outfile))) {
    AVI_print_error("AVI open");
    exit(1);
  }


  // read video info;

  width  =  AVI_video_width(avifile1);
  height =  AVI_video_height(avifile1);

  fps    =  AVI_frame_rate(avifile1);
  codec  =  AVI_video_compressor(avifile1);

  //set video in outputfile
  AVI_set_video(avifile, width, height, fps, codec);

  if (comfile!=NULL)
    AVI_set_comment_fd(avifile, open(comfile, O_RDONLY));

  //multi audio tracks?
  aud_tracks = AVI_audio_tracks(avifile1);
  if (out_track_num < 0) out_track_num = aud_tracks;

  for(j=0; j<aud_tracks; ++j) {

      if (out_track_num == j) continue;
      AVI_set_audio_track(avifile1, j);

      rate   =  AVI_audio_rate(avifile1);
      chan   =  AVI_audio_channels(avifile1);
      bits   =  AVI_audio_bits(avifile1);

      format =  AVI_audio_format(avifile1);
      mp3rate=  AVI_audio_mp3rate(avifile1);
      //printf("TRACK %d MP3RATE %ld VBR %ld\n", j, mp3rate, AVI_get_audio_vbr(avifile1));

      //set next track of output file
      AVI_set_audio_track(avifile, j);
      AVI_set_audio(avifile, chan, rate, bits, format, mp3rate);
      AVI_set_audio_vbr(avifile, AVI_get_audio_vbr(avifile1));
  }

  if(audfile!=NULL) goto audio_merge;

  // close reopen in merger function
  AVI_close(avifile1);

  //-------------------------------------------------------------

  printf("merging multiple AVI-files (concatenating) ...\n");

  // extract and write to new files

  printf ("file %02d %s\n", ++cc, infile);
  merger(avifile, infile);

  while (optind < argc) {

    printf ("file %02d %s\n", ++cc, argv[optind]);
    merger(avifile, argv[optind++]);
  }

  // close new AVI file

  AVI_close(avifile);

  printf("... done merging %d file(s) in %s\n", cc, outfile);

  // reopen file for video/audio info
  if(NULL == (avifile = AVI_open_input_file(outfile,1))) {
    AVI_print_error("AVI open");
    exit(1);
  }
  AVI_info(avifile);

  return(0);

  //-------------------------------------------------------------


// *************************************************
// Merge the audio track of an additional AVI file
// *************************************************

 audio_merge:

  printf("merging audio %s track %d (multiplexing) into %d ...\n", audfile, track_num, out_track_num);

  // open audio file read only
  if(NULL == (avifile2 = AVI_open_input_file(audfile,1))) {
    int f=open(audfile, O_RDONLY), ret=0;
    char head[1024], *c;
    c = head;
    if (f>0 && (1024 == read(f, head, 1024)) ) {
      while ((c-head<1024-8) && (ret = tc_probe_audio_header(c, 8))<=0 ) {
	c++;
      }
      close(f);

      if (ret > 0) {
	aud_offset = c-head;
	//printf("found atrack 0x%x off=%ld\n", ret, aud_offset);
	goto merge_mp3;
      }
    }

    AVI_print_error("AVI open");
    exit(1);
  }

  AVI_info(avifile2);

  //switch to requested track

  if(AVI_set_audio_track(avifile2, track_num)<0) {
    fprintf(stderr, "invalid audio track\n");
  }

  rate   =  AVI_audio_rate(avifile2);
  chan   =  AVI_audio_channels(avifile2);
  bits   =  AVI_audio_bits(avifile2);

  format =  AVI_audio_format(avifile2);
  mp3rate=  AVI_audio_mp3rate(avifile2);

  //set next track
  AVI_set_audio_track(avifile, out_track_num);
  AVI_set_audio(avifile, chan, rate, bits, format, mp3rate);
  AVI_set_audio_vbr(avifile, AVI_get_audio_vbr(avifile2));

  AVI_seek_start(avifile1);
  frames =  AVI_video_frames(avifile1);
  offset = 0;

  printf ("file %02d %s\n", ++cc, infile);

  for (n=0; n<AVI_MAX_TRACKS; n++)
    aud_ms_w[n] = 0.0;
  vid_chunks=0;

  for (n=0; n<frames; ++n) {

    // video
    bytes = AVI_read_frame(avifile1, data, &key);

    if(bytes < 0) {
      AVI_print_error("AVI read video frame");
      return(-1);
    }

    if(AVI_write_frame(avifile, data, bytes, key)<0) {
      AVI_print_error("AVI write video frame");
      return(-1);
    }
    ++vid_chunks;
    vid_ms = vid_chunks*1000.0/fps;

    for(j=0; j<aud_tracks; ++j) {

      if (j == out_track_num) continue;
      AVI_set_audio_track(avifile1, j);
      AVI_set_audio_track(avifile, j);
      chan   = AVI_audio_channels(avifile1);

      // audio
      chan = AVI_audio_channels(avifile1);
      if(chan) {
	  sync_audio_video_avi2avi(vid_ms, &aud_ms_w[j], avifile1, avifile);
      }
    }


    // merge additional track

    // audio
    chan = AVI_audio_channels(avifile2);
    AVI_set_audio_track(avifile, out_track_num);

    if(chan) {
	sync_audio_video_avi2avi(vid_ms, &aud_ms, avifile2, avifile);
    }

    // progress
    fprintf(stderr, "[%s] (%06ld-%06ld)\r", outfile, offset, offset + n);

  }

  fprintf(stderr,"\n");

  offset = frames;

  //more files to merge?

  AVI_close(avifile1);

  while (optind < argc) {

    printf ("file %02d %s\n", ++cc, argv[optind]);

    if(NULL == ( avifile1 = AVI_open_input_file(argv[optind++],1))) {
      AVI_print_error("AVI open");
      goto finish;
    }

    AVI_seek_start(avifile1);
    frames =  AVI_video_frames(avifile1);

    for (n=0; n<frames; ++n) {

      // video
      bytes = AVI_read_frame(avifile1, data, &key);

      if(bytes < 0) {
	AVI_print_error("AVI read video frame");
	return(-1);
      }

      if(AVI_write_frame(avifile, data, bytes, key)<0) {
	AVI_print_error("AVI write video frame");
	return(-1);
      }

      ++vid_chunks;
      vid_ms = vid_chunks*1000.0/fps;

      // audio
      for(j=0; j<aud_tracks; ++j) {

	if (j == out_track_num) continue;
	AVI_set_audio_track(avifile1, j);
	AVI_set_audio_track(avifile, j);

	chan   = AVI_audio_channels(avifile1);

	if(chan) {
	  sync_audio_video_avi2avi(vid_ms, &aud_ms_w[j], avifile1, avifile);
	}
      }

      // merge additional track

      chan   = AVI_audio_channels(avifile2);
      AVI_set_audio_track(avifile, out_track_num);

      if(chan) {
	  sync_audio_video_avi2avi(vid_ms, &aud_ms, avifile2, avifile);
      } // chan

      // progress
      fprintf(stderr, "[%s] (%06ld-%06ld)\r", outfile, offset, offset + n);
    }

    fprintf(stderr, "\n");

    offset += frames;
    AVI_close(avifile1);
  }

 finish:

  // close new AVI file

  printf("... done multiplexing in %s\n", outfile);

  AVI_info(avifile);
  AVI_close(avifile);

  return(0);


// *************************************************
// Merge a raw audio file which is either MP3 or AC3
// *************************************************

merge_mp3:

  f = fopen(audfile,"rb");
  if (!f) { perror ("fopen"); exit(1); }

  fseek(f, aud_offset, SEEK_SET);
  len = fread(head, 1, 8, f);
  format_add  = tc_probe_audio_header(head, len);
  headlen = tc_get_audio_header(head, len, format_add, &chan_i, &rate_i, &mp3rate_i);
  fprintf(stderr, "... this looks like a %s track ...\n", (format_add==0x55)?"MP3":"AC3");

  fseek(f, aud_offset, SEEK_SET);

  //set next track
  AVI_set_audio_track(avifile, out_track_num);
  AVI_set_audio(avifile, chan_i, rate_i, 16, format_add, mp3rate_i);
  AVI_set_audio_vbr(avifile, is_vbr);

  AVI_seek_start(avifile1);
  frames =  AVI_video_frames(avifile1);
  offset = 0;

  for (n=0; n<AVI_MAX_TRACKS; ++n)
      aud_ms_w[n] = 0.0;

  for (n=0; n<frames; ++n) {

    // video
    bytes = AVI_read_frame(avifile1, data, &key);

    if(bytes < 0) {
      AVI_print_error("AVI read video frame");
      return(-1);
    }

    if(AVI_write_frame(avifile, data, bytes, key)<0) {
      AVI_print_error("AVI write video frame");
      return(-1);
    }

    vid_chunks++;
    vid_ms = vid_chunks*1000.0/fps;

    for(j=0; j<aud_tracks; ++j) {

      if (j == out_track_num) continue;
      AVI_set_audio_track(avifile1, j);
      AVI_set_audio_track(avifile, j);
      chan   = AVI_audio_channels(avifile1);

      if(chan) {
	sync_audio_video_avi2avi(vid_ms, &aud_ms_w[j], avifile1, avifile);
      }
    }


    // merge additional track

    if(headlen>4 && !aud_error) {
      while (aud_ms < vid_ms) {
	//printf("reading Audio Chunk ch(%ld) vms(%lf) ams(%lf)\n", vid_chunks, vid_ms, aud_ms);
	pos = ftell(f);

	len = fread (head, 1, 8, f);
	if (len<=0) { //eof
	  fprintf(stderr, "EOF in %s; continuing ..\n", audfile);
	  aud_error=1;
	  break;
	}

	if ( (headlen = tc_get_audio_header(head, len, format_add, NULL, NULL, &mp3rate_i))<0) {
	  fprintf(stderr, "Broken %s track #(%d)? skipping\n", (format_add==0x55?"MP3":"AC3"), aud_tracks);
	  aud_ms = vid_ms;
	  aud_error=1;
	} else { // look in import/tcscan.c for explanation
	  aud_ms += (headlen*8.0)/(mp3rate_i);
	}

	fseek (f, pos, SEEK_SET);

	len = fread (data, headlen, 1, f);
	if (len<=0) { //eof
	  fprintf(stderr, "EOF in %s; continuing ..\n", audfile);
	  aud_error=1;
	  break;
	}

	AVI_set_audio_track(avifile, out_track_num);

	if(AVI_write_audio(avifile, data, headlen)<0) {
	  AVI_print_error("AVI write audio frame");
	  return(-1);
	}

      }
    }

    // progress
    fprintf(stderr, "[%s] (%06ld-%06ld)\r", outfile, offset, offset + n);

  }

  fprintf(stderr,"\n");
  offset = frames;

  // more files?
  while (optind < argc) {

    printf ("file %02d %s\n", ++cc, argv[optind]);

    if(NULL == ( avifile1 = AVI_open_input_file(argv[optind++],1))) {
      AVI_print_error("AVI open");
      goto finish;
    }

    AVI_seek_start(avifile1);
    frames =  AVI_video_frames(avifile1);

    for (n=0; n<frames; ++n) {

      // video
      bytes = AVI_read_frame(avifile1, data, &key);

      if(bytes < 0) {
	AVI_print_error("AVI read video frame");
	return(-1);
      }

      if(AVI_write_frame(avifile, data, bytes, key)<0) {
	AVI_print_error("AVI write video frame");
	return(-1);
      }

      vid_chunks++;
      vid_ms = vid_chunks*1000.0/fps;

      for(j=0; j<aud_tracks; ++j) {

	if (j == out_track_num) continue;
	AVI_set_audio_track(avifile1, j);
	AVI_set_audio_track(avifile, j);
	chan   = AVI_audio_channels(avifile1);

	if(chan) {
	  sync_audio_video_avi2avi(vid_ms, &aud_ms_w[j], avifile1, avifile);
	}
      }

      // merge additional track
      // audio

      if(headlen>4 && !aud_error) {
	while (aud_ms < vid_ms) {
	  //printf("reading Audio Chunk ch(%ld) vms(%lf) ams(%lf)\n", vid_chunks, vid_ms, aud_ms);
	  pos = ftell(f);

	  len = fread (head, 8, 1, f);
	  if (len<=0) { //eof
	    fprintf(stderr, "EOF in %s; continuing ..\n", audfile);
	    aud_error=1; break;
	  }

	  if ( (headlen = tc_get_audio_header(head, len, format_add, NULL, NULL, &mp3rate_i))<0) {
	    fprintf(stderr, "Broken %s track #(%d)?\n", (format_add==0x55?"MP3":"AC3"), aud_tracks);
	    aud_ms = vid_ms;
	    aud_error=1;
	  } else { // look in import/tcscan.c for explanation
	    aud_ms += (headlen*8.0)/(mp3rate_i);
	  }

	  fseek (f, pos, SEEK_SET);

	  len = fread (data, headlen, 1, f);
	  if (len<=0) { //eof
	    fprintf(stderr, "EOF in %s; continuing ..\n", audfile);
	    aud_error=1; break;
	  }

	  AVI_set_audio_track(avifile, out_track_num);

	  if(AVI_write_audio(avifile, data, headlen)<0) {
	    AVI_print_error("AVI write audio frame");
	    return(-1);
	  }

	}
      }

      // progress
      fprintf(stderr, "[%s] (%06ld-%06ld)\r", outfile, offset, offset + n);
    }

    fprintf(stderr, "\n");

    offset += frames;
    AVI_close(avifile1);
  }


  if (f) fclose(f);

  printf("... done multiplexing in %s\n", outfile);

  AVI_close(avifile);

  return(0);
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
