/*
    Copyright (C) 2003 Daniel Moreno <comac@comac.darktech.org>
    Converted for use in transcode by Tilmann Bitterberg <transcode@tibit.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#define MOD_NAME    "filter_hqdn3d.so"
#define MOD_VERSION "v1.0.2 (2003-08-15)"
#define MOD_CAP     "High Quality 3D Denoiser"
#define MOD_AUTHOR  "Daniel Moreno, A'rpi"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include <math.h>


#define PARAM1_DEFAULT 4.0
#define PARAM2_DEFAULT 3.0
#define PARAM3_DEFAULT 6.0

//===========================================================================//

typedef struct vf_priv_s {
        int Coefs[4][512*16];
        unsigned int *Line;
	unsigned short *Frame[3];
	int pre;
} MyFilterData;


/***************************************************************************/

static inline unsigned int LowPassMul(unsigned int PrevMul, unsigned int CurrMul, int* Coef){
//    int dMul= (PrevMul&0xFFFFFF)-(CurrMul&0xFFFFFF);
    int dMul= PrevMul-CurrMul;
    int d=((dMul+0x10007FF)/(65536/16));
    return CurrMul + Coef[d];
}

static void deNoise(unsigned char *Frame,        // mpi->planes[x]
                    unsigned char *FrameDest,    // dmpi->planes[x]
                    unsigned int *LineAnt,      // vf->priv->Line (width bytes)
		    unsigned short **FrameAntPtr,
                    int W, int H, int sStride, int dStride,
                    int *Horizontal, int *Vertical, int *Temporal)
{
    int X, Y;
    int sLineOffs = 0, dLineOffs = 0;
    unsigned int PixelAnt;
    int PixelDst;
    unsigned short* FrameAnt=(*FrameAntPtr);

    if(!FrameAnt){
	(*FrameAntPtr)=FrameAnt=tc_malloc(W*H*sizeof(unsigned short));
	for (Y = 0; Y < H; Y++){
	    unsigned short* dst=&FrameAnt[Y*W];
	    unsigned char* src=Frame+Y*sStride;
	    for (X = 0; X < W; X++) dst[X]=src[X]<<8;
	}
    }

    /* First pixel has no left nor top neightbour. Only previous frame */
    LineAnt[0] = PixelAnt = Frame[0]<<16;
    PixelDst = LowPassMul(FrameAnt[0]<<8, PixelAnt, Temporal);
    FrameAnt[0] = ((PixelDst+0x1000007F)/256);
    FrameDest[0]= ((PixelDst+0x10007FFF)/65536);

    /* Fist line has no top neightbour. Only left one for each pixel and
     * last frame */
    for (X = 1; X < W; X++){
        LineAnt[X] = PixelAnt = LowPassMul(PixelAnt, Frame[X]<<16, Horizontal);
        PixelDst = LowPassMul(FrameAnt[X]<<8, PixelAnt, Temporal);
	FrameAnt[X] = ((PixelDst+0x1000007F)/256);
	FrameDest[X]= ((PixelDst+0x10007FFF)/65536);
    }

    for (Y = 1; Y < H; Y++){
	unsigned int PixelAnt;
	unsigned short* LinePrev=&FrameAnt[Y*W];
	sLineOffs += sStride, dLineOffs += dStride;
        /* First pixel on each line doesn't have previous pixel */
        PixelAnt = Frame[sLineOffs]<<16;
        LineAnt[0] = LowPassMul(LineAnt[0], PixelAnt, Vertical);
	PixelDst = LowPassMul(LinePrev[0]<<8, LineAnt[0], Temporal);
	LinePrev[0] = ((PixelDst+0x1000007F)/256);
	FrameDest[dLineOffs]= ((PixelDst+0x10007FFF)/65536);

        for (X = 1; X < W; X++){
	    int PixelDst;
            /* The rest are normal */
            PixelAnt = LowPassMul(PixelAnt, Frame[sLineOffs+X]<<16, Horizontal);
            LineAnt[X] = LowPassMul(LineAnt[X], PixelAnt, Vertical);
	    PixelDst = LowPassMul(LinePrev[X]<<8, LineAnt[X], Temporal);
	    LinePrev[X] = ((PixelDst+0x1000007F)/256);
	    FrameDest[dLineOffs+X]= ((PixelDst+0x10007FFF)/65536);
        }
    }
}


//===========================================================================//


static void PrecalcCoefs(int *Ct, double Dist25)
{
    int i;
    double Gamma, Simil, C;

    Gamma = log(0.25) / log(1.0 - Dist25/255.0 - 0.00001);

    for (i = -256*16; i < 256*16; i++)
    {
        Simil = 1.0 - (i>0?i:-i) / (16*255.0);
        C = pow(Simil, Gamma) * 65536.0 * (double)i / 16.0;
        Ct[16*256+i] = (C<0) ? (C-0.5) : (C+0.5);
    }
}

static void help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help\n"
"* Overview\n"
"  This filter aims to reduce image noise producing\n"
"  smooth images and making still images really still\n"
"  (This should enhance compressibility).\n"
"* Options\n"
"             luma : spatial luma strength (%f)\n"
"           chroma : spatial chroma strength (%f)\n"
"    luma_strength : temporal luma strength (%f)\n"
"  chroma_strength : temporal chroma strength (%f)\n"
"              pre : run as a pre filter (0)\n"
		, MOD_CAP,
		PARAM1_DEFAULT,
		PARAM2_DEFAULT,
		PARAM3_DEFAULT,
		PARAM3_DEFAULT*PARAM2_DEFAULT/PARAM1_DEFAULT);
}

// main filter routine
int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  static vob_t *vob=NULL;
  /* FIXME: these use the filter ID as an index--the ID can grow
   * arbitrarily large, so this needs to be fixed */
  static MyFilterData *mfd[100];
  static char *buffer[100];
  int instance = ptr->filter_id;


  if(ptr->tag & TC_AUDIO)
      return 0;

  if(ptr->tag & TC_FILTER_GET_CONFIG) {

      char buf[128];
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VYMOE", "2");

      tc_snprintf(buf, 128, "%f", PARAM1_DEFAULT);
      optstr_param (options, "luma", "spatial luma strength", "%f", buf, "0.0", "100.0" );

      tc_snprintf(buf, 128, "%f", PARAM2_DEFAULT);
      optstr_param (options, "chroma", "spatial chroma strength", "%f", buf, "0.0", "100.0" );

      tc_snprintf(buf, 128, "%f", PARAM3_DEFAULT);
      optstr_param (options, "luma_strength", "temporal luma strength", "%f", buf, "0.0", "100.0" );

      tc_snprintf(buf, 128, "%f", PARAM3_DEFAULT*PARAM2_DEFAULT/PARAM1_DEFAULT);
      optstr_param (options, "chroma_strength", "temporal chroma strength", "%f", buf, "0.0", "100.0" );
      tc_snprintf(buf, 128, "%d", mfd[instance]->pre);
      optstr_param (options, "pre", "run as a pre filter", "%d", buf, "0", "1" );

      return 0;
  }

  if(ptr->tag & TC_FILTER_INIT) {

      double LumSpac, LumTmp, ChromSpac, ChromTmp;
      double Param1=0.0, Param2=0.0, Param3=0.0, Param4=0.0;

      if((vob = tc_get_vob())==NULL) return(-1);

      if (vob->im_v_codec != TC_CODEC_YUV420P) {
	  tc_log_error(MOD_NAME, "This filter is only capable of YUV 4:2:0 mode");
	  return -1;
      }

      mfd[instance] = tc_zalloc(sizeof(MyFilterData));

      if (mfd[instance]) {
	  mfd[instance]->Line = tc_zalloc(TC_MAX_V_FRAME_WIDTH*sizeof(int));
      }

      buffer[instance] = tc_zalloc(SIZE_RGB_FRAME);

      if (!mfd[instance] || !mfd[instance]->Line || !buffer[instance]) {
	  tc_log_error(MOD_NAME, "Malloc failed");
	  return -1;
      }


      // defaults

      LumSpac = PARAM1_DEFAULT;
      LumTmp = PARAM3_DEFAULT;

      ChromSpac = PARAM2_DEFAULT;
      ChromTmp = LumTmp * ChromSpac / LumSpac;

      if (options) {

	  if (optstr_lookup (options, "help")) {
	      help_optstr();
	  }

	  optstr_get (options, "luma",           "%lf",    &Param1);
	  optstr_get (options, "luma_strength",  "%lf",    &Param3);
	  optstr_get (options, "chroma",         "%lf",    &Param2);
	  optstr_get (options, "chroma_strength","%lf",    &Param4);
	  optstr_get (options, "pre", "%d",    &mfd[instance]->pre);

	  // recalculate only the needed params

	  if (Param1!=0.0) {

	      LumSpac = Param1;
	      LumTmp = PARAM3_DEFAULT * Param1 / PARAM1_DEFAULT;

	      ChromSpac = PARAM2_DEFAULT * Param1 / PARAM1_DEFAULT;
	      ChromTmp = LumTmp * ChromSpac / LumSpac;
	  }
	  if (Param2!=0.0) {

	      ChromSpac = Param2;
	      ChromTmp = LumTmp * ChromSpac / LumSpac;
	  }
	  if (Param3!=0.0) {

	      LumTmp = Param3;
	      ChromTmp = LumTmp * ChromSpac / LumSpac;

	  }
	  if (Param4!=0.0) {

	      ChromTmp = Param4;
	  }

      }

      PrecalcCoefs(mfd[instance]->Coefs[0], LumSpac);
      PrecalcCoefs(mfd[instance]->Coefs[1], LumTmp);
      PrecalcCoefs(mfd[instance]->Coefs[2], ChromSpac);
      PrecalcCoefs(mfd[instance]->Coefs[3], ChromTmp);


      if(verbose) {
	  tc_log_info(MOD_NAME, "%s %s #%d", MOD_VERSION, MOD_CAP, instance);
	  tc_log_info(MOD_NAME, "Settings luma=%.2f chroma=%.2f luma_strength=%.2f chroma_strength=%.2f",
		  LumSpac, ChromSpac, LumTmp, ChromTmp);
      }
      return 0;
  }



  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE) {

      if (buffer[instance]) {free(buffer[instance]); buffer[instance]=NULL;}
      if (mfd[instance]) {
	  if(mfd[instance]->Line){free(mfd[instance]->Line);mfd[instance]->Line=NULL;}
	  if(mfd[instance]->Frame[0]){free(mfd[instance]->Frame[0]);mfd[instance]->Frame[0]=NULL;}
	  if(mfd[instance]->Frame[1]){free(mfd[instance]->Frame[1]);mfd[instance]->Frame[1]=NULL;}
	  if(mfd[instance]->Frame[2]){free(mfd[instance]->Frame[2]);mfd[instance]->Frame[2]=NULL;}
	  free(mfd[instance]);
      }
      mfd[instance]=NULL;

      return(0);

  } /* filter close */

  //actually do the filter

  if(((ptr->tag & TC_PRE_M_PROCESS  && mfd[instance]->pre) ||
	  (ptr->tag & TC_POST_M_PROCESS && !mfd[instance]->pre)) &&
	  !(ptr->attributes & TC_FRAME_IS_SKIPPED)) {

      ac_memcpy (buffer[instance], ptr->video_buf, ptr->video_size);

      deNoise(buffer[instance],                                  ptr->video_buf,
	      mfd[instance]->Line, &mfd[instance]->Frame[0], ptr->v_width, ptr->v_height,
	      ptr->v_width,  ptr->v_width,
	      mfd[instance]->Coefs[0], mfd[instance]->Coefs[0], mfd[instance]->Coefs[1]);

      deNoise(buffer[instance] + ptr->v_width*ptr->v_height,     ptr->video_buf + ptr->v_width*ptr->v_height,
	      mfd[instance]->Line, &mfd[instance]->Frame[1], ptr->v_width>>1, ptr->v_height>>1,
	      ptr->v_width>>1,  ptr->v_width>>1,
	      mfd[instance]->Coefs[2], mfd[instance]->Coefs[2], mfd[instance]->Coefs[3]);

      deNoise(buffer[instance] + 5*ptr->v_width*ptr->v_height/4, ptr->video_buf + 5*ptr->v_width*ptr->v_height/4,
	      mfd[instance]->Line, &mfd[instance]->Frame[2], ptr->v_width>>1, ptr->v_height>>1,
	      ptr->v_width>>1,  ptr->v_width>>1,
	      mfd[instance]->Coefs[2], mfd[instance]->Coefs[2], mfd[instance]->Coefs[3]);

  }
  return 0;

}


//===========================================================================//
