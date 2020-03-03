/*
 *  filter_subtitler.c
 *
 *  Copyright (C) Jan Panteltje  2001
 *
 *  This file is part of transcode, a video stream processing tool
 *  Font reading etc from Linux mplayer
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


#include "subtitler.h"


/* for YUV to RGB in X11 */
#define LIMIT(x) ((((x)>0xffffff)?0xff0000:(((x)<=0xffff)?0:(x)&0xff0000))>>16)
int write_ppm_flag;

int debug_flag;
int frame_offset;
font_desc_t *vo_font;
font_desc_t *subtitle_current_font_descriptor;
uint8_t *ImageData;
int image_width, image_height;
int default_font;
struct passwd *userinfo;
char *home_dir;
char *user_name;
int line_h_start, line_h_end;
int center_flag;
char *frame_memory0, *frame_memory1;
char *subtitle_file;
char *default_font_dir;
vob_t *vob;
double dmax_vector;
int use_pre_processing_flag;
char *subtitle_font_path;
char *default_subtitle_font_name;
int default_subtitle_symbols;
int default_subtitle_font_size;
int default_subtitle_iso_extention;
double default_subtitle_radius;
double default_subtitle_thickness;

int show_output_flag;
int window_open_flag;
int window_size;
unsigned char *ucptrs, *ucptrd;
int color_depth;

double default_font_factor;
double subtitle_extra_character_space;

int border_luminance;
int default_border_luminance;

double subtitle_h_factor;
double subtitle_v_factor;
double extra_character_space;

int rgb_palette[16][3]; // rgb
int rgb_palette_valid_flag;

int default_subtitle_font_symbols;

int dcontrast, brightness;
double dsaturation;
double dhue, dhue_line_drift;
int u_shift, v_shift;
int slice_level;

int add_objects_flag;
int help_flag;
int de_stripe_flag;

int movie_id;

static double acr, acg, acb, acu, acv;
static int use_emphasis2_for_anti_aliasing_flag;

extern int add_objects(int);
extern int execute(char *);

/*
subtitle 'filter',
it adds objects as described in a file in .ppml format,
*/
int tc_filter(frame_list_t *pfl_, char *options)
{
vframe_list_t *pfl = (vframe_list_t *)pfl_;
int a, i, x;
double da, db;
int pre = 0;
int vid = 0;
static FILE *pppm_file;
static FILE *fptr;
char temp[4096];
static int frame_nr;
static uint8_t *pfm, *opfm, *pfmend, *opfmend;
static uint8_t *pline_start, *pline_end, *opline_start, *opline_end;
static int x_shift;
char *running;
char *token;
static int y, b;
uint8_t *py, *pu, *pv;
static int cr, cg, cb, cy, cu, cv;
static int have_bottom_margin_flag;
//aframe_list_t *afl;


if (pfl->tag & TC_FILTER_GET_CONFIG) {
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "Panteltje", "VRYO", "1");
      return 0;
}

/* filter init */
if(pfl->tag & TC_FILTER_INIT)
	{
	vob = tc_get_vob();
	if(! vob)
		{
		tc_log_error(MOD_NAME, "could not tc_get_vob() failed");

		return -1;
		}
	if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

	/* identify */
    tc_log_info(MOD_NAME,
        "Panteltje (c) movie composer%s (alias subtitle-filter)",
    	SUBTITLER_VERSION);

	/* get home directory */
	userinfo = getpwuid(getuid() );
	home_dir = strsave(userinfo -> pw_dir);
	user_name = strsave(userinfo -> pw_name);

	/* set some defaults */

	/* for subtitles */

	/* use post processing */
	use_pre_processing_flag = 0;

	/* a frame in transcode is 40 ms */
	frame_offset = 0;

	/* this selects some other symbols it seems */
	default_font = 0;	// 1 = strange symbols like stars etc..

	/* this sets the font outline */
	default_font_factor = 10.75;	// outline, was .75

	/* this sets the horizontal space for the subtitles */
	subtitle_h_factor = SUBTITLE_H_FACTOR;

	/* this sets how far the subtitles start from the bottom */
	subtitle_v_factor = SUBTITLE_V_FACTOR;

	/* location where font.descr is */
	tc_snprintf(temp, sizeof(temp), "%s/.xste/fonts", home_dir);
	default_font_dir = strsave(temp);
	if(! default_font_dir)
		{
		tc_log_error(MOD_NAME, "subtitler(): could not allocate space for default_font_dir");

		return -1;
		}

	/*
	the mplayer font seems to overlap getween 'l' and 't', added some
	extra space between characters
	*/
	extra_character_space = EXTRA_CHAR_SPACE;
	subtitle_extra_character_space = EXTRA_CHAR_SPACE;

	/* the ppml file */
	tc_snprintf(temp, sizeof(temp), "%s/.subtitles/demo.ppml", home_dir);
	subtitle_file = strsave(temp);
	if(! subtitle_file)
		{
		tc_log_error(MOD_NAME,
		"subtitler(): could not allocate space for subtitle_file");

		return -1;
		}

	/* for picture adjust */
	brightness = 0;			// steps
	dcontrast = 100.0;		// percent
	dsaturation = 100.0;	// percent
	u_shift = 0;			// steps
	v_shift = 0;			// steps

	/* for color correction */
	dhue = 0.0;
	dhue_line_drift = 0.0;	// The rotation in degrees at the start and end
							// of a line.
							// This is for correcting color errors in NTSC
							// tapes.
							// Use in combination with dhue in color
							// correction.


	/* for show output in X11 */
	window_open_flag = 0;
	color_depth = 0; /* get from X */

	/* module settings */
	add_objects_flag = 1;
	de_stripe_flag = 0;
	write_ppm_flag = 0;
	show_output_flag = 0;
	center_flag = 1;

	/* uses when we call transcode recursive, to run a movie in a movie */
	movie_id = 0;

	/* for chroma key, better do this not each pixel .. */
	dmax_vector = sqrt( (127.0 * 127.0) + (127.0 * 127.0) );

	/*
	for rotate and shear, the level we cut the remaining parts.
	Note that yuv_to_ppm() also uses this, to write a modified .ppm
	that does NOT have this luminance in it, for use by mogrify.
	This ONLY happens if rotate or shear present.
	*/
	default_border_luminance = LUMINANCE_MASK;

	tc_snprintf(temp, sizeof(temp), "%s/.xste/fonts", home_dir);
	subtitle_font_path = strsave(temp);
	if(! subtitle_font_path)
		{
		tc_log_error(MOD_NAME,
		"subtitler: tc_filter(): could not allocate space for subtitle_font_path, aborting");

		exit(1);
		}

	default_subtitle_font_name = strsave("arial.ttf");
	if(! default_subtitle_font_name)
		{
		tc_log_error(MOD_NAME,
		"subtitler: tc_filter(): could not allocate space for default_subtitle_font_name, aborting");

		exit(1);
		}

	default_subtitle_symbols = 0;
	default_subtitle_font_size = 28;
	default_subtitle_iso_extention = 15;
	default_subtitle_radius = 1.0;
	default_subtitle_thickness = 0.1;

	default_subtitle_font_symbols = 0;

	rgb_palette_valid_flag = 0;

	/* color standard */

	/* Y spec */
	acr = 0.3;
	acg = 0.59;
	acb = 0.11;

	/* U spec */
	acu = .5 / (1.0 - acb);

	/* V spec */
	acv = .5 / (1.0 - acr);

	use_emphasis2_for_anti_aliasing_flag = 0;

	debug_flag = 0;

	/* end defaults */
	if(debug_flag)
		{
		tc_log_info(MOD_NAME, "options=%s", options);
		}

	if(temp[0] != 0)
		{
		running = strsave(options);
		if(! running)
			{
			tc_log_error(MOD_NAME, "subtitler(): strsave(options) failed");

			return -1;
			}
		while(1)
			{
			token = strsep (&running, " ");
			if(token == NULL) break;

			/* avoid empty string */
			if(token[0] == 0) continue;

			if(strncmp(token, "no_objects", 10) == 0)
				{
				add_objects_flag = 0;
				}
			else if(strncmp(token, "write_ppm", 9) == 0)
				{
 				write_ppm_flag = 1;
				}
			else if(strncmp(token, "debug", 5) == 0)
				{
				debug_flag = 1;
				}
			else if(strncmp(token, "help", 4) == 0)
				{
				help_flag = 1;
				print_options();

				/* error exit */
				return 0;
				//exit(1);
				}
			 else if(strncmp(token, "subtitle_file=", 14) == 0)
				{
				a = sscanf(token, "subtitle_file=%s", temp);
				if(a == 1)
					{
					free(subtitle_file);
					subtitle_file = strsave(temp);
					if(! subtitle_file)
						{
						tc_log_error(MOD_NAME,
			"subtitler(): could not allocate space for subtitle_file");

						return -1;
						}
					}
				}
			else if(strncmp(token, "font_dir=", 9) == 0)
				{
				a = sscanf(token, "font_dir=%s", temp);
				if(a == 1)
					{
					free(default_font_dir);
					default_font_dir = strsave(temp);
					if(! default_font_dir)
						{
						tc_log_error(MOD_NAME,
			"subtitler(): could not allocate space for default_font_dir");

						return -1;
						}
					}
				}
			sscanf(token, "color_depth=%d", &color_depth);
			sscanf(token, "font=%d", &default_font);
			sscanf(token, "font_factor=%lf", &default_font_factor);
			sscanf(token, "frame_offset=%d", &frame_offset);
			sscanf(token, "movie_id=%d", &movie_id);

			if(strcmp(token, "anti_alias") == 0) use_emphasis2_for_anti_aliasing_flag = 1;

			if(strcmp(token, "use_pre_processing") == 0)
				{
				use_pre_processing_flag = 1;
				}
			} /* end while parse options */

		free(running);
		} /* end if options */

	if(use_pre_processing_flag)
		{
		tc_log_info(MOD_NAME, "Using pre processing");
		}
	else
		{
		tc_log_info(MOD_NAME, "Using post processing");
		}

	if(debug_flag)
		{
		tc_log_info(MOD_NAME, "PARSER RESULT: "
		"write_ppm_flag=%d add_objects_flag=%d show_output_flag=%d "
		"color_depth=%d frame_offset=%d movie_id=%d "
		"use_pre_processing_flag=%d",
		write_ppm_flag, add_objects_flag, show_output_flag,\
		color_depth, frame_offset, movie_id,\
		use_pre_processing_flag\
		);
		}

	if(add_objects_flag)
		{
		/* read in font (also needed for frame counter) */
//		tc_snprintf(temp, sizeof(temp), "%s/font.desc", default_font_dir);
		tc_snprintf(temp, sizeof(temp), "arial.ttf");
		vo_font = add_font(temp, default_subtitle_symbols, 28, 15, 1.0, 0.1);
		if(! vo_font)
			{
			tc_log_error(MOD_NAME, "subtitler(): Could not load font");

			/* return init error */
			return -1;
			}

		subtitle_current_font_descriptor = vo_font;

		/* load ppml file */
		if(! load_ppml_file(subtitle_file) )
			{
			tc_log_error(MOD_NAME, "subtitler(): could not load file %s",\
			subtitle_file);

			/* return init error */
			return -1;
			}
		} /* end if add_objects_flag */

	/* return init OK */
	return 0;
	} /* end if filter init */

/* filter close */
if(pfl->tag & TC_FILTER_CLOSE)
	{
	/* rely on exit() */

	/* return close OK */
	return 0;
	} /* end if filter close */

/*
filter frame routine
tag variable indicates, if we are called before
transcodes internal video/audo frame processing routines
or after and determines video/audio context
*/
if(verbose & TC_STATS)
	{
	tc_log_info(MOD_NAME, "%s/%s %s %s",\
	vob->mod_path, MOD_NAME, MOD_VERSION, MOD_CAP);

	/*
	tag variable indicates, if we are called before
	transcodes internal video/audo frame processing routines
	or after and determines video/audio context
   	*/

	if(pfl->tag & TC_PRE_M_PROCESS) pre = 1;
	if(pfl->tag & TC_POST_M_PROCESS) pre = 0;

	if(pfl->tag & TC_VIDEO) vid = 1;
	if(pfl->tag & TC_AUDIO) vid = 0;

	tc_log_info(MOD_NAME, "frame [%06d] %s %16s call",\
	pfl->id, (vid)?"(video)":"(audio)",\
	(pre)?"pre-process filter":"post-process filter");
	} /* end if verbose and stats */

/*
default:
add the subtitles, after the coding, else edges in text get bad
*/
if(use_pre_processing_flag)
	{
	a = (pfl->tag & TC_PRE_M_PROCESS) && (pfl->tag & TC_VIDEO);
	}
else
	{
	a = (pfl->tag & TC_POST_M_PROCESS) && (pfl->tag & TC_VIDEO);
	}

if(a)
	{
	ImageData = pfl->video_buf;
	image_width = pfl->v_width;
	image_height = pfl->v_height;
	frame_nr = pfl->id;
	if(! have_bottom_margin_flag)
		{
		window_bottom = image_height - window_bottom;
		have_bottom_margin_flag = 1;
		}

	if(debug_flag)
		{
		tc_log_info(MOD_NAME, \
		"frame_nr=%d \
		ImageData=%lu image_width=%d image_height=%d",\
		frame_nr,\
		(unsigned long)ImageData, image_width, image_height);
		}

	/*
	calculate where to put and how to reformat the subtitles.
	These are globals.
	*/
	line_h_start = subtitle_h_factor * (double)image_width;
	line_h_end = (double)image_width - (double)line_h_start;
	window_bottom = image_height - (subtitle_v_factor * (double)image_height);

	if(de_stripe_flag)
		{
		/*
		create a place to save the current frame,
		going to use it next frame to replace the lines that are all white,
		as caused by severe dropouts in ancient Umatic tapes.
		NOTE!:
		This cannot be done in INIT as then pfl->v_width and pfl->v_height
		are not available yet (zero).
		*/
		if(! frame_memory0)
			{
			/* for RGB */
			frame_memory0 = malloc(pfl->v_width * pfl->v_height * 3);
			if(! frame_memory0)
				{
				tc_log_error(MOD_NAME, "de_striper(): could not malloc frame_memory0");

				/* return error */
				return -1;
				}
			frame_memory1 = malloc(pfl->v_width * pfl->v_height * 3);
			if(! frame_memory1)
				{
				tc_log_error(MOD_NAME, "de_striper(): could not malloc frame_memory1");

				/* return error */
				return -1;
				}
			} /* end if ! frame_memory */

		/* save the current frame for later */
		for(i = 0; i < pfl->v_width * pfl->v_height * 3; i++)
			{
			frame_memory0[i] = pfl->video_buf[i];
			}

		slice_level = 0;
		pfm = pfl->video_buf;
		opfm = frame_memory1;
		pfmend = ImageData + (pfl->v_height * pfl->v_width * 3);
		opfmend = frame_memory1 + (pfl->v_height * pfl->v_width * 3);
		for(y = 0; y < pfl->v_height; y++)
			{
			/* get line boundaries for video buffer */
			pline_start = pfm;
			pline_end = pfm + pfl->v_width * 3;

			/* get line boundaries for frame_memory1 */
			opline_start = opfm;
			opline_end = opfm + pfl->v_width * 3;
			x_shift = 0;
			/*
			perhaps expand condition for more then one pixel in a line
			*/
			for(x = 0; x < pfl->v_width; x++)
				{
				if(pfm >= pfmend - 3) break;

				/* test if white stripe */
				if( (pfm[0] - opfm[0] > slice_level) &&\
				(pfm[1] - opfm[1] > slice_level) &&\
				(pfm[2] - opfm[2] > slice_level) )
					{

					/* test for out of range pointers due to x_shift */
					if( (opfm + x_shift >= (uint8_t *)frame_memory1) &&\
					(opfm + x_shift < opfmend) )
						{
						/* replace with data from previous frame */
						pfm[0] = *(opfm + x_shift);
						pfm[1] = *(opfm + 1 + x_shift);
						pfm[2] = *(opfm + 2 + x_shift);
						} /* end if in range */
					} /* end if white stripe */

				pfm += 3;
				opfm += 3;

				} /* end for all x */

			if(pfm >= pfmend - 3) break;

			} /* end for all y */

		/* save the current frame for later */
		for(i = 0; i < pfl->v_width * pfl->v_height * 3; i++)
			{
			frame_memory1[i] = frame_memory0[i];
			}

		} /* end if de_stripe_flag */

	if\
	(\
	(dcontrast != 100.0) ||\
	(dsaturation != 100.0) ||\
	(u_shift) ||\
	(v_shift)\
	)
		{
		/*
		brightness, contrast, saturation, U zero shift, V zero shift.
		*/
		ucptrs = ImageData;
		/* set pointers */
	    py = ImageData;
	    pv = ImageData + image_width * image_height;
	    pu = ImageData + (image_width * image_height * 5) / 4;

		if(vob->im_v_codec == TC_CODEC_RGB24)
			{
			for(y = 0; y < pfl->v_height; y++)
				{
				for(x = 0; x < pfl->v_width * 3; x++)
					{
					/* brightness */
					if( (brightness + *py) > 255) *py = 255;
					else if ( (brightness + *py) < 0) *py = 0;
					else *py += brightness;

					/* contrast */
					da = *py;
					da *= dcontrast / 100.0;
					*py = (int)da;

					} /* end for all x */

				} /* end for y */
			} /* end if color_depth 32 */
		else if(vob->im_v_codec == TC_CODEC_YUV420P)
			{
			for(y = 0; y < pfl->v_height; y++)
				{
				for(x = 0; x < pfl->v_width; x++)
					{
					/* brightness */
					if( (brightness + *py) > 255) *py = 255;
					else if ( (brightness + *py) < 0) *py = 0;
					else *py += brightness;

					/* contrast */
					da = *py;
					da *= dcontrast / 100.0;
					*py = (int)da;

					/* saturation */
					a = (int)*pu - 128;
					b = (int)*pv - 128;

					a *= dsaturation / 100.0;
					b *= dsaturation / 100.0;

					*pu = (uint8_t)a + 128;
					*pv = (uint8_t)b + 128;

					/* u_shift */
					*pu += u_shift;
					*pu &= 0xff;

					/* v_shift */
					*pv += v_shift;
					*pv &= 255;

					/* increment Y pointer */
					py++;

					/* increment U and V vector pointers */
					if(x % 2)
						{
						pu++;
						pv++;
						}
					} /* end for all x */

				if( (y + 1) % 2)
					{
					pu -= pfl->v_width / 2;
					pv -= pfl->v_width / 2;
					}

				} /* end for y */
			} /* end if buffer is YUV */
		} /* end if contrast, saturation, u_shift, v_shift */

	if( dhue || dhue_line_drift)
		{
		/*
		UV vector rotation.
		Dynamic UV vector rotation (NTSC line phase error correction).
		*/
		if(vob->im_v_codec == TC_CODEC_RGB24)
			{
			tc_log_error(MOD_NAME, \
			"hue operations only available in YUV 420");
            return(-1);
			} /* end if CODEC_RGB */
		else if(vob->im_v_codec == TC_CODEC_YUV420P)
			{
			/* set pointers */
		    py = ImageData;
		    pv = ImageData + image_width * image_height;
		    pu = ImageData + (image_width * image_height * 5) / 4;

			for(y = 0; y < pfl->v_height; y++)
				{
				for(x = 0; x < pfl->v_width; x++)
					{
					/*
					NTSC color correction at start and end of line
					Assuming middle to be correct, most users would have
					adjusted on face color somewhere in the middle.
					*/

					/* the phase drift over one horizontal line */
					da = (double)x / (double)pfl->v_width; // 0 to 1

					/* go for middle, now -.5 to +.5 */
					da -= .5;

					/* multiply by specified dynamic correction factor */
					db = dhue_line_drift * da;

					/* add the static hue correction specified */
					db += (double)dhue;

					/* hue and saturation*/
					a = (int)*pu - 128;
					b = (int)*pv - 128;
					adjust_color(&a, &b, db, dsaturation);
					*pu = (uint8_t)a + 128;
					*pv = (uint8_t)b + 128;

					/* increment Y pointer */
					py++;

					/* increment U and V vector pointers */
					if(x % 2)
						{
						pu++;
						pv++;
						}
					} /* end for all x */

				/*
				2 x 2 color pixels on screen for each Y value,
				repeat each line twice.

				Orientation on screen Y (*) and U V (o)
				* o
				o o
				drop shadow :-) color less area below char looks better.
				sink a line.
				*/
				if( (y + 1) % 2)
					{
					pu -= pfl->v_width / 2;
					pv -= pfl->v_width / 2;
					}

				} /* end for y */
			} /* end if buffer is YUV */

		} /* end if some sort of hue */

	if(add_objects_flag)
		{
		/*
		collect any objects from database for this frame
		and add to object list.
		*/
		process_frame_number(frame_nr);

		/* add objects in object list to display, and update params */
		add_objects(frame_nr);

		} /* end if add_objects_flag */

	if(write_ppm_flag)
		{
		if(vob->im_v_codec == TC_CODEC_RGB24)
			{
			tc_log_error(MOD_NAME, \
			"subtitler(): write_ppm only available in YUV 420\n");
			return(-1);
			} /* end if CODEC_RGB */
		else if(vob->im_v_codec == TC_CODEC_YUV420P)
			{
			/* set pointers */
		    py = ImageData;
		    pv = ImageData + image_width * image_height;
		    pu = ImageData + (image_width * image_height * 5) / 4;

			/* open the ppm file for write */
			tc_snprintf(temp, sizeof(temp), "%s/.subtitles/%d.ppm", home_dir, movie_id);
			pppm_file = fopen(temp, "w");
			if(! pppm_file)
				{
				tc_log_error(MOD_NAME,
				"could not open file %s for write, aborting",\
				 temp);
				return(-1);
				}

			/* write the ppm header */
			fprintf(pppm_file,\
			"P6\n%i %i\n255\n", pfl->v_width, pfl->v_height);

			for(y = 0; y < pfl->v_height; y++)
				{
				/* get a line from buffer start, to file in RGB */
				for(x = 0; x < pfl->v_width; x++)
					{
					cy = ( (0xff & *py) - 16);
					cy  *= 76310;

					cu = (0xff & *pu) - 128;
					cv = (0xff & *pv) - 128;

					cr = 104635 * cv;
					cg = -25690 * cu + -53294 * cv;
					cb = 132278 * cu;

					fprintf(pppm_file, "%c%c%c",\
					LIMIT(cr + cy), LIMIT(cg + cy), LIMIT(cb + cy) );

					/* increment Y pointer */
					py++;

					/* increment U and V vector pointers */
					if(x % 2)
						{
						pu++;
						pv++;
						}
					} /* end for all x */

				if( (y + 1) % 2)
					{
					pu -= pfl->v_width / 2;
					pv -= pfl->v_width / 2;
					}

				} /* end for y (all lines) */
			} /* end if buffer is YUV */
		fclose(pppm_file);

		/* set the semaphore indicating the .ppm file is ready */
		tc_snprintf(temp, sizeof(temp), "touch %s/.subtitles/%d.sem", home_dir, movie_id);
		execute(temp);

		/* now wait for the semaphore to be removed, by calling */
		tc_snprintf(temp, sizeof(temp), "%s/.subtitles/%d.sem", home_dir, movie_id);
		while(1)
			{
			fptr = fopen(temp, "r");
			if(! fptr) break;

			fclose(fptr);

			/* reduce processor load */
			usleep(10000); // 10 ms
			} /* end while wait for handshake */

		} /* end if write_ppm_flag */

	if(show_output_flag)
		{
		/* create an xwindows display */
		if(! window_open_flag)
			{
			if(debug_flag)
				{
				tc_log_info(MOD_NAME, "opening window");
				}

//			openwin(argc, argv, width, height);
			openwin(0, NULL, pfl->v_width, pfl->v_height);

			window_size = pfl->v_width * pfl->v_height;
			window_open_flag = 1;

			if(color_depth == 0) color_depth = get_x11_bpp();

			} /* end if ! window_open_flag */
		else /* have window */
			{
			if( (pfl->v_width * pfl->v_height) != window_size)
				{
				/* close window and open a new one */
//				closewin(); //crashes
//				resize_window(xsize, ysize); // problem ?
// no problem, now we have 2 windows, use window manager to kill one

//				openwin(argc, argv, xsize, ysize);
				openwin(0, NULL, pfl->v_width, pfl->v_height);

				window_size = pfl->v_width * pfl->v_height;
				} /* end if different window size */

			/* get X11 buffer */
			ucptrd = (unsigned char *)getbuf();

			/* copy data to X11 buffer */
			ucptrs = ImageData;

			if(vob->im_v_codec == TC_CODEC_RGB24)
				{
				/* need vertical flip, but not horizontal flip */
				if(color_depth == 32)
					{
					/* ucptrs points to start buffer, ucptrd to X buffer */
					ucptrd += (window_size - pfl->v_width) * 4;
					for(y = 0; y < pfl->v_height; y++)
						{
						/*
						get a line from buffer start, copy to xbuffer end
						*/
						for(x = 0; x < pfl->v_width; x++)
							{
							*ucptrd++ = *ucptrs++;
							*ucptrd++ = *ucptrs++;
							*ucptrd++ = *ucptrs++;

							ucptrd++; /* nothing in byte 4 */
							}

						/* move back a line, so we V flip */
						ucptrd -= pfl->v_width * 8;
						} /* end for y (all lines) */
					} /* end if color_depth 32 */
				else if(color_depth == 24) // NOT TESTED!!!!!!!!
					{
					/* ucptrs points to start buffer, ucptrd to X buffer */
					ucptrd += (window_size - pfl->v_width) * 3;
					for(y = 0; y < pfl->v_height; y++)
						{
						/*
						get a line from buffer start, copy to xbuffer end
						*/
						for(x = 0; x < pfl->v_width; x++)
							{
							*ucptrd++ = *ucptrs++;
							*ucptrd++ = *ucptrs++;
							*ucptrd++ = *ucptrs++;
							}

						/* move back a line, so we V flip */
						ucptrd -= pfl->v_width * 6;
						} /* end for y (all lines) */
					} /* end if color_depth 32 */
				} /* end if buffer is RGB */
			else if(vob->im_v_codec == TC_CODEC_YUV420P)
				{
				/* set pointers */
			    py = ImageData;
			    pv = ImageData + image_width * image_height;
			    pu = ImageData + (image_width * image_height * 5) / 4;
				/* ucptrd is pointer to xbuffer BGR */

				for(y = 0; y < pfl->v_height; y++)
					{
					/* get a line from buffer start, copy to xbuffer BGR */
					for(x = 0; x < pfl->v_width; x++)
						{
						cy = ( (0xff & *py) - 16);
						cy  *= 76310;

						cu = (0xff & *pu) - 128;
						cv = (0xff & *pv) - 128;

						cr = 104635 * cv;
						cg = -25690 * cu + -53294 * cv;
						cb = 132278 * cu;

						if(color_depth == 32) // 4 bytes per pixel
							{
							*ucptrd++ = LIMIT(cb + cy); // B
							*ucptrd++ = LIMIT(cg + cy); // G
							*ucptrd++ = LIMIT(cr + cy); // R

							/* one more byte */
							*ucptrd++ = 0; // last byte is empty.
							} /* end if color depth 32 */

						/* 24 bpp not tested */
						else if(color_depth == 24) // 3 bytes per pixel
							{
							*ucptrd++ = LIMIT(cb + cy); // B
							*ucptrd++ = LIMIT(cg + cy); // G
							*ucptrd++ = LIMIT(cr + cy); // R
							}

						/* increment Y pointer */
						py++;

						/* increment U and V vector pointers */
						if(x % 2)
							{
							pu++;
							pv++;
							}
						} /* end for all x */

					/*
					2 x 2 color pixels on screen for each Y value,
					repeat each line twice.

					Orientation on screen Y (*) and U V (o)
					* o
					o o
					drop shadow :-) color less area below char looks better.
					sink a line.
					*/
					if( (y + 1) % 2)
						{
						pu -= pfl->v_width / 2;
						pv -= pfl->v_width / 2;
 						}

					} /* end for y (all lines) */
				} /* end if buffer is YUV */

			/* show X11 buffer */
			putimage(pfl->v_width, pfl->v_height);
			} /* end if window_open_flag */

		} /* end if show_output_flag */

	} /* end if TC_VIDEO && TC_POST_M_PROCESS */

/* return OK */
return 0;
} /* end function tc_filter */


int add_text(\
int x, int y,\
char *text,
struct object *pa,\
int u, int v,\
double contrast, double transparency, font_desc_t *pfd, int espace)
{
int a;
char *ptr;

if(debug_flag)
	{
	tc_log_info(MOD_NAME, "add_text(): x=%d y=%d text=%s \
	pa=%p u=%d v=%d contrast=%.2f transparency=%.2f \
	font_desc_t=%lu espace=%d",\
	x, y, (const char *)pa, text, u, v, contrast, transparency, (unsigned long)pfd, espace);
	}

ptr = text;
while(*ptr)
	{
	/* convert to signed */
	a = *ptr;
	if(*ptr < 0) a += 256;

	if(a == ' ')
		{
        /* want to print background only here, not '_' */
        draw_char(x, y, a, pa, u, v, contrast, transparency, pfd, 1);
		}
	else
		{
		draw_char(x, y, a, pa, u, v, contrast, transparency, pfd, 0);
		}

	x += pfd->width[a] + pfd->charspace;


	x += espace; //extra_character_space;
	ptr++;
	}

return 1;
} /* end function add_text */


int draw_char(\
int x, int y, int c,\
struct object *pa,\
int u, int v,\
double contrast, double transparency, font_desc_t *pfd, int is_space)
{
if(debug_flag)
	{
	tc_log_info(MOD_NAME, "draw_char(): arg \
	x=%d y=%d c=%d pa=%p u=%d v=%d contrast=%.2f transparency=%.2f \
	pfd=%lu is_space=%d",\
	x, y, c, pa, u, v, contrast, transparency, (unsigned long)pfd, is_space);
	}

draw_alpha(\
	x,\
	y,\
	pa,\
	pfd->width[c],\
	pfd->pic_a[pa -> font_symbols]->h,\
	pfd->pic_b[pa -> font_symbols]->bmp + pfd->start[c],\
	pfd->pic_a[pa -> font_symbols]->bmp + pfd->start[c],\
	pfd->pic_a[pa -> font_symbols]->w,\
	u, v, contrast, transparency, is_space);

return 1;
} /* end function draw_char */


void draw_alpha(\
	int x0, int y0,\
	struct object *pa,\
	int w, int h,\
	uint8_t *src, uint8_t *srca, int stride,\
	int u, int v, double contrast, double transparency, int is_space)
{
int a, b, c, x, y, sx, cd;
uint8_t *py, *pu, *pv;
uint8_t *sc, *sa;
double dmto = 0, dmti = 0;
uint8_t uy, ur, ug, ub, ua, uc;
int iu, iv;
int iy;
unsigned char *dst;
double dir, dig, dib, dor, dog, dob;
double diy, diu, div, doy, dou, dov;
double da, db;
double opaqueness_p, opaqueness_e1, opaqueness_e2;
double dmci;
double dmti_p = 0, dmti_e1 = 0, dmti_e2 = 0;
double dmto_p = 0, dmto_e1 = 0, dmto_e2 = 0;
double dy, dblur;
double dmulto, dmulti;


if(debug_flag)
	{
	tc_log_info(MOD_NAME, \
	"draw_alpha(): x0=%d y0=%d pa=%p w=%d h=%d \
	src=%lu srca=%lu stride=%d u=%d v=%d \
	contrast=%.2f transparency=%.2f is_space=%d",\
	x0, y0, pa, w, h,\
	(unsigned long)src, (unsigned long)srca, stride, u, v,\
	contrast, transparency, is_space);

	tc_log_info(MOD_NAME, "vob->im_v_codec=%d", vob -> im_v_codec);
	tc_log_info(MOD_NAME, "image_width=%d image_height=%d", image_width, image_height);
	tc_log_info(MOD_NAME, "ImageData=%lu", (unsigned long)ImageData);
	}

/* all */
db = (1.0 - (double)pa -> transparency / 100.0);
dmci = (pa -> contrast / 100.0);

if(rgb_palette_valid_flag)
	{
	/* pattern */
	/* calculate 'visibility' insert */
	da = (double) pa -> pattern_contrast / 15.0;
	opaqueness_p = da * db;

	/* combine subtitler and DVD transparency */
	dmto_p = 1.0 - opaqueness_p;
	dmti_p = 1.0 - dmto_p;

	dmti_p *= dmci;


	/* emphasis1 */
	/* calculate 'visibility' insert */
	da = (double) pa -> emphasis1_contrast / 15.0;
	opaqueness_e1 = da * db;

	/* combine subtitler and DVD transparency */
	dmto_e1 = 1.0 - opaqueness_e1;
	dmti_e1 = 1.0 - dmto_e1;

	dmti_e1 *= dmci;


	/* emphasis2 */
	/* calculate 'visibility' insert */
	da = (double) pa -> emphasis2_contrast / 15.0;
	opaqueness_e2 = da * db;

	/* combine subtitler and DVD transparency */
	dmto_e2 = 1.0 - opaqueness_e2;
	dmti_e2 = 1.0 - dmto_e2;

	dmti_e2 *= dmci;

	}
else
	{
	/* calculate multiplier for transparency ouside loops */
	dmti = db;
	dmto = 1.0 - dmti;

	dmti *= dmci;
	}

sc = src;
sa = srca;

if(vob->im_v_codec == TC_CODEC_RGB24)
	{
	a = 3 * (image_height * image_width); // size of a picture

	for(y = 0; y < h; y++)
		{
		b = 3 * ( (y + y0) * image_width);

		for(x = 0; x < w; x++)
			{
			c = 3 * (image_width - (x + x0) );

			dst = ImageData + a - (b + c);

			/* clip right scroll */
			if( (x + x0) > image_width - 1) continue;

			/* clip left scroll */
			if( (x + x0 ) < 0) continue;

			/* clip top scroll */
			if( (y + y0) > image_height - 1) continue;

			/* clip bottom scroll */
			if( (y + y0) < 0) continue;

			if(! rgb_palette_valid_flag)
				{
				if(sa[x] && !is_space)
					{

					/* get original */
					dob = (double) dst[0];
					dog = (double) dst[1];
					dor = (double) dst[2];

					/* get insert (character) original is BW */
					diy = (double) (sa[x] >> 8) + sc[x];

					/* transparency */
					diy *= dmti;

					dob *= dmto;
					dog *= dmto;
					dor *= dmto;

					if(sa[x])
						{
						dst[0] = (int) (dob + diy);
						dst[1] = (int) (dog + diy);
						dst[2] = (int) (dor + diy);
						}
					else /* border */
						{
						dst[0] = (int) (dob);
						dst[1] = (int) (dog);
						dst[2] = (int) (dor);
						}

					}
				} /* end if ! rgb_palette_valid_flag */
			else /* DVD like subs */
				{
				/* some temp vars */
				ub = dst[0];
				ug = dst[1];
				ur = dst[2];

				ua = sa[x];
				uc = sc[x];

				/* get original */
				dob = (double)dst[0];
				dog = (double)dst[1];
				dor = (double)dst[2];

				/* blur factor y * sa[x] */
				dy = .3* dor + .59 * dog + .11 * dob;
				dblur = (double) ( ((int)dy * ua) >> 8) + uc;
				dblur /= 255.0;

				if(sa[x] && !is_space)
					{
					if(sc[x] > 5)
						{
						dir = (double)rgb_palette[pa -> pattern][0];
						dig = (double)rgb_palette[pa -> pattern][1];
						dib = (double)rgb_palette[pa -> pattern][2];

						dir *= dblur;
						dig *= dblur;
						dib *= dblur;

						/* transparency */
						dir *= dmti_p;
						dig *= dmti_p;
						dib *= dmti_p;

						dor *= dmto_p;
						dog *= dmto_p;
						dob *= dmto_p;

						}
					else /* emphasis1 */
						{
						dir = (double)rgb_palette[pa -> emphasis1][0];
						dig = (double)rgb_palette[pa -> emphasis1][1];
						dib = (double)rgb_palette[pa -> emphasis1][2];

						/* transparency */
						dir *= dmti_e1;
						dig *= dmti_e1;
						dib *= dmti_e1;

						dor *= dmto_e1;
						dog *= dmto_e1;
						dob *= dmto_e1;

						}
					} /* end if sc[x] */
				else /* emphasis2 */
					{
					/* get new part */
					dir = (double)rgb_palette[pa -> emphasis2][0];
					dig = (double)rgb_palette[pa -> emphasis2][1];
					dib = (double)rgb_palette[pa -> emphasis2][2];

					/* transparency */
					dir *= dmti_e2;
					dig *= dmti_e2;
					dib *= dmti_e2;

					dor *= dmto_e2;
					dog *= dmto_e2;
					dob *= dmto_e2;
					}

				/* combine old and new parts in output */
				dst[0] = (int) (dob + dib);
				dst[1] = (int) (dog + dig);
				dst[2] = (int) (dor + dir);
				} /* end if DVD like subs */

			} /* end for all x */

		sc += stride;
		sa += stride;

		} /* end for all y */

	} /* end if RGB */
else if(vob->im_v_codec == TC_CODEC_YUV420P)
	{
	/*
	We seem to be in this format I420:
    y = dest;
    v = dest + width * height;
    u = dest + width * height * 5 / 4;

	Orientation of Y (*) relative to chroma U and V (o)
	* o
	o o
	So, an array of 2x2 chroma pixels exists for each luminance pixel
	The consequence of this is that there will be a color-less area
	of one line on the right and on the bottom of each character.
	Dropshadow :-)
	*/

	py = ImageData;
	pv = ImageData + image_width * image_height;
	pu = ImageData + (image_width * image_height * 5) / 4;

	a = y0 * image_width;
	b = image_width / 4;
	c = image_width / 2;

	py += x0 + a;
	a /= 4;

	pu += (x0 / 2) + a;
	pv += (x0 / 2) + a;

	/* on odd lines, need to go a quarter of a 'line' back */
	if(y0 % 2)
		{
		pu -= b;
		pv -= b;
		}

	for(y = 0; y < h; y++)
		{
		for(x = 0; x < w; x++)
			{

			/* clip right scroll */
			if( (x + x0) > image_width - 1) continue;

			/* clip left scroll */
			if( (x + x0 ) < 0) continue;

			/* clip top scroll */
			if( (y + y0) > image_height - 1) continue;

			/* clip bottom scroll */
			if( (y + y0) < 0) continue;

			if(! rgb_palette_valid_flag)
				{
				if(sa[x] && !is_space)
					{
					/* trailing shadow no */
					sx = 1;
					if( (x + x0) % 2) sx = 0;

//					if(x  < (w - 4) ) sx = 1; // hack, looks better :-)
//					else sx = 0;

					/* some temp vars */
					uy = py[x];
					ua = sa[x];
					uc = sc[x];

					/* get decision factor before we change anything */
//					cd = ( (py[x] * sa[x]) >> 8) < 5;
					cd = ( (uy * ua) >> 8) < 5;

					/* get original */
					doy = (double) py[x];
					dou = (double) (pu[x / 2 + sx] - 128);
					dov = (double) (pv[x / 2 + sx] - 128);

					/* blur factor y * sa[x] */
					dblur = (double) ( ((int)doy * ua) >> 8) + uc;
					dblur /= 255.0;

					/* calculate value insert (character) */
					diy = (double) ( (uy * ua) >> 8) + uc;

					/* transparency */
					diy *= dmti;
					doy *= dmto;

					/* add what is left of the insert (character) */
					py[x] = (int) (doy + diy);

					if(cd)
						{
						diu *= dblur;
						div *= dblur;

						/* change color too */
						diu = u * dmti;
						div = v * dmti;

						dou *= dmto;
						dov *= dmto;

						if(sc[x]) /* white part of char */
							{
							/* set U vector */
							pu[x / 2 + sx] = (int) (128.0 + dou + diu);

							/* set V vector */
							pv[x / 2 + sx] = (int) (128.0 + dov + div);
							}
						else /* keep border around character colorless */
							{
							/* set U vector */
							pu[x / 2 + sx] = (int) (128.0 + dou); // + diu);

							/* set V vector */
							pv[x / 2 + sx] = (int) (128.0 + dov); // + div);
							}

						} /* end if sa[x] */
					} /* end if cd */

				} /* end if ! rgb_palette_valid_flag */
			else
				{
				/*
				OK lets get this straight:
				I do not understand character anti-aliasing, but looked at the variables with
				printf().
				Here I see that:
				sa[x] > 0 for character space, it varies from 255 to 0
				sc[x] > 0 for character body (pattern), it varies from 0 to 255;

				sa[x] seems to be the aliasing or blur? multiplier, and is used to attenuate
                the original! Not my idea of anti alias, but OK.
				sc[x] is the multiplier for the insert (character).

				sa[x] is 1 within the body of a character.
				sa[x] increases as you go from center character outwards (fade in background)
				sa[x] is zero outside the character space.


				so:
				sa[x] = 0 is not in character space.
				sa[x] = 1 is character body
				sa[x] > 1 is outline1


				Variable names used:
				dxxxxo for double (format) referring to original data
				dxxxxi for double (format) referring to insert data

				*/

				/*
				test for in character space, and something to print
				These character sets seem to have '_' in place of a real space, so that is why is_space,
				to prevent a '_' from appearing where a space is intended.
				*/

				/* some color trick */
				sx = 1;
				if( (x + x0) % 2) sx = 0;

				/* trailing shadow no */
//				if(x  < (w - 4) ) sx = 1; // hack, looks better :-)
//				else sx = 0;

				if(sa[x] && !is_space)
					{

					/* get multiplier as double */
					dmulto = (double) sa[x] / 256.0;

					/* multiplier to range 0 to 1.0 */
					dmulti = (double) sc[x] / 256.0;

					if(use_emphasis2_for_anti_aliasing_flag)
						{
						/* multiplier to range 0 to 1.0 */
						dmulti = (double) sc[x] / 256.0;
						}
					else
						{
						if(sc[x]) dmulti = 1.0;
						else dmulti = 0.0;
						}

					/* test for character body */
					if(dmulti > .5)
						{
						/* get original */
						doy = (double) py[x];
						dou = (double) pu[x / 2 + sx] - 128;
						dov = (double) pv[x / 2 + sx] - 128;

						rgb_to_yuv(\
						rgb_palette[pa -> pattern][0],\
						rgb_palette[pa -> pattern][1],\
						rgb_palette[pa -> pattern][2],\
						&iy, &iu, &iv);

						/*
						better to multiply AFTER rgb_to_uuv(),
						as strange values may happen for u and v if low values of rgb.
						*/

						/* NOTE: what we call 'contrast' in DVD is actually transparency */

						/* insert to double */
						diy = (double) iy;
						diu = (double) iu;
						div = (double) iv;

						/* transparency */
						diy *= dmti_p;
/* u and v here for color change */
						diu *= dmti_p;
						div *= dmti_p;

						doy *= dmto_p;
						dou *= dmto_p;
						dov *= dmto_p;

						da = (doy * dmulto) + (diy * dmulti);
						py[x] = (int) da;

						/* set U vector */
						da = (dou * dmulto) + (diu * dmulti);
						pu[x / 2 + sx] = 128 + (int)da;

						/* set V vector */
						da = (dov * dmulto) + (div * dmulti);
						pv[x / 2 + sx] = 128 + (int)da;
						} /* end if body */
					else
						{
						if(use_emphasis2_for_anti_aliasing_flag)
							{
							/* use outline2 for anti aliasing, set to grey 50 % */
                            if( (dmulti > 0) && (dmulti < .5) )
								{
								/* get original */
								doy = (double) py[x];
								dou = (double) pu[x / 2 + sx] - 128;
								dov = (double) pv[x / 2 + sx] - 128;

								/* draw outline2 */
								rgb_to_yuv(\
								rgb_palette[pa -> emphasis2][0],\
								rgb_palette[pa -> emphasis2][1],\
								rgb_palette[pa -> emphasis2][2],\
								&iy, &iu, &iv);

								/* insert to double */
								diy = (double) iy;
								diu = (double) iu;
								div = (double) iv;

								/* transparency */
								diy *= dmti_e2;
								diu *= dmti_e2;
								div *= dmti_e2;

								doy *= dmto_e2;
								dou *= dmto_e2;
								dov *= dmto_e2;

								/* add what is left of the insert (character) */
								py[x] = (int) (doy + diy);

								/* set U vector */
								pu[x / 2 + sx] = 128 + (int) (dou + diu);

								/* set V vector */
								pv[x / 2 + sx] = 128 + (int) (dov + div);
								} /* end emphasis2 for anti-aliasing */
							else
								{
								/* get original */
								doy = (double) py[x];
								dou = (double) pu[x / 2 + sx] - 128;
								dov = (double) pv[x / 2 + sx] - 128;

								rgb_to_yuv(\
								rgb_palette[pa -> emphasis1][0],\
								rgb_palette[pa -> emphasis1][1],\
								rgb_palette[pa -> emphasis1][2],\
								&iy, &iu, &iv);

								/* insert to double */
								diy = (double) iy;
								diu = (double) iu;
								div = (double) iv;

								/* transparency */
								diy *= dmti_e1;
								diu *= dmti_e1;
								div *= dmti_e1;

								doy *= dmto_e1;
								dou *= dmto_e1;
								dov *= dmto_e1;

								da = doy + diy;
								py[x] = (int) da;

								/* set U vector */
								da = dou  + diu;
								pu[x / 2 + sx] = 128 + (int)da;

								/* set V vector */
								da = dov + div;
								pv[x / 2 + sx] = 128 + (int)da;
								} /* end emphasis1 */
							} /* end if use_emphasis2_for_anti_aliasing_flag */
						else /* outline1 */
							{
							/* get original */
							doy = (double) py[x];
							dou = (double) pu[x / 2 + sx] - 128;
							dov = (double) pv[x / 2 + sx] - 128;

							rgb_to_yuv(\
							rgb_palette[pa -> emphasis1][0],\
							rgb_palette[pa -> emphasis1][1],\
							rgb_palette[pa -> emphasis1][2],\
							&iy, &iu, &iv);

							/* insert to double */
							diy = (double) iy;
							diu = (double) iu;
							div = (double) iv;

							/* transparency */
							diy *= dmti_e1;
							diu *= dmti_e1;
							div *= dmti_e1;

							doy *= dmto_e1;
							dou *= dmto_e1;
							dov *= dmto_e1;

							da = doy + diy;
							py[x] = (int) da;

							/* set U vector */
							da = dou  + diu;
							pu[x / 2 + sx] = 128 + (int)da;

							/* set V vector */
							da = dov + div;
							pv[x / 2 + sx] = 128 + (int)da;
							} /* end no anti-alias */
						} /* end dmulti < 0.5 */
					} /* end if sa[x] && ! is_space */
				else /* outline2 */
					{
					if(! use_emphasis2_for_anti_aliasing_flag ) /* use emphasis2 for  character space */
						{
						/* get original */
						doy = (double) py[x];
						dou = (double) pu[x / 2 + sx] - 128;
						dov = (double) pv[x / 2 + sx] - 128;

						/* draw outline2 */
						rgb_to_yuv(\
						rgb_palette[pa -> emphasis2][0],\
						rgb_palette[pa -> emphasis2][1],\
						rgb_palette[pa -> emphasis2][2],\
						&iy, &iu, &iv);

						/* insert to double */
						diy = (double) iy;
						diu = (double) iu;
						div = (double) iv;

						/* transparency */
						diy *= dmti_e2;
						diu *= dmti_e2;
						div *= dmti_e2;

						doy *= dmto_e2;
						dou *= dmto_e2;
						dov *= dmto_e2;

						/* add what is left of the insert (character) */
						py[x] = (int) (doy + diy);

						/* set U vector */
						pu[x / 2 + sx] = 128 + (int) (dou + diu);

						/* set V vector */
						pv[x / 2 + sx] = 128 + (int) (dov + div);
						} /* end if outline2 */
					} /* end if ! use_emphasis2_for_anti_aliasing_flag */
				} /* end if rgb_palette_valid_flag */
			} /* end for all x */

		sc += stride;
		sa += stride;

		py += image_width;

		if( (y + y0) % 2)
			{
			pu += c;
			pv += c;
			}

		} /* end for all y */

	} /* end if YUV */
} /* end function draw_alpha */


int add_background(struct object *pa)
{
int a, b, c, x, y, sx;
uint8_t *py, *pu, *pv;
double da;
int iu, iv;
double dr, dg, db;
int iy;
double dmci, dmti, dmto;
double dir, dig, dib, diy, diu, div;
double dor, dog, dob, doy, dou, dov;
unsigned char *dst;
int width, height;
double opaqueness;

if(debug_flag)
	{
	tc_log_info(MOD_NAME, "add_background(): arg pa=%p", pa);

	tc_log_info(MOD_NAME,\
	"pa->line_number=%d pa->bg_y_start=%d pa->bg_y_end=%d pa->bg_x_start=%d pa->bg_x_end=%d",\
	pa -> line_number, pa -> bg_y_start, pa -> bg_y_end, pa -> bg_x_start, pa -> bg_x_end);

	tc_log_info(MOD_NAME,\
	"pa->background=%d pa->background_contrast=%d",\
	pa -> background, pa -> background_contrast);

	tc_log_info(MOD_NAME,\
	"pa->contrast=%.2f, pa->transparency=%.2f",\
	pa -> contrast, pa -> transparency);
	}

/* only background if palette specified */
if(! rgb_palette_valid_flag) return 1;

/* parameter check */
if(pa -> bg_y_start < 0) return 0;
if(pa -> bg_y_start > image_height - 1) return 0;

if(pa -> bg_x_start < 0) return 0;
if(pa -> bg_x_start > image_width - 1) return 0;

if(pa -> bg_y_end < pa -> bg_y_start) return 0;
if(pa -> bg_y_end > image_height - 1) return 0;

if(pa -> bg_x_end < pa -> bg_x_start) return 0;
if(pa -> bg_x_end > image_width - 1) return 0;

/* calculate 'visibility' insert */
da = (double) pa -> background_contrast / 15.0; // DVD background request, 1.0 for 100 % opaque
db = (1.0 - (double)pa -> transparency / 100.0); // subtitler background request, 1.0 for 100 % opaque
opaqueness = da * db;

/* combine subtitler and DVD transparency */
dmto = 1.0 - opaqueness; // background, 1.0 for 100 % transparent
dmti = 1.0 - dmto; // insert, 0.0 for 100 % transparent

/*
do not multiply color (saturation) with contrast,
saturation could be done in adjust color, but done here for speed
*/
dmci = (pa -> contrast / 100.0); // contrast insert, 1.0 for 100 % contrast

dmti *= dmci;

if(vob->im_v_codec == TC_CODEC_RGB24)
	{
	a = 3 * (image_height * image_width); // size of a picture

	for(y = pa -> bg_y_start; y < pa -> bg_y_end; y++)
		{
		b = 3 * (y * image_width);

		for(x = pa -> bg_x_start; x < pa -> bg_x_end; x++)
			{
			c = 3 * (image_width - x);

			dst = ImageData + a - (b + c);

			/* get original */
			dob = (double)dst[0];
			dog = (double)dst[1];
			dor = (double)dst[2];

			/* get new part */
			dir = (double)rgb_palette[pa -> background][0];
			dig = (double)rgb_palette[pa -> background][1];
			dib = (double)rgb_palette[pa -> background][2];

			/* transparency */
			dir *= dmti;
			dig *= dmti;
			dib *= dmti;

			dor *= dmto;
			dog *= dmto;
			dob *= dmto;

			/* combine old and new parts in output */
			dst[0] = (int) (dib + dob);
			dst[1] = (int) (dig + dog);
			dst[2] = (int) (dir + dor);

			} /* end for all x */

		} /* end for all y */

	} /* end if RGB */
else if(vob->im_v_codec == TC_CODEC_YUV420P)
	{
	/*
	We seem to be in this format I420:
    y = dest;
    v = dest + width * height;
    u = dest + width * height * 5 / 4;

	Orientation of Y (*) relative to chroma U and V (o)
	* o
	o o
	So, an array of 2x2 chroma pixels exists for each luminance pixel
	The consequence of this is that there will be a color-less area
	of one line on the right and on the bottom of each character.
	Dropshadow :-)
	*/

	height = pa -> bg_y_end - pa -> bg_y_start;
	width = pa -> bg_x_end - pa -> bg_x_start;

	py = ImageData;
	pv = ImageData + image_width * image_height;
	pu = ImageData + (image_width * image_height * 5) / 4;

	a = pa -> bg_y_start * image_width;
	b = image_width / 4;
	c = image_width / 2;

	py += pa -> bg_x_start + a;
	a /= 4;

	pu += (pa -> bg_x_start / 2) + a;
	pv += (pa -> bg_x_start / 2) + a;

	/* on odd lines, need to go a quarter of a 'line' back */
	if(pa -> bg_y_start % 2)
		{
		pu -= b;
		pv -= b;
		}

	for(y = 0; y < height; y++)
		{
		for(x = 0; x < width; x++)
			{
			sx = 1;
			if( (x + pa -> bg_x_start) % 2) sx = 0;

			/* get old part */
			doy = (double)py[x];
			dou = (double)pu[x / 2 + sx] - 128;
			dov = (double)pv[x / 2 + sx] - 128;

			/* get new part */
			dr = (double)rgb_palette[pa -> background][0];
			dg = (double)rgb_palette[pa -> background][1];
			db = (double)rgb_palette[pa -> background][2];

			rgb_to_yuv(dr, dg, db, &iy, &iu, &iv);
			/*
			better to multiply AFTER rgb_to_uuv(),
			as strange values may happen for u and v if low values of rgb.
			*/

			diy = (double)iy;
			diu = (double)iu;
			div = (double)iv;

			/* transparency */
			diy *= dmti;
			diu *= dmti;
			div *= dmti;

			doy *= dmto;
			dou *= dmto;
			dov *= dmto;

			/* add what is left of the insert (character) */
			py[x] = (int) (doy + diy);

			/* set U vector */
			pu[x / 2 + sx] = 128 + (int) (dou + diu);

			/* set V vector */
			pv[x / 2 + sx] = 128 + (int) (dov + div);

			} /* end for all x */

		py += image_width;

		if( (y + pa -> bg_y_start) % 2)
			{
			pu += c;
			pv += c;
			}

		} /* end for all y */

	} /* end if YUV */

return 1;
} /* end function add_background */


int print_options(void)
{
if(debug_flag)
	{
	tc_log_info(MOD_NAME, "print options(): arg none");
	}
/*
From transcode -0.5.1 ChangeLog:
Example: -J my_filter="fonts=3 position=55 -v"
*/

 tc_log_info(MOD_NAME, "(%s) help\n"
"Usage -J subtitler=\"[no_objects] [subtitle_file=s]\n\
[color_depth=n]\n\
[font_dir=s] [font=n] [font_factor=f\n\
[frame_offset=n]\n\
[debug] [help] [use_pre_processing]\"\n\
\n\
f is float, h is hex, n is integer, s is string.\n\
\n\
no_objects           disables subtitles and other objects (off).\n\
color_depth=         32 or 24 (overrides X auto) (32).\n\
font=                0 or 1, 1 gives strange symbols... (0).\n\
font_dir=            place where font.desc is (%s).\n\
font_factor=         .1 to 100 outline characters (10.75).\n\
frame_offset=        positive (text later) or negative (earlier) integer (0).\n\
subtitle_file=       pathfilename.ppml location of ppml file (%s).\n\
debug                prints debug messages (off).\n\
help                 prints this list and exits.\n\
use_pre_processing   uses pre_processing.\n",
MOD_CAP, default_font_dir, subtitle_file);

return 1;
} /* end function print_options */


int add_picture(struct object *pa)
{
/*
reads yuyv in pa -> data into the YUV 420 ImageData buffer.
*/
uint8_t *py, *pu, *pv;
int a, b, c, x, y;
char *ps;
char ca;
int u_time;
int in_range;
double dc, dd, dm, ds;
int ck_flag = 0;
int odd_line;

if(debug_flag)
	{
	tc_log_info(MOD_NAME, "add_picture(): arg pa=%lu\
	pa->xsize=%.2f pa->ysize=%.2f pa->ck_color=%.2f",\
	(unsigned long)pa,\
	pa -> xsize, pa -> ysize,\
	pa -> chroma_key_color);
	}

/* argument check */
if(! ImageData) return 0;
if(! pa) return 0;
if( (int)pa -> xsize == 0) return 1;
if( (int)pa -> ysize == 0) return 1;

/* calculate multiplier for transparency ouside loops */
dm = (100.0 - pa -> transparency) / 100.0;
dd = 1.0 - dm;

dc = dm * (pa -> contrast / 100.0);
ds = (pa -> saturation / 100.0);

/* saturation could be done in adjust color, but done here for speed */
//ds = 1.0;

if(vob->im_v_codec == TC_CODEC_RGB24)
	{
	/* ImageData, image_width, image_height */

	tc_log_error(MOD_NAME, \
	"subtitler ONLY works with YUV 420");

	return(-1);
	} /* end if RGB */
else if(vob->im_v_codec == TC_CODEC_YUV420P)
	{
	b = image_width / 4;
	c = image_width / 2;

	py = ImageData;
	pu = ImageData + (image_width * image_height * 5) / 4;
	pv = ImageData + (image_width * image_height);

	a = (int)pa -> ypos * image_width;
	py += (int)pa -> xpos + a;
	a /= 4;
	pu += ( (int)pa -> xpos / 2) + a;
	pv += ( (int)pa -> xpos / 2) + a;

	ps = pa -> data;

	if( (int)pa -> ypos % 2 )
		{
		pu -= b;
		pv -= b;
		}

	// reading sequence is YUYV, so U is first.
	u_time = 1;
	for(y = 0; y < (int)pa -> ysize; y++)
		{
		odd_line = (y + (int)pa -> ypos) % 2;

		for(x = 0; x < (int)pa -> xsize; x++)
			{
			/* find out if OK to display */
			in_range = 1;
			/* clip right scroll */
			if( (x + (int)pa -> xpos) > image_width) in_range = 0;

			/* clip left scroll */
			if( (x + (int)pa -> xpos ) < 0) in_range = 0;

			/* clip top scroll */
			if( (y + (int)pa -> ypos) > image_height) in_range = 0;

			/* clip bottom scroll */
			if( (y + (int)pa -> ypos) < 0) in_range = 0;

			/* slice level */
			a = *ps;
			if(a < 0) a += 256;
			if( a < ( (int)pa -> slice_level) ) in_range = 0;

			if(\
			(pa -> zrotation != 0) ||\
			(pa -> xshear != 0) || (pa -> yshear != 0)\
			)
				{
				/*
				for rotate and shear, the luminance value of the border
				to cut away.
				Since this would remove picture data, for this not to
				happen, we add 1 step to the luminance if it happens to
				be the same as border_luminanc in yuv_to_ppm().
				With this trick it is guaranteed border_luminance never happens
				in the .ppm file that mogrify processes.
				*/
				if(pa -> mask_level)
					{
					if(a == pa -> mask_level) in_range = 0;
					}
				else
					{
					if(a == default_border_luminance) in_range = 0;
					}
				} /* end if rotate or shear */

			/* test for chroma key match if color specified */
			if(pa -> chroma_key_saturation)
				{
				if(u_time)
					{
					if(! odd_line)
						{
						a = (int)pu[x / 2] - 128;
						b = (int)pv[x / 2] - 128;
						ck_flag =\
						chroma_key(\
						a, b,\
						pa -> chroma_key_color,\
						pa -> chroma_key_window,\
						pa -> chroma_key_saturation);
						} /* end if even line */
					else
						{
						a = (int)pu[(x / 2) + c] - 128;
						b = (int)pv[(x / 2) + c] - 128;
						ck_flag =\
						chroma_key(\
						a, b,\
						pa -> chroma_key_color,\
						pa -> chroma_key_window,\
						pa -> chroma_key_saturation);
						} /* end if odd line */
					} /* end if u_time */

				/* transport to next time here ! */
				if(! ck_flag) in_range = 0;
				} /* end if chroma key */

			if(in_range)
				{
				py[x] *= dd;
				py[x] += dc * (uint8_t)*ps;
				} /* end if in_range */

			ps++;

			if(in_range)
				{
				if(u_time)
					{
					ca = *ps;
					ca = 128 + ( ( (uint8_t)*ps - 128 ) * ds);

					pu[x / 2] *= dd;
					pu[x / 2] += dm * (uint8_t)ca;
					}
				else
					{
					ca = *ps;
					ca = 128 + ( ( (uint8_t)*ps - 128 ) * ds);

					pv[x / 2] *= dd;
					pv[x / 2] += dm * (uint8_t)ca;
					}

				/* apply hue correction if both U and V set */

//				if(! u_time)
					{
					if(pa -> hue)
						{
						/*
						hue,
						saturation done outside adjust_color() for speed
						*/

						a = (int)pu[x / 2] - 128;
						b = (int)pv[x / 2] - 128;
						adjust_color(&a, &b, pa -> hue, 100.0);
						pu[x / 2] = (uint8_t)a + 128;
						pv[x / 2] = (uint8_t)b + 128;

						} /* end if hue */

					} /* end if ! u_time */
				} /* end if in range */

			ps++;
			u_time = 1 - u_time;

			} /* end for all x */

		if( (int) pa -> xsize % 2) u_time = 1 - u_time;

		py += image_width;

		if(odd_line)
			{
			pu += c;
			pv += c;
			}

		} /* end for all y */

	} /* end if YUV 420 */

return 1;
}/* end function add_picture */


int set_main_movie_properties(struct object *pa)
{
if(debug_flag)
	{
	tc_log_info(MOD_NAME, "set_main_movie_properties(): arg pa=%lu", (unsigned long)pa);
	}

if(! pa) return 0;

dcontrast = pa -> contrast;
brightness = (int)pa -> brightness;
dsaturation = pa -> saturation;
dhue = pa -> hue;
dhue_line_drift = pa -> hue_line_drift;
u_shift = (int)pa -> u_shift;
v_shift = (int)pa -> v_shift;
de_stripe_flag = (int)pa -> de_stripe;
show_output_flag = (int)pa -> show_output;

return 1;
} /* end function set_main_movie_properties */


int rgb_to_yuv(int r, int g, int b, int *y, int *u, int *v)
{
double dr, dg, db, dy, du, dv;

if(debug_flag)
	{
	tc_log_info(MOD_NAME, "rgb_to_yuv(): arg r=%d g=%d b=%d", r, g, b);
	}

dr = (double) r;
dg = (double) g;
db = (double) b;

/* acy, acu, acv pre-calculated in init */
/* convert to YUV */

/* test yuv coding here */
dy = acr * dr + acg * dg + acb * db;

dy = (219.0 / 256.0) * dy + 16.5;  /* nominal range: 16..235 */

du = acu * (db - dy);
du = (224.0 / 256.0) * du; // + 128.5; /* 16..240 */

dv = acv * (dr - dy);
dv = (224.0 / 256.0) * dv; // + 128.5; /* 16..240 */

*y = (int) dy;
*u = (int) du;
*v = (int) dv;

return 1;
} /* end function rgb_to_yuv */

