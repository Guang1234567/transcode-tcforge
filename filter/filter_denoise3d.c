/*
    Copyright (C) 2003 Daniel Moreno <comac@comac.darktech.org>
    Converted for use in transcode by Tilmann Bitterberg <transcode@tibit.org>
    Converted hqdn3d -> denoise3d and also heavily optimised for transcode
        by Erik Slagter <erik@oldconomy.org> (GPL) 2003

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

#define MOD_NAME    "filter_denoise3d.so"
#define MOD_VERSION "v1.0.6 (2003-12-20)"
#define MOD_CAP     "High speed 3D Denoiser"
#define MOD_AUTHOR  "Daniel Moreno, A'rpi"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include <math.h>

/*
	set tabstop=4 for best layout

	Changelog

	1.0.3	EMS	first public version
	1.0.4	EMS	added YUV422 support
	1.0.5	EMS	added RGB support
				large cleanup
				added arbitrary layout support
				denoising U&V (colour) planes now actually works
	1.0.6	EMS	fixed annoying typo
*/

#define MAX_PLANES 3

#define DEFAULT_LUMA_SPATIAL 4.0
#define DEFAULT_CHROMA_SPATIAL 3.0
#define DEFAULT_LUMA_TEMPORAL 6.0
#define DEFAULT_CHROMA_TEMPORAL 4.0

typedef enum { dn3d_yuv420p, dn3d_yuv422, dn3d_rgb } dn3d_fmt_t;
typedef enum { dn3d_planar, dn3d_packed } dn3d_basic_layout_t;
typedef enum { dn3d_luma, dn3d_chroma, dn3d_disabled } dn3d_plane_type_t;

typedef enum
{
	dn3d_off_y420,	dn3d_off_u420,	dn3d_off_v420,
	dn3d_off_y422,	dn3d_off_u422,	dn3d_off_v422,
	dn3d_off_r,		dn3d_off_g,		dn3d_off_b
} dn3d_offset_t;

typedef struct
{
	dn3d_plane_type_t	plane_type;
	dn3d_offset_t		offset;
	int					skip;
	int					scale_x;
	int					scale_y;
} dn3d_single_layout_t;

typedef struct
{
	int						tc_fmt;
	dn3d_fmt_t				fmt;
	dn3d_basic_layout_t		layout_type;
	dn3d_single_layout_t	layout[MAX_PLANES];
} dn3d_layout_t;

typedef struct
{
	vob_t *			vob;
	dn3d_layout_t	layout_data;

	struct
	{
		double luma_spatial;
		double chroma_spatial;
		double luma_temporal;
		double chroma_temporal;
	} parameter;

	int				coefficients[4][512];
	unsigned char *	lineant;
	unsigned char * previous;
	int				prefilter;
	int				enable_luma;
	int				enable_chroma;

} dn3d_private_data_t;

/* FIXME: this uses the filter ID as an index--the ID can grow
 * arbitrarily large, so this needs to be fixed */
static dn3d_private_data_t dn3d_private_data[100];

static const dn3d_layout_t dn3d_layout[] =
{
	{ TC_CODEC_YUV420P, dn3d_yuv420p, dn3d_planar, {{ dn3d_luma, dn3d_off_y420,  1, 1, 1 }, { dn3d_chroma, dn3d_off_u420,  1, 2, 2 }, { dn3d_chroma, dn3d_off_v420,  1, 2, 2 }}},
	{ TC_CODEC_YUV422P, dn3d_yuv422,  dn3d_planar, {{ dn3d_luma, dn3d_off_y422,  1, 1, 1 }, { dn3d_chroma, dn3d_off_u422,  1, 2, 1 }, { dn3d_chroma, dn3d_off_v422,  1, 2, 1 }}},
	{ TC_CODEC_RGB24,     dn3d_rgb,     dn3d_packed, {{ dn3d_luma, dn3d_off_r,     3, 1, 1 }, { dn3d_luma,   dn3d_off_g,     3, 1, 1 }, { dn3d_luma,   dn3d_off_b,     3, 1, 1 }}}
};

#define LowPass(prev, curr, coef) (curr + coef[prev - curr])

static inline int ABS(int i)
{
    return(((i) >= 0) ? i : (0 - i));
}

static void deNoise(unsigned char * frame, unsigned char * frameprev, unsigned char * lineant,
                    int w, int h,
                    int * horizontal, int * vertical, int * temporal,
					int offset, int skip)
{
    int x, y;
    unsigned char pixelant;
    unsigned char * lineantptr = lineant;

    horizontal += 256;
    vertical   += 256;
    temporal   += 256;

	frame		+= offset;
	frameprev	+= offset;

    // First pixel has no left nor top neighbour, only previous frame

    *lineantptr = pixelant = *frame;
    *frame = *frameprev = LowPass(*(frameprev), *lineantptr, temporal);
	frame += skip;
	frameprev += skip;
    lineantptr++;

    // First line has no top neighbour, only left one for each pixel and last frame

    for(x = 1; x < w; x++)
    {
        pixelant = LowPass(pixelant, *frame,  horizontal);
        *lineantptr = pixelant;
        *frame = *frameprev = LowPass(*(frameprev), *lineantptr, temporal);
		frame += skip;
		frameprev += skip;
		lineantptr++;
    }

    for (y = 1; y < h; y++)
    {
		lineantptr = lineant;

        // First pixel on each line doesn't have previous pixel

        pixelant = *frame;
        *lineantptr = LowPass(*lineantptr, pixelant, vertical);
        *frame = *frameprev = LowPass(*(frameprev), *lineantptr, temporal);
		frame += skip;
		frameprev += skip;
		lineantptr++;

        for (x = 1; x < w; x++)
        {
            // The rest is normal

            pixelant = LowPass(pixelant, *frame,  horizontal);
            *lineantptr = LowPass(*lineantptr, pixelant, vertical);
            *frame = *frameprev = LowPass(*(frameprev), *lineantptr, temporal);
			// *frame ^= 255; // debug
			frame += skip;
			frameprev += skip;
	    	lineantptr++;
        }
    }
}

static void PrecalcCoefs(int * ct, double dist25)
{
	int i;
	double gamma, simil, c;

	gamma = log(0.25) / log(1.0 - dist25 / 255.0);

	for(i = -256; i <= 255; i++)
	{
		simil = 1.0 - (double)ABS(i) / 255.0;
		c = pow(simil, gamma) * (double)i;
		ct[256 + i] = (int)((c < 0) ? (c - 0.5) : (c + 0.5));
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
"   luma:            spatial luma strength (%f)\n"
"   chroma:          spatial chroma strength (%f)\n"
"   luma_strength:   temporal luma strength (%f)\n"
"   chroma_strength: temporal chroma strength (%f)\n"
"   pre:             run as a pre filter (0)\n"
		, MOD_CAP,
		DEFAULT_LUMA_SPATIAL,
		DEFAULT_CHROMA_SPATIAL,
		DEFAULT_LUMA_TEMPORAL,
		DEFAULT_CHROMA_TEMPORAL);
}

int tc_filter(frame_list_t *vframe_, char * options)
{
	vframe_list_t *vframe = (vframe_list_t *)vframe_;
	int instance;
	int tag = vframe->tag;
	dn3d_private_data_t * pd;

	if(tag & TC_AUDIO)
		return(0);

	instance	= vframe->filter_id;
	pd			= &dn3d_private_data[instance];

	if(tag & TC_FILTER_GET_CONFIG)
	{
		char buf[128];
		optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VYMOE", "2");

		tc_snprintf(buf, 128, "%f", DEFAULT_LUMA_SPATIAL);
		optstr_param(options, "luma", "spatial luma strength", "%f", buf, "0.0", "100.0" );

		tc_snprintf(buf, 128, "%f", DEFAULT_CHROMA_SPATIAL);
		optstr_param(options, "chroma", "spatial chroma strength", "%f", buf, "0.0", "100.0" );

		tc_snprintf(buf, 128, "%f", DEFAULT_LUMA_TEMPORAL);
		optstr_param(options, "luma_strength", "temporal luma strength", "%f", buf, "0.0", "100.0" );

		tc_snprintf(buf, 128, "%f", DEFAULT_CHROMA_TEMPORAL);
		optstr_param(options, "chroma_strength", "temporal chroma strength", "%f", buf, "0.0", "100.0" );

		tc_snprintf(buf, 128, "%d", dn3d_private_data[instance].prefilter);
		optstr_param(options, "pre", "run as a pre filter", "%d", buf, "0", "1" );
	}

	if(tag & TC_FILTER_INIT)
	{
		int format_index, plane_index, found;
		const dn3d_layout_t * lp;
		size_t size;

		if(!(pd->vob = tc_get_vob()))
			return(TC_IMPORT_ERROR);

		pd->parameter.luma_spatial		= 0;
		pd->parameter.luma_temporal		= 0;
		pd->parameter.chroma_spatial	= 0;
		pd->parameter.chroma_temporal	= 0;

		if(!options)
		{
			tc_log_error(MOD_NAME, "options not set!");
			return(TC_IMPORT_ERROR);
		}

		if(optstr_lookup(options, "help"))
		{
			help_optstr();
			return(TC_IMPORT_ERROR);
	  	}

		optstr_get(options, "luma",				"%lf",	&pd->parameter.luma_spatial);
		optstr_get(options, "luma_strength",	"%lf",	&pd->parameter.luma_temporal);
		optstr_get(options, "chroma",			"%lf",	&pd->parameter.chroma_spatial);
		optstr_get(options, "chroma_strength",	"%lf",	&pd->parameter.chroma_temporal);
		optstr_get(options, "pre",				"%d",	&dn3d_private_data[instance].prefilter);

		if((pd->parameter.luma_spatial < 0) || (pd->parameter.luma_temporal < 0))
			pd->enable_luma = 0;
		else
		{
			pd->enable_luma = 1;

			if(pd->parameter.luma_spatial == 0)
			{
				if(pd->parameter.luma_temporal == 0)
				{
					pd->parameter.luma_spatial	= DEFAULT_LUMA_SPATIAL;
					pd->parameter.luma_temporal	= DEFAULT_LUMA_TEMPORAL;
				}
				else
				{
					pd->parameter.luma_spatial = pd->parameter.luma_temporal * 3 / 2;
				}
			}
			else
			{
				if(pd->parameter.luma_temporal == 0)
				{
					pd->parameter.luma_temporal = pd->parameter.luma_spatial * 2 / 3;
				}
			}
		}

		if((pd->parameter.chroma_spatial < 0) || (pd->parameter.chroma_temporal < 0))
			pd->enable_chroma = 0;
		else
		{
			pd->enable_chroma = 1;

			if(pd->parameter.chroma_spatial == 0)
			{
				if(pd->parameter.chroma_temporal == 0)
				{
					pd->parameter.chroma_spatial	= DEFAULT_CHROMA_SPATIAL;
					pd->parameter.chroma_temporal	= DEFAULT_CHROMA_TEMPORAL;
				}
				else
				{
					pd->parameter.chroma_spatial = pd->parameter.chroma_temporal * 3 / 2;
				}
			}
			else
			{
				if(pd->parameter.chroma_temporal == 0)
				{
					pd->parameter.chroma_temporal = pd->parameter.chroma_spatial * 2 / 3;
				}
			}
		}

		for(format_index = 0, found = 0; format_index < (sizeof(dn3d_layout) / sizeof(*dn3d_layout)); format_index++)
		{
			if(pd->vob->im_v_codec == dn3d_layout[format_index].tc_fmt)
			{
				found = 1;
				break;
			}
		}

		if(!found)
		{
			tc_log_error(MOD_NAME, "This filter is only capable of YUV, YUV422 and RGB mode");
	  		return(TC_IMPORT_ERROR);
		}

		lp = &dn3d_layout[format_index];
		pd->layout_data = *lp;

		for(plane_index = 0; plane_index < MAX_PLANES; plane_index++)
		{
			if((pd->layout_data.layout[plane_index].plane_type == dn3d_luma) && !pd->enable_luma)
				pd->layout_data.layout[plane_index].plane_type = dn3d_disabled;

			if((pd->layout_data.layout[plane_index].plane_type == dn3d_chroma) && !pd->enable_chroma)
				pd->layout_data.layout[plane_index].plane_type = dn3d_disabled;
		}

		size = pd->vob->im_v_width * MAX_PLANES * sizeof(char) * 2;

		pd->lineant = tc_zalloc(size);
		if(pd->lineant == NULL)
			tc_log_error(MOD_NAME, "Malloc failed");

		size *= pd->vob->im_v_height * 2;

		pd->previous = tc_zalloc(size);
		if(pd->previous == NULL)
			tc_log_error(MOD_NAME, "Malloc failed");

		PrecalcCoefs(pd->coefficients[0], pd->parameter.luma_spatial);
		PrecalcCoefs(pd->coefficients[1], pd->parameter.luma_temporal);
		PrecalcCoefs(pd->coefficients[2], pd->parameter.chroma_spatial);
		PrecalcCoefs(pd->coefficients[3], pd->parameter.chroma_temporal);

		if(verbose)
		{
			tc_log_info(MOD_NAME, "%s %s #%d", MOD_VERSION, MOD_CAP, instance);
			tc_log_info(MOD_NAME, "Settings luma (spatial): %.2f "
                                  "luma_strength (temporal): %.2f "
                                  "chroma (spatial): %.2f "
                                  "chroma_strength (temporal): %.2f",
				        pd->parameter.luma_spatial,
        				pd->parameter.luma_temporal,
		        		pd->parameter.chroma_spatial,
				        pd->parameter.chroma_temporal);
			tc_log_info(MOD_NAME, "luma enabled: %s, chroma enabled: %s",
		                pd->enable_luma ? "yes" : "no",
                        pd->enable_chroma ? "yes" : "no");
		}
	}

	if(((tag & TC_PRE_M_PROCESS  && pd->prefilter) || (tag & TC_POST_M_PROCESS && !pd->prefilter)) &&
		!(vframe->attributes & TC_FRAME_IS_SKIPPED))
	{
		int plane_index, coef[2];
		int offset = 0;
		const dn3d_single_layout_t * lp;

		for(plane_index = 0; plane_index < MAX_PLANES; plane_index++)
		{
			lp = &pd->layout_data.layout[plane_index];

			if(lp->plane_type != dn3d_disabled)
			{
				// if(plane_index != 2) // debug
				//	continue;

				coef[0] = (lp->plane_type == dn3d_luma) ? 0 : 2;
				coef[1] = coef[0] + 1;

				switch(lp->offset)
				{
					case(dn3d_off_r):		offset = 0; break;
					case(dn3d_off_g):		offset = 1; break;
					case(dn3d_off_b):		offset = 2; break;

					case(dn3d_off_y420):	offset = vframe->v_width * vframe->v_height * 0 / 4; break;
					case(dn3d_off_u420):	offset = vframe->v_width * vframe->v_height * 4 / 4; break;
					case(dn3d_off_v420):	offset = vframe->v_width * vframe->v_height * 5 / 4; break;

					case(dn3d_off_y422):	offset = vframe->v_width * vframe->v_height * 0 / 2; break;
					case(dn3d_off_u422):	offset = vframe->v_width * vframe->v_height * 2 / 2; break;
					case(dn3d_off_v422):	offset = vframe->v_width * vframe->v_height * 3 / 2; break;

				}

				deNoise(vframe->video_buf,			// frame
					pd->previous,					// previous (saved) frame
					pd->lineant,					// line buffer
					vframe->v_width / lp->scale_x,	// width (pixels)
					vframe->v_height / lp->scale_y,	// height (pixels) // debug
					pd->coefficients[coef[0]],		// horizontal (spatial) strength
					pd->coefficients[coef[0]],		// vertical (spatial) strength
					pd->coefficients[coef[1]],		// temporal strength
					offset,							// offset in bytes of first relevant pixel in frame
					lp->skip						// skip this amount of bytes between two pixels
				);
			}
		}
	}

	if(tag & TC_FILTER_CLOSE)
	{
		if(pd->previous)
		{
			free(pd->previous);
			pd->previous = 0;
		}

		if(pd->lineant)
		{
			free(pd->lineant);
			pd->lineant = 0;
		}
	}

	return(0);
}
