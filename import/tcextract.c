/*
 *  tcextract.c
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
#include "tccore/tcinfo.h"

#include "libtcutil/xio.h"
#include "ioaux.h"
#include "tc.h"

#include <limits.h>


#define EXE "tcextract"

#define MAX_BUF 1024

int verbose=TC_INFO;

void import_exit(int code)
{
  if(verbose & TC_DEBUG)
    tc_log_msg(EXE, "(pid=%d) exit (code %d)", (int) getpid(), code);
  exit(code);
}

/* ------------------------------------------------------------
 *
 * print a usage/version message
 *
 * ------------------------------------------------------------*/

void version(void)
{
    /* print id string to stderr */
    fprintf(stderr, "%s (%s v%s) (C) 2001-2003 Thomas Oestreich"
                                   " 2003-2010 Transcode Team\n",
                    EXE, PACKAGE, VERSION);
}

static void usage(int status)
{
  version();

  fprintf(stderr,"\nUsage: %s [options]\n", EXE);
  fprintf(stderr,"    -i name           input file name [stdin]\n");
  fprintf(stderr,"    -t magic          file type [autodetect]\n");
  fprintf(stderr,"    -a track          track number [0]\n");
  fprintf(stderr,"    -x codec          source codec\n");
  fprintf(stderr,"    -d mode           verbosity mode\n");
  fprintf(stderr,"    -C s-e            process only (video frame/audio byte) range [all]\n");
  fprintf(stderr,"    -f seekfile       seek/index file [off]\n");
  fprintf(stderr,"    -v                print version\n");

  exit(status);

}


/* ------------------------------------------------------------
 *
 * universal extract thread frontend
 *
 * ------------------------------------------------------------*/

int main(int argc, char *argv[])
{

    info_t ipipe;

    int user=0;

    long
	stream_stype = TC_STYPE_UNKNOWN,
	stream_magic = TC_MAGIC_UNKNOWN,
	stream_codec = TC_CODEC_UNKNOWN;

    int ch, done=0, track=0;
    char *magic=NULL, *codec=NULL, *name=NULL;

    //proper initialization
    memset(&ipipe, 0, sizeof(info_t));
    ipipe.frame_limit[0]=0;
    ipipe.frame_limit[1]=LONG_MAX;

    libtc_init(&argc, &argv);

    while ((ch = getopt(argc, argv, "d:x:i:f:a:vt:C:?h")) != -1) {

	switch (ch) {

	case 'i':

	  if(optarg[0]=='-') usage(EXIT_FAILURE);
	  name = optarg;

	  break;

	case 'd':

	  if(optarg[0]=='-') usage(EXIT_FAILURE);
	  verbose = atoi(optarg);

	  break;

	case 'x':

	  if(optarg[0]=='-') usage(EXIT_FAILURE);
	  codec = optarg;
	  break;

	case 'f':

	  if(optarg[0]=='-') usage(EXIT_FAILURE);
	  ipipe.nav_seek_file = optarg;

	  break;

	case 't':

	  if(optarg[0]=='-') usage(EXIT_FAILURE);
	  magic = optarg;
	  user=1;

	  break;

	case 'a':

	  if(optarg[0]=='-') usage(EXIT_FAILURE);
	  track = strtol(optarg, NULL, 0);
	  break;

        case 'C':

          if(optarg[0]=='-') usage(EXIT_FAILURE);
          if (2 != sscanf(optarg,"%ld-%ld", &ipipe.frame_limit[0], &ipipe.frame_limit[1])) usage(EXIT_FAILURE);
          if (ipipe.frame_limit[0] > ipipe.frame_limit[1])
          {
                tc_log_error(EXE, "Invalid -C options");
                usage(EXIT_FAILURE);
          }
          break;

	case 'v':
	  version();
	  exit(0);
	  break;

	case 'h':
	  usage(EXIT_SUCCESS);
	default:
	  usage(EXIT_FAILURE);
	}
    }

    ac_init(AC_ALL);

    /* ------------------------------------------------------------
     *
     * fill out defaults for info structure
     *
     * ------------------------------------------------------------*/

    // assume defaults
    if(name==NULL) stream_stype=TC_STYPE_STDIN;

    // no autodetection yet
    if(codec==NULL && magic==NULL) {
      tc_log_error(EXE, "invalid codec %s", codec);
      usage(EXIT_FAILURE);
    }

    if(codec==NULL) codec="";

    // do not try to mess with the stream
    if(stream_stype!=TC_STYPE_STDIN) {

      if(tc_file_check(name)) exit(1);

      if((ipipe.fd_in = xio_open(name, O_RDONLY))<0) {
	tc_log_perror(EXE, "file open");
	return(-1);
      }

      stream_magic = fileinfo(ipipe.fd_in, 0);

      if(verbose & TC_DEBUG)
	tc_log_msg(EXE, "(pid=%d) %s", getpid(), filetype(stream_magic));

    } else ipipe.fd_in = STDIN_FILENO;

    if(verbose & TC_DEBUG)
	tc_log_msg(EXE, "(pid=%d) starting, doing %s", getpid(), codec);

    // fill out defaults for info structure
    ipipe.fd_out = STDOUT_FILENO;

    ipipe.magic   = stream_magic;
    ipipe.stype   = stream_stype;
    ipipe.codec   = stream_codec;
    ipipe.track   = track;
    ipipe.select  = TC_VIDEO;

    ipipe.verbose = verbose;

    ipipe.name = name;

    /* ------------------------------------------------------------
     *
     * codec specific section
     *
     * note: user provided magic values overwrite autodetection!
     *
     * ------------------------------------------------------------*/

    if(magic==NULL) magic="";

    // OGM

    if (ipipe.magic == TC_MAGIC_OGG) {

	// dummy for video
	if(strcmp(codec, "raw")==0) ipipe.codec = TC_CODEC_RGB24;
	if((strcmp(codec, "vorbis")==0) || (strcmp(codec, "ogg")==0)) {
	    ipipe.codec = TC_CODEC_VORBIS;
	    ipipe.select = TC_AUDIO;
	}
	if(strcmp(codec, "mp3")==0) {
	    ipipe.codec = TC_CODEC_MP3;
	    ipipe.select = TC_AUDIO;
	}
	if(strcmp(codec, "pcm")==0) {
	    ipipe.codec = TC_CODEC_PCM;
	    ipipe.select = TC_AUDIO;
	}

	extract_ogm(&ipipe);
	done = 1;
    }

    // MPEG2
    if(strcmp(codec,"mpeg2")==0) {

      ipipe.codec = TC_CODEC_MPEG2;

      if(strcmp(magic, "vob")==0) ipipe.magic = TC_MAGIC_VOB;
      if(strcmp(magic, "m2v")==0) ipipe.magic = TC_MAGIC_M2V;
      if(strcmp(magic, "raw")==0) ipipe.magic = TC_MAGIC_RAW;

      extract_mpeg2(&ipipe);
      done = 1;
    }

    // PCM
    if(strcmp(codec,"pcm")==0) {

	ipipe.codec = TC_CODEC_PCM;
	ipipe.select = TC_AUDIO;

	if(strcmp(magic, "vob")==0) ipipe.magic = TC_MAGIC_VOB;
	if(strcmp(magic, "avi")==0) ipipe.magic = TC_MAGIC_AVI;
	if(strcmp(magic, "raw")==0) ipipe.magic = TC_MAGIC_RAW;
	if(strcmp(magic, "wav")==0) ipipe.magic = TC_MAGIC_WAV;

	extract_pcm(&ipipe);
	done = 1;
    }

    // SUBTITLE (private_stream_1)
    if(strcmp(codec,"ps1")==0) {

	ipipe.codec = TC_CODEC_PS1;
	ipipe.select = TC_AUDIO;

	if(strcmp(magic, "vob")==0) ipipe.magic = TC_MAGIC_VOB;
	if(strcmp(magic, "vdr")==0) ipipe.magic = TC_MAGIC_VDR;

	extract_ac3(&ipipe);
	done = 1;
    }


    // DV
    if(strcmp(codec,"dv")==0) {

	ipipe.codec = TC_CODEC_DV;

	if(strcmp(magic, "avi")==0) ipipe.magic = TC_MAGIC_AVI;
	if(strcmp(magic, "raw")==0) ipipe.magic = TC_MAGIC_RAW;

	extract_dv(&ipipe);
	done = 1;
    }


    // RGB
    if(strcmp(codec,"rgb")==0) {

	ipipe.codec = TC_CODEC_RGB24;

	if(strcmp(magic, "avi")==0) ipipe.magic = TC_MAGIC_AVI;
	if(strcmp(magic, "raw")==0) ipipe.magic = TC_MAGIC_RAW;
	if(strcmp(magic, "wav")==0) ipipe.magic = TC_MAGIC_WAV;

	extract_rgb(&ipipe);
	done = 1;
    }


    // DTS
    if(strcmp(codec,"dts")==0) {

	ipipe.codec = TC_CODEC_DTS;
	ipipe.select = TC_AUDIO;

	if(strcmp(magic, "raw")==0) ipipe.magic = TC_MAGIC_RAW;
	if(strcmp(magic, "vob")==0) ipipe.magic = TC_MAGIC_VOB;

	extract_ac3(&ipipe);
	done = 1;
    }

    // AC3
    if(strcmp(codec,"ac3")==0) {

	ipipe.codec = TC_CODEC_AC3;
	ipipe.select = TC_AUDIO;

	if(strcmp(magic, "raw")==0) ipipe.magic = TC_MAGIC_RAW;
	if(strcmp(magic, "vob")==0) ipipe.magic = TC_MAGIC_VOB;

	extract_ac3(&ipipe);
	done = 1;
    }

    // MP3
    if(strcmp(codec,"mp3")==0 || strcmp(codec,"mp2")==0) {

	ipipe.codec = TC_CODEC_MP3;
	ipipe.select = TC_AUDIO;

	if(strcmp(magic, "avi")==0) ipipe.magic = TC_MAGIC_AVI;
	if(strcmp(magic, "raw")==0) ipipe.magic = TC_MAGIC_RAW;
	if(strcmp(magic, "vob")==0) ipipe.magic = TC_MAGIC_VOB;

	extract_mp3(&ipipe);
	done = 1;
    }

    // YUV420P
    if(strcmp(codec,"yuv420p")==0) {

	ipipe.codec = TC_CODEC_YUV420P;

	if(strcmp(magic, "avi")==0) ipipe.magic = TC_MAGIC_AVI;
	if(strcmp(magic, "raw")==0) ipipe.magic = TC_MAGIC_RAW;
	if(strcmp(magic, "yuv4mpeg")==0) ipipe.magic = TC_MAGIC_YUV4MPEG;

	extract_yuv(&ipipe);
	done = 1;
    }

    // YUV422P
    if(strcmp(codec,"yuv422p")==0) {

	ipipe.codec = TC_CODEC_YUV422P;

	if(strcmp(magic, "avi")==0) ipipe.magic = TC_MAGIC_AVI;
	if(strcmp(magic, "raw")==0) ipipe.magic = TC_MAGIC_RAW;
	if(strcmp(magic, "yuv4mpeg")==0) ipipe.magic = TC_MAGIC_YUV4MPEG;

	extract_yuv(&ipipe);
	done = 1;
    }

    // UYVY
    if(strcmp(codec,"uyvy")==0) {

	ipipe.codec = TC_CODEC_UYVY;

	if(strcmp(magic, "avi")==0) ipipe.magic = TC_MAGIC_AVI;
	if(strcmp(magic, "raw")==0) ipipe.magic = TC_MAGIC_RAW;

	extract_yuv(&ipipe);
	done = 1;
    }


    // LZO
    if(strcmp(codec,"lzo")==0) {

	ipipe.codec = TC_CODEC_YUV420P;

	if(strcmp(magic, "avi")==0) ipipe.magic = TC_MAGIC_AVI;
	if(strcmp(magic, "raw")==0) ipipe.magic = TC_MAGIC_RAW;

	extract_lzo(&ipipe);
	done = 1;
    }


    // AVI extraction

    //need to check if there isn't a codec from the input option (if we have a file with TC_MAGIC_AVI and we specify -x pcm we have pcm and rgb output)
    if ((strcmp(magic, "avi")==0 || ipipe.magic==TC_MAGIC_AVI)&& (codec == NULL)) {

	ipipe.magic=TC_MAGIC_AVI;
	extract_avi(&ipipe);
	done = 1;
    }

    if (strcmp(codec, "raw")==0 || strcmp(codec, "video")==0) {
	ipipe.select=TC_VIDEO-1;
	ipipe.magic=TC_MAGIC_AVI;
	extract_avi(&ipipe);
	done = 1;
    }


    if(!done) {
	tc_log_error(EXE, "(pid=%d) unable to handle codec %s", getpid(), codec);
	exit(1);
    }

    if(ipipe.fd_in != STDIN_FILENO) xio_close(ipipe.fd_in);

    return(0);
}

#include "libtcutil/static_xio.h"

