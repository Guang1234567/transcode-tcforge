/*
 * Copyright (C) Jan Panteltje 2003
 *
 * This file is part of transcode, a video stream processing tool
 *
 * Transcode is copyright Thomas Oestreich
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 */

#ifndef _SUBTITLER_H_
#define _SUBTITLER_H_

#define MOD_NAME    "filter_subtitler.so"
#define MOD_VERSION "v0.8.1 (2003/10/25)"
#define MOD_CAP     "subtitle filter"
#define MOD_AUTHOR  "Jan Panteltje"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include <pwd.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>

#include "load_font.h"
#include "frame_list.h"
#include "object_list.h"


/* maximum movie length in frames */
#define MAX_FRAMES	300000

/* set some limits for this system */
#define MAX_H_PIXELS		2048
#define MAX_V_PIXELS		2048
#define MAX_SCREEN_LINES	200

/* temp string sizes */
#define READSIZE	65535
#define TEMP_SIZE	65535

/* for status in frame browser USE 1 2 4 8 */
#define NEW_ENTRY		0
#define NO_SPACE		1
#define TOO_LONG		2
#define TXT_HOLD		4

/* for object type IDs */
#define FORMATTED_TEXT			1
#define X_Y_Z_T_TEXT			2
#define X_Y_Z_T_PICTURE			3
#define X_Y_Z_T_FRAME_COUNTER	4
#define X_Y_Z_T_MOVIE			5
#define MAIN_MOVIE				6
#define SUBTITLE_CONTROL		7

/* for formatting subtitles */
#define SUBTITLE_H_FACTOR	.02
#define SUBTITLE_V_FACTOR	.042

/* for this specfic default font */
#define EXTRA_CHAR_SPACE	1

/*
for masking out areas in rotate and shear.
These 2 values are related, and I have not figured out the relation yet.
YUV_MASK is used to prevent picture areas to be cut out.
*/
#define LUMINANCE_MASK	178
#define YUV_MASK		164

/* status of operations on an object, use 1, 2, 4, etc. */
#define OBJECT_STATUS_NEW			0
#define OBJECT_STATUS_INIT			1
#define OBJECT_STATUS_GOTO			2
#define OBJECT_STATUS_HAVE_X_DEST	4
#define OBJECT_STATUS_HAVE_Y_DEST	8
#define OBJECT_STATUS_HAVE_Z_DEST	16

/* maximum number of movie objects that can be inserted */
#define MAX_MOVIES	1024

#define SUBTITLER_VERSION "-0.8.4"

extern int debug_flag;
extern font_desc_t *vo_font;
extern font_desc_t *subtitle_current_font_descriptor;
extern uint8_t *ImageData;
extern int image_width, image_height;
// extern int default_font;
// extern struct passwd *userinfo;
extern char *home_dir;
// extern char *user_name;
// int sync_mode;
// int osd_transp;
extern int screen_start[MAX_H_PIXELS];
extern char *tptr;
// extern int screen_lines;
// extern char screen_text[MAX_SCREEN_LINES][MAX_H_PIXELS];
// char format_start_str[50];
// char format_end_str[50];
// int vert_position;
// extern int line_height;
extern int line_h_start, line_h_end;
extern int center_flag;
// int wtop, wbottom, hstart, hend;
// extern int window_top;
extern int window_bottom;
// extern char *frame_memory0, *frame_memory1 ;
// int file_format;
// extern char *subtitle_file;
// extern char *default_font_dir;
// extern vob_t *vob;
// char *selected_data_directory;
// char *selected_project;
extern int frame_offset;
extern double dmax_vector;
// extern int use_pre_processing_flag;

// extern char *subtitle_font_path;
extern char *default_subtitle_font_name;
extern int default_subtitle_symbols;
extern int default_subtitle_font_size;
extern int default_subtitle_iso_extention;
extern double default_subtitle_radius;
extern double default_subtitle_thickness;

/* for x11 stuff */
// extern int show_output_flag;
// extern int window_open_flag;
// extern int window_size;
// int buffer_size;
// extern unsigned char *ucptrs, *ucptrd;
// extern int color_depth;
/* end x11 stuff */

/* threads for other instances of transcode in insert movie */
// extern pthread_t *movie_thread[MAX_MOVIES];
// pthread_attr_t *attributes;

/* global subtitle parameters */
// double ssat, dssat, scol, dscol;
// extern double default_font_factor;
extern double subtitle_extra_character_space;

// int anti_alias_flag;
// int subtitle_anti_alias_flag;

/* for rotate and shear, the luminance where we cut out the border */
extern int border_luminance;
extern int default_border_luminance;

extern double subtitle_h_factor;
extern double subtitle_v_factor;
extern double extra_character_space;

extern int rgb_palette[16][3]; // rgb
extern int rgb_palette_valid_flag;

extern int default_subtitle_font_symbols;

/* this last, so proto knows about structure definitions etc. */
#include "subtitler_proto.h"

#endif /* _SUBTITLER_H_ */

