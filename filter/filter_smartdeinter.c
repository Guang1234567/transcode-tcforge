/*
    filter_smartdeinter.c

    This file is part of transcode, a video stream processing tool

    Smart Deinterlacing Filter for VirtualDub -- performs deinterlacing only
    in moving picture areas, allowing full resolution in static areas.
    Copyright (C) 1999-2001 Donald A. Graft
    Miscellaneous suggestions and optimizations by Avery Lee.
    Useful suggestions by Hans Zimmer, Jim Casaburi, Ondrej Kavka,
	and Gunnar Thalin. Field-only differencing based on algorithm by
	Gunnar Thalin.

    modified 2002 by Tilmann Bitterberg for use with transcode

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    The author can be contacted at:
    Donald Graft
    neuron2@home.com.
    http://sauron.mordor.net/dgraft/
*/

#define MOD_NAME    "filter_smartdeinter.so"
#define MOD_VERSION "v2.7b (2003-02-01)"
#define MOD_CAP     "VirtualDub's smart deinterlacer"
#define MOD_AUTHOR  "Donald Graft, Tilmann Bitterberg"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include "libtcvideo/tcvideo.h"


static vob_t *vob=NULL;

/* vdub compat */
typedef unsigned int	Pixel;
typedef unsigned int	Pixel32;
typedef unsigned char	Pixel8;
typedef int		PixCoord;
typedef	int		PixDim;
typedef	int		PixOffset;


///////////////////////////////////////////////////////////////////////////

#define FRAME_ONLY 0
#define FIELD_ONLY 1
#define FRAME_AND_FIELD 2

typedef struct MyFilterData {
	int			*prevFrame;
	int			*saveFrame;
	Pixel32			*convertFrameIn;
	Pixel32			*convertFrameOut;
	unsigned char		*moving;
	unsigned char		*fmoving;
	int			srcPitch;
	int			dstPitch;
	int			motionOnly;
	int 			Blend;
	int 			threshold;
	int			scenethreshold;
	int			fieldShift;
	int			inswap;
	int			outswap;
	int			highq;
	int			diffmode;
	int			colordiff;
	int			noMotion;
	int			cubic;
	int			codec;
	TCVHandle		tcvhandle;
} MyFilterData;

static MyFilterData *mfd;

static void help_optstr(void)
{
   tc_log_info (MOD_NAME, "(%s) help\n"
"* Overview\n"
"    This filter provides a smart, motion-based deinterlacing\n"
"    capability. In static picture areas, interlacing artifacts do not\n"
"    appear, so data from both fields is used to provide full detail. In\n"
"    moving areas, deinterlacing is performed\n"
"\n"
"* Options\n"
"       'threshold' Motion Threshold (0-255) [15]\n"
"  'scenethreshold' Scene Change Threshold (0-255) [100]:\n"
"        'diffmode' Motion Detection (0=frame, 1=field, 2=both) [0] \n"
"       'colordiff' Compare color channels instead of luma (0=off, 1=on) [1]\n"
"      'motionOnly' Show motion areas only (0=off, 1=on) [0]\n"
"           'Blend' Blend instead of interpolate in motion areas (0=off, 1=on) [0]\n"
"           'cubic' Use cubic for interpolation (0=off, 1=on) [0]\n"
"      'fieldShift' Phase shift (0=off, 1=on) [0]\n"
"          'inswap' Field swap before phase shift (0=off, 1=on) [0]\n"
"         'outswap' Field swap after phase shift (0=off, 1=on) [0]\n"
"           'highq' Motion map denoising for field-only (0=off, 1=on) [0]\n"
"        'noMotion' Disable motion processing (0=off, 1=on) [0]\n"
		, MOD_CAP);
}

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_INIT) {

	unsigned int width, height;

	if((vob = tc_get_vob())==NULL) return(-1);


	mfd = tc_zalloc(sizeof(MyFilterData));

	if (!mfd) {
		tc_log_error(MOD_NAME, "No memory!");
		return (-1);
	}

	width  = vob->im_v_width;
	height = vob->im_v_height;

	/* default values */
	mfd->threshold      = 15;
	mfd->scenethreshold = 100;
	mfd->highq          = 0;
	mfd->diffmode       = FRAME_ONLY;
	mfd->colordiff      = 1;
	mfd->noMotion       = 0;
	mfd->cubic          = 0;
	mfd->codec          = vob->im_v_codec;

	if (options != NULL) {

	  if(verbose) tc_log_info(MOD_NAME, "options=%s", options);

	  optstr_get (options, "motionOnly",     "%d",  &mfd->motionOnly       );
	  optstr_get (options, "Blend",          "%d",  &mfd->Blend            );
	  optstr_get (options, "threshold",      "%d",  &mfd->threshold        );
	  optstr_get (options, "scenethreshold", "%d",  &mfd->scenethreshold   );
	  optstr_get (options, "fieldShift",     "%d",  &mfd->fieldShift       );
	  optstr_get (options, "inswap",         "%d",  &mfd->inswap           );
	  optstr_get (options, "outswap",        "%d",  &mfd->outswap          );
	  optstr_get (options, "noMotion",       "%d",  &mfd->noMotion         );
	  optstr_get (options, "highq",          "%d",  &mfd->highq            );
	  optstr_get (options, "diffmode",       "%d",  &mfd->diffmode         );
	  optstr_get (options, "colordiff",      "%d",  &mfd->colordiff        );
	  optstr_get (options, "cubic",          "%d",  &mfd->cubic            );

	  if (optstr_lookup (options, "help") != NULL) {
		  help_optstr();
	  }
	}

	if (verbose > 1) {

	  tc_log_info (MOD_NAME, " Smart Deinterlacer Filter Settings (%dx%d):", width, height);
	  tc_log_info (MOD_NAME, "        motionOnly = %d", mfd->motionOnly);
	  tc_log_info (MOD_NAME, "             Blend = %d", mfd->Blend);
	  tc_log_info (MOD_NAME, "         threshold = %d", mfd->threshold);
	  tc_log_info (MOD_NAME, "    scenethreshold = %d", mfd->scenethreshold);
	  tc_log_info (MOD_NAME, "        fieldShift = %d", mfd->fieldShift);
	  tc_log_info (MOD_NAME, "            inswap = %d", mfd->inswap);
	  tc_log_info (MOD_NAME, "           outswap = %d", mfd->outswap);
	  tc_log_info (MOD_NAME, "          noMotion = %d", mfd->noMotion);
	  tc_log_info (MOD_NAME, "             highq = %d", mfd->highq);
	  tc_log_info (MOD_NAME, "          diffmode = %d", mfd->diffmode);
	  tc_log_info (MOD_NAME, "         colordiff = %d", mfd->colordiff);
	  tc_log_info (MOD_NAME, "             cubic = %d", mfd->cubic);
	}

	/* fetch memory */

	mfd->convertFrameIn = tc_zalloc (width * height * sizeof(Pixel32));
	mfd->convertFrameOut = tc_zalloc (width * height * sizeof(Pixel32));

	if (mfd->diffmode == FRAME_ONLY || mfd->diffmode == FRAME_AND_FIELD)
	{
		mfd->prevFrame = tc_zalloc (width*height*sizeof(int));
	}

	if (mfd->fieldShift ||
		(mfd->inswap && !mfd->outswap) || (!mfd->inswap && mfd->outswap))
	{
		mfd->saveFrame = tc_malloc (width*height*sizeof(int));
	}

	if (!mfd->noMotion)
	{
		mfd->moving = tc_zalloc (sizeof(unsigned char)*width*height);
	}

	if (mfd->highq)
	{
		mfd->fmoving = tc_malloc (sizeof(unsigned char)*width*height);
	}

	mfd->tcvhandle = tcv_init();

	// filter init ok.
	if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

	return 0;

  } /* TC_FILTER_INIT */


  if(ptr->tag & TC_FILTER_GET_CONFIG) {
      char buf[255];
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYE", "1");

      tc_snprintf (buf, sizeof(buf), "%d", mfd->motionOnly);
      optstr_param (options, "motionOnly", "Show motion areas only" ,"%d", buf, "0", "1");
      tc_snprintf (buf, sizeof(buf), "%d", mfd->Blend);
      optstr_param (options, "Blend", "Blend instead of interpolate in motion areas", "%d", buf, "0", "1" );
      tc_snprintf (buf, sizeof(buf), "%d", mfd->threshold);
      optstr_param (options, "threshold", "Motion Threshold", "%d", buf, "0", "255" );
      tc_snprintf (buf, sizeof(buf), "%d", mfd->scenethreshold);
      optstr_param (options, "scenethreshold", "Scene Change Threshold", "%d", buf, "0", "255" );
      tc_snprintf (buf, sizeof(buf), "%d", mfd->fieldShift);
      optstr_param (options, "fieldShift", "Phase shift", "%d", buf, "0", "1" );
      tc_snprintf (buf, sizeof(buf), "%d", mfd->inswap);
      optstr_param (options, "inswap", "Field swap before phase shift", "%d", buf, "0", "1" );
      tc_snprintf (buf, sizeof(buf), "%d", mfd->outswap);
      optstr_param (options, "outswap", "Field swap after phase shift", "%d", buf, "0", "1" );
      tc_snprintf (buf, sizeof(buf), "%d", mfd->noMotion);
      optstr_param (options, "noMotion", "Disable motion processing", "%d", buf, "0", "1" );
      tc_snprintf (buf, sizeof(buf), "%d", mfd->highq);
      optstr_param (options, "highq", "Motion map denoising for field-only", "%d", buf, "0", "1" );
      tc_snprintf (buf, sizeof(buf), "%d", mfd->diffmode);
      optstr_param (options, "diffmode", "Motion Detection (0=frame, 1=field, 2=both)", "%d", buf, "0", "2" );
      tc_snprintf (buf, sizeof(buf), "%d", mfd->colordiff);
      optstr_param (options, "colordiff", "Compare color channels instead of luma", "%d", buf, "0", "1" );
      tc_snprintf (buf, sizeof(buf), "%d", mfd->cubic);
      optstr_param (options, "cubic", "Use cubic for interpolation", "%d", buf, "0", "1" );

      return (0);
  }

  if(ptr->tag & TC_FILTER_CLOSE) {

	if (!mfd)
		return 0;

	if (mfd->diffmode == FRAME_ONLY || mfd->diffmode == FRAME_AND_FIELD)
	{
		if (mfd->prevFrame)
			free(mfd->prevFrame);
		mfd->prevFrame = NULL;
	}

	if (mfd->fieldShift ||
		(mfd->inswap && !mfd->outswap) || (!mfd->inswap && mfd->outswap))
	{
		if (mfd->saveFrame)
			free(mfd->saveFrame);
		mfd->saveFrame = NULL;
	}

	if (!mfd->noMotion)
	{
		if (mfd->moving)
			free(mfd->moving);
		mfd->moving = NULL;
	}

	if (mfd->highq)
	{
		if (mfd->fmoving)
			free(mfd->fmoving);
		mfd->fmoving = NULL;
	}

	if (mfd->convertFrameIn) {
		free (mfd->convertFrameIn);
		mfd->convertFrameIn = NULL;
	}

	if (mfd->convertFrameOut) {
		free (mfd->convertFrameOut);
		mfd->convertFrameOut = NULL;
	}

	tcv_free(mfd->tcvhandle);

	if (mfd)
		free(mfd);

	return 0;

  } /* TC_FILTER_CLOSE */

///////////////////////////////////////////////////////////////////////////

      if(ptr->tag & TC_PRE_M_PROCESS && ptr->tag & TC_VIDEO && !(ptr->attributes & TC_FRAME_IS_SKIPPED)) {

	const int		srcpitch = ptr->v_width*sizeof(Pixel32);
	const int		srcpitchtimes2 = 2 * srcpitch;
	const int		dstpitch = ptr->v_width*sizeof(Pixel32);
	const int		dstpitchtimes2 = 2 * dstpitch;

	const PixDim		w = ptr->v_width;
	const int		wminus1 = w - 1;
	const int		wtimes2 = w * 2;
	const int		wtimes4 = w * 4;

	const PixDim		h = ptr->v_height;
	const int		hminus1 = h - 1;
	const int		hminus3 = h - 3;
	const int		hover2 = h / 2;

	Pixel32			*src, *dst, *srcminus, *srcplus, *srcminusminus=NULL, *srcplusplus=NULL;
	unsigned char		*moving, *movingminus, *movingplus;
	unsigned char		*fmoving;
	int			*saved=NULL, *sv;
	Pixel32 		*src1=NULL, *src2=NULL, *s1, *s2;
	Pixel32 		*dst1=NULL, *dst2=NULL, *d1, *d2;
	int			*prev;
	int			scenechange;
	long			count;
	int			x, y;
	long			prevValue, nextValue, luma=0, lumap, luman;
	Pixel32			p0, p1, p2;
	long			r, g, b, rp, gp, bp, rn, gn, bn, T;
	long			rpp, gpp, bpp, rnn, gnn, bnn, R, G, B;
	unsigned char		frMotion, fiMotion;
	int			copyback;
	int			cubic = mfd->cubic;

	Pixel32 * dst_buf;
	Pixel32 * src_buf;

	tcv_convert(mfd->tcvhandle, ptr->video_buf,
		    (uint8_t *)mfd->convertFrameIn,
		    ptr->v_width, ptr->v_height,
		    mfd->codec==TC_CODEC_YUV420P ? IMG_YUV_DEFAULT : IMG_RGB24,
		    ac_endian()==AC_LITTLE_ENDIAN ? IMG_BGRA32 : IMG_ARGB32);

	src_buf = mfd->convertFrameIn;
	dst_buf = mfd->convertFrameOut;

	/* If we are performing Advanced Processing... */
	if (mfd->inswap || mfd->outswap || mfd->fieldShift)
	{
		/* Advanced Processing is used typically to clean up PAL video
		   which has erroneously been digitized with the field phase off by
		   one field. The result is that the frames look interlaced,
		   but really if we can phase shift by one field, we'll get back
		   the original progressive frames. Also supported are field swaps
		   before and/or after the phase shift to accommodate different
		   capture cards and telecining methods, as explained in the
		   help file. Finally, the user can optionally disable full
		   motion processing after this processing. */
		copyback = 1;
		if (!mfd->fieldShift)
		{
			/* No phase shift enabled, but we have swap(s) enabled. */
			if (mfd->inswap && mfd->outswap)
			{
				if (mfd->noMotion)
				{
					/* Swapping twice is a null operation. */
					src1 = src_buf;
					dst1 = dst_buf;
					for (y = 0; y < h; y++)
					{
						ac_memcpy(dst1, src1, wtimes4);
						src1 = (Pixel *)((char *)src1 + srcpitch);
						dst1 = (Pixel *)((char *)dst1 + dstpitch);
					}
					goto filter_done;
				}
				else
				{
					copyback = 0;
				}
			}
			else
			{
				/* Swap fields. */
				src1 = (Pixel32 *)((char *)src_buf + srcpitch);
				saved = mfd->saveFrame + w;
				for (y = 0; y < hover2; y++)
				{
					ac_memcpy(saved, src1, wtimes4);
					src1 = (Pixel *)((char *)src1 + srcpitchtimes2);
					saved += wtimes2;
				}
				src1 = src_buf;
				dst1 = (Pixel32 *)((char *)dst_buf+ dstpitch);
				for (y = 0; y < hover2; y++)
				{
					ac_memcpy(dst1, src1, wtimes4);
					src1 = (Pixel *)((char *)src1 + srcpitchtimes2);
					dst1 = (Pixel *)((char *)dst1 + dstpitchtimes2);
				}
				dst1 = dst_buf;
				saved = mfd->saveFrame + w;
				for (y = 0; y < hover2; y++)
				{
					ac_memcpy(dst1, saved, wtimes4);
					dst1 = (Pixel *)((char *)dst1 + dstpitchtimes2);
					saved += wtimes2;
				}
			}
		}
		/* If we reach here, then phase shift has been enabled. */
		else
		{
			switch (mfd->inswap | (mfd->outswap << 1))
			{
			case 0:
				/* No inswap, no outswap. */
				src1 = src_buf;
				src2 = (Pixel32 *)((char *)src1 + srcpitch);
				dst1 = (Pixel32 *)((char *)dst_buf+ dstpitch);
				dst2 = dst_buf;
				saved = mfd->saveFrame + w;
				break;
			case 1:
				/* Inswap, no outswap. */
				src1 = (Pixel32 *)((char *)src_buf + srcpitch);
				src2 = src_buf;
				dst1 = (Pixel32 *)((char *)dst_buf+ dstpitch);
				dst2 = dst_buf;
				saved = mfd->saveFrame;
				break;
			case 2:
				/* No inswap, outswap. */
				src1 = src_buf;
				src2 = (Pixel32 *)((char *)src_buf + srcpitch);
				dst1 = dst_buf;
				dst2 = (Pixel32 *)((char *)dst_buf + dstpitch);
				saved = mfd->saveFrame + w;
				break;
			case 3:
				/* Inswap, outswap. */
				src1 = (Pixel32 *)((char *)src_buf + srcpitch);
				src2 = src_buf;
				dst1 = dst_buf;
				dst2 = (Pixel32 *)((char *)dst_buf + dstpitch);
				saved = mfd->saveFrame;
				break;
			}

			s1 = src1;
			d1 = dst1;
			for (y = 0; y < hover2; y++)
			{
				ac_memcpy(d1, s1, wtimes4);
				s1 = (Pixel *)((char *)s1 + srcpitchtimes2);
				d1 = (Pixel *)((char *)d1 + dstpitchtimes2);
			}

			/* If this is not the first frame, copy the buffered field
			   of the last frame to the output. This creates a correct progressive
			   output frame. If this is the first frame, a buffered field is not
			   available, so interpolate the field from the current field. */
			if (ptr->id <= 1 )
			{
				s1 = src1;
				d2 = dst2;
				for (y = 0; y < hover2; y++)
				{
					ac_memcpy(d2, s1, wtimes4);
					s1 = (Pixel *)((char *)s1 + srcpitchtimes2);
					d2 = (Pixel *)((char *)d2 + dstpitchtimes2);
				}
			}
			else
			{
				d2 = dst2;
				sv = saved;
				for (y = 0; y < hover2; y++)
				{
					ac_memcpy(d2, sv, wtimes4);
					sv += wtimes2;
					d2 = (Pixel *)((char *)d2 + dstpitchtimes2);
				}
			}
			/* Finally, save the unused field of the current frame in the buffer.
			   It will be used to create the next frame. */
			s2 = src2;
			sv = saved;
			for (y = 0; y < hover2; y++)
			{
				ac_memcpy(sv, s2, wtimes4);
				sv += wtimes2;
				s2 = (Pixel *)((char *)s2 + srcpitchtimes2);
			}
		}
		if (mfd->noMotion) goto filter_done;

		if (copyback)
		{
			/* We're going to do motion processing also, so copy
			   the result back into the src bitmap. */
			src1 = dst_buf;
			dst1 = src_buf;
			for (y = 0; y < h; y++)
			{
				ac_memcpy(dst1, src1, wtimes4);
				src1 = (Pixel *)((char *)src1 + srcpitch);
				dst1 = (Pixel *)((char *)dst1 + dstpitch);
			}
		}
	}
	else if (mfd->noMotion)
	{
		/* Well, I suppose somebody might select no advanced processing options
		   but tick disable motion processing. This covers that. */
		src1 = src_buf;
		dst1 = dst_buf;
		for (y = 0; y < h; y++)
		{
			ac_memcpy(dst1, src1, srcpitch);
			src1 = (Pixel *)((char *)src1 + srcpitch);
			dst1 = (Pixel *)((char *)dst1 + dstpitch);
		}
		goto filter_done;
	}

	/* End advanced processing mode code. Now do full motion-adaptive deinterlacing. */

	/* Not much deinterlacing to do if there aren't at least 2 lines. */
	if (h < 2) goto filter_done;

	count = 0;
	if (mfd->diffmode == FRAME_ONLY || mfd->diffmode == FRAME_AND_FIELD)
	{
		/* Skip first and last lines, they'll get a free ride. */
		src = (Pixel *)((char *)src_buf + srcpitch);
		srcminus = (Pixel *)((char *)src - srcpitch);
		prev = mfd->prevFrame + w;
		moving = mfd->moving + w;
		for (y = 1; y < hminus1; y++)
		{
			x = 0;
			do
			{
				// First check frame motion.
				// Set the moving flag if the diff exceeds the configured
				// threshold.
				moving[x] = 0;
				frMotion = 0;
				prevValue = *prev;
				if (!mfd->colordiff)
				{
					r = (src[x] >> 16) & 0xff;
					g = (src[x] >> 8) & 0xff;
					b = src[x] & 0xff;
					luma = (76 * r + 30 * b + 150 * g) >> 8;
					if (abs(luma - prevValue) > mfd->threshold) frMotion = 1;
				}
				else
				{
					b = src[x] & 0xff;
					bp = prevValue & 0xff;
					if (abs(b - bp) > mfd->threshold) frMotion = 1;
					else
					{
						r = (src[x] >>16) & 0xff;
						rp = (prevValue >> 16) & 0xff;
						if (abs(r - rp) > mfd->threshold) frMotion = 1;
						else
						{
							g = (src[x] >> 8) & 0xff;
							gp = (prevValue >> 8) & 0xff;
							if (abs(g - gp) > mfd->threshold) frMotion = 1;
						}
					}
				}

				// Now check field motion if applicable.
				if (mfd->diffmode == FRAME_ONLY) moving[x] = frMotion;
				else
				{
					fiMotion = 0;
					if (y & 1)
						prevValue = srcminus[x];
					else
						prevValue = *(prev + w);
					if (!mfd->colordiff)
					{
						r = (src[x] >> 16) & 0xff;
						g = (src[x] >> 8) & 0xff;
						b = src[x] & 0xff;
						luma = (76 * r + 30 * b + 150 * g) >> 8;
						if (abs(luma - prevValue) > mfd->threshold) fiMotion = 1;
					}
					else
					{
						b = src[x] & 0xff;
						bp = prevValue & 0xff;
						if (abs(b - bp) > mfd->threshold) fiMotion = 1;
						else
						{
							r = (src[x] >> 16) & 0xff;
							rp = (prevValue >> 16) & 0xff;
							if (abs(r - rp) > mfd->threshold) fiMotion = 1;
							else
							{
								g = (src[x] >> 8) & 0xff;
								gp = (prevValue >> 8) & 0xff;
								if (abs(g - gp) > mfd->threshold) fiMotion = 1;
							}
						}
					}
					moving[x] = (fiMotion && frMotion);
				}
				if (!mfd->colordiff)
					*prev++ = luma;
				else
					*prev++ = src[x];
				/* Keep a count of the number of moving pixels for the
				   scene change detection. */
				if (moving[x]) count++;
			} while(++x < w);
			src = (Pixel *)((char *)src + srcpitch);
			srcminus = (Pixel *)((char *)srcminus + srcpitch);
			moving += w;
		}

		/* Determine whether a scene change has occurred. */
		if ((100L * count) / (h * w) >= mfd->scenethreshold) scenechange = 1;
		else scenechange = 0;

		/*
		tc_log_msg(MOD_NAME, "Frame (%04d) count (%8ld) sc (%d) calc (%02ld)",
				ptr->id, count, scenechange, (100 * count) / (h * w));
				*/

		/* Perform a denoising of the motion map if enabled. */
		if (!scenechange && mfd->highq)
		{
			int xlo, xhi, ylo, yhi;
			int u, v;
			int N = 5;
			int Nover2 = N/2;
			int sum;
			unsigned char *m;

			// Erode.
			fmoving = mfd->fmoving;
			for (y = 0; y < h; y++)
			{
				for (x = 0; x < w; x++)
				{
					if (!((mfd->moving + y * w)[x]))
					{
						fmoving[x] = 0;
						continue;
					}
					xlo = x - Nover2; if (xlo < 0) xlo = 0;
					xhi = x + Nover2; if (xhi >= w) xhi = wminus1;
					ylo = y - Nover2; if (ylo < 0) ylo = 0;
					yhi = y + Nover2; if (yhi >= h) yhi = hminus1;
					m = mfd->moving + ylo * w;
					sum = 0;
					for (u = ylo; u <= yhi; u++)
					{
						for (v = xlo; v <= xhi; v++)
						{
							sum += m[v];
						}
						m += w;
					}
					if (sum > 9)
						fmoving[x] = 1;
					else
						fmoving[x] = 0;
				}
				fmoving += w;
			}
			// Dilate.
			N = 5;
			Nover2 = N/2;
			moving = mfd->moving;
			for (y = 0; y < h; y++)
			{
				for (x = 0; x < w; x++)
				{
					if (!((mfd->fmoving + y * w)[x]))
					{
						moving[x] = 0;
						continue;
					}
					xlo = x - Nover2; if (xlo < 0) xlo = 0;
					xhi = x + Nover2; if (xhi >= w) xhi = wminus1;
					ylo = y - Nover2; if (ylo < 0) ylo = 0;
					yhi = y + Nover2; if (yhi >= h) yhi = hminus1;
					m = mfd->moving + ylo * w;
					for (u = ylo; u <= yhi; u++)
					{
						for (v = xlo; v <= xhi; v++)
						{
							m[v] = 1;
						}
						m += w;
					}
				}
				moving += w;
			}
		}
	}
	else
	{
		/* Field differencing only mode. */
		T = mfd->threshold * mfd->threshold;
		src = (Pixel *)((char *)src_buf + srcpitch);
		srcminus = (Pixel *)((char *)src - srcpitch);
		srcplus = (Pixel *)((char *)src + srcpitch);
		moving = mfd->moving + w;
		for (y = 1; y < hminus1; y++)
		{
			x = 0;
			do
			{
				// Set the moving flag if the diff exceeds the configured
				// threshold.
				moving[x] = 0;
				if (y & 1)
				{
					// Now check field motion.
					fiMotion = 0;
					nextValue = srcplus[x];
					prevValue = srcminus[x];
					if (!mfd->colordiff)
					{
						r = (src[x] >> 16) & 0xff;
						rp = (prevValue >> 16) & 0xff;
						rn = (nextValue >> 16) & 0xff;
						g = (src[x] >> 8) & 0xff;
						gp = (prevValue >> 8) & 0xff;
						gn = (nextValue >> 8) & 0xff;
						b = src[x] & 0xff;
						bp = prevValue & 0xff;
						bn = nextValue & 0xff;
						luma = (76 * r + 30 * b + 150 * g) >> 8;
						lumap = (76 * rp + 30 * bp + 150 * gp) >> 8;
						luman = (76 * rn + 30 * bn + 150 * gn) >> 8;
						if ((lumap - luma) * (luman - luma) > T)
							moving[x] = 1;
					}
					else
					{
						b = src[x] & 0xff;
						bp = prevValue & 0xff;
						bn = nextValue & 0xff;
						if ((bp - b) * (bn - b) > T) moving[x] = 1;
						else
						{
							r = (src[x] >> 16) & 0xff;
							rp = (prevValue >> 16) & 0xff;
							rn = (nextValue >> 16) & 0xff;
							if ((rp - r) * (rn - r) > T) moving[x] = 1;
							else
							{
								g = (src[x] >> 8) & 0xff;
								gp = (prevValue >> 8) & 0xff;
								gn = (nextValue >> 8) & 0xff;
								if ((gp - g) * (gn - g) > T) moving[x] = 1;
							}
						}
					}
				}
				/* Keep a count of the number of moving pixels for the
				   scene change detection. */
				if (moving[x]) count++;
			} while(++x < w);
			src = (Pixel *)((char *)src + srcpitch);
			srcminus = (Pixel *)((char *)srcminus + srcpitch);
			srcplus = (Pixel *)((char *)srcplus + srcpitch);
			moving += w;
		}

		/* Determine whether a scene change has occurred. */
		if ((100L * count) / (h * w) >= mfd->scenethreshold) scenechange = 1;
		else scenechange = 0;

		/* Perform a denoising of the motion map if enabled. */
		if (!scenechange && mfd->highq)
		{
			int xlo, xhi, ylo, yhi;
			int u, v;
			int N = 5;
			int Nover2 = N/2;
			int sum;
			unsigned char *m;

			// Erode.
			fmoving = mfd->fmoving;
			for (y = 0; y < h; y++)
			{
				for (x = 0; x < w; x++)
				{
					if (!((mfd->moving + y * w)[x]))
					{
						fmoving[x] = 0;
						continue;
					}
					xlo = x - Nover2; if (xlo < 0) xlo = 0;
					xhi = x + Nover2; if (xhi >= w) xhi = wminus1;
					ylo = y - Nover2; if (ylo < 0) ylo = 0;
					yhi = y + Nover2; if (yhi >= h) yhi = hminus1;
					m = mfd->moving + ylo * w;
					sum = 0;
					for (u = ylo; u <= yhi; u++)
					{
						for (v = xlo; v <= xhi; v++)
						{
							sum += m[v];
						}
						m += w;
					}
					if (sum > 9)
						fmoving[x] = 1;
					else
						fmoving[x] = 0;
				}
				fmoving += w;
			}

			// Dilate.
			N = 5;
			Nover2 = N/2;
			moving = mfd->moving;
			for (y = 0; y < h; y++)
			{
				for (x = 0; x < w; x++)
				{
					if (!((mfd->fmoving + y * w)[x]))
					{
						moving[x] = 0;
						continue;
					}
					xlo = x - Nover2; if (xlo < 0) xlo = 0;
					xhi = x + Nover2; if (xhi >= w) xhi = wminus1;
					ylo = y - Nover2; if (ylo < 0) ylo = 0;
					yhi = y + Nover2; if (yhi >= h) yhi = hminus1;
					m = mfd->moving + ylo * w;
					for (u = ylo; u <= yhi; u++)
					{
						for (v = xlo; v <= xhi; v++)
						{
							m[v] = 1;
						}
						m += w;
					}
				}
				moving += w;
			}
		}
	}

	// Render.
    // The first line gets a free ride.
	src = src_buf;
	dst = dst_buf;
	ac_memcpy(dst, src, wtimes4);
	src = (Pixel *)((char *)src_buf + srcpitch);
	srcminus = (Pixel *)((char *)src - srcpitch);
	srcplus = (Pixel *)((char *)src + srcpitch);
	if (cubic)
	{
		srcminusminus = (Pixel *)((char *)src - 3 * srcpitch);
		srcplusplus = (Pixel *)((char *)src + 3 * srcpitch);
	}
	dst = (Pixel *)((char *)dst_buf + dstpitch);
	moving = mfd->moving + w;
	movingminus = moving - w;
	movingplus = moving + w;
	for (y = 1; y < hminus1; y++)
	{
		if (mfd->motionOnly)
		{
			if (mfd->Blend)
			{
				x = 0;
				do {
					if (!(movingminus[x] | moving[x] | movingplus[x]) && !scenechange)
						dst[x] = 0x7f7f7f;
					else
					{
						/* Blend fields. */
						p0 = src[x];
						p0 &= 0x00fefefe;

						p1 = srcminus[x];
						p1 &= 0x00fcfcfc;

						p2 = srcplus[x];
						p2 &= 0x00fcfcfc;

						dst[x] = (p0>>1) + (p1>>2) + (p2>>2);
					}
				} while(++x < w);
			}
			else
			{
				x = 0;
				do {
					if (!(movingminus[x] | moving[x] | movingplus[x]) && !scenechange)
						dst[x] = 0x7f7f7f;
					else if (y & 1)
					{
						if (cubic && (y > 2) && (y < hminus3))
						{
							rpp = (srcminusminus[x] >> 16) & 0xff;
							rp =  (srcminus[x] >> 16) & 0xff;
							rn =  (srcplus[x] >> 16) & 0xff;
							rnn = (srcplusplus[x] >>16) & 0xff;
							gpp = (srcminusminus[x] >> 8) & 0xff;
							gp =  (srcminus[x] >> 8) & 0xff;
							gn =  (srcplus[x] >>8) & 0xff;
							gnn = (srcplusplus[x] >> 8) & 0xff;
							bpp = (srcminusminus[x]) & 0xff;
							bp =  (srcminus[x]) & 0xff;
							bn =  (srcplus[x]) & 0xff;
							bnn = (srcplusplus[x]) & 0xff;
							R = (5 * (rp + rn) - (rpp + rnn)) >> 3;
							if (R > 255) R = 255;
							else if (R < 0) R = 0;
							G = (5 * (gp + gn) - (gpp + gnn)) >> 3;
							if (G > 255) G = 255;
							else if (G < 0) G = 0;
							B = (5 * (bp + bn) - (bpp + bnn)) >> 3;
							if (B > 255) B = 255;
							else if (B < 0) B = 0;
							dst[x] = (R << 16) | (G << 8) | B;
						}
						else
						{
							p1 = srcminus[x];
							p1 &= 0x00fefefe;

							p2 = srcplus[x];
							p2 &= 0x00fefefe;
							dst[x] = (p1>>1) + (p2>>1);
						}
					}
					else
						dst[x] = src[x];
				} while(++x < w);
			}
		}
		else  /* Not motion only */
		{
			if (mfd->Blend)
			{
				x = 0;
				do {
					if (!(movingminus[x] | moving[x] | movingplus[x]) && !scenechange)
						dst[x] = src[x];
					else
					{
						/* Blend fields. */
						p0 = src[x];
						p0 &= 0x00fefefe;

						p1 = srcminus[x];
						p1 &= 0x00fcfcfc;

						p2 = srcplus[x];
						p2 &= 0x00fcfcfc;

						dst[x] = (p0>>1) + (p1>>2) + (p2>>2);
					}
				} while(++x < w);
			}
			else
			{
				// Doing line interpolate. Thus, even lines are going through
				// for moving and non-moving mode. Odd line pixels will be subject
				// to the motion test.
				if (y&1)
				{
					x = 0;
					do {
						if (!(movingminus[x] | moving[x] | movingplus[x]) && !scenechange)
							dst[x] = src[x];
						else
						{
							if (cubic && (y > 2) && (y < hminus3))
							{
								rpp = (srcminusminus[x] >> 16) & 0xff;
								rp =  (srcminus[x] >> 16) & 0xff;
								rn =  (srcplus[x] >> 16) & 0xff;
								rnn = (srcplusplus[x] >>16) & 0xff;
								gpp = (srcminusminus[x] >> 8) & 0xff;
								gp =  (srcminus[x] >> 8) & 0xff;
								gn =  (srcplus[x] >>8) & 0xff;
								gnn = (srcplusplus[x] >> 8) & 0xff;
								bpp = (srcminusminus[x]) & 0xff;
								bp =  (srcminus[x]) & 0xff;
								bn =  (srcplus[x]) & 0xff;
								bnn = (srcplusplus[x]) & 0xff;
								R = (5 * (rp + rn) - (rpp + rnn)) >> 3;
								if (R > 255) R = 255;
								else if (R < 0) R = 0;
								G = (5 * (gp + gn) - (gpp + gnn)) >> 3;
								if (G > 255) G = 255;
								else if (G < 0) G = 0;
								B = (5 * (bp + bn) - (bpp + bnn)) >> 3;
								if (B > 255) B = 255;
								else if (B < 0) B = 0;
								dst[x] = (R << 16) | (G << 8) | B;
							}
							else
							{
								p1 = srcminus[x];
								p1 &= 0x00fefefe;

								p2 = srcplus[x];
								p2 &= 0x00fefefe;

								dst[x] = (p1>>1) + (p2>>1);
							}
						}
					} while(++x < w);
				}
				else
				{
					// Even line; pass it through.
					ac_memcpy(dst, src, wtimes4);
				}
			}
		}
		src = (Pixel *)((char *)src + srcpitch);
		srcminus = (Pixel *)((char *)srcminus + srcpitch);
		srcplus = (Pixel *)((char *)srcplus + srcpitch);
		if (cubic)
		{
			srcminusminus = (Pixel *)((char *)srcminusminus + srcpitch);
			srcplusplus = (Pixel *)((char *)srcplusplus + srcpitch);
		}
		dst = (Pixel *)((char *)dst + dstpitch);
		moving += w;
		movingminus += w;
		movingplus += w;
	}

	// The last line gets a free ride.
	ac_memcpy(dst, src, wtimes4);

filter_done:
	tcv_convert(mfd->tcvhandle, (uint8_t *)mfd->convertFrameOut,
		    ptr->video_buf, ptr->v_width, ptr->v_height,
		    ac_endian()==AC_LITTLE_ENDIAN ? IMG_BGRA32 : IMG_ARGB32,
		    mfd->codec==TC_CODEC_YUV420P ? IMG_YUV_DEFAULT : IMG_RGB24);

	return 0;
  }
  return 0;
}

