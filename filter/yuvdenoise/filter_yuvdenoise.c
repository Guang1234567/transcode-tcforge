/*
 *  filter_yuvdenoise.c
 *
 *  Copyright (C) Tilmann Bitterberg, July 2002
 *                based on work by Stefan Fendt
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

#define MOD_NAME    "filter_yuvdenoise.so"
#define MOD_VERSION "v0.2.1 (2003-11-26)"
#define MOD_CAP     "mjpegs YUV denoiser"
#define MOD_AUTHOR  "Stefan Fendt, Tilmann Bitterberg"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include <stdint.h>

#include "mjpeg_types.h"

#include "global.h"
#include "motion.h"
#include "denoise.h"
#include "deinterlace.h"

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

void allc_buffers(void);
void free_buffers(void);
void print_settings(void);
void turn_on_accels(void);
void display_help(void);

struct DNSR_GLOBAL denoiser;

extern uint32_t (*calc_SAD)         (uint8_t * , uint8_t * );
extern uint32_t (*calc_SAD_uv)      (uint8_t * , uint8_t * );
extern uint32_t (*calc_SAD_half)    (uint8_t * , uint8_t * ,uint8_t *);
extern void     (*deinterlace)      (void);


static int pre = 0; /* run as a pre process filter */
static int filter_verbose = 0;

/***********************************************************
 *                                                         *
 ***********************************************************/

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  static vob_t *vob=NULL;

  static int frame_offset;
  static int uninitialized = 1;
  static int frame_offset4;

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------

  if(ptr->tag & TC_AUDIO)
      return 0;


  if (ptr->tag & TC_FILTER_GET_CONFIG && options) {
      char buf[255];

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.delay); // frames_needed
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VYEO", buf);

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.radius);
      optstr_param (options, "radius",         "Search radius", "%d", buf, "8", "24");

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.threshold);
      optstr_param (options, "threshold",      "Denoiser threshold", "%d", buf, "0", "255");

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.pp_threshold);
      optstr_param (options, "pp_threshold",   "Pass II threshold", "%d",  buf, "0", "255");

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.delay);
      optstr_param (options, "delay",          "Average 'n' frames for a time-lowpassed pixel", "%d", buf, "1", "255"  );

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.postprocess);
      optstr_param (options, "postprocess",    "Filter internal postprocessing", "%d", buf, "0", "1"  );

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.luma_contrast);
      optstr_param (options, "luma_contrast",  "Luminance contrast in percent", "%d", buf, "0", "255" );

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.chroma_contrast);
      optstr_param (options, "chroma_contrast","Chrominance contrast in percent.", "%d", buf, "0", "255" );

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.sharpen);
      optstr_param (options, "sharpen",        "Sharpness in percent", "%d", buf, "0", "255"  );

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.deinterlace);
      optstr_param (options, "deinterlace",    "Force deinterlacing", "%d", buf, "0", "1" );

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.mode);
      optstr_param (options, "mode",           "[0]: Progressive [1]: Interlaced [2]: Fast", "%d", buf, "0", "2" );

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.scene_thres);
      optstr_param (options, "scene_thres",    "Blocks where motion estimation should fail before scenechange", "%d%%", buf, "0", "100" );

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.block_thres);
      optstr_param (options, "block_thres",    "Every SAD value greater than this will be considered bad", "%d", buf, "0", "oo" );

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.do_reset);
      optstr_param (options, "do_reset",       "Reset the filter for `n' frames after a scene", "%d", buf, "0", "oo" );

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.increment_cr);
      optstr_param (options, "increment_cr",   "Increment Cr with constant", "%d", buf, "-128", "127" );

      tc_snprintf (buf, sizeof(buf), "%d", denoiser.increment_cb);
      optstr_param (options, "increment_cb",   "Increment Cb with constant", "%d", buf, "-128", "127"  );

      tc_snprintf (buf, sizeof(buf), "%dx%d-%dx%d",
	denoiser.border.x, denoiser.border.y, denoiser.border.w, denoiser.border.h);
      optstr_param (options, "border",         "Active image area", "%dx%d-%dx%d", buf, "0", "W", "0", "H", "0", "W", "0", "H");

      optstr_param (options, "pre",   "run this filter as a pre-processing filter","%d", "0", "0", "1"  );


      return 0;
  }

  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL) return(-1);

    if (vob->im_v_codec == TC_CODEC_RGB24) {
      tc_log_error(MOD_NAME, "filter is not capable for RGB-Mode !");
      return(-1);
    }

    filter_verbose = verbose;

    /* setup denoiser's global variables */
    denoiser.radius          = 8;
    denoiser.threshold       = 5; /* assume medium noise material */
    denoiser.pp_threshold    = 4; /* same for postprocessing */
    denoiser.delay           = 3; /* short delay for good regeneration of rapid sequences */
    denoiser.postprocess     = 1;
    denoiser.luma_contrast   = 100;
    denoiser.chroma_contrast = 100;
    denoiser.sharpen         = 125; /* very little sharpen by default */
    denoiser.deinterlace     = 0;
    denoiser.mode            = 0; /* initial mode is progressive */
    denoiser.border.x        = 0;
    denoiser.border.y        = 0;
    denoiser.border.w        = 0;
    denoiser.border.h        = 0;

    denoiser.reset           = 0;
    denoiser.do_reset        = 2; /* reseting the denoiser after a scenechange */
				  /* gives much better results */
    denoiser.scene_thres     = 50;
    denoiser.block_thres     = 1024;

    denoiser.increment_cb    = 2;
    denoiser.increment_cr    = 2; /* maybe more? */


    /* process commandline */
    if (options) {
	int t1, t2, t3, t4; /* cant read in the struct correctly */
	if (optstr_get (options, "radius",         "%d", &t1) >= 0) denoiser.radius = t1&0xff;
	if (optstr_get (options, "threshold",      "%d", &t1) >= 0) denoiser.threshold = t1&0xff;
	if (optstr_get (options, "pp_threshold",   "%d", &t1) >= 0) denoiser.pp_threshold = t1&0xff;
	if (optstr_get (options, "delay",          "%d", &t1) >= 0) denoiser.delay = t1&0xff;
	if (optstr_get (options, "postprocess",    "%d", &t1) >= 0) denoiser.postprocess = t1&0xffff;
	if (optstr_get (options, "luma_contrast",  "%d", &t1) >= 0) denoiser.luma_contrast = t1&0xffff;
	if (optstr_get (options, "chroma_contrast","%d", &t1) >= 0) denoiser.chroma_contrast = t1&0xffff;
	if (optstr_get (options, "sharpen",        "%d", &t1) >= 0) denoiser.sharpen = t1&0xffff;
	if (optstr_get (options, "deinterlace",    "%d", &t1) >= 0) denoiser.deinterlace = t1&0xff;
	if (optstr_get (options, "mode",           "%d", &t1) >= 0) denoiser.mode = t1&0xff;

	if (optstr_get (options, "scene_thres",    "%d%%", &t1) >= 0) denoiser.scene_thres=t1;
	if (optstr_get (options, "block_thres",    "%d", &t1) >= 0) denoiser.block_thres=t1;
	if (optstr_get (options, "do_reset",       "%d", &t1) >= 0) denoiser.do_reset=t1;
	if (optstr_get (options, "increment_cr",   "%d", &t1) >= 0) denoiser.increment_cr=t1;
	if (optstr_get (options, "increment_cb",   "%d", &t1) >= 0) denoiser.increment_cb=t1;

	if (optstr_get (options, "border",         "%dx%d-%dx%d", &t1, &t2, &t3, &t4) >= 0) {
	    denoiser.border.x = t1&0xffff; denoiser.border.y = t2&0xffff;
	    denoiser.border.w = t3&0xffff; denoiser.border.h = t4&0xffff;
	}

	optstr_get (options, "pre",            "%d", &pre);

	if (optstr_lookup (options, "help") != NULL)
	    display_help();

        if(denoiser.radius<8) {
          denoiser.radius=8;
  	      tc_log_warn (MOD_NAME, "Minimum allowed search radius is 8 pixel.");
        } else if(denoiser.radius>24) {
  	      tc_log_warn (MOD_NAME, "Maximum suggested search radius is 24 pixel.");
        }
        if(denoiser.delay<1) {
          denoiser.delay=1;
  	      tc_log_warn (MOD_NAME, "Minimum allowed frame delay is 1.");
        } else if(denoiser.delay>8) {
  	      tc_log_warn (MOD_NAME, "Maximum suggested frame delay is 8.");
        }
	//denoiser.deinterlace=0;
    }

    if (pre) {
	denoiser.frame.w         = vob->im_v_width;
	denoiser.frame.h         = vob->im_v_height;
    } else {
	denoiser.frame.w         = vob->ex_v_width;
	denoiser.frame.h         = vob->ex_v_height;
    }


    frame_offset             = 32*denoiser.frame.w;
    frame_offset4            = frame_offset/4;

    if(denoiser.border.w == 0)
    {
	denoiser.border.x        = 0;
	denoiser.border.y        = 0;
	denoiser.border.w        = denoiser.frame.w;
	denoiser.border.h        = denoiser.frame.h;
    }

    /* get enough memory for the buffers */
    allc_buffers();

    /* print denoisers settings */
    if (verbose > 1)
	print_settings();

    /* turn on accelerations if any */
    turn_on_accels();

    // filter init ok.
    if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE) {
      free_buffers();
    return(0);
  }

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------

  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  if (vob->im_v_codec!=TC_CODEC_YUV420P)
      return 0;

  if(((ptr->tag & TC_PRE_M_PROCESS  && pre) ||
	  (ptr->tag & TC_POST_M_PROCESS && !pre)) &&
	  !(ptr->attributes & TC_FRAME_IS_SKIPPED)) {
      /* readability */
      unsigned int y_size  = denoiser.frame.w*denoiser.frame.h;
      unsigned int y_size4 = denoiser.frame.w*denoiser.frame.h>>2;

#ifdef HAVE_FILTER_IO_BUF
      /* Move into internal buffer */
      ac_memcpy(denoiser.frame.io[Yy], ptr->video_buf,            y_size );
      ac_memcpy(denoiser.frame.io[Cr], ptr->video_buf+y_size    , y_size4);
      ac_memcpy(denoiser.frame.io[Cb], ptr->video_buf+y_size*5/4, y_size4);

      /* pre-fixup for non-greenish look --tibit */
      {
	  int y;
	  uint8_t *p = denoiser.frame.io[Cb];
	  uint8_t *q = denoiser.frame.io[Cr];
	  int32_t pi;
	  int32_t qi;
	  for (y=0;y<W2*H2;y++) {
	      // Cb
	      pi = *p;
	      pi += denoiser.increment_cb;
	      *p = (pi>C_HI_LIMIT?C_HI_LIMIT:pi)&0xff;
	      *p = (pi<C_LO_LIMIT?C_LO_LIMIT:pi)&0xff;
	      p++;

	      // Cr
	      qi = *q;
	      qi += denoiser.increment_cr;
	      *q = (qi>C_HI_LIMIT?C_HI_LIMIT:qi)&0xff;
	      *q = (qi<C_LO_LIMIT?C_LO_LIMIT:qi)&0xff;
	      q++;
	  }
      }

#else
      denoiser.frame.io[Yy] = ptr->video_buf;
      denoiser.frame.io[Cr] = ptr->video_buf+y_size;
      denoiser.frame.io[Cb] = ptr->video_buf+y_size*5/4;
#endif

      /* Move frame down by 32 lines into reference buffer */
      ac_memcpy(denoiser.frame.ref[Yy]+frame_offset , denoiser.frame.io[Yy], y_size  );
      ac_memcpy(denoiser.frame.ref[Cr]+frame_offset4, denoiser.frame.io[Cr], y_size4);
      ac_memcpy(denoiser.frame.ref[Cb]+frame_offset4, denoiser.frame.io[Cb], y_size4);

      if(uninitialized) {
	  uninitialized=0;

	  ac_memcpy(denoiser.frame.avg[Yy]+frame_offset,   denoiser.frame.io[Yy],y_size );
	  ac_memcpy(denoiser.frame.avg[Cr]+frame_offset4,  denoiser.frame.io[Cr],y_size4);
	  ac_memcpy(denoiser.frame.avg[Cb]+frame_offset4,  denoiser.frame.io[Cb],y_size4);
	  ac_memcpy(denoiser.frame.avg2[Yy]+frame_offset,  denoiser.frame.io[Yy],y_size );
	  ac_memcpy(denoiser.frame.avg2[Cr]+frame_offset4, denoiser.frame.io[Cr],y_size4);
	  ac_memcpy(denoiser.frame.avg2[Cb]+frame_offset4, denoiser.frame.io[Cb],y_size4);
      }

      if(!denoiser.reset) { denoise_frame(); emms(); }

      if(denoiser.reset) {
	  if(verbose && denoiser.reset==denoiser.do_reset)
	    tc_log_info(MOD_NAME, "Scene change detected at frame <%d>", ptr->id);

	  ac_memcpy(denoiser.frame.avg[Yy]+frame_offset,   denoiser.frame.io[Yy],y_size );
	  ac_memcpy(denoiser.frame.avg[Cr]+frame_offset4,  denoiser.frame.io[Cr],y_size4);
	  ac_memcpy(denoiser.frame.avg[Cb]+frame_offset4,  denoiser.frame.io[Cb],y_size4);
	  ac_memcpy(denoiser.frame.avg2[Yy]+frame_offset,  denoiser.frame.io[Yy],y_size );
	  ac_memcpy(denoiser.frame.avg2[Cr]+frame_offset4, denoiser.frame.io[Cr],y_size4);
	  ac_memcpy(denoiser.frame.avg2[Cb]+frame_offset4, denoiser.frame.io[Cb],y_size4);

	  denoise_frame();
	  emms();
	  denoiser.reset--;
      }


      /* Move frame up by 32 lines into I/O buffer */
      ac_memcpy(denoiser.frame.io[Yy],denoiser.frame.avg2[Yy]+frame_offset ,y_size );
      ac_memcpy(denoiser.frame.io[Cr],denoiser.frame.avg2[Cr]+frame_offset4,y_size4);
      ac_memcpy(denoiser.frame.io[Cb],denoiser.frame.avg2[Cb]+frame_offset4,y_size4);

#ifdef HAVE_FILTER_IO_BUF
      /* move back to transcode */
      ac_memcpy(ptr->video_buf,           denoiser.frame.io[Yy] ,y_size );
      ac_memcpy(ptr->video_buf+y_size,    denoiser.frame.io[Cr] ,y_size4);
      ac_memcpy(ptr->video_buf+y_size*5/4,denoiser.frame.io[Cb] ,y_size4);
#endif

  }

  return(0);
}


static uint8_t *alloc_buf(size_t size)
{
  uint8_t *ret = (uint8_t *)malloc(size);
  if( ret == NULL )
    tc_log_error(MOD_NAME, "Out of memory: could not allocate buffer" );
  return ret;
}



void allc_buffers(void)
{
  int luma_buffsize = denoiser.frame.w * denoiser.frame.h;
  int chroma_buffsize = (denoiser.frame.w * denoiser.frame.h) / 4;

  /* now, the MC-functions really(!) do go beyond the vertical
   * frame limits so we need to make the buffers larger to avoid
   * bound-checking (memory vs. speed...)
   */

  luma_buffsize += 64*denoiser.frame.w;
  chroma_buffsize += 64*denoiser.frame.w;

#ifdef HAVE_FILTER_IO_BUF
  denoiser.frame.io[Yy] = alloc_buf (luma_buffsize);
  denoiser.frame.io[Cr] = alloc_buf (chroma_buffsize);
  denoiser.frame.io[Cb] = alloc_buf (chroma_buffsize);
#endif

  denoiser.frame.ref[Yy] = alloc_buf (luma_buffsize);
  denoiser.frame.ref[Cr] = alloc_buf (chroma_buffsize);
  denoiser.frame.ref[Cb] = alloc_buf (chroma_buffsize);

  denoiser.frame.avg[Yy] = alloc_buf (luma_buffsize);
  denoiser.frame.avg[Cr] = alloc_buf (chroma_buffsize);
  denoiser.frame.avg[Cb] = alloc_buf (chroma_buffsize);

  denoiser.frame.dif[Yy] = alloc_buf (luma_buffsize);
  denoiser.frame.dif[Cr] = alloc_buf (chroma_buffsize);
  denoiser.frame.dif[Cb] = alloc_buf (chroma_buffsize);

  denoiser.frame.dif2[Yy] = alloc_buf (luma_buffsize);
  denoiser.frame.dif2[Cr] = alloc_buf (chroma_buffsize);
  denoiser.frame.dif2[Cb] = alloc_buf (chroma_buffsize);

  denoiser.frame.avg2[Yy] = alloc_buf (luma_buffsize);
  denoiser.frame.avg2[Cr] = alloc_buf (chroma_buffsize);
  denoiser.frame.avg2[Cb] = alloc_buf (chroma_buffsize);

  denoiser.frame.tmp[Yy] = alloc_buf (luma_buffsize);
  denoiser.frame.tmp[Cr] = alloc_buf (chroma_buffsize);
  denoiser.frame.tmp[Cb] = alloc_buf (chroma_buffsize);

  denoiser.frame.sub2ref[Yy] = alloc_buf (luma_buffsize);
  denoiser.frame.sub2ref[Cr] = alloc_buf (chroma_buffsize);
  denoiser.frame.sub2ref[Cb] = alloc_buf (chroma_buffsize);

  denoiser.frame.sub2avg[Yy] = alloc_buf (luma_buffsize);
  denoiser.frame.sub2avg[Cr] = alloc_buf (chroma_buffsize);
  denoiser.frame.sub2avg[Cb] = alloc_buf (chroma_buffsize);

  denoiser.frame.sub4ref[Yy] = alloc_buf (luma_buffsize);
  denoiser.frame.sub4ref[Cr] = alloc_buf (chroma_buffsize);
  denoiser.frame.sub4ref[Cb] = alloc_buf (chroma_buffsize);

  denoiser.frame.sub4avg[Yy] = alloc_buf (luma_buffsize);
  denoiser.frame.sub4avg[Cr] = alloc_buf (chroma_buffsize);
  denoiser.frame.sub4avg[Cb] = alloc_buf (chroma_buffsize);
}



void free_buffers(void)
{
  int i;

  for (i = 0; i < 3; i++)
    {
#ifdef HAVE_FILTER_IO_BUF
      free (denoiser.frame.io[i]);
      denoiser.frame.io[i] = NULL;
#endif
      free (denoiser.frame.ref[i]);
      free (denoiser.frame.avg[i]);
      free (denoiser.frame.dif[i]);
      free (denoiser.frame.dif2[i]);
      free (denoiser.frame.avg2[i]);
      free (denoiser.frame.tmp[i]);
      free (denoiser.frame.sub2ref[i]);
      free (denoiser.frame.sub2avg[i]);
      free (denoiser.frame.sub4ref[i]);
      free (denoiser.frame.sub4avg[i]);

      denoiser.frame.ref[i] = NULL;
      denoiser.frame.avg[i] = NULL;
      denoiser.frame.dif[i] = NULL;
      denoiser.frame.dif2[i] = NULL;
      denoiser.frame.avg2[i] = NULL;
      denoiser.frame.tmp[i] = NULL;
      denoiser.frame.sub2ref[i] = NULL;
      denoiser.frame.sub2avg[i] = NULL;
      denoiser.frame.sub4ref[i] = NULL;
      denoiser.frame.sub4avg[i] = NULL;
    }
}

// ***

void print_settings(void)
{
    tc_log_info(MOD_NAME, " denoiser - Settings:\n");
    tc_log_info(MOD_NAME, " --------------------\n");
    tc_log_info(MOD_NAME, " \n");
    tc_log_info(MOD_NAME, " Mode             : %s\n",
    (denoiser.mode==0)? "Progressive frames" : (denoiser.mode==1)? "Interlaced frames": "PASS II only");
    tc_log_info(MOD_NAME, " Deinterlacer     : %s\n",(denoiser.deinterlace==0)? "Off":"On");
    tc_log_info(MOD_NAME, " Postprocessing   : %s\n",(denoiser.postprocess==0)? "Off":"On");
    tc_log_info(MOD_NAME, " Frame border     : x:%3i y:%3i w:%3i h:%3i\n",denoiser.border.x,denoiser.border.y,denoiser.border.w,denoiser.border.h);
    tc_log_info(MOD_NAME, " Search radius    : %3i\n",denoiser.radius);
    tc_log_info(MOD_NAME, " Filter delay     : %3i\n",denoiser.delay);
    tc_log_info(MOD_NAME, " Filter threshold : %3i\n",denoiser.threshold);
    tc_log_info(MOD_NAME, " Pass 2 threshold : %3i\n",denoiser.pp_threshold);
    tc_log_info(MOD_NAME, " Y - contrast     : %3i %%\n",denoiser.luma_contrast);
    tc_log_info(MOD_NAME, " Cr/Cb - contrast : %3i %%\n",denoiser.chroma_contrast);
    tc_log_info(MOD_NAME, " Sharpen          : %3i %%\n",denoiser.sharpen);
    tc_log_info(MOD_NAME, " --------------------\n");
    tc_log_info(MOD_NAME, " Run as pre filter: %s\n",(pre==0)? "Off":"On");
    tc_log_info(MOD_NAME, " block_threshold  : %d\n",denoiser.block_thres);
    tc_log_info(MOD_NAME, " scene_threshold  : %d%%\n",denoiser.scene_thres);
    tc_log_info(MOD_NAME, " SceneChange Reset: %s\n",(denoiser.do_reset==0)? "Off":"On");
    tc_log_info(MOD_NAME, " increment_cr     : %d\n",denoiser.increment_cr);
    tc_log_info(MOD_NAME, " increment_cb     : %d\n",denoiser.increment_cb);
    tc_log_info(MOD_NAME, " \n");

}

void turn_on_accels(void)
{
/* XXX: very weird effects, #undef'ed in global.h -- tibit */
#ifdef HAVE_ASM_MMX
  uint32_t CPU_CAP = tc_get_session()->acceleration; /* XXX ugly */

  if( (CPU_CAP & AC_MMXEXT)!=0 ||
      (CPU_CAP & AC_SSE   )!=0
    ) /* MMX+SSE */
  {
    calc_SAD    = &calc_SAD_mmxe;
    calc_SAD_uv = &calc_SAD_uv_mmxe;
    calc_SAD_half = &calc_SAD_half_mmxe;
    deinterlace = &deinterlace_mmx;
    if (filter_verbose)
	tc_log_info(MOD_NAME, "Using extended MMX SIMD optimisations.");
  }
  else
    if( (CPU_CAP & AC_MMX)!=0 ) /* MMX */
    {
      calc_SAD    = &calc_SAD_mmx;
      calc_SAD_uv = &calc_SAD_uv_mmx;
      calc_SAD_half = &calc_SAD_half_mmx;
      deinterlace = &deinterlace_mmx;
      if (filter_verbose)
	  tc_log_info(MOD_NAME, "Using MMX SIMD optimisations.");
    }
    else
#endif
    {
      calc_SAD    = &calc_SAD_noaccel;
      calc_SAD_uv = &calc_SAD_uv_noaccel;
      calc_SAD_half = &calc_SAD_half_noaccel;
      deinterlace = &deinterlace_noaccel;
      if (filter_verbose)
	  tc_log_info(MOD_NAME, "Sorry, no SIMD optimisations available.");
    }
}

void
display_help(void)
{
    tc_log_info(MOD_NAME, "\n\n"
"denoiser Usage:\n"
"===========================================================================\n"
"\n"
"threshold <0..255> denoiser threshold\n"
"                   accept any image-error up to +/- threshold for a single\n"
"                   pixel to be accepted as valid for the image. If the\n"
"                   absolute error is greater than this, exchange the pixel\n"
"                   with the according pixel of the reference image.\n"
"                   (default=%i)"
"\n"
"delay <1...255>    Average 'n' frames for a time-lowpassed pixel. Values\n"
"                   below 2 will lead to a good response to the reference\n"
"                   frame, while larger values will cut out more noise (and\n"
"                   as a drawback will lead to noticable artefacts on high\n"
"                   motion scenes.) Values above 8 are allowed but rather\n"
"                   useless. (default=%i)\n"
"\n"
"radius <8...24>    Limit the search radius to that value. Usually it will\n"
"                   not make sense to go higher than 16. Esp. for VCD sizes.\n"
"                   (default=%i)"
"\n"
"border <x>x<y>-<w>x<h> Set active image area. Every pixel outside will be set\n"
"                   to <16,128,128> (\"pure black\"). This can save a lot of bits\n"
"                   without even touching the image itself (eg. on 16:9 movies\n"
"                   on 4:3 (VCD and SVCD) (default=%ix%i-%ix%i)\n"
"\n"
"luma_contrast <0...255>    Set luminance contrast in percent. (default=%i)\n"
"\n"
"chroma_contrast <0...255>  Set chrominance contrast in percent. AKA \"Saturation\"\n"
"                           (default=%i)"
"\n"
"sharpen <0...255>  Set sharpness in percent. WARNING: do not set too high\n"
"                   as this will gain bit-noise. (default=%i)\n"
"\n"
"deinterlace <0..1> Force deinterlacing. By default denoise interlaced.\n"
"\n"
"mode <0..2>        [2]: Fast mode. Use only Pass II (bitnoise-reduction) for\n"
"                   low to very low noise material. (default off)\n"
"                   [1]: Interlaced material\n"
"                   [0]: Progressive material (default)\n"
"\n"
"pp_threshold <0...255>   Pass II threshold (same as -t).\n"
"                   WARNING: If set to values greater than 8 you *will* see\n"
"                   artefacts...(default=%i)\n"
"\n"
"postprocess <0..1> [0]: disable filter internal postprocessing\n"
"                   [1]: enable filter internal postprocessing (default)\n"
"\n"
"pre <0..1>         [0]: run as a post process filter (default)\n"
"                   [1]: run as a pre process filter (not recommended)\n"
"\n"
"do_reset <0..n>    [n]: reset the filter for n frames after a scene change\n"
"                   [0]: dont reset\n"
"                   (default=%i)\n"
"\n"
"block_thres <0..oo>   Every SAD value greater than this will be considered \"bad\" \n"
"                   (default=%i)\n"
"\n"
"scene_thres <0%%..100%%> Percentage of blocks where motion estimation should fail\n"
"                   before a scene is considered changed (default=%i%%)\n"
"\n"
"increment_cb <-128..127> Increment Cb with a constant (default=%d)\n"
"\n"
"increment_cr <-128..127> Increment Cr with a constant (default=%d)\n",
		denoiser.threshold,
		denoiser.delay,
		denoiser.radius,
		denoiser.border.x,
		denoiser.border.y,
		denoiser.border.w,
		denoiser.border.h,
		denoiser.luma_contrast,
		denoiser.chroma_contrast,
		denoiser.sharpen,
		denoiser.pp_threshold,
		denoiser.do_reset,
		denoiser.block_thres,
		denoiser.scene_thres,
		denoiser.increment_cr,
		denoiser.increment_cb
		);
}

/* vim: sw=4
 */
