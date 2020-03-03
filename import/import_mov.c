/*
 *  import_mov.c
 *
 *  Copyright (C) Thomas Oestreich - January 2002
 *  updated by Christian Vogelgsang <Vogelgsang@informatik.uni-erlangen.de>
 *
 *  This file is part of transcode, a video stream  processing tool
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

#define MOD_NAME    "import_mov.so"
#define MOD_VERSION "v0.1.3 (2005-12-04)"
#define MOD_CODEC   "(video) * | (audio) *"

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_PCM | TC_CAP_RGB | TC_CAP_YUV |
                             TC_CAP_YUV422 | TC_CAP_VID;

#define MOD_PRE mov
#include "import_def.h"

#include <quicktime.h>
#include <colormodels.h>
#include <lqt.h>
#include "magic.h"

#include "aclib/imgconvert.h"
#include "src/filter.h"

/* movie handles */
static quicktime_t *qt_audio=NULL;
static quicktime_t *qt_video=NULL;

/* row pointer for decode frame */
static unsigned char **row_ptr = NULL;

/* raw or decode frame */
static int rawVideoMode = 0;

/* raw or decode audio */
static int rawAudioMode = 0;

/* frame size */
static int w=0, h=0;

/* number of audio channels */
static int chan=0;

/* number of audio bits */
static int bits=0;

/* number of frames */
static int frames=0;

/* number of audio samples */
static int no_samples=0;

/* import colormodel */
static int qt_cm = 0;


/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
  /* audio */
  if(param->flag == TC_AUDIO) {
    int numTrk;
    long rate;
    char *codec;

    param->fd = NULL;

    /* open movie for audio extraction */
    if(qt_audio==NULL) {
      if(NULL == (qt_audio = quicktime_open((char *)vob->audio_in_file,1,0))){
	       tc_log_warn(MOD_NAME, "can't open quicktime!");
	       return(TC_IMPORT_ERROR);
      }
    }

    /* check for audio track */
    numTrk = quicktime_audio_tracks(qt_audio);
    if(numTrk==0) {
      tc_log_warn(MOD_NAME, "AUDIO: --no audio track in quicktime found --");
      no_samples=0;
      return(TC_IMPORT_OK);
    }

    /* extract audio parameters */
    rate   = quicktime_sample_rate(qt_audio, 0);
    chan   = quicktime_track_channels(qt_audio, 0);
    bits   = quicktime_audio_bits(qt_audio, 0);
    codec  = quicktime_audio_compressor(qt_audio, 0);

    /* The total frames */
    no_samples=quicktime_audio_length(qt_audio, 0);

    /* verbose info */
    tc_log_info(MOD_NAME, "codec=%s, rate=%ld Hz, bits=%d,"
                    " channels=%d, samples=%d",
                    codec, rate, bits, chan, no_samples);

    /* check bits */
    if((bits!=8)&&(bits!=16)) {
      tc_log_warn(MOD_NAME, "unsupported sample bits: %d",bits);
      return(TC_IMPORT_ERROR);
    }

    /* check channels */
    if(chan>2) {
      tc_log_warn(MOD_NAME, "too many audio channels: %d",chan);
      return(TC_IMPORT_ERROR);
    }

    /* check codec string */
    if(strlen(codec)==0) {
      tc_log_warn(MOD_NAME, "empty codec in quicktime?");
      return(TC_IMPORT_ERROR);
    }

    /* check if audio compressor is supported */
    if(quicktime_supported_audio(qt_audio, 0)!=0) {
      rawAudioMode = 0;
    }
#if !defined(LIBQUICKTIME_000904)
    /* RAW PCM is directly supported */
    else if(strcasecmp(codec,QUICKTIME_RAW)==0) {
      rawAudioMode = 1;
      tc_log_warn(MOD_NAME, "using RAW audio mode!");
    }
#endif
    /* unsupported codec */
    else {
      tc_log_warn(MOD_NAME, "quicktime audio codec '%s' not supported!",
	      codec);
      return(TC_IMPORT_ERROR);
    }
    return(TC_IMPORT_OK);
  }

  /* video */
  if(param->flag == TC_VIDEO) {
    double fps;
    char *codec;
    int numTrk;

    param->fd = NULL;

    /* open movie for video extraction */
    if(qt_video==NULL)
      if(NULL == (qt_video = quicktime_open((char *)vob->video_in_file,1,0))){
	       tc_log_warn(MOD_NAME,"can't open quicktime!");
	       return(TC_IMPORT_ERROR);
      }

    /* check for audio track */
    numTrk = quicktime_video_tracks(qt_video);
    if(numTrk==0) {
      tc_log_warn(MOD_NAME,"no video track in quicktime found!");
      return(TC_IMPORT_ERROR);
    }

    /* read all video parameter from input file */
    w      =  quicktime_video_width(qt_video, 0);
    h      =  quicktime_video_height(qt_video, 0);
    fps    =  quicktime_frame_rate(qt_video, 0);
    codec  =  quicktime_video_compressor(qt_video, 0);

    //ThOe total frames
    frames=quicktime_video_length(qt_video, 0);

    /* verbose info */
    tc_log_info(MOD_NAME, "VIDEO: codec=%s, fps=%6.3f, width=%d,"
                    " height=%d, frames=%d",
                    codec, fps, w, h, frames);

    /* check codec string */
    if(strlen(codec)==0) {
      tc_log_warn(MOD_NAME, "empty codec in quicktime?");
      return(TC_IMPORT_ERROR);
    }

    /* check if a suitable compressor is available */
    if(quicktime_supported_video(qt_video,0)==0) {
	     tc_log_warn(MOD_NAME, "quicktime codec '%s'"
                             " not supported for RGB!",
		             codec);
	     return(TC_IMPORT_ERROR);
    }


    /* set color model */
    switch(vob->im_v_codec) {
        case TC_CODEC_RGB24:
              /* use raw mode when possible */
              /* not working ?*/
              /*if (strcmp(qt_codec, "raw ")) rawVideo=1; */
	      	    /* allocate buffer for row pointers */
	      	    row_ptr = tc_malloc(h*sizeof(char *));
	      	    if(row_ptr==0) {
		        tc_log_error(MOD_NAME,"can't alloc row pointers");
			return(TC_IMPORT_ERROR);
	      	    }

              quicktime_set_cmodel(qt_video, BC_RGB888); qt_cm = BC_RGB888;
              break;

        case TC_CODEC_YUV420P:
            /* use raw mode when possible */
            /* not working ?*/
            /* if (strcmp(qt_codec, "yv12")) rawVideo=1; */

            /* allocate buffer for row pointers */
            row_ptr = tc_malloc(3*sizeof(char *));
            if(row_ptr==0) {
                tc_log_error(MOD_NAME,"can't alloc row pointers");
                return(TC_IMPORT_ERROR);
            }

            if (!quicktime_reads_cmodel(qt_video, BC_YUV420P, 0)) {
                if (quicktime_reads_cmodel(qt_video, BC_YUVJ420P, 0)) {
                    /* stolen from import_ffmpeg */
                    /* load levels filter */
                    if (!tc_filter_add("levels", "output=16-240:pre=1")) {
                        tc_log_warn(MOD_NAME, "cannot load levels filter. Try -V rgb24.");
                    }
                    quicktime_set_cmodel(qt_video, BC_YUVJ420P);

                } else {
                    tc_log_error(MOD_NAME,"unable to handle colormodel. Try -V rgb24.");
                    return(TC_IMPORT_ERROR);
                }
            } else {
                quicktime_set_cmodel(qt_video, BC_YUV420P);
            }

            qt_cm = BC_YUV420P;
            break;

        case TC_CODEC_YUV422P:
            /* allocate buffer for row pointers */
            row_ptr = tc_malloc(3*sizeof(char *));
            if(row_ptr==0) {
                tc_log_error(MOD_NAME,"can't alloc row pointers");
                return(TC_IMPORT_ERROR);
            }

            if (!quicktime_reads_cmodel(qt_video, BC_YUV422P, 0)) {
                tc_log_error(MOD_NAME,"unable to handle colormodel. Try -V rgb24.");
                return(TC_IMPORT_ERROR);
            }

            quicktime_set_cmodel(qt_video, BC_YUV422P); qt_cm = BC_YUV422P;
            break;

        case TC_CODEC_YUY2:
              quicktime_set_cmodel(qt_video, BC_YUV422); qt_cm = BC_YUV422;
              break;

         /* passthrough */
         case TC_CODEC_RAW:
              rawVideoMode = 1;
              break;

        default:
            /* unsupported internal format */
            tc_log_warn(MOD_NAME,"unsupported internal video format %x",
                	vob->ex_v_codec);
            return(TC_EXPORT_ERROR);
            break;
       }

    return(TC_IMPORT_OK);
  }

  return(TC_IMPORT_ERROR);
}


/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

MOD_decode
{
  /* video */
  if(param->flag == TC_VIDEO) {
    if(rawVideoMode) {
      /* read frame raw */
      param->size = quicktime_read_frame(qt_video, param->buffer, 0);
      if(param->size<=0) {
	if(verbose & TC_DEBUG)
	  tc_log_warn(MOD_NAME,"quicktime read video frame");
	return(TC_IMPORT_ERROR);
      }
    } else {
      /* decode frame */
      unsigned char *mem = param->buffer;

      int iy,sl;

      switch(qt_cm) {
      case BC_RGB888:
              /* setup row pointers for RGB: inverse! */
              sl = w*3;
              for(iy=0;iy<h;iy++){
                  row_ptr[iy] = mem;
                  mem += sl;
              }

              param->size = (h*w) * 3;
              break;

      case BC_YUV420P: {
              /* setup row pointers for YUV420P */
              YUV_INIT_PLANES(row_ptr, mem, IMG_YUV420P, h, w);
              param->size = (h*w*3)/2;
              break;
              }

      case BC_YUV422P: {
	      /* setup row pointers for YUV422P */
              YUV_INIT_PLANES(row_ptr, mem, IMG_YUV422P, h, w);
              param->size = (h*w)*2;
              break;
              }
      }

      /* decode the next frame */
      if(lqt_decode_video(qt_video,row_ptr,0)<0) {
        if(verbose & TC_DEBUG)
          tc_log_warn(MOD_NAME,"can't decode frame");
          return(TC_IMPORT_ERROR);
        }
    }

    //ThOe trust file header and terminate after all frames have been processed.
    if(frames--==0) return(TC_IMPORT_ERROR);
    return(TC_IMPORT_OK);
  }

  /* audio */
  if(param->flag == TC_AUDIO) {

    int bytes_read;

    /* Leave if audio track is empty */
    if (no_samples==0){
      param->size=0;
      return(TC_IMPORT_OK);
    }

    /* raw read mode */
#if !defined(LIBQUICKTIME_000904)
    if(rawAudioMode) {
      bytes_read = quicktime_read_audio(qt_audio,
					param->buffer, param->size, 0);
    } else
#endif
    {
      /* decode audio mode */
      long pos = quicktime_audio_position(qt_audio,0);
      long samples = param->size;
      if(bits==16)
	samples >>= 1;

      /* mono */
      if(chan==1) {
	/* direct copy */
	bytes_read = quicktime_decode_audio(qt_audio,
					    (int16_t *)param->buffer,NULL,
					    samples,0);

	/* check result */
	if(bytes_read<0) {
	  if(verbose & TC_DEBUG)
	    tc_log_warn(MOD_NAME,"reading quicktime audio frame!");
	  return(TC_IMPORT_ERROR);
	}
      }
      /* stereo */
      else {
	int16_t *tgt;
	int16_t *tmp;
	int s,t;

	samples >>= 1;
	tgt = (int16_t *)param->buffer;
	tmp = tc_malloc(samples*sizeof(int16_t));

	/* read first channel into target buffer */
	bytes_read = quicktime_decode_audio(qt_audio,tgt,NULL,samples,0);
	if(bytes_read<0) {
	  if(verbose & TC_DEBUG)
	    tc_log_warn(MOD_NAME,"reading quicktime audio frame!");
	  return(TC_IMPORT_ERROR);
	}

	/* read second channel in temp buffer */
	quicktime_set_audio_position(qt_audio,pos,0);
	bytes_read = quicktime_decode_audio(qt_audio,tmp,NULL,samples,1);
	if(bytes_read<0) {
	  if(verbose & TC_DEBUG)
	    tc_log_warn(MOD_NAME,"reading quicktime audio frame!");
	  return(TC_IMPORT_ERROR);
	}

	/* spread first channel */
	for(s=samples-1;s>=0;s--)
	  tgt[s<<1] = tgt[s];

	/* fill in second channel from temp buffer */
	t = 1;
	for(s=0;s<samples;s++) {
	  tgt[t] = tmp[s];
	  t += 2;
	}

	free(tmp);
      }
      quicktime_set_audio_position(qt_audio,pos+samples,0);
    }

    return(TC_IMPORT_OK);
  }

  return(TC_IMPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
  /* free up audio */
  if(param->flag == TC_AUDIO) {
    if(qt_audio!=NULL) {
      quicktime_close(qt_audio);
      qt_audio=NULL;
    }
    return(TC_IMPORT_OK);
  }

  /* free up video */
  if(param->flag == TC_VIDEO) {
    if(qt_video!=NULL) {
      quicktime_close(qt_video);
      qt_video=NULL;
    }
    /* free row pointer */
    if(row_ptr!=0)
      free(row_ptr);

    return(TC_IMPORT_OK);
  }

  return(TC_IMPORT_ERROR);
}
