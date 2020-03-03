/*
 *  filter_normalize.c
 *
 *  Copyright (C) pl <p_l@gmx.fr> 2002 and beyond...
 *                Tilmann Bitterberg - June 2002 ported to transcode
 *
 *  Sources: some ideas from volnorm plugin for xmms
 *
 *  This file is part of transcode, a video stream processing tool
 *
 *  transcode is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 *  transcode is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Values for AVG:
 * 1: uses a 1 value memory and coefficients new=a*old+b*cur (with a+b=1)
 *
 * 2: uses several samples to smooth the variations (standard weighted mean
 *    on past samples)
 *
 * Limitations:
 *  - only AFMT_S16_LE supported
 *
 * */

#define MOD_NAME    "filter_normalize.so"
#define MOD_VERSION "v0.1.1 (2002-06-18)"
#define MOD_CAP     "Volume normalizer"
#define MOD_AUTHOR  "pl, Tilmann Bitterberg"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include <math.h>



// basic parameter
// mul is the value by which the samples are scaled
// and has to be in [MUL_MIN, MUL_MAX]
#define MUL_INIT 1.0
#define MUL_MIN 0.1
#define MUL_MAX 5.0

#define MIN_SAMPLE_SIZE 32000

// Some limits
#define MIN_S16 -32768
#define MAX_S16  32767

// "Ideal" level
#define MID_S16 (MAX_S16 * 0.25)

// Silence level
// FIXME: should be relative to the level of the samples
#define SIL_S16 (MAX_S16 * 0.01)


// Local data

#define NSAMPLES 128

struct mem_t {
    double avg;		// average level of the sample
    int32_t len;	// sample size (weight)
};

typedef struct MyFilterData {
	int format;
	double mul;
	double SMOOTH_MUL;
	double SMOOTH_LASTAVG;
	double lastavg;
	int idx;
	struct mem_t mem[NSAMPLES];
	int AVG;
} MyFilterData;

static MyFilterData *mfd = NULL;

/* should probably honor the other flags too */

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

static void help_optstr(void)
{
   tc_log_info (MOD_NAME, "(%s) help\n"
"* Overview\n"
"    normalizes audio\n"
"* Options\n"
"     'smooth' double for smoothing ]0.0 1.0[  [0.06]\n"
" 'smoothlast' double for smoothing last sample ]0.0, 1.0[  [0.06]\n"
"       'algo' Which algorithm to use (1 or 2) [1]\n"
"            1: uses a 1 value memory and coefficients new=a*old+b*cur (with a+b=1)\n"
"            2: uses several samples to smooth the variations (standard weighted mean\n"
"            on past samples)\n"
		, MOD_CAP);
}

static void reset(void)
{
  int i;
  mfd->mul = MUL_INIT;
  switch(mfd->format) {
      case(1): /* XXX: bogus */
      mfd->lastavg = MID_S16;
      for(i=0; i < NSAMPLES; ++i) {
	      mfd->mem[i].len = 0;
	      mfd->mem[i].avg = 0;
      }
      mfd->idx = 0;

      break;
    default:
      break;
  }
}

int tc_filter(frame_list_t *ptr_, char *options)
{
  aframe_list_t *ptr = (aframe_list_t *)ptr_;
  static vob_t *vob=NULL;

  if(ptr->tag & TC_FILTER_GET_CONFIG) {
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "pl, Tilmann Bitterberg", "AE", "1");
      optstr_param (options, "smooth", "Value for smoothing ]0.0 1.0[", "%f", "0.06", "0.0", "1.0");
      optstr_param (options, "smoothlast", "Value for smoothing last sample ]0.0, 1.0[", "%f", "0.06", "0.0", "1.0");
      optstr_param (options, "algo", "Algorithm to use (1 or 2). 1=uses a 1 value memory and coefficients new=a*old+b*cur (with a+b=1).   2=uses several samples to smooth the variations (standard weighted mean on past samples)", "%d", "1", "1", "2");
      return 0;
  }


  //----------------------------------
  //
  // filter init
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL) return(-1);

    if (vob->a_bits != 16) {
	tc_log_error(MOD_NAME, "This filter only works for 16 bit samples");
	return (-1);

    }
    if((mfd = tc_malloc (sizeof(MyFilterData))) == NULL) return (-1);

    mfd->format  = 1; /* XXX bogus */
    mfd->mul     = MUL_INIT;
    mfd->lastavg = MID_S16;
    mfd->idx     = 0;
    mfd->SMOOTH_MUL     = 0.06;
    mfd->SMOOTH_LASTAVG = 0.06;
    mfd->AVG     = 1;

    reset();

    if (options != NULL) {

	if(verbose) tc_log_info(MOD_NAME, "options=%s", options);

	optstr_get(options, "smooth", "%lf", &mfd->SMOOTH_MUL);
	optstr_get(options, "smoothlast", "%lf", &mfd->SMOOTH_LASTAVG);
	optstr_get(options, "algo", "%d", &mfd->AVG);

	if (mfd->AVG > 2) mfd->AVG = 2;
	if (mfd->AVG < 1) mfd->AVG = 1;

    }

#if 0
    if (verbose > 1) {
	tc_log_info (MOD_NAME, " Normalize Filter Settings:");
    }
#endif

    if (options)
	if (optstr_lookup (options, "help")) {
	    help_optstr();
	}

    // filter init ok.
    if (verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);


    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE) {

    if (mfd) {
	free(mfd);
    }

    return(0);

  } /* filter close */

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------


  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  if((ptr->tag & TC_PRE_M_PROCESS) && (ptr->tag & TC_AUDIO) && !(ptr->attributes & TC_FRAME_IS_SKIPPED))  {


#define CLAMP(x,m,M) do { if ((x)<(m)) (x) = (m); else if ((x)>(M)) (x) = (M); } while(0)

    int16_t* data=(int16_t *)ptr->audio_buf;
    int len=ptr->audio_size / 2; // 16 bits samples

    int32_t i, tmp;
    double curavg, newavg;

    double neededmul;

    double avg;
    int32_t totallen;

    // Evaluate current samples average level
    curavg = 0.0;
    for (i = 0; i < len ; ++i) {
      tmp = data[i];
      curavg += tmp * tmp;
    }
    curavg = sqrt(curavg / (double) len);

    // Evaluate an adequate 'mul' coefficient based on previous state, current
    // samples level, etc
    if (mfd->AVG == 1) {
	if (curavg > SIL_S16) {
	    neededmul = MID_S16 / ( curavg * mfd->mul);
	    mfd->mul = (1.0 - mfd->SMOOTH_MUL) * mfd->mul + mfd->SMOOTH_MUL * neededmul;

	    // Clamp the mul coefficient
	    CLAMP(mfd->mul, MUL_MIN, MUL_MAX);
	}
    } else if (mfd->AVG == 2) {
	avg = 0.0;
	totallen = 0;

	for (i = 0; i < NSAMPLES; ++i) {
	    avg += mfd->mem[i].avg * (double) mfd->mem[i].len;
	    totallen += mfd->mem[i].len;
	}

	if (totallen > MIN_SAMPLE_SIZE) {
	    avg /= (double) totallen;
	    if (avg >= SIL_S16) {
		mfd->mul = (double) MID_S16 / avg;
		CLAMP(mfd->mul, MUL_MIN, MUL_MAX);
	    }
	}
    }

    // Scale & clamp the samples
    for (i = 0; i < len ; ++i) {
      tmp = mfd->mul * data[i];
      CLAMP(tmp, MIN_S16, MAX_S16);
      data[i] = tmp;
    }

    // Evaluation of newavg (not 100% accurate because of values clamping)
    newavg = mfd->mul * curavg;

    // Stores computed values for future smoothing
    if (mfd->AVG == 1) {
	mfd->lastavg = (1.0-mfd->SMOOTH_LASTAVG)*mfd->lastavg + mfd->SMOOTH_LASTAVG*newavg;
    } else if (mfd->AVG == 2) {
	mfd->mem[mfd->idx].len = len;
	mfd->mem[mfd->idx].avg = newavg;
	mfd->idx = (mfd->idx + 1) % NSAMPLES;
    }


  }

  return(0);
}

