/*
 *  tcscan.c
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
#include "libtc/ratiocodes.h"
#include "avilib/avilib.h"

#include "ioaux.h"
#include "tc.h"

#include <math.h>

#define EXE "tcscan"

int verbose=TC_QUIET;

int tc_get_mp3_header(unsigned char* hbuf, int* chans, int* srate, int *bitrate);
#define tc_decode_mp3_header(hbuf)  tc_get_mp3_header(hbuf, NULL, NULL, NULL)

void import_exit(int code)
{
  if(verbose & TC_DEBUG)
    tc_log_msg(EXE, "(pid=%d) exit (code %d)", (int) getpid(), code);
  exit(code);
}

#define CHUNK_SIZE 4096

static int min=0, max=0;

static void check (int v)
{

  if (v > max) {
    max = v;
  } else if (v < min) {
    min = v;
  }

  return;
}

/*************************************************************************/

/* from ac3scan.c */

static int get_ac3_bitrate(uint8_t *ptr)
{
    static const int bitrates[] = {
	32, 40, 48, 56,
	64, 80, 96, 112,
	128, 160, 192, 224,
	256, 320, 384, 448,
	512, 576, 640
    };
    int ratecode = (ptr[2] & 0x3E) >> 1;
    if (ratecode < sizeof(bitrates)/sizeof(*bitrates))
	return bitrates[ratecode];
    return -1;
}

static int get_ac3_samplerate(uint8_t *ptr)
{
    static const int samplerates[] = {48000, 44100, 32000, -1};
    return samplerates[ptr[2]>>6];
}

static int get_ac3_framesize(uint8_t *ptr)
{
    int bitrate = get_ac3_bitrate(ptr);
    int samplerate = get_ac3_samplerate(ptr);
    if (bitrate < 0 || samplerate < 0)
	return -1;
    return bitrate * 96000 / samplerate + (samplerate==44100 ? ptr[2]&1 : 0);
}

/*************************************************************************/

/* enc_bitrate:  Print bitrate information about the source data.
 *
 * Parameters:
 *       frames: Number of frames in the source.
 *          fps: Frames per second of the source.
 *     abitrate: Audio bitrate (bits per second).
 *     discsize: User-specified disc size in bytes, or 0 for none.
 * Return value:
 *     None.
 */

static void enc_bitrate(long frames, double fps, int abitrate, double discsize)
{
    static const int defsize[] = {650, 700, 1300, 1400};
    long time;
    double audiosize, videosize, vbitrate;

    if (frames <= 0 || fps <= 0.0)
         return;
    time = frames / fps;
    audiosize = (double)abitrate/8 * time;

    /* Print basic source information */
    printf("[%s] V: %ld frames, %ld sec @ %.3f fps\n",
	   EXE, frames, time, fps);
    printf("[%s] A: %.2f MB @ %d kbps\n",
	   EXE, audiosize/(1024*1024), abitrate/1000);

    /* Print recommended bitrates for user-specified or default disc sizes */
    if (discsize) {
        videosize = discsize - audiosize;
        vbitrate = videosize / time * 8;
        printf("USER CDSIZE: %4d MB | V: %6.1f MB @ %.1f kbps\n",
               (int)floor(discsize/(1024*1024)), videosize/(1024*1024),
               vbitrate/1024);
    } else {
        int i;
        for (i = 0; i < sizeof(defsize) / sizeof(*defsize); i++) {
            videosize = defsize[i]*1024*1024 - audiosize;
            vbitrate = videosize / time * 8;
            printf("USER CDSIZE: %4d MB | V: %6.1f MB @ %.1f kbps\n",
                   defsize[i], videosize/(1024*1024),
                   vbitrate/1024);
        }
    }
}

/*************************************************************************/

/* ------------------------------------------------------------
 *
 * print a usage/version message
 *
 * ------------------------------------------------------------*/

void version(void)
{
    /* print id string to stderr */
    fprintf(stderr, "%s (%s v%s) (C) 2001-2003 Thomas Oestreich,"
                                   " 2003-2010 Transcode Team\n",
                    EXE, PACKAGE, VERSION);
}


static void usage(int status)
{
  version();

  fprintf(stderr,"\nUsage: %s [options]\n", EXE);

  fprintf(stderr,"    -i file           input file name [stdin]\n");
  fprintf(stderr,"    -x codec          source codec\n");
  fprintf(stderr,"    -e r[,b[,c]]      PCM audio stream parameter [%d,%d,%d]\n", RATE, BITS, CHANNELS);
  fprintf(stderr,"    -f rate,frc       frame rate [%.3f][,frc]\n", PAL_FPS);
  fprintf(stderr,"    -w num            estimate bitrate for num frames\n");
  fprintf(stderr,"    -b bitrate        audio encoder bitrate kBits/s [%d]\n", ABITRATE);
  fprintf(stderr,"    -c cdsize         user defined CD size in MB [0]\n");
  fprintf(stderr,"    -d mode           verbosity mode\n");
  fprintf(stderr,"    -v                print version\n");

  exit(status);

}


/* ------------------------------------------------------------
 *
 * scan stream
 *
 * ------------------------------------------------------------*/

int main(int argc, char *argv[])
{

  info_t ipipe;

  long stream_stype = TC_STYPE_UNKNOWN;

  long magic = TC_MAGIC_UNKNOWN;

  FILE *in_file;

  int n=0, ch;
  char *codec=NULL, *name=NULL;

  int bytes_per_sec, bytes_read, bframes=0;

  uint64_t total=0;

  int a_rate=RATE, a_bits=BITS, chan=CHANNELS;

  int on=1;
  short *s;

  char buffer[CHUNK_SIZE];

  double fps=PAL_FPS, frames, fmin, fmax, vol=0.0;

  int frc;

  int pseudo_frame_size=0;

  int ac_bytes=0, frame_size, bitrate=ABITRATE;

  float rbytes;

  uint32_t i=0, j=0;
  uint16_t sync_word = 0;
  double cdsize = 0.0;

  //proper initialization
  memset(&ipipe, 0, sizeof(info_t));

  libtc_init(&argc, &argv);

  while ((ch = getopt(argc, argv, "c:b:e:i:vx:f:d:w:?h")) != -1) {

    switch (ch) {
    case 'c':
      if(optarg[0]=='-') usage(EXIT_FAILURE);
      cdsize = atof(optarg) * (1024*1024);  /* MB -> bytes */

      break;

    case 'd':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      verbose = atoi(optarg);

      break;

    case 'e':

      if(optarg[0]=='-') usage(EXIT_FAILURE);

      if (3 != sscanf(optarg,"%d,%d,%d", &a_rate, &a_bits, &chan)) {
	tc_log_error(EXE, "invalid pcm parameter set for option -e");
	usage(EXIT_FAILURE);
      }

      if(a_rate > RATE || a_rate <= 0) {
	tc_log_error(EXE, "invalid pcm parameter 'rate' for option -e");
	usage(EXIT_FAILURE);
      }

      if(!(a_bits == 16 || a_bits == 8)) {
	tc_log_error(EXE, "invalid pcm parameter 'bits' for option -e");
	usage(EXIT_FAILURE);
      }

      if(!(chan == 0 || chan == 1 || chan == 2)) {
	tc_log_error(EXE, "invalid pcm parameter 'channels' for option -e");
	usage(EXIT_FAILURE);
      }

      break;

    case 'i':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      name = optarg;
      break;

    case 'x':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      codec = optarg;
      break;

    case 'f':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      n = sscanf(optarg,"%lf,%d", &fps, &frc);

      if (n == 2 && (frc > 0 && frc <= 0x10))
        tc_frc_code_to_value(frc, &fps);

      if(fps<=0) {
	tc_log_error(EXE,"invalid frame rate for option -f");
	exit(1);
      }
      break;

    case 'w':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      bframes = atoi(optarg);

      if(bframes <=0) {
	tc_log_error(EXE,"invalid parameter for option -w");
	exit(1);
      }
      break;

    case 'b':

      if(optarg[0]=='-') usage(EXIT_FAILURE);
      bitrate = atoi(optarg);

      if(bitrate < 0) {
	tc_log_error(EXE,"invalid bitrate for option -b");
	exit(1);
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

  // simple bitrate calculator
  if(bframes) {
    enc_bitrate(bframes, fps, bitrate*1000, cdsize);
    exit(0);
  }

  // assume defaults
  if(name==NULL) stream_stype=TC_STYPE_STDIN;

  // no autodetection yet
  if(codec==NULL && name == NULL) {
    tc_log_error(EXE, "invalid codec %s", codec);
    usage(EXIT_FAILURE);
  }

  if(codec==NULL) codec="";

  // do not try to mess with the stream
  if(stream_stype!=TC_STYPE_STDIN) {

    if(tc_file_check(name)) exit(1);

    if((ipipe.fd_in = xio_open(name, O_RDONLY))<0) {
      tc_log_perror(EXE, "open file");
      exit(1);
    }

    magic = fileinfo(ipipe.fd_in, 0);

  } else ipipe.fd_in = STDIN_FILENO;


  /* ------------------------------------------------------------
   *
   * AC3 stream
   *
   * ------------------------------------------------------------*/

  if(strcmp(codec,"ac3")==0 || magic==TC_MAGIC_AC3) {


    for(;;) {

      for(;;) {

	if (tc_pread(ipipe.fd_in, buffer, 1) !=1) {
	  tc_log_perror(EXE, "ac3 sync frame scan failed");
	  goto ac3_summary;
	}

	sync_word = (sync_word << 8) + (uint8_t) buffer[0];

	++i;

	if(sync_word == 0x0b77) break;
      }

      i=i-2;

      if (tc_pread(ipipe.fd_in, buffer, 3) !=3) {
	tc_log_perror(EXE, "ac3 header read failed");
	goto ac3_summary;
      }

      if((frame_size = 2*get_ac3_framesize(buffer)) < 1) {
	tc_log_warn(EXE, "ac3 framesize %d invalid - frame broken?", frame_size);
	goto more;
      }

      //FIXME: I assume that a single AC3 frame contains 6kB PCM bytes

      rbytes = (float) SIZE_PCM_FRAME/1024/6 * frame_size;
      pseudo_frame_size = (int) rbytes;
      bitrate = get_ac3_bitrate(buffer);

      printf("[%s] [%05d] offset %06d (%06d) %04d bytes, bitrate %03d kBits/s\n", EXE, n++, i, j, frame_size, bitrate);

      // read the rest

      ac_bytes = frame_size-5;

      if(ac_bytes>CHUNK_SIZE) {
	tc_log_error(EXE, "Oops, no buffer space framesize %d", ac_bytes);
	exit(1);
      }

      if ((bytes_read=tc_pread(ipipe.fd_in, buffer, ac_bytes)) != ac_bytes) {
	tc_log_warn(EXE, "error reading ac3 frame (%d/%d)", bytes_read, ac_bytes);
	break;
      }

    more:

      i+=frame_size;
      j=i;
    }

  ac3_summary:

    vol = (double) (n * 1024 * 6)/4/RATE;

    printf("[%s] valid AC3 frames=%d, estimated clip length=%.2f seconds\n", EXE, n, vol);

    return(0);
  }

  /* ------------------------------------------------------------
   *
   * PCM stream
   *
   * ------------------------------------------------------------*/

  if(strcmp(codec,"pcm")==0) {

      while(on) {

	  if( (bytes_read = tc_pread(ipipe.fd_in, buffer, CHUNK_SIZE)) != CHUNK_SIZE) on = 0;

	  total += (uint64_t) bytes_read;

	  s=(short *) buffer;

	  for(n=0; n<bytes_read>>1; ++n) {
	      check((int) (*s));
	      s++;
	  }
      }

      bytes_per_sec = a_rate * (a_bits/8) * chan;

      frames = (fps*((double)total)/bytes_per_sec);

      fmin = -((double) min)/SHRT_MAX;
      fmax =  ((double) max)/SHRT_MAX;

      if(min==0 || max == 0) exit(0);

      vol = (fmin<fmax) ? 1./fmax : 1./fmin;

      printf("[%s] audio frames=%.2f, estimated clip length=%.2f seconds\n", EXE, frames, frames/fps);
      printf("[%s] (min/max) amplitude=(%.3f/%.3f), suggested volume rescale=%.3f\n", EXE, -fmin, fmax, vol);

      enc_bitrate((long) frames, fps, bitrate*1000, cdsize);

      return(0);
  }

  if(strcmp(codec,"mp3")==0 || magic == TC_MAGIC_MP3) {

      char header[4];
      int framesize = 0;
      int chunks = 0;
      int srate=0 , chans=0, bitrate=0;
      unsigned long bitrate_add = 0;
      off_t pos=0;
      double ms = 0;
      char bitrate_buf[TC_BUF_MIN];

      min = 500;
      max = 0;

      pos = lseek(ipipe.fd_in, 0, SEEK_CUR);
      // find mp3 header
      while ((total += read(ipipe.fd_in, header, 4))) {
	  if ( (framesize = tc_get_mp3_header (header, &chans, &srate, &bitrate)) > 0) {
	      break;
	  }
	  pos++;
	  lseek(ipipe.fd_in, pos, SEEK_SET);
      }
      tc_log_msg(EXE, "POS %lld", (long long)pos);

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
	  if ( (bytes_read = read(ipipe.fd_in, buffer, framesize-4)) != framesize-4) {
	      on = 0;
	  } else {
	      total += bytes_read;
	      while ((total += read(ipipe.fd_in, header, 4))) {

		  //tc_log_msg(EXE, "%x %x %x %x", header[0]&0xff, header[1]&0xff, header[2]&0xff, header[3]&0xff);

		  if ( (framesize = tc_get_mp3_header (header, &chans, &srate, &bitrate)) < 0) {
		      tc_log_warn(EXE, "corrupt mp3 file?");
		      on = 0;
		      break;
		  } else  {

		      /*
		      tc_log_msg(EXE, "Found new header (%d) (framesize = %d) chan(%d) srate(%d) bitrate(%d)",
			  chunks, framesize, chans, srate, bitrate);
			  */

		      bitrate_add += bitrate;
		      check(bitrate);
		      ms += (framesize*8)/(bitrate);
		      ++chunks;
		      break;
		  }
	      }


	  }
      }

      if (min != max)
	tc_snprintf(bitrate_buf, sizeof(bitrate_buf), "(%d-%d)", min, max);
      else
	tc_snprintf(bitrate_buf, sizeof(bitrate_buf), "(cbr)");
      printf("[%s] MPEG-1 layer-3 stream. Info: -e %d,%d,%d\n", EXE,
		  srate, 16, chans);
      printf("[%s] Found %d MP3 chunks. Average bitrate is %3.2f kbps %s\n", EXE,
		  chunks, (double)bitrate_add/chunks, bitrate_buf);
      printf("[%s] AVI overhead will be max. %d*(8+16) = %d bytes (%dk)\n", EXE,
		  chunks, chunks*8+chunks*16, (chunks*8+chunks*16)/1024);
      printf("[%s] Estimated time is %.0f ms (%02d:%02d:%02d.%02d)\n", EXE,
		  ms,
		  (int)(ms/1000.0/60.0/60.0),
		  (int)(ms/1000.0/60.0)%60,
		  (int)(ms/1000)%60,
		  (int)(ms)%(1000));
      return(0);
  }

  /* ------------------------------------------------------------
   *
   * MPEG program stream
   *
   * ------------------------------------------------------------*/

  if(strcmp(codec, "mpeg2")==0 || strcmp(codec, "mpeg")==0 || strcmp(codec, "vob")==0 || magic==TC_MAGIC_VOB || magic==TC_MAGIC_M2V) {

    in_file = fdopen(ipipe.fd_in, "r");

    scan_pes(verbose, in_file);

    return(0);
  }

  /* ------------------------------------------------------------
   *
   * AVI
   *
   * ------------------------------------------------------------*/

  if(magic==TC_MAGIC_AVI || TC_MAGIC_WAV) {

      if(name!=NULL) AVI_scan(name);

      return(0);
  }


  tc_log_error(EXE, "unable to handle codec/filetype %s", codec);

  exit(1);

}


// from mencoder
//----------------------- mp3 audio frame header parser -----------------------

static int tabsel_123[2][3][16] = {
   { {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0},
     {0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384,0},
     {0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320,0} },

   { {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0} }
};
static long freqs[9] = { 44100, 48000, 32000, 22050, 24000, 16000 , 11025 , 12000 , 8000 };

/*
 * return frame size or -1 (bad frame)
 */
int tc_get_mp3_header(unsigned char* hbuf, int* chans, int* srate, int *bitrate){
    int stereo, ssize, crc, lsf, mpeg25, framesize;
    int padding, bitrate_index, sampling_frequency;
    unsigned long newhead =
      hbuf[0] << 24 |
      hbuf[1] << 16 |
      hbuf[2] <<  8 |
      hbuf[3];


#if 1
    // head_check:
    if( (newhead & 0xffe00000) != 0xffe00000 ||
        (newhead & 0x0000fc00) == 0x0000fc00){
	//tc_log_warn(EXE, "head_check failed");
	return -1;
    }
#endif

    if((4-((newhead>>17)&3))!=3){
      //tc_log_warn(EXE, "not layer-3");
      return -1;
    }

    if( newhead & ((long)1<<20) ) {
      lsf = (newhead & ((long)1<<19)) ? 0x0 : 0x1;
      mpeg25 = 0;
    } else {
      lsf = 1;
      mpeg25 = 1;
    }

    if(mpeg25)
      sampling_frequency = 6 + ((newhead>>10)&0x3);
    else
      sampling_frequency = ((newhead>>10)&0x3) + (lsf*3);

    if(sampling_frequency>8){
	tc_log_warn(EXE, "invalid sampling_frequency");
	return -1;  // valid: 0..8
    }

    crc = ((newhead>>16)&0x1)^0x1;
    bitrate_index = ((newhead>>12)&0xf);
    padding   = ((newhead>>9)&0x1);
//    fr->extension = ((newhead>>8)&0x1);
//    fr->mode      = ((newhead>>6)&0x3);
//    fr->mode_ext  = ((newhead>>4)&0x3);
//    fr->copyright = ((newhead>>3)&0x1);
//    fr->original  = ((newhead>>2)&0x1);
//    fr->emphasis  = newhead & 0x3;

    stereo    = ( (((newhead>>6)&0x3)) == 3) ? 1 : 2;

    if(!bitrate_index){
      tc_log_warn(EXE, "Free format not supported.");
      return -1;
    }

    if(lsf)
      ssize = (stereo == 1) ? 9 : 17;
    else
      ssize = (stereo == 1) ? 17 : 32;
    if(crc) ssize += 2;

    framesize = tabsel_123[lsf][2][bitrate_index] * 144000;
    if (bitrate) *bitrate = tabsel_123[lsf][2][bitrate_index];

    if(!framesize){
	tc_log_warn(EXE, "invalid framesize/bitrate_index");
	return -1;  // valid: 1..14
    }

    framesize /= freqs[sampling_frequency]<<lsf;
    framesize += padding;

//    if(framesize<=0 || framesize>MAXFRAMESIZE) return FALSE;
    if(srate) *srate = freqs[sampling_frequency];
    if(chans) *chans = stereo;

    return framesize;
}

#include "libtcutil/static_xio.h"

