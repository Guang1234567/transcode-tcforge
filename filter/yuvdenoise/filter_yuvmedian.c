/*
 *    Copyright (C) 2001 Mike Bernson <mike@mlb.org>
 *      Modified for use in transcode by
 *              Tilmann Bitterberg <transcode@tibit.org>
 *
 *    This file is part of transcode, a video stream processing tool
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *    Filter Based on code from Jim Cassburi filter: 2dclean
 *
 *    This filter look around the current point for a radius and averages
 *    this values that fall inside a threshold.
 */
#define MOD_NAME    "filter_yuvmedian.so"
#define MOD_VERSION "v0.1.0 (2003-01-24)"
#define MOD_CAP     "mjpegs YUV median filter"
#define MOD_AUTHOR  "Mike Bernson, Tilmann Bitterberg"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include <stdint.h>

#include "mjpeg_types.h"

//extern int verbose = 1;
static uint8_t	*input_frame[3];
static uint8_t	*output_frame[3];

static void	filter(int width, int height, uint8_t *input[], uint8_t * const output[]);
static void	filter_buffer(int width, int height, int stride, int radius, int threshold, uint8_t * const input, uint8_t * const output);

static int	threshold_luma = 2;
static int	threshold_chroma = 2;

static int	radius_luma = 2;
static int	radius_chroma = 2;
static int      interlace = 0;
static int	avg_replace[1024];
static int	ovr_replace = 0;
static int	chg_replace = 0;

static int      pre =1;

static void Usage(void)
{
    tc_log_info(MOD_NAME, "(%s) help"
"* Options\n"
"           'radius' Radius for median (luma)   [2]\n"
"        'threshold' Trigger threshold (luma)   [2]\n"
"    'radius_chroma' Radius for median (chroma) [2]\n"
" 'threshold_chroma' Trigger threshold (chroma) [2]\n"
"              'pre' Run as a PRE filter        [1]\n"
"        'interlace' Treat input as interlaced  [0]\n"
"             'help' show this help\n"
		, MOD_CAP);
}


int tc_filter(frame_list_t *ptr_, char *options)
{
	vframe_list_t *ptr = (vframe_list_t *)ptr_;
	int	i;
	static int	avg = 0;
	static int frame_count;
	static int horz, vert;

	static vob_t *vob=NULL;

	if(ptr->tag & TC_AUDIO)
	    return 0;

	if (ptr->tag & TC_FILTER_GET_CONFIG && options) {
	    char buf[255];

	    optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VYOE", "1");

	    tc_snprintf (buf, sizeof(buf), "%d", radius_luma);
	    optstr_param (options, "radius_luma",    "Radius for median (luma)", "%d", buf, "1", "24");

	    tc_snprintf (buf, sizeof(buf), "%d", radius_chroma);
	    optstr_param (options, "radius_chroma",  "Radius for median (chroma)", "%d", buf, "1", "24");

	    tc_snprintf (buf, sizeof(buf), "%d", threshold_luma);
	    optstr_param (options, "threshold_luma",  "Trigger threshold (luma)", "%d", buf, "1", "32");

	    tc_snprintf (buf, sizeof(buf), "%d", threshold_chroma);
	    optstr_param (options, "threshold_chroma",  "Trigger threshold (chroma)", "%d", buf, "1", "32");

	    tc_snprintf (buf, sizeof(buf), "%d", interlace);
	    optstr_param (options, "interlace",  "Treat input as interlaced", "%d", buf, "0", "1");

	    tc_snprintf (buf, sizeof(buf), "%d", pre);
	    optstr_param (options, "pre",  "Run as a PRE filter", "%d", buf, "0", "1");

	    return 0;
	}

	if(ptr->tag & TC_FILTER_INIT) {

	    if((vob = tc_get_vob())==NULL) return(-1);

	    if (vob->im_v_codec == TC_CODEC_RGB24) {
		tc_log_error(MOD_NAME, "filter is not capable for RGB-Mode !");
		return(-1);
	    }


	    if (options) {
		optstr_get (options, "radius_luma",         "%d", &radius_luma);
		optstr_get (options, "radius_chroma",       "%d", &radius_chroma);
		optstr_get (options, "threshold_luma",      "%d", &threshold_luma);
		optstr_get (options, "threshold_chroma",    "%d", &threshold_chroma);
		optstr_get (options, "interlace",           "%d", &interlace);
		optstr_get (options, "pre",                 "%d", &pre);

		pre       = !!pre;
		interlace = !!interlace;

		if (optstr_lookup (options, "help") != NULL)
		    Usage();

	    }

	    // ptr->v_width/height is invalid here
	    if (pre) {
		horz         = vob->im_v_width;
		vert         = vob->im_v_height;
	    } else {
		horz         = vob->ex_v_width;
		vert         = vob->ex_v_height;
	    }


	    if( interlace && vert % 2 != 0 )
	    {
		tc_log_error(MOD_NAME,
			"Input images have odd number of lines - can't treats as interlaced!");
		return -1;
	    }

	    input_frame[0] = malloc(horz * vert);
	    input_frame[1] = malloc((horz / 2) * (vert / 2));
	    input_frame[2] = malloc((horz / 2) * (vert / 2));

	    if ( !input_frame[0] || !input_frame[1] || !input_frame[2] )
		return (1);

	    frame_count = 0;
	    if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
	    return(0);
	} // INIT


	if(ptr->tag & TC_FILTER_CLOSE) {
	    if (input_frame[0])  { free(input_frame[0]);  input_frame[0]=NULL; }
	    if (input_frame[1])  { free(input_frame[1]);  input_frame[1]=NULL; }
	    if (input_frame[2])  { free(input_frame[2]);  input_frame[2]=NULL; }
	    if (verbose > 1)
		tc_log_info(MOD_NAME, "frames=%d avg=%d replaced=%d",
			avg, chg_replace, ovr_replace);
	    return(0);
	} // CLOSE


	if(((ptr->tag & TC_PRE_M_PROCESS  && pre) ||
		(ptr->tag & TC_POST_M_PROCESS && !pre)) &&
		!(ptr->attributes & TC_FRAME_IS_SKIPPED)) {

	    unsigned int y_size  = ptr->v_width*ptr->v_height;
	    unsigned int y_size4 = ptr->v_width*ptr->v_height>>2;

	    ac_memcpy(input_frame[0], ptr->video_buf,            y_size );
	    ac_memcpy(input_frame[1], ptr->video_buf+y_size    , y_size4);
	    ac_memcpy(input_frame[2], ptr->video_buf+y_size*5/4, y_size4);

	    output_frame[0] = ptr->video_buf;
	    output_frame[1] = ptr->video_buf+y_size;
	    output_frame[2] = ptr->video_buf+y_size*5/4;

	    frame_count++;
	    filter(ptr->v_width, ptr->v_height,  input_frame, output_frame);

	    for (avg=0, i=0; i < 64; i++)
		avg += avg_replace[i];

	    return 0;

	} // run filter

	return 0;
}

static void
filter(int width, int height, uint8_t *input[], uint8_t * const output[])
{
	if( interlace )
	{
		filter_buffer(width, height/2, width*2,
					  radius_luma, threshold_luma,
					  input[0], output[0]);
		filter_buffer(width, height/2, width*2,
					  radius_luma, threshold_luma,
					  input[0]+width, output[0]+width);
		filter_buffer(width/2, height/4, width,
					  radius_chroma, threshold_chroma,
					  input[1], output[1]);
		filter_buffer(width/2, height/4, width,
					  radius_chroma, threshold_chroma,
					  input[1]+width/2, output[1]+width/2);
		filter_buffer(width/2, height/4, width, radius_chroma,
					  threshold_chroma,
					  input[2], output[2]);
		filter_buffer(width/2, height/4, width, radius_chroma,
					  threshold_chroma,
					  input[2]+width/2, output[2]+width/2);
	}
	else
	{
		filter_buffer(width, height, width,
					  radius_luma, threshold_luma,
					  input[0], output[0]);
		filter_buffer(width/2, height/2, width/2,
					  radius_chroma, threshold_chroma,
					  input[1], output[1]);
		filter_buffer(width/2, height/2, width/2,
					  radius_chroma, threshold_chroma,
					  input[2], output[2]);
	}
}

static void
filter_buffer(int width, int height, int row_stride,
			  int radius, int threshold, uint8_t * const input, uint8_t * const output)
{
	int	reference;
	int	diff;
	int	a;
	int	b;
	uint8_t *pixel;
	int	total;
	int	count;
	int	radius_count;
	int	x;
	int	y;
	int	offset;
	int	min_count;
	uint8_t *refpix;
	uint8_t *outpix;
	radius_count = radius + radius + 1;
	min_count = (radius_count * radius_count + 2)/3;


	count = 0;

	offset = radius*row_stride+radius;	/* Offset top-left of processing */
	                                /* Window to its centre */
	refpix = &input[offset];
	outpix = &output[offset];
	for(y=radius; y < height-radius; y++)
	{
		for(x=radius; x < width - radius; x++)
		{
			reference = *refpix;
			total = 0;
			count = 0;
			pixel = refpix-offset;
			b = radius_count;
			while( b > 0 )
			{
				a = radius_count;
				--b;
				while( a > 0 )
				{
					diff = reference - *pixel;
					--a;
					if (diff < threshold && diff > -threshold)
					{
						total += *pixel;
						count++;
					}
					++pixel;
				}
				pixel += (row_stride - radius_count);
			}
			++avg_replace[count];

			/*
			 * If we don't have enough samples to make a decent
			 * pseudo-median use a simple mean
			 */
			if (count <= min_count)
			{
				*outpix =
					( ( (refpix[-row_stride-1] + refpix[-row_stride]) +
						(refpix[-row_stride+1] +  refpix[-1])
						)
					  +
					  ( ((refpix[0]<<3) + 8 + refpix[1]) +
						(refpix[row_stride-1] + refpix[row_stride]) +
						refpix[row_stride+1]
						  )
					 ) >> 4;
			} else {
				*outpix = total / count;
 			}
			++refpix;
			++outpix;
		}
		refpix += (row_stride-width+(radius*2));
		outpix += (row_stride-width+(radius*2));
	}
}
