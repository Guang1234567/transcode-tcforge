/*
 *  aviindex.c
 *
 *  extracts the index of an AVI file for easy seeking with --nav_seek
 *
 *  Copyright (C) Tilmann Bitterberg - June 2003
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libtcutil/xio.h"
#include "aud_scan.h"
#include "avimisc.h"

#define EXE "aviindex"


void version(void)
{
  printf("%s (%s v%s) (C) 2003-2004 Tilmann Bitterberg,"
                        " 2004-2010 Transcode Team\n",
                      EXE, PACKAGE, VERSION);
}


static void usage(int status)
{
  version();
  printf("\nUsage: %s [options]\n", EXE);
  printf("    -o file   output file\n");
  printf("    -i file   input file\n");
  printf("    -f        force the use of the existing index\n");
  printf("              only to use when avi > 2GB, because\n");
  printf("              the default is to -n with big files\n");
  printf("    -n        read index in \"smart\" mode: don't use the existing index\n");
  printf("    -x        don't use the existing index to generate the keyframes\n");
  printf("              this flag forces -n\n");
  printf("    -v        print version\n");
  exit(status);
}

#define PAD_EVEN(x) ( ((x)+1) & ~1 )
static unsigned long str2ulong(unsigned char *str)
{
   return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}

#define AVI_MAX_LEN (UINT_MAX-(1<<20)*16-2048)

static inline uint32_t SWAP(uint32_t a) {
#ifdef WORDS_BIGENDIAN
    return ( (a<<24&0xff000000) | (a<< 8&0x00ff0000) |
	     (a>> 8&0x0000ff00) | (a>>24&0x000000ff));
#else
    return a;
#endif
}

static inline int avi_stream_id(unsigned int id){
  unsigned char *p=(unsigned char *)&id;
  unsigned char a,b;
#if WORDS_BIGENDIAN
  a=p[3]-'0'; b=p[2]-'0';
#else
  a=p[0]-'0'; b=p[1]-'0';
#endif
  if(a>9 || b>9) return 100; // invalid ID
  return a*10+b;
}

// returns 1 for video
//         2 for 1st audio
//         3 for 2nd audio
static int avi_stream_nr(int id)
{
    unsigned char *p = (unsigned char *)&id;

#if WORDS_BIGENDIAN
    if (p[1] == 'd') {
	return 1;
    } else  {
	int res = (p[3]-'0')+(p[2]-'0');
	res = (res==0)?2:res+1;
	return res;
    }
#else
    if (p[2] == 'd') {
	return 1;
    } else {
	int res = (p[0]-'0')+(p[1]-'0');
	res = (res==0)?2:res+1;
	return res;
    }
#endif
    return 0;
}

const int LEN=10;

typedef enum {
    UNKNOWN = 0,
    RIFF,
    AVIIDX1,
    MPIDX1,
} ftype_t;

typedef struct {
    uint32_t ckid;
    uint32_t dwFlags;
    uint32_t dwChunkOffset;
    uint32_t dwChunkLength;
} AVIINDEXENTRY;

static int aviidx1_to_mpidx1(char *in_file, FILE *out_fd)
{
    char *data;
    FILE *in;
    int size=0, i;
    AVIINDEXENTRY *idx;
    char line[255], d;
    char *dummy;  // for avoiding compiler warnings

    in = fopen (in_file, "r");
    if (!in) return 1;

    // skip header
    dummy = fgets (line, sizeof(line), in);
    dummy = fgets (line, sizeof(line), in);
    while (fgets(line, sizeof(line), in)) {
	d = line[5] - '1';
	if        (d == 0) {
	    size++;
	} else if (d == 1 || d == 2 || d == 3 || d == 4 ||
		   d == 5 || d == 6 || d == 7 || d == 8  ) {
	    size++;
	} else continue;
    }
    data = malloc (size * sizeof (AVIINDEXENTRY));
    fseek(in, 0, SEEK_SET);
    dummy = fgets (line, sizeof(line), in);
    dummy = fgets (line, sizeof(line), in);

    i = size;
    idx = &((AVIINDEXENTRY *)data)[0];
    while (fgets(line, sizeof(line), in) && i--) {
	char *c=line;
	idx->ckid=*(int *)c;
	idx->ckid=SWAP(idx->ckid);
	c = strchr(c, ' ')+1; // type
	c = strchr(c, ' ')+1; // chunk
	c = strchr(c, ' ')+1; // chunk/type
	c = strchr(c, ' ')+1; // pos
	idx->dwChunkOffset = strtol(c, &c, 10);
	idx->dwChunkLength = strtol(c+1, &c, 10);
	idx->dwFlags = strtol(c+1, &c, 10);
	idx->dwFlags = idx->dwFlags?0x10:0;
	idx++;

    }
    if (fwrite ("MPIDX1", 6, 1, out_fd) != 1
     || fwrite (&size, 4, 1, out_fd) != 1
     || fwrite (data, sizeof(AVIINDEXENTRY), size, out_fd) != size
    ) {
        return 1;
    }

    free(data);
    fclose (in);
    fclose(out_fd);
    return 0;
}

static int mpidx1_to_aviidx1(char *in_file, FILE *out_fd)
{
    char head[10];
    char *data;
    FILE *in;
    int size, i;
    AVIINDEXENTRY *idx;
    int streams[100];

    in = fopen (in_file, "r");
    if (!in) return 1;
    i = fread (head, 10, 1, in);

    // header. Magic tag is AVIIDX1
    fprintf(out_fd, "AVIIDX1 # Generated by %s (%s-%s)\n", EXE, PACKAGE, VERSION); // Magic
    fprintf(out_fd, "TAG TYPE CHUNK CHUNK/TYPE POS LEN KEY MS\n");

    size = *(int *)(head+6);
    data = malloc (size * sizeof(AVIINDEXENTRY));
    memset (streams, 0, sizeof(streams));
    if (size != fread (data, sizeof(AVIINDEXENTRY), size, in)) {
	perror("fread"); return 1;
    }
    for (i = 0; i<size; i++) {
	uint32_t ckid;
	idx = &((AVIINDEXENTRY *)data)[i];
	ckid = SWAP(idx->ckid);
	fprintf(out_fd,
		"%.4s %d %d %d %d %d %d 0\n",
		(char *)&ckid,
		avi_stream_nr(idx->ckid),
		i,
		streams[avi_stream_id(idx->ckid)],
		idx->dwChunkOffset,
		idx->dwChunkLength,
		idx->dwFlags?1:0);

	streams[avi_stream_id(idx->ckid)]++;
    }


    free(data);
    fclose (in);
    fclose(out_fd);

    return 0;
}

// data is only 8 bytes long
static int AVI_read_data_fast(avi_t *AVI, char *buf, off_t *pos, off_t *len, off_t *key, char *data)
{

/*
 * Return codes:
 *
 *    0 = reached EOF
 *    1 = video data read
 *    2 = audio data read from track 0
 *    3 = audio data read from track 1
 *    4 = audio data read from track 2
 *    ....
 *   10 = traditional idx1 chunk
 */

   off_t n;
   int rlen;
   *key=(off_t)0;

   if(AVI->mode==AVI_MODE_WRITE) return 0;

   while(1)
   {
      /* Read tag and length */

      if( xio_read(AVI->fdes,data,8) != 8 ) return 0;

      n = PAD_EVEN(str2ulong(data+4));

      if(strncasecmp(data,"LIST",4) == 0 ||
	 strncasecmp(data,"RIFF",4) == 0) { // prevents skipping extended RIFF chunks
	  if( xio_read(AVI->fdes,data,4) != 4 ) return 0;
	  n -= 4;
	  // put here tags of lists that need to be looked into
	  if(strncasecmp(data,"movi",4) == 0 ||
	     strncasecmp(data,"rec ",4) == 0 ||
	     strncasecmp(data,"AVI ",4) == 0 ||
	     strncasecmp(data,"AVIX",4) == 0) {
	    // xio_lseek(AVI->fdes,-4,SEEK_CUR);
	    continue; // proceed to look into it
	  } // otherwise seek over it later on
      }

      // the following list of comparisons should not include list tags;
      // these should all go in the list above
      if(strncasecmp(data,"IDX1",4) == 0)
      {
	 // deal with it to extract keyframe info
         *len = str2ulong(data+4);
	 *pos = xio_lseek(AVI->fdes, 0, SEEK_CUR)-(off_t)8;
	 //fprintf (stderr, "Found an index chunk at %lld len %lld\n", *pos, *len);
         if(xio_lseek(AVI->fdes,n,SEEK_CUR)==(off_t)-1)  return 0;
	 return 10;
      }

      if(strncasecmp(data,AVI->video_tag,3) == 0)
      {
         *len = str2ulong(data+4);
	 *pos = xio_lseek(AVI->fdes, 0, SEEK_CUR)-(off_t)8;
         AVI->video_pos++;
	 rlen = n;
	 if(xio_read(AVI->fdes, buf, rlen) != rlen)  return 0;
         if(xio_lseek(AVI->fdes,n-rlen,SEEK_CUR)==(off_t)-1)  return 0;
         return 1;
      }
      else if(AVI->anum>=1 && strncasecmp(data,AVI->track[0].audio_tag,4) == 0)
      {
         *len = str2ulong(data+4);
	 *pos = xio_lseek(AVI->fdes, 0, SEEK_CUR)-(off_t)8;
	 AVI->track[0].audio_posc++;
	 rlen = (n<LEN)?n:LEN;
	 if(xio_read(AVI->fdes, buf, rlen) != rlen)  return 0;
         if(xio_lseek(AVI->fdes,n-rlen,SEEK_CUR)==(off_t)-1)  return 0;
         return 2;
         break;
      }
      else if(AVI->anum>=2 && strncasecmp(data,AVI->track[1].audio_tag,4) == 0)
      {
         *len = str2ulong(data+4);
	 *pos = xio_lseek(AVI->fdes, 0, SEEK_CUR)-(off_t)8;
	 AVI->track[1].audio_posc++;
	 rlen = (n<LEN)?n:LEN;
	 if(xio_read(AVI->fdes, buf, rlen) != rlen)  return 0;
         if(xio_lseek(AVI->fdes,n-rlen,SEEK_CUR)==(off_t)-1)  return 0;
         return 3;
         break;
      }
      else if(AVI->anum>=3 && strncasecmp(data,AVI->track[2].audio_tag,4) == 0)
      {
         *len = str2ulong(data+4);
	 *pos = xio_lseek(AVI->fdes, 0, SEEK_CUR)-(off_t)8;
	 AVI->track[2].audio_posc++;
	 rlen = (n<LEN)?n:LEN;
	 if(xio_read(AVI->fdes, buf, rlen) != rlen)  return 0;
         if(xio_lseek(AVI->fdes,n-rlen,SEEK_CUR)==(off_t)-1)  return 0;
         return 4;
         break;
      }
      else if(AVI->anum>=4 && strncasecmp(data,AVI->track[3].audio_tag,4) == 0)
      {
         *len = str2ulong(data+4);
	 *pos = xio_lseek(AVI->fdes, 0, SEEK_CUR)-(off_t)8;
	 AVI->track[3].audio_posc++;
	 rlen = (n<LEN)?n:LEN;
	 if(xio_read(AVI->fdes, buf, rlen) != rlen)  return 0;
         if(xio_lseek(AVI->fdes,n-rlen,SEEK_CUR)==(off_t)-1)  return 0;
         return 5;
         break;
      }
      else if(AVI->anum>=5 && strncasecmp(data,AVI->track[4].audio_tag,4) == 0)
      {
         *len = str2ulong(data+4);
	 *pos = xio_lseek(AVI->fdes, 0, SEEK_CUR)-(off_t)8;
	 AVI->track[4].audio_posc++;
	 rlen = (n<LEN)?n:LEN;
	 if(xio_read(AVI->fdes, buf, rlen) != rlen)  return 0;
         if(xio_lseek(AVI->fdes,n-rlen,SEEK_CUR)==(off_t)-1)  return 0;
         return 6;
         break;
      }
      else if(AVI->anum>=6 && strncasecmp(data,AVI->track[5].audio_tag,4) == 0)
      {
         *len = str2ulong(data+4);
	 *pos = xio_lseek(AVI->fdes, 0, SEEK_CUR)-(off_t)8;
	 AVI->track[5].audio_posc++;
	 rlen = (n<LEN)?n:LEN;
	 if(xio_read(AVI->fdes, buf, rlen) != rlen)  return 0;
         if(xio_lseek(AVI->fdes,n-rlen,SEEK_CUR)==(off_t)-1)  return 0;
         return 7;
         break;
      }
      else if(AVI->anum>=7 && strncasecmp(data,AVI->track[6].audio_tag,4) == 0)
      {
         *len = str2ulong(data+4);
	 *pos = xio_lseek(AVI->fdes, 0, SEEK_CUR)-(off_t)8;
	 AVI->track[6].audio_posc++;
	 rlen = (n<LEN)?n:LEN;
	 if(xio_read(AVI->fdes, buf, rlen) != rlen)  return 0;
         if(xio_lseek(AVI->fdes,n-rlen,SEEK_CUR)==(off_t)-1)  return 0;
         return 8;
         break;
      }
      else if(AVI->anum>=8 && strncasecmp(data,AVI->track[7].audio_tag,4) == 0)
      {
         *len = str2ulong(data+4);
	 *pos = xio_lseek(AVI->fdes, 0, SEEK_CUR)-(off_t)8;
	 AVI->track[7].audio_posc++;
	 rlen = (n<LEN)?n:LEN;
	 if(xio_read(AVI->fdes, buf, rlen) != rlen)  return 0;
         if(xio_lseek(AVI->fdes,n-rlen,SEEK_CUR)==(off_t)-1)  return 0;
         return 9;
         break;
      } else {
	  if(xio_lseek(AVI->fdes,n,SEEK_CUR)==(off_t)-1)  return 0;
      }
      // else if(xio_lseek(AVI->fdes,n,SEEK_CUR)==(off_t)-1)  return 0;
   }
}

static int is_key(unsigned char *data, long size, char *codec)
{
    if (strncasecmp(codec, "div3", 4) == 0) {

	int32_t c=( (data[0]<<24) | (data[1]<<16) | (data[2]<<8) | (data[3]&0xff) );
	if(c&0x40000000) return(0);
	else return 1;

    } else if (strncasecmp(codec, "xvid", 4) == 0 || strncasecmp(codec, "divx", 4) == 0
	    || strncasecmp(codec, "dx50", 4) == 0 || strncasecmp(codec, "div4", 4) == 0
	    || strncasecmp(codec, "mpg4", 4) == 0) {
        int result = 0;
        int i;

        for(i = 0; i < size - 5; i++)
        {
                if( data[i]     == 0x00 && data[i + 1] == 0x00 &&
		    data[i + 2] == 0x01 && data[i + 3] == 0xb6) {

                        if((data[i + 4] & 0xc0) == 0x0) return 1;
                        else                            return 0;
                }
        }

        return result;

    }

    // mjpeg, uncompressed, etc
    return 1;

}


int main(int argc, char *argv[])
{

  avi_t *avifile1=NULL;

  char *in_file=NULL, *out_file=NULL;

  long frames;

  double fps;

  int track_num=0, aud_tracks;

  int ret;
  long i=0, chunk=0;

  int ch;
  int progress=0, old_progress=0;

  long rate;
  int format, chan, bits;
  int aud_bitrate = 0;

  FILE *out_fd    = NULL;
  int open_without_index=0,index_keyframes=0;
  int force_with_index=0;

  double vid_ms = 0.0, print_ms = 0.0;
  double aud_ms [ AVI_MAX_TRACKS ];
  char tag[8];
  char *data;
  int vid_chunks=0, aud_chunks[AVI_MAX_TRACKS];
  off_t pos, len, key=0, index_pos=0, index_len=0,size=0;
  struct stat st;
  char *codec;
  int idx_type=0;
  off_t ioff;
  char fcclen[8]; // FOURCC + len

  ftype_t ftype;
  FILE *idxfile;

  char *dummy;  // for avoiding compiler warnings

  ac_init(AC_ALL);

  if(argc==1) usage(EXIT_FAILURE);

  for (i=0; i<AVI_MAX_TRACKS; i++) {
    aud_chunks[i] = 0;
    aud_ms[i] = 0;
  }

  while ((ch = getopt(argc, argv, "a:vi:o:nxf?h")) != -1)
    {

	switch (ch) {

	case 'i':

	     if(optarg[0]=='-') usage(EXIT_FAILURE);
	    in_file=optarg;

	    break;

	case 'a':

	  if(optarg[0]=='-') usage(EXIT_FAILURE);
	  track_num = atoi(optarg);

	  if(track_num<0) usage(EXIT_FAILURE);

	  break;

	case 'o':

	    if(optarg[0]=='-') usage(EXIT_FAILURE);
	    out_file=optarg;

	    break;

	case 'n':

	    open_without_index=1;

	    break;

	case 'x':

	    open_without_index=1;
	    index_keyframes=1;

	    break;

	case 'f':
	    force_with_index=1;
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

  // check
  if(in_file==NULL) usage(EXIT_FAILURE);
  if (!out_file) out_fd = stdout;
  else out_fd = fopen(out_file, "w+r");

  if (!out_fd) {
      perror("ERROR cannot open outputfile");
      exit(1);
  }

  idxfile = fopen(in_file, "r");
  if (fread (fcclen, 8, 1, idxfile) != 1) {
      perror("ERROR cannot read from index file");
      exit(1);
  }
  if      (strncasecmp(fcclen, "RIFF", 4)    == 0) ftype = RIFF;
  else if (strncasecmp(fcclen, "AVIIDX1", 7) == 0) ftype = AVIIDX1;
  else if (strncasecmp(fcclen, "MPIDX1", 6)  == 0) ftype = MPIDX1;
  else    ftype = UNKNOWN;

  fclose (idxfile);

  switch (ftype) {
      case RIFF:    fprintf(stderr, "[%s] Seems to be an AVI file.\n", EXE); break;
      case AVIIDX1: fprintf(stderr, "[%s] Converting a transcode to an mplayer index file.\n", EXE);
		    return aviidx1_to_mpidx1(in_file, out_fd);
      case MPIDX1:  fprintf(stderr, "[%s] Converting an mplayer to a transcode index file.\n", EXE);
		    return mpidx1_to_aviidx1(in_file, out_fd);
      default:      fprintf(stderr, "[%s] Unrecognized format\n", EXE); return (1);
  }


  // if file is larger than 2GB, regen index

  if (stat(in_file, &st)<0) {
      perror("Stat input file");
      return 1;
  }

  size = st.st_size;
  if (size > (off_t)AVI_MAX_LEN/2)
      if (!force_with_index) open_without_index = 1;

  if (open_without_index)
      if (index_keyframes) fprintf(stderr, "[%s] Open \"%s\" without index and don't use index for keyframes info\n",EXE, in_file);
      else fprintf(stderr, "[%s] Open \"%s\" without index but use index (if any) for keyframes info\n",EXE, in_file);
  else
      fprintf(stderr, "[%s] Open \"%s\" with index (fast)\n", EXE, in_file);

  // header. Magic tag is AVIIDX1
  fprintf(out_fd, "AVIIDX1 # Generated by %s (%s-%s)\n", EXE, PACKAGE, VERSION); // Magic
  fprintf(out_fd, "TAG TYPE CHUNK CHUNK/TYPE POS LEN KEY MS\n");

  data = malloc (5*1024*1204);

  if (open_without_index) {

    // open file with index.
    if(NULL == (avifile1 = AVI_open_input_file(in_file,0))) {
      AVI_print_error("AVI open input file");
      exit(1);
    }

    aud_tracks = frames = 0;
    frames = AVI_video_frames(avifile1);
    fps    = AVI_frame_rate  (avifile1);
    codec  = AVI_video_compressor(avifile1);

    aud_tracks = AVI_audio_tracks(avifile1);
    //printf("frames (%ld), aud_tracks (%d)\n", frames, aud_tracks);

    pos = key = len = (off_t)0;
    i = 0;

    while ( (ret = AVI_read_data_fast (avifile1, data, &pos, &len, &key, fcclen)) != 0) {
      int audtr = ret-2;

      /* don't need this and it saves time
       * */
      if (audtr>=0 && audtr<=7) {
	AVI_set_audio_track(avifile1, audtr);
	format = AVI_audio_format (avifile1);
	chan   = AVI_audio_channels(avifile1);
	rate   = AVI_audio_rate   (avifile1);
	bits   = AVI_audio_bits   (avifile1);
	bits = bits==0?16:bits;
	if (tc_format_ms_supported(format)) {

	  aud_bitrate = format==0x1?1:0;

	  if (!aud_bitrate && tc_get_audio_header(data, LEN, format, NULL, NULL, &aud_bitrate)<0) {
	    aud_ms[audtr] = vid_ms;
	  } else
	    aud_ms[audtr] += (len*8.0)/(format==0x1?((double)(rate*chan*bits)/1000.0):aud_bitrate);
	}
      }

      switch (ret) {
	case 1: ac_memcpy(tag, fcclen, 4);
		print_ms = vid_ms = (avifile1->video_pos)*1000.0/fps;
		chunk = avifile1->video_pos;
		key = is_key(data, len, codec);
		break;
	case 2: case 3:
	case 4: case 5:
	case 6: case 7:
	case 8:
	case 9: ac_memcpy(tag, fcclen, 4);
		print_ms = aud_ms[audtr];
		chunk = avifile1->track[audtr].audio_posc;
		break;
	case 10: tc_snprintf(tag, sizeof(tag), "idx1");
		 index_pos = pos;
		 index_len = len;
		 print_ms = 0.0;
		 chunk = 0;
		 break;

	case 0:
	default:
		 // never get here
		 break;
      }


      //if (index_pos != pos)
      // tag, chunk_nr
      fprintf(out_fd, "%.4s %d %ld %ld %lld %lld %lld %.2f\n",
              tag, ret, i, chunk-1,
              (long long)pos, (long long)len, (long long)key,
              print_ms);
      i++;

      // don't update the counter every chunk
      progress = (int)(pos*100/size)+1;
      if (old_progress != progress) {
	  fprintf(stderr, "[%s] Scanning ... %d%%\r", EXE, progress);
	  old_progress = progress;
      }

    }
    fprintf(stderr, "\n");

    // check if we have found an index chunk to restore keyframe info
    if (!index_pos || !index_len || index_keyframes)
	goto aviout;

    fprintf(stderr, "[%s] Found an index chunk. Using it to regenerate keyframe info.\n", EXE);
    fseek (out_fd, 0, SEEK_SET);

    dummy = fgets(data, 100, out_fd); // magic
    dummy = fgets(data, 100, out_fd); // comment

    len = (off_t)0;
    vid_chunks = 0;

    xio_lseek(avifile1->fdes, index_pos+8, SEEK_SET);
    while (len<index_len) {
	if (xio_read(avifile1->fdes, tag, 8) != 8) {
            fprintf(stderr, "[%s] Read error\n", EXE);
            exit(1);
        }

	// if its a keyframe and is a video chunk
	if (str2ulong(tag+4) && tag[1] == '0') {
	    int typen, keyn;
	    long chunkn, chunkptypen;
	    long long posn, lenn;
	    char tagn[5];
	    double msn=0.0;

	    chunk = (long)(len/16);
	    i = 0;
	    //fprintf(stderr, "keyframe in chunk %ld\n", chunk);

	    // find line "chunk" in the logfile

	    while (i<chunk-vid_chunks) {
		if (!fgets(data, 100, out_fd)) {
                    fprintf(stderr, "[%s] Read error\n", EXE);
                    exit(1);
                }
		i++;
	    }

	    vid_chunks += (chunk-vid_chunks);
	    posn = ftell(out_fd);
	    if (!fgets(data, 100, out_fd)) {
                fprintf(stderr, "[%s] Read error\n", EXE);
                exit(1);
            }
	    fseek(out_fd, posn, SEEK_SET);
	    sscanf(data, "%s %d %ld %ld %lld %lld %d %lf",
		      tagn, &typen, &chunkn, &chunkptypen, &posn, &lenn, &keyn, &msn);
	    fprintf(out_fd, "%s %d %ld %ld %lld %lld %d %.2f",
		      tagn, typen, chunkn, chunkptypen, posn, lenn, 1, msn);
	}

	xio_lseek(avifile1->fdes, 8, SEEK_CUR);
	len += 16;
    }



  } else { // with index

    // open file with index.
    if(NULL == (avifile1 = AVI_open_input_file(in_file,1))) {
      AVI_print_error("AVI open input file");
      exit(1);
    }
    i=0;

    AVI_info(avifile1);

    // idx1 contains only info for first chunk of opendml AVI
    if(avifile1->idx && !avifile1->is_opendml)
    {
      off_t pos, len;

      /* Search the first videoframe in the idx1 and look where
         it is in the file */

      for(i=0;i<avifile1->n_idx;i++)
         if( strncasecmp(avifile1->idx[i],avifile1->video_tag,3)==0 ) break;

      pos = str2ulong(avifile1->idx[i]+ 8);
      len = str2ulong(avifile1->idx[i]+12);

      xio_lseek(avifile1->fdes,pos,SEEK_SET);
      if(xio_read(avifile1->fdes,data,8)!=8) return 1;
      if( strncasecmp(data,avifile1->idx[i],4)==0 && str2ulong(data+4)==len )
      {
         idx_type = 1; /* Index from start of file */
      }
      else
      {
         xio_lseek(avifile1->fdes,pos+avifile1->movi_start-4,SEEK_SET);
         if(xio_read(avifile1->fdes,data,8)!=8) return 1;
         if( strncasecmp(data,avifile1->idx[i],4)==0 && str2ulong(data+4)==len )
         {
            idx_type = 2; /* Index from start of movi list */
         }
      }
      /* idx_type remains 0 if neither of the two tests above succeeds */


      ioff = idx_type == 1 ? 0 : avifile1->movi_start-4;
    //fprintf(stderr, "index type (%d), ioff (%ld)\n", idx_type, (long)ioff);
      i=0;

      //printf("nr idx: %d\n", avifile1->n_idx);
      while (i<avifile1->n_idx) {
	ac_memcpy(tag, avifile1->idx[i], 4);
	// tag
	fprintf(out_fd, "%c%c%c%c",
	    avifile1->idx[i][0], avifile1->idx[i][1],
	    avifile1->idx[i][2], avifile1->idx[i][3]);

	// type, absolute chunk number
	fprintf(out_fd, " %c %ld", avifile1->idx[i][1]+1, i);


	switch (avifile1->idx[i][1]) {
	  case '0':
	    fprintf(out_fd, " %d", vid_chunks);
	    vid_chunks++;
	    break;
	  case '1': case '2':
	  case '3': case '4':
	  case '5': case '6':
	  case '7': case '8':
	    // uhoh
	    ret = avifile1->idx[i][1]-'0';
	    fprintf(out_fd, " %d", aud_chunks[ret]);
	    aud_chunks[ret]++;
	    break;
	  default:
	    fprintf(out_fd, " %d", -1);
	    break;
	}

	pos = str2ulong(avifile1->idx[i]+ 8);
	pos += ioff;
	// pos
	fprintf(out_fd, " %llu", (unsigned long long)pos);
	// len
	fprintf(out_fd, " %lu", str2ulong(avifile1->idx[i]+12));
	// flags (keyframe?);
	fprintf(out_fd, " %d", (str2ulong(avifile1->idx[i]+ 4))?1:0);

	// ms (not available here)
	fprintf(out_fd, " %.2f", 0.0);

	fprintf(out_fd, "\n");

	i++;
      }
    }

    else
    { // try to extract from the index that AVILIB built,
      // possibly from OpenDML superindex

      long aud_entry [ AVI_MAX_TRACKS ] = { 0 };
      long vid_entry = 0;
      char* tagp;

      off_t pos, len = 0;
      i = chunk = 0;


      while (1) {
	ret = pos = 0;
	int j = 0;

	if(vid_entry < avifile1->video_frames) {
	  pos = avifile1->video_index[vid_entry].pos;
	  len = avifile1->video_index[vid_entry].len;
	  key = (avifile1->video_index[vid_entry].key) & 16 ? 1 : 0;
	  chunk = vid_entry;
	  ret = 1;
	}
	for(j = 0; j < AVI_audio_tracks(avifile1); ++j) {
	  if(aud_entry[j] < avifile1->track[j].audio_chunks) {
	    if(!ret || avifile1->track[j].audio_index[aud_entry[j]].pos < pos) {
	      pos = avifile1->track[j].audio_index[aud_entry[j]].pos;
	      len = avifile1->track[j].audio_index[aud_entry[j]].len;
	      key = 0;
	      chunk = aud_entry[j];
	      ret = j + 2;
	    }
	  }
	}

	if(!ret) // end of all index streams
	  break;

	if (ret == 1)
	{
	  ++vid_entry;
	  tagp = avifile1->video_tag;
	}
	else
	{
	  aud_entry[ret-2]++;
	  tagp = avifile1->track[ret-2].audio_tag;
	}

	// index points to data in chunk, but chunk offset is needed here
	pos -= 8;
	fprintf(out_fd, "%.4s %d %ld %ld %lld %lld %lld %.2f\n",
                    tagp, ret, i, chunk,
                    (long long)pos, (long long)len, (long long)key,
                    0.0);
	i++;

      }

    }

  }


aviout:
  free(data);
  if (out_fd!=stdout) fclose (out_fd);
  AVI_close(avifile1);

  return(0);
}

#include "libtcutil/tcutil.h"

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
