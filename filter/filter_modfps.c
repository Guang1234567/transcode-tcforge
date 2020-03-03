/*
 *  filter_modfps.c
 *
 *  Copyright (C) Marrq - July 2003
 *
 *  This file is part of transcode, a video stream processing tool
 *  Based on the excellent work of Donald Graft in Decomb and of
 *  Thanassis Tsiodras of transcode's decimate filter and Tilmann Bitterberg
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

// ----------------- Changes
// 0.9 -> 0.10: marrq
//		added scene change detection code courtesy of Tilmann Bitterberg
//		so we won't blend if there's a change of scene (we'll still interpolate)
//		added clone_phosphor_average
// 0.8 -> 0.9: marrq
//		added fancy_clone and associated functions
// 0.7 -> 0.8: Tilmann Bitterberg
//		make mode=1 the default
// 0.6 -> 0.7: marrq
//		make mode=1 independant of frame numbers, and modified verbose
//		mode to make a bit more sense given the conditionals
//		modified "todo" to reflect this
// 0.5 -> 0.6: Tilmann Bitterberg
//             Make mode=0 independent of the Frame number transcode
//             gives us.
// 0.4 -> 0.5: marrq
//		initialize memory at runtime.
//		skip at PRE_S_ clone at POST_S_
//		fix counting/buffering bugs related to mode=1
// 0.3 -> 0.4: Tilmann Bitterberg
//             Fix a typo in the optstr_param printout and correct the filter
//             flags.
//             Fix a bug related to scanrange.

#define MOD_NAME    "filter_modfps.so"
#define MOD_VERSION "v0.10 (2003-08-18)"
#define MOD_CAP     "plugin to modify framerate"
#define MOD_AUTHOR  "Marrq"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtc/ratiocodes.h"
#include "libtcutil/optstr.h"

#include <math.h>


//#define DEBUG 1

static int show_results=0;

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

static int mode=1;
static double infps  = 29.97;
static double outfps = 23.976;
static int infrc  = 0;
// default settings for NTSC 29.97 -> 23.976
static int numSample=5;
static int offset = 32;
static int runnow = 0;

static char **frames = NULL;
static int frbufsize;
static int frameIn = 0, frameOut = 0;
static int *framesOK, *framesScore;
static int scanrange = 0;
static int clonetype = 0;

static void help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help\n"
"* Overview\n"
"  This filter aims to allow transcode to alter the fps\n"
"  of video.  While one can reduce the fps to any amount,\n"
"  one can only increase the fps to at most twice the\n"
"  original fps\n"
"  There are two modes of operation, buffered and unbuffered,\n"
"  unbuffered is quick, but buffered, especially when dropping frames\n"
"  should look better\n"
"  For most users, modfps will need either no options, or just mode=1\n"
"* Options\n"
"    mode : (0=unbuffered, 1=buffered [%d]\n"
"    infps : original fps (override what transcode supplies) [%f]\n"
"    infrc : original frc (overwrite infps) [%d]\n"
"    buffer : number of frames to buffer [%d]\n"
"    subsample : number of pixels to subsample when examining buffers [%d]\n"
"    clonetype : when cloning and mode=1 do something special [%d]\n"
"        0 = none\n"
"        1 = merge fields, cloned frame first(good for interlaced displays)\n"
"        2 = merge fields, cloned frame 2nd (good for interlaced displays)\n"
"        3 = average frames\n"
"        4 = temporally average frame\n"
"        5 = pseudo-phosphor average frames (YUV only) (slow)\n"
"    verbose : 0 = not verbose, 1 is verbose [%d]\n"
		, MOD_CAP,
		mode,
		infps,
		infrc,
		numSample,
		offset,
		clonetype,
		show_results);
}

#define ABS_u8(a) (((a)^((a)>>7))-((a)>>7))

static int yuv_detect_scenechange(uint8_t *_src, uint8_t *_prev, const int _threshold,
		const int _scenethreshold, int _width, const int _height, const int srcpitch){
  uint8_t *src, *src_buf, *srcminus, *prev;
  const int w=_width;
  const int h=_height;
  const int hminus1 = h-1;
  int x,y,count=0,scenechange=0;

  /* Skip first and last lines, they'll get a free ride. */
  src_buf = _src;
  src = src_buf + srcpitch;
  srcminus = src - srcpitch;
  prev = _prev + srcpitch;

  for (y = 1; y < hminus1; y++){
    if (y & 1){
      for (x=0; x<w; x++) {
        int luma = *src++;
	int p0 = luma - (*(srcminus+x));
	int p1 = luma - (*(prev));

	count += ((ABS_u8(p0) > _threshold) & (ABS_u8(p1) > _threshold));
	++prev;
      }
    } else {
      for (x=0; x<w; x++) {
        int luma = *src++ & 0xff;
	int p0 = luma - (*(prev+w)&0xff);
	int p1 = luma - (*(prev)&0xff);

	count += ((ABS_u8(p0) > _threshold) & (ABS_u8(p1) > _threshold));
	++prev;
      }
    }

    srcminus += srcpitch;
  }

  if ((100L * count) / (h * w) >= _scenethreshold){
    scenechange = 1;
  } else {
    scenechange = 0;
  }

  return scenechange;
}

static int tc_detect_scenechange(unsigned char*clone, unsigned char *next, vframe_list_t *ptr){
  const int thresh = 14;
  const int scenethresh = 31;
  if(ptr->v_codec == TC_CODEC_YUV420P){
    return yuv_detect_scenechange((uint8_t *)next, (uint8_t *)clone, thresh, scenethresh,
    				ptr->v_width, ptr->v_height, ptr->v_width);
  } else {
    // implement RGB and YUY2
    return 0;
  }
}
/**********
 * phosphor average will likely only make sense for YUV data.
 * we'll do a straight average for the UV data, but for the Y
 * data, we'll cube the pixel, average them and take the cube root
 * of that.  This way, brightness (and hopefully motion) is easily
 * noticed by the eye
 **********/
static void clone_phosphor_average(unsigned char *clone, unsigned char *next, vframe_list_t *ptr){
  int i;

  // let's not blend if there's a scenechange
  if (tc_detect_scenechange(clone,next,ptr)){
    return;
  } // else
  for(i=0;i<(ptr->v_width*ptr->v_height);i++){
    ptr->video_buf[i] = (unsigned char)(long)rint(pow((double) (( clone[i]*clone[i]*clone[i] +
    					      next[i]*next[i]*next[i]) >> 1),
					      1.0/3.0));
  }
  for(; i<ptr->video_size; i++){
    ptr->video_buf[i] = (unsigned char)( ((short int)clone[i] + (short int)next[i]) >> 1);
  }
}

static void clone_average(unsigned char *clone, unsigned char *next, vframe_list_t *ptr){
  int i;

  // let's not blend if there's a scenechange
  if (tc_detect_scenechange(clone,next,ptr)){
    return;
  } // else
  for(i=0;i<ptr->video_size;i++){
    ptr->video_buf[i] = (unsigned char)( ((short int)clone[i] + (short int)next[i]) >> 1);
  }
}

static void clone_temporal_average(unsigned char *clone, unsigned char*next, vframe_list_t *ptr, int tin, int tout){
  // basic algorithm is to weight the pixels of a frame based
  // on how close the blended frame should be if this frame was
  // perfectly placed  since's we'll be merging the tin and the tin+1
  // frame into the tout'th frame, we calculate the time that
  // tout will be played at, and compare it to tin and tin+1

  // because the main body is buffering frames, when we're called, tin and tout might
  // not be appropriate for when clones should be called (in otherwords, the
  // buffering allows a small amount of AV slippage) ... what this means, is
  // that sometimes to have things match up temporally best, we should just
  // copy in the next frame  This tends to happen when outfps < 1.5*infps

  double weight1,weight2;
  int i;
  static int first=1;

  weight1 = 1.0 - ( (double)tout/outfps*infps - (double)tin );
  weight2 = 1.0 - ( (double)(tin+1) - (double)(tout)/outfps*infps );
  // weight2 is also 1.0-weight1

  if (show_results){
    tc_log_info(MOD_NAME, "temporal_clone tin=%4d tout=%4d w1=%1.5f w2=%1.5f",
                    tin,tout,weight1,weight2);
  }

  if (weight1 < 0.0){
    if (show_results){
      tc_log_info(MOD_NAME, "temporal_clone: w1 is weak, copying next frame");
    }
    ac_memcpy(ptr->video_buf,next,ptr->video_size);
    return;
  } // else
  if (weight2 < 0.0){
    // I think this case cannot happen
    if (show_results){
      tc_log_info(MOD_NAME, "temporal_clone: w2 is weak, simple cloning of frame");
    }
    // no memcpy needed, as we're keeping the orig
    return;
  } // else

  // let's not blend if there's a scenechange
  if (tc_detect_scenechange(clone,next,ptr)){
    return;
  } // else

  if (weight1 > 1.0 || weight2 > 1.0){
    tc_log_info(MOD_NAME, "clone_temporal_average: error: weights are out of range, w1=%f w2=%f", weight1,weight2);
    return;
  } // else

  for(i=0; i<ptr->video_size; i++){
    ptr->video_buf[i] = (unsigned char)( (double)(clone[i])*weight1 + (double)(next[i])*weight2);
  }
  first=0;
}

static void clone_interpolate(char *clone, char *next, vframe_list_t *ptr){
  int i,width = 0,height;
  char *dest, *s1, *s2;

  if (TC_CODEC_RGB24 == ptr->v_codec){
    // in RGB, the data is packed, three bytes per pixel
    width = 3*ptr->v_width;
  } else if (TC_CODEC_YUY2 == ptr->v_codec){
    // in YUY2, the data again is packed.
    width = 2*ptr->v_width;
  } else if (TC_CODEC_YUV420P == ptr->v_codec){
    // we'll handle the planar colours later
    width = ptr->v_width;
  }
  height = ptr->v_height;
  dest = ptr->video_buf;
  s1 = clone;
  s2 = next+width;
  for(i=0;i<height;i++){
    ac_memcpy(dest,s1,width);
    dest += width;
    // check to make sure we don't have an odd number of rows;
    if (++i < height){
      ac_memcpy(dest,s2,width);
      dest += width;
      s1 += width<<1;
      s2 += width<<1;
    }
  }
  if (TC_CODEC_YUV420P == ptr->v_codec){
    // here we handle the planar color part of the data
    dest = ptr->video_buf + width*height;
    s1 = ptr->video_buf+width*height;
    s2 = ptr->video_buf+width*height+(width>>1);
    // we'll save some shifting and recalc width;
    width = width >>1;

    // we don't have to divide the height by 2 because we've
    // got two colors, we'll handle them in one sweep.
    for(i=0;i<height;i++){
      ac_memcpy(dest,s1,width);
      dest+=width;
      // check to make sure we don't have an odd number of rows;
      if(++i < height){
        ac_memcpy(dest,s2,width);
	dest += width;
	s1 += width<<1;
	s2 += width<<1;
      }
    }
  }
}

/******
 * Clone takes in 2 buffers, the current vframe list (for height,width,codec,etc),
 * and the frame number in the input and the output
 * stream and will then do anything fancy needed
 ******/
static void fancy_clone(char* clone, char* next, vframe_list_t *ptr, int tin, int tout){
  if ((ptr == NULL) || (clone == NULL) || (next == NULL) || (ptr->video_buf == NULL)){
    tc_log_error(MOD_NAME, "Big error; we're about to dereference NULL");
    return;
  }
  switch (clonetype){
    case 0:
      ac_memcpy(ptr->video_buf,clone,ptr->video_size);
      break;
    case 1:
      clone_interpolate(clone,next,ptr);
      break;
    case 2:
      clone_interpolate(next,clone,ptr);
      break;
    case 3:
      clone_average(clone,next,ptr);
      break;
    case 4:
      clone_temporal_average(clone,next,ptr,tin,tout);
      break;
    case 5:
      if (ptr->v_codec != TC_CODEC_YUV420P){
        tc_log_error(MOD_NAME, "phosphor merge only implemented for YUV data");
	return;
      }
      clone_phosphor_average(clone,next,ptr);
      break;
    default:
      tc_log_error(MOD_NAME, "unimplemented clonetype");
      break;
  }
  return;
}

static int memory_init(vframe_list_t * ptr){

  int i;
  frbufsize = numSample +1;
  if (ptr->v_codec == TC_CODEC_YUV420P){
    // we only care about luminance
    scanrange = ptr->v_height*ptr->v_width;
  } else if (ptr->v_codec == TC_CODEC_RGB24){
    scanrange = ptr->v_height*ptr->v_width*3;
  } else if (ptr->v_codec == TC_CODEC_YUY2){
    // we only care about luminance, but since this is packed
    // we'll look at everything.
    scanrange = ptr->v_height*ptr->v_width*2;
  }

  if (scanrange > ptr->video_size){
    // error, we'll overwalk boundaries later on
    tc_log_error(MOD_NAME, "video_size doesn't look to be big enough (scan=%d video_size=%d).",
                    scanrange,ptr->video_size);
    return -1;
  }

  frames = tc_malloc(sizeof (char*)*frbufsize);
  if (NULL == frames){
    tc_log_error(MOD_NAME, "Error allocating memory in init");
    return -1;
  } // else
  for (i=0;i<frbufsize; i++){
    frames[i] = tc_malloc(sizeof(char)*ptr->video_size);
    if (NULL == frames[i]){
      tc_log_error(MOD_NAME, "Error allocating memory in init");
      return -1;
    }
  }
  framesOK = tc_malloc(sizeof(int)*frbufsize);
  if (NULL == framesOK){
    tc_log_error(MOD_NAME, "Error allocating memory in init");
    return -1;
  }
  framesScore = tc_malloc(sizeof(int)*frbufsize);
  if (NULL == framesScore){
    tc_log_error(MOD_NAME, "Error allocating memory in init");
    return -1;
  }
  if (mode == 1){
    return 0;
  }
  return -1;
}

int tc_filter(frame_list_t *ptr_, char *options)
{
    vframe_list_t *ptr = (vframe_list_t *)ptr_;
    static vob_t *vob = NULL;
    static int framesin = 0;
    static int init = 1;
    static int cloneq = 0; // queue'd clones ;)
    static int outframes = 0;

    //----------------------------------
    //
    // filter init
    //
    //----------------------------------


    if (ptr->tag & TC_FILTER_INIT) {

	if ((vob = tc_get_vob()) == NULL)
	    return (-1);

	// defaults
	outfps = vob->ex_fps;
	infps  = vob->fps;
	infrc  = vob->im_frc;

	// filter init ok.
	if (options != NULL) {
	  if (optstr_lookup (options, "help")) {
	    help_optstr();
	  }
	  optstr_get (options, "verbose", "%d", &show_results);
	  optstr_get (options, "mode", "%d", &mode);
	  optstr_get (options, "infps", "%lf", &infps);
	  optstr_get (options, "infrc", "%d", &infrc);
	  optstr_get (options, "buffer", "%d", &numSample);
	  optstr_get (options, "subsample", "%d", &offset);
	  optstr_get (options, "clonetype", "%d", &clonetype);

	}

	if (infrc>0 && infrc < 16){
	  tc_frc_code_to_value(infrc, &infps);
	}

	if (verbose){
	  tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
	  tc_log_info(MOD_NAME, "converting from %2.4ffps to %2.4ffps",infps,outfps);
	}

	if (outfps > infps*2.0){
	  tc_log_error(MOD_NAME, "desired output fps can not be greater");
	  tc_log_error(MOD_NAME, "than twice the input fps");
	  return -1;
	}

	if ( (outfps == infps) || (infrc && infrc == vob->ex_frc)) {
	  tc_log_error(MOD_NAME, "No framerate conversion requested, exiting");
	  return -1;
	}

	// clone in POST_S skip in PRE_S
	if (outfps > infps){
	  runnow = TC_POST_S_PROCESS;
	} else {
	  runnow = TC_PRE_S_PROCESS;
	}

	if ((mode >= 0) && (mode < 2)){
	  return 0;
	} // else

	tc_log_error(MOD_NAME, "only two modes of operation.");
	return -1;
    }

    if (ptr->tag & TC_FILTER_GET_CONFIG){
      char buf[255];
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VYRE", "1");

      tc_snprintf(buf, sizeof(buf), "%d",mode);
      optstr_param(options,"mode","mode of operation", "%d", buf, "0", "1");
      tc_snprintf(buf, sizeof(buf), "%f", infps);
      optstr_param(options, "infps", "Original fps", "%f", buf, "MIN_FPS", "200.0");
      tc_snprintf(buf, sizeof(buf), "%d", infrc);
      optstr_param(options, "infrc", "Original frc", "%d", buf, "0", "16");
      tc_snprintf(buf, sizeof(buf), "%d", numSample);
      optstr_param(options,"examine", "How many frames to buffer", "%d", buf, "2", "25");
      tc_snprintf(buf, sizeof(buf), "%d", offset);
      optstr_param(options, "subsample", "How many pixels to subsample", "%d", buf, "1", "256");
      tc_snprintf(buf, sizeof(buf), "%d", clonetype);
      optstr_param(options, "clonetype", "How to clone frames", "%d", buf, "0", "16");
      tc_snprintf(buf, sizeof(buf), "%d", verbose);
      optstr_param(options, "verbose", "run in verbose mode", "%d", buf, "0", "1");
      return 0;
    }

    //----------------------------------
    //
    // filter close
    //
    //----------------------------------


    if (ptr->tag & TC_FILTER_CLOSE) {

	return (0);
    }
    //----------------------------------
    //
    // filter frame routine
    //
    //----------------------------------


    // tag variable indicates, if we are called before
    // transcodes internal video/audio frame processing routines
    // or after and determines video/audio context

    if ((ptr->tag & runnow) && (ptr->tag & TC_VIDEO)) {
      if (mode == 0){
        if (show_results){
          tc_log_info(MOD_NAME, "in=%5d out=%5d win=%05.3f wout=%05.3f ",
                          framesin,outframes,(double)framesin/infps,outframes/outfps);
	}
        if (infps < outfps){
	  // Notes; since we currently only can clone frames (and just clone
	  // them once, we can at most double the input framerate.
	  if (ptr->attributes & TC_FRAME_WAS_CLONED){
	    // we can't clone it again, so we'll record the outframe and exit
	    ++outframes;
	    if (show_results){
	      tc_log_info(MOD_NAME, "\n");
	    }
	    return 0;
	  } // else
	  if ((double)framesin++/infps > (double)outframes++/outfps){
	    if (show_results){
	      tc_log_info(MOD_NAME, "FRAME IS CLONED");
	    }
	    ptr->attributes |= TC_FRAME_IS_CLONED;
	  }
	} else {
	  if ((double)framesin++/infps > outframes / outfps){
	       ++outframes;
	  } else {
	    if (show_results){
	      tc_log_info(MOD_NAME, "FRAME IS SKIPPED");
	    }
	    ptr->attributes |= TC_FRAME_IS_SKIPPED;
	  }
	}
	if (show_results){
	  tc_log_info(MOD_NAME, "\n");
	}
	return(0);
      } // else
      if (mode == 1){
        int i;
	if (init){
	  init = 0;
	  i = memory_init(ptr);
	  if(i!=0){
	    return i;
	  }
	}
	if (show_results){
          tc_log_info(MOD_NAME, "frameIn=%d frameOut=%d in=%5d out=%5d win=%05.3f wout=%05.3f ",
                          frameIn,frameOut,framesin-numSample,outframes+cloneq,
                          (double)(framesin-numSample)/infps,(double)(outframes+cloneq)/outfps);
	}
	if (ptr->attributes & TC_FRAME_WAS_CLONED){
	  // don't do anything.  Since it's cloned, we don't
	  // want to put it our buffers, as it will just clog
	  // them up.  Later, we can try some merging/interpolation
	  // as the user requests and then leave, but for now, we'll
	  // just flee.
	  if (framesOK[(frameIn+0)%frbufsize]){
	    tc_log_warn(MOD_NAME, "this frame wasn't cloned but we thought it was");
	  }
	  ++outframes;
	  --cloneq;
	  if (show_results){
	    tc_log_info(MOD_NAME, "no slot needed for clones");
	  }
	  fancy_clone(frames[frameIn],frames[(frameIn+1)%frbufsize],ptr,framesin-numSample,outframes+cloneq+1);
	  return 0;
	} // else
	ac_memcpy(frames[frameIn], ptr->video_buf, ptr->video_size);
	framesOK[frameIn] = 1;
#ifdef DEBUG
	tc_log_info(MOD_NAME, "Inserted frame %d into slot %d",framesin, frameIn);
#endif // DEBUG

	// Now let's look and see if we should compute a frame's
	// score.
	if (framesin > 0){
	  char *t1, *t2;
	  int *score,t;
	  t=(frameIn+numSample)%frbufsize;
	  score = &framesScore[t];
	  t1 = frames[t];
	  t2 = frames[frameIn];
#ifdef DEBUG
	    tc_log_info(MOD_NAME, "score: slot=%d, t1=%p t2=%p ",
	      t,t1,t2);
#endif // DEBUG
	  *score=0;
	  for(i=0; i<ptr->video_size; i+=offset){
	    *score += abs(t2[i] - t1[i]);
	  }
#ifdef DEBUG
	    tc_log_info(MOD_NAME, "score = %d\n",*score);
#endif // DEBUG
	}

	// the first frbufsize-1 frames are not processed; only buffered
	// so that we might be able to effectively see the future frames
	// when deciding about a frame.
	if(framesin < frbufsize-1){
	  ptr->attributes |= TC_FRAME_IS_SKIPPED;
	  frameIn = (frameIn+1) % frbufsize;
	  ++framesin;
	  if (show_results){
	    tc_log_info(MOD_NAME, "\n");
	  }
	  return 0;
	} // else

	// having filled the buffer, we will now check to see if we
	// are ready to clone/skip a frame.  If we are, we look for the frame to skip
	// in the buffer
	if (infps < outfps){
	  if ((double)(framesin-numSample)/infps > (double)(cloneq+outframes++)/outfps){
	    // we have to find a frame to clone
	    int diff=-1, mod=-1;
#ifdef DEBUG
	      tc_log_info(MOD_NAME, "start=%d end=%d",(frameIn+1)%frbufsize,frameIn);
#endif // DEBUG
	    fflush(stdout);
	    for(i=((frameIn+1)%frbufsize); i!=frameIn; i=((i+1)%frbufsize)){
#ifdef DEBUG
	        tc_log_info(MOD_NAME, "i=%d Ok=%d Score=%d",i,framesOK[i],framesScore[i]);
#endif // DEBUG
	      // make sure we haven't skipped/cloned this frame already
	      if(framesOK[i]){
	        // look for the frame with the most difference from it's next neighbor
	        if (framesScore[i] > diff){
		  diff = framesScore[i];
		  mod = i;
		}
	      }
	    }
	    if (mod == -1){
	      tc_log_error(MOD_NAME, "Error calculating frame to clone");
	      return -1;
	    }
#ifdef DEBUG
	    tc_log_info(MOD_NAME, "cloning  %d",mod);
#endif // DEBUG
	    ++cloneq;
	    framesOK[mod] = 0;
	  }
	  ac_memcpy(ptr->video_buf,frames[frameOut],ptr->video_size);
	  if (framesOK[frameOut]){
	    if (show_results){
	      tc_log_info(MOD_NAME, "giving   slot %2d frame %6d",frameOut,ptr->id);
	    }
	  } else {
	    ptr->attributes |= TC_FRAME_IS_CLONED;
	    if (show_results){
	      tc_log_info(MOD_NAME, "cloning  slot %2d frame %6d",frameOut,ptr->id);
	    }
	  }
	  frameOut = (frameOut+1) % frbufsize;
	} else {
	  // check to skip frames
	  if ((double)(framesin-numSample)/infps < (double)(outframes)/outfps){
	    int diff=INT_MAX, mod=-1;

	    // since we're skipping, we look for the frame with the lowest
	    // difference between the frame which follows it.
	    for(i=((frameIn+1)%frbufsize); i!=frameIn; i=((i+1)%frbufsize)){
#ifdef DEBUG
	        tc_log_info(MOD_NAME, "i=%d Ok=%d Score=%d",i,framesOK[i],framesScore[i]);
#endif // debug
	      // make sure we haven't skipped/cloned this frame already
	      if(framesOK[i]){
	        if (framesScore[i] < diff){
		  diff = framesScore[i];
		  mod = i;
		}
	      }
	    }
	    if (mod == -1){
	      tc_log_error(MOD_NAME, "Error calculating frame to skip");
	      return -1;
	    }
	    framesOK[mod] = 0;
	  } else {
	    ++outframes;
	  }
	  if (framesOK[frameOut]){
	    ac_memcpy(ptr->video_buf,frames[frameOut],ptr->video_size);
	    if (show_results){
	      tc_log_info(MOD_NAME, "giving   slot %2d frame %6d",frameOut,ptr->id);
	    }
	  } else {
	    ptr->attributes |= TC_FRAME_IS_SKIPPED;
	    if (show_results){
	      tc_log_warn(MOD_NAME, "skipping slot %2d frame %6d",frameOut,ptr->id);
	    }
	  }
	  frameOut = (frameOut+1) % frbufsize;
	}
	frameIn = (frameIn+1) % frbufsize;
	++framesin;
	return 0;
      }
      tc_log_error(MOD_NAME, "currently only 2 modes of operation");
      return(-1);
    }

    return (0);
}
