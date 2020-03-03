/*
 *  tcmp3cut.c
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

#include "transcode.h"
#include "framebuffer.h"
#include "aud_scan.h"

#include <sys/errno.h>


#define EXE "tcmp3cut"

int verbose=TC_QUIET;

#define CHUNK_SIZE 4096

static int min=0, max=0;



/* ------------------------------------------------------------
 *
 * print a usage/version message
 *
 * ------------------------------------------------------------*/

void version(void)
{
    printf("%s (%s v%s) (C) 2003 Tilmann Bitterberg\n", EXE, PACKAGE, VERSION);
}

static void usage(int status)
{
  version();

  fprintf(stderr,"\nUsage: %s [options]\n", EXE);

  fprintf(stderr,"    -i file           input file name\n");
  fprintf(stderr,"    -o base           output file name base\n");
  fprintf(stderr,"    -e r[,b[,c]]      MP3 audio stream parameter [%d,%d,%d]\n", RATE, BITS, CHANNELS);
  fprintf(stderr,"    -t c1[,c2[,.]]    cut points in milliseconds\n");
  fprintf(stderr,"    -d mode           verbosity mode\n");
  fprintf(stderr,"    -v                print version\n");

  exit(status);

}


/* ------------------------------------------------------------
 *
 * scan stream
 *
 * ------------------------------------------------------------*/

#define MAX_SONGS 50

int main(int argc, char *argv[])
{


  int fd=-1;
  FILE *out=NULL;

  int n=0, ch;
  char *name=NULL, *offset=NULL, *base=NULL;
  char outfile[1024];
  int cursong=0;

  int bytes_read;

  uint64_t total=0;

  int a_rate=RATE, a_bits=BITS, chan=CHANNELS;
  int songs[MAX_SONGS];
  int numsongs=0;

  int on=1;

  char buffer[CHUNK_SIZE];

  uint32_t i=0;

  if (argc<2)
      usage(EXIT_SUCCESS);

  while ((ch = getopt(argc, argv, "o:e:i:t:d:v?h")) != -1) {

    switch (ch) {
    case 'd':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      verbose = atoi(optarg);

      break;

    case 'e':

      if(optarg[0]=='-') usage(EXIT_FAILURE);

      if (3 != sscanf(optarg,"%d,%d,%d", &a_rate, &a_bits, &chan)) fprintf(stderr, "invalid pcm parameter set for option -e");

      if(a_rate > RATE || a_rate <= 0) {
	fprintf(stderr, "invalid pcm parameter 'rate' for option -e");
	usage(EXIT_FAILURE);
      }

      if(!(a_bits == 16 || a_bits == 8)) {
	fprintf(stderr, "invalid pcm parameter 'bits' for option -e");
	usage(EXIT_FAILURE);
      }

      if(!(chan == 0 || chan == 1 || chan == 2)) {
	fprintf(stderr, "invalid pcm parameter 'channels' for option -e");
	usage(EXIT_FAILURE);
      }

      break;

    case 'i':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      name = optarg;
      break;

    case 't':
      if(optarg[0]=='-') usage(EXIT_FAILURE);

      offset = optarg;
      i=0;
      songs[i]=atoi(offset);
      while ((offset = strchr(offset,','))) {
	  offset++;
	  i++;
	  songs[i]=atoi(offset);
      }
      numsongs=i+1;
      break;

    case 'o':
      if(optarg[0]=='-') usage(EXIT_FAILURE);
      base = optarg;
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

  printf("Got %d songs:\n", numsongs);
  for (n=0; n<numsongs; n++)
      printf("%d : %d\n", n, songs[n]);


  if (!name) {
      fprintf(stderr, "No filename given\n");
      exit (EXIT_FAILURE);
  }

  if ( (fd = open(name, O_RDONLY)) < 0) {
      perror("open()");
      return -1;
  }

  if ( (fd = open(name, O_RDONLY)) < 0) {
      perror("open()");
      return -1;
  }

  tc_snprintf(outfile, sizeof(outfile), "%s-%04d.mp3", base, cursong);
  if ( (out = fopen(outfile, "w")) == NULL) {
      perror ("fopen() output");
      return -1;
  }

  if(1) {

      uint8_t header[4];
      int framesize = 0;
      int chunks = 0;
      int srate=0 , chans=0, bitrate=0;
      off_t pos=0;
      double ms = 0;

      min = 500;
      max = 0;

      pos = lseek(fd, 0, SEEK_CUR);
      // find mp3 header
      while ((total += read(fd, header, 4))) {
	  if ( (framesize = tc_get_mp3_header (header, &chans, &srate, &bitrate)) > 0) {
	      if (fwrite (header, 4, 1, out) != 1) {
                  perror("write frame header");
                  return -1;
              }
	      ms += (framesize*8)/(bitrate);
	      break;
	  }
	  pos++;
	  lseek(fd, pos, SEEK_SET);
      }
      printf("POS %lld\n", (long long)pos);

      // Example for _1_ mp3 chunk
      //
      // fps       = 25
      // framesize = 480 bytes
      // bitrate   = 160 kbit/s == 20 kbytes/s == 20480 bytes/s == 819.20 bytes / frame
      //
      // 480 bytes = 480/20480 s/bytes = .0234 s = 23.4 ms
      //
      //  ms = (framesize*1000*8)/(bitrate*1000);
      //                           why 1000 and not 1024?
      //  correct? yes! verified with "cat file.mp3|mpg123 -w /dev/null -v -" -- tibit

      while (on) {
	  if ( (bytes_read = read(fd, buffer, framesize-4)) != framesize-4) {
	      on = 0;
	  } else {
	      total += bytes_read;
	      if (fwrite (buffer, bytes_read, 1, out) != 1) {
                  perror("write");
                  return -1;
              }
	      while ((total += read(fd, header, 4))) {

		  //printf("%x %x %x %x\n", header[0]&0xff, header[1]&0xff, header[2]&0xff, header[3]&0xff);

		  if ( (framesize = tc_get_mp3_header (header, &chans, &srate, &bitrate)) < 0) {
		      fprintf(stderr, "[%s] corrupt mp3 file?\n", EXE);
		      on = 0;
		      break;
		  } else  {

		      /*
		      printf("Found new header (%d) (framesize = %d) chan(%d) srate(%d) bitrate(%d)\n",
			  chunks, framesize, chans, srate, bitrate);
			  */

		      ms += (framesize*8)/(bitrate);
		      // close/open

		      if (ms>=songs[cursong]) {
			  fclose(out);
			  cursong++;
			  if (cursong>numsongs)
			      break;
			  tc_snprintf(outfile, sizeof(outfile), "%s-%04d.mp3", base, cursong);
			  if ( (out = fopen(outfile, "w")) == NULL) {
			      perror ("fopen() output");
			      return -1;
			  }
		      }
		      if (fwrite (header, 4, 1, out) != 1) {
                          perror("write");
                          return -1;
                      }

		      ++chunks;
		      break;
		  }
	      }


	  }
      }
      fclose(out);
      close(fd);
      return(0);
  }


  fprintf(stderr, "[%s] unable to handle codec/filetype\n", EXE);

  exit(1);

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
