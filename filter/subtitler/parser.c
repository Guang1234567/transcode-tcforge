#include "subtitler.h"

int end_frame_nr;
int screen_start[MAX_H_PIXELS];
char *tptr;
int screen_lines;
char screen_text[MAX_SCREEN_LINES][MAX_H_PIXELS];
int line_height;
int window_top, window_bottom;

int parse_frame_entry(struct frame *pa)
{
int a, c, i, j, x, y, z;
char *token, *running;
struct frame *pb = 0;
struct object *po = 0;
double da, dx, dy;
double dgx, dgy, dgz;
int frame_nr;
char *cptr, *tptr;
int screen_lines;
int line_height;
char font_dir[4096];
char font_name[4096];
font_desc_t *pfd;
int temp_palette[16][3];
int text_start, max_width, line_len;
struct object *pf = 0;
struct object *pc;
int bg_height, bg_width;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME,
	"subtitler(): parse_frame_entry():\n\
	pa->name=%s pa->type=%d\n\
	pa->end_frame=%d\n\
	pa->data=%lu",
	pa -> name,
	pa -> type,
	pa -> end_frame,
	(unsigned long)pa -> data);
	}

if(pa -> data[0] == '*')
	{
	/* parse the data line */
	running = strsave(pa -> data);
	if(! running)
		{
		tc_log_warn(MOD_NAME, "subtitler(): strsave(pa -> data) failed");

		return -1;
		}

	po = 0;
	while(1)
		{
		token = strsep (&running, " ");
		if(token == NULL) break;

		if(debug_flag)
			{
			tc_log_msg(MOD_NAME, "token=%s", token);
			}

		/* avoid empty string */
		if(token[0] == 0) continue;

		/* check for object reference */
		if(token[0] == '*')
			{
			/* recursive we are in our own list */
			pb = lookup_frame(token);
			if(! pb)
				{
				tc_log_msg(MOD_NAME,
				"subtitler(): undefined object referenced: %s ignoring",
				token);

				return 1;
//				exit(1);
				}

			/* get data for this object */
			if(debug_flag)
				{
//				tc_log_msg(MOD_NAME, "parser(): object %s data=%s", token, pb -> data);
				}

			/*
			add this object to the display list, if it is already there,
			we get a pointer to it.
			*/
			po = install_object_at_end_of_list(token);
			if(! po)
				{
				tc_log_msg(MOD_NAME,
				"subtitler(): parse_frame_entry():\n\
				could not install or find object %s in display list",\
				token);

				exit(1);
				}
			}
		else /* token[0] != 0, must be an argument */
			{
			/* modify arguments for display object */
			/*
			po must have been set in the preceeding argument in this
			line, so '11 *this vpos=5', but '11 vops=5 *this' will NOT work,
			and cause an error exit.
			*/
			if(! po)
				{
				tc_log_msg(MOD_NAME,
		"subtitler(): syntax error (object must be first), line reads:\n\
				%s", pa -> name);

				exit(1);
				}
			/* copy data if not there yet */
			if(! po -> data)
				{
				po -> type = pb -> type;
				po -> data = pb -> data;
				po -> org_xsize = (double) pb -> xsize;
				po -> org_ysize = (double) pb -> ysize;
				po -> org_zsize = (double) pb -> zsize;
				po -> xsize = (double) pb -> xsize;
				po -> ysize = (double) pb -> ysize;
				po -> zsize = (double) pb -> zsize;
				po -> id = pb -> id;

				po -> pfd = pb -> pfd;

				pa -> status = OBJECT_STATUS_NEW;

				/* set some defaults */
				po -> extra_character_space = extra_character_space;

				}

			/* parse line */
			sscanf(token, "de_stripe=%lf", &po -> de_stripe);
			sscanf(token, "show_output=%lf", &po -> show_output);

			sscanf(token, "xpos=%lf", &po -> xpos);
			sscanf(token, "ypos=%lf", &po -> ypos);
			sscanf(token, "zpos=%lf", &po -> zpos);

			sscanf(token, "dxpos=%lf", &po -> dxpos);
			sscanf(token, "dypos=%lf", &po -> dypos);
			sscanf(token, "dzpos=%lf", &po -> dzpos);

			a = sscanf(token, "xdest=%lf", &dgx);
			if(a) po -> status |= OBJECT_STATUS_HAVE_X_DEST;

			a = sscanf(token, "ydest=%lf", &dgy);
			if(a) po -> status |= OBJECT_STATUS_HAVE_Y_DEST;

			a = sscanf(token, "zdest=%lf", &dgz);
			if(a) po -> status |= OBJECT_STATUS_HAVE_Z_DEST;

			if(\
			(po -> status & OBJECT_STATUS_HAVE_X_DEST) ||\
			(po -> status & OBJECT_STATUS_HAVE_Y_DEST) ||\
			(po -> status & OBJECT_STATUS_HAVE_Z_DEST)\
			)
				{
				/* use current position if nothing specified */
				if(po -> status & OBJECT_STATUS_HAVE_X_DEST)
					{
					po -> xdest = dgx;
					}
				else
					{
					po -> xdest = po -> xpos;
					}

				if(po -> status & OBJECT_STATUS_HAVE_Y_DEST)
					{
					po -> ydest = dgy;
					}
				else
					{
					po -> ydest = po -> ypos;
					}

				if(po -> status & OBJECT_STATUS_HAVE_Z_DEST)
					{
					po -> zdest = dgz;
					}
				else
					{
					po -> zdest = po -> zpos;
					}

				/* calculate a distance */
				/* x distance */
				dx = po -> xdest - po -> xpos;

				/*
				since the 'heading' calculation in object_list.c
				uses aspect correction, we have to to pre-correct here
				the other way around.
				*/
				dx *=\
				(double)image_height / (double)image_width;

				/* y distance */
				dy = po -> ydest - po -> ypos;

				/* true distance */
				po -> distance = sqrt( (dx * dx) + (dy * dy) );

				/* sine */
				da = dx / po -> distance;

				/* calculate a heading angle */
				errno = 0;
				po -> heading = asin(da);
				if(errno == EDOM)
					{
					tc_log_perror(MOD_NAME, "subtitler(): parse_frame_entry():\n\
					asin NOT A NUMBER :-)");

					exit(1);
					}

				/* flip y, x=0, y=0 is top left */
				dy = -1 * dy;

				/* if dy is negative, we move to the other 2 quadrants */
				if(dy < 0) po -> heading = M_PI - po -> heading;

				po -> heading *= 180.0 / M_PI;

				/* indicate we are on our way */
				po -> status |= OBJECT_STATUS_GOTO;

				/*
				in object list we will now each frame substract the
                travelled distance, until po -> distance is 0.
				Then we will reset the mode to NEW.
				*/
				} /* end if some x, y, or z dest */

			sscanf(token, "xrot=%lf", &po -> xrotation);
			sscanf(token, "yrot=%lf", &po -> yrotation);
			sscanf(token, "zrot=%lf", &po -> zrotation);

			sscanf(token, "dxrot=%lf", &po -> dxrotation);
			sscanf(token, "dyrot=%lf", &po -> dyrotation);
			sscanf(token, "dzrot=%lf", &po -> dzrotation);

			sscanf(token, "xshear=%lf", &po -> xshear);
			sscanf(token, "yshear=%lf", &po -> yshear);
			sscanf(token, "zshear=%lf", &po -> zshear);

			sscanf(token, "dxshear=%lf", &po -> dxshear);
			sscanf(token, "dyshear=%lf", &po -> dyshear);
			sscanf(token, "dzshear=%lf", &po -> dzshear);

			sscanf(token, "xsize=%lf", &po -> xsize);
			sscanf(token, "ysize=%lf", &po -> ysize);
			sscanf(token, "zsize=%lf", &po -> zsize);

			sscanf(token, "dxsize=%lf", &po -> dxsize);
			sscanf(token, "dysize=%lf", &po -> dysize);
			sscanf(token, "dzsize=%lf", &po -> dzsize);

			if(strncmp(token, "rsize", 5) == 0)
				{
				po -> xsize = po -> org_xsize;
				po -> ysize = po -> org_ysize;
				po -> zsize = po -> org_zsize;

				po -> dxsize = 0.0;
				po -> dysize = 0.0;
				po -> dzsize = 0.0;
				}

			sscanf(token, "heading=%lf", &po -> heading);
			sscanf(token, "dheading=%lf", &po -> dheading);

			sscanf(token, "speed=%lf", &po -> speed);
			sscanf(token, "dspeed=%lf", &po -> dspeed);
			sscanf(token, "ddspeed=%lf", &po -> ddspeed);

			sscanf(token, "transp=%lf", &po -> transparency);
			sscanf(token, "dtransp=%lf", &po -> dtransparency);

			sscanf(token, "sat=%lf", &po -> saturation);
			sscanf(token, "dsat=%lf", &po -> dsaturation);

			sscanf(token, "hue=%lf", &po -> hue);
			sscanf(token, "dhue=%lf", &po -> dhue);

			sscanf(token, "hue_ldrift=%lf", &po -> hue_line_drift);
			sscanf(token, "dhue_ldrift=%lf", &po -> dhue_line_drift);

			sscanf(token, "contr=%lf", &po -> contrast);
			sscanf(token, "dcontr=%lf", &po -> dcontrast);

			sscanf(token, "u_shift=%lf", &po -> u_shift);
			sscanf(token, "du_shift=%lf", &po -> du_shift);

			sscanf(token, "v_shift=%lf", &po -> v_shift);
			sscanf(token, "dv_shift=%lf", &po -> dv_shift);

			sscanf(token, "slice=%lf", &po -> slice_level);
			sscanf(token, "dslice=%lf", &po -> dslice_level);

			sscanf(token, "mask=%lf", &po -> mask_level);
			sscanf(token, "dmask=%lf", &po -> dmask_level);

			sscanf(token, "bright=%lf", &po -> brightness);
			sscanf(token, "dbright=%lf", &po -> dbrightness);

			sscanf(token, "ck_color=%lf", &po -> chroma_key_color);
			sscanf(token, "dck_color=%lf", &po -> dchroma_key_color);

			sscanf(token, "ck_sat=%lf", &po -> chroma_key_saturation);
			sscanf(token, "dck_sat=%lf", &po -> dchroma_key_saturation);

			sscanf(token, "ck_window=%lf", &po -> chroma_key_window);
			sscanf(token, "dck_window=%lf", &po -> dchroma_key_window);

			sscanf(token, "u=%lf", &po -> u);
			sscanf(token, "du=%lf", &po -> du);

			sscanf(token, "v=%lf", &po -> v);
			sscanf(token, "dv=%lf", &po -> dv);

			sscanf(token, "color=%lf", &po -> color);
			sscanf(token, "dcolor=%lf", &po -> dcolor);

			a = sscanf(token, "center=%lf", &da);
			if(a == 1) center_flag = (int)da;

			sscanf(token, "aspect=%lf", &po -> aspect);

			/* these are globals (double) for subtitles */
			sscanf(token, "hfactor=%lf", &subtitle_h_factor);
			sscanf(token, "vfactor=%lf", &subtitle_v_factor);

			/* font related */

			font_dir[0] = 0;
			a = sscanf(token, "font_dir=%s", font_dir);
			if(a == 1)
				{
				po -> font_dir = strsave(font_dir);
				if(! po -> font_dir)
					{
					tc_log_msg(MOD_NAME,
					"subtitler: parse_frame_entry(): could not allocate space for font_dir, aborting");

					exit(1);
					}
				}

			/* also allow font_path, for compatibility with xste-3.1 */
			a = sscanf(token, "font_path=%s", font_dir);
			if(a == 1)
				{
				po -> font_dir = strsave(font_dir);
				if(! po -> font_dir)
					{
					tc_log_msg(MOD_NAME,
					"subtitler: parse_frame_entry(): could not allocate space for font_dir, aborting");

					exit(1);
					}
				}

			font_name[0] = 0;
			a = sscanf(token, "font_name=%s", font_name);
			if(a == 1)
				{
				po -> font_name = strsave(font_name);
				if(! po -> font_name)
					{
					tc_log_msg(MOD_NAME,
					"subtitler: parse_frame_entry(): could not allocate space for font_name, aborting");

					exit(1);
					}
				}

			a = sscanf(token, "font_size=%d", &po -> font_size);
			a = sscanf(token, "font_iso_extension=%d", &po -> font_iso_extension);
			a = sscanf(token, "font_outline_thickness=%lf", &po -> font_outline_thickness);
			a = sscanf(token, "font_blur_radius=%lf", &po -> font_blur_radius);

			if(debug_flag)
				{
				tc_log_msg(MOD_NAME, "frame=%s font_dir=%s font_name=%s\n\
				font_size=%d font_iso_extension=%d font_outline_thickness=%.2f font_blur_radius=%.2f",\
				pa -> name, po -> font_dir, po -> font_name,\
				po -> font_size, po -> font_iso_extension,\
				po -> font_outline_thickness, po -> font_blur_radius);
				}

			/* also reload font if font_factor changed */
			if( (po -> font_dir) && (po -> font_name) &&\
			(po -> font_size > 0) && (po -> font_iso_extension > 0) &&\
			(po -> font_outline_thickness > 0.0) && (po -> font_blur_radius > 0.0) )
				{
				/*
				IMPORTANT! this sets data in frame_list (pb)!!!!! NOT<<
           	    in object_list (po).
				In fact replaces frame_list pb -> font_dir with the
                new definition.
				Later, when the object is referenced again, a pointer
				is handed to the object list.
				Else the data would be erased when the object was no
           	    longer displayed.
				*/
				po -> font_symbols = default_subtitle_font_symbols;

				/* read in font (also needed for frame counter) */

				pfd = add_font(\
					po -> font_name, po -> font_symbols, po -> font_size, po -> font_iso_extension,\
					po -> font_outline_thickness, po -> font_blur_radius);
				if(! pfd)
					{
					tc_log_msg(MOD_NAME,
					"subtitler(): parser.c: could not load font:\n\
					font_dir=%s font_name=%s symbols=%d size=%d iso extension=%d\n\
					outline_thickness=%.2f  blur_radius=%.2f, aborting",\
					po -> font_dir, po -> font_name, po -> font_symbols, po -> font_size,\
					po -> font_iso_extension,\
					po -> font_outline_thickness, po -> font_blur_radius );

					/* return error */
					exit(1);
					}

				/* to frame list */
				pb -> pfd = pfd;

				/* modify pointer in object list */
				po -> pfd = pb -> pfd;
				if(pb -> type == SUBTITLE_CONTROL)
					{
					/* set the global for subtitles to the current value */
					subtitle_current_font_descriptor = pb -> pfd;
//					subtitle_current_spacing = po -> extra_character_space;
					/*
					every subtitle read from the ppml file is formatted using
					this setting, until a line with a subtitle reference
               		with a new value in read.
					*/
					}
				} /* end if font_dir specified */
			/* end font related */


			/* DVD like subs from xste palette, color from palette, and contrast */

			a = sscanf(token,\
"palette=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,\
 %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",\
			&temp_palette[0][0], &temp_palette[0][1], &temp_palette[0][2],\
			&temp_palette[1][0], &temp_palette[1][1], &temp_palette[1][2],\
			&temp_palette[2][0], &temp_palette[2][1], &temp_palette[2][2],\
			&temp_palette[3][0], &temp_palette[3][1], &temp_palette[3][2],\
			&temp_palette[4][0], &temp_palette[4][1], &temp_palette[4][2],\
			&temp_palette[5][0], &temp_palette[5][1], &temp_palette[5][2],\
			&temp_palette[6][0], &temp_palette[6][1], &temp_palette[6][2],\
			&temp_palette[7][0], &temp_palette[7][1], &temp_palette[7][2],\
			&temp_palette[8][0], &temp_palette[8][1], &temp_palette[8][2],\
			&temp_palette[9][0], &temp_palette[9][1], &temp_palette[9][2],\
			&temp_palette[10][0], &temp_palette[10][1], &temp_palette[10][2],\
			&temp_palette[11][0], &temp_palette[11][1], &temp_palette[11][2],\
			&temp_palette[12][0], &temp_palette[12][1], &temp_palette[12][2],\
			&temp_palette[13][0], &temp_palette[13][1], &temp_palette[13][2],\
			&temp_palette[14][0], &temp_palette[14][1], &temp_palette[14][2],\
			&temp_palette[15][0], &temp_palette[15][1], &temp_palette[15][2]);

			if(a == 48)
				{
				for(i = 0; i < 16; i++)
					{
					for(j = 0; j < 3; j++)
						{
						rgb_palette[i][j] = temp_palette[i][j];

						if(debug_flag)
							{
							tc_log_msg(MOD_NAME, "rgb_palette[%d][%d]=%d", i, j, rgb_palette[i][j]);
							}
						}
					}

				rgb_palette_valid_flag = 1;
				}
			else if(a >= 1)
				{
				tc_log_msg(MOD_NAME,
				"subtitler: parser.c frame %s only %d of 48 arguments found in palette, aborting.",\
				pa -> name, a);

				exit(1);
				}

			a = sscanf(token, "background_color=%d", &po -> background);
			a = sscanf(token, "pattern_color=%d", &po -> pattern);

			a = sscanf(token, "emphasis1_color=%d", &po -> emphasis1);
			a = sscanf(token, "emphasis2_color=%d", &po -> emphasis2);

			a = sscanf(token, "background_contrast=%d", &po -> background_contrast);
			a = sscanf(token, "pattern_contrast=%d", &po -> pattern_contrast);
			a = sscanf(token, "emphasis1_contrast=%d", &po -> emphasis1_contrast);
			a = sscanf(token, "emphasis2_contrast=%d", &po -> emphasis2_contrast);

			/* end palette, color from palette, and contrast */

			/* some text releated */
			sscanf(token, "espace=%lf", &po -> extra_character_space);
			sscanf(token, "despace=%lf", &po -> dextra_character_space);

			sscanf(token, "anti_alias=%d", &po -> anti_alias_flag);

			/* end font or text related */

			/* some other commands */
			if(strncmp(token, "kill", 4) == 0)
				{
				po -> end_frame = atoi(pa -> name);
				}
			sscanf(token, "kill=%d", &po -> end_frame);

//			if(strncmp(token, "exit", 4) == 0)
//				{
//				tc_log_msg(MOD_NAME, "subtitler(): exit request in .ppml file");

//				exit(1);
//				}
			/* add your sscanfs here */

			} /* end else ! object name */

		/* sort the list for zpos */
		sort_objects_by_zaxis();

		} /* end while parse options */

	free(running);
	return 1;
	} /* end if pa -> data[0] == '*', object reference */

if(pa -> type == FORMATTED_TEXT)
	{
	/* pa -> data points to text with possible formatting slashes */

	frame_nr = atoi(pa -> name);
	end_frame_nr = pa -> end_frame;

//	if(verbose & TC_STATS)
		{
		tc_log_msg(MOD_NAME,
		"subtitler(): frame_nr=%d end_frame_nr=%d\ntext=%s",\
		frame_nr, end_frame_nr, pa -> data);
		}

	/*
	Set hor_position for start text in all lines to zero,
	center_text() may overrule this if center_flag.
	*/
	for(i = 0; i < MAX_SCREEN_LINES; i++)
		{
		screen_text[i][0] = 0;
		screen_start[i] = 0; // pixels from left used to center text
		}

	/* reformat text inserting (multiple) '/n' if too long */
	extra_character_space = po -> extra_character_space;

	tptr =\
	p_reformat_text(\
	pa -> data, line_h_end - line_h_start, subtitle_current_font_descriptor);
	if(! tptr)
		{
		tc_log_msg(MOD_NAME, "subtitler(): could not reformat text=%s", pa -> data);

		/* return error */
		return -1;
		} /* end if reformat text failed */

	/* center text */
	if(center_flag) p_center_text(tptr, subtitle_current_font_descriptor);

	/* text to array screen_text[] */
	cptr = tptr;
	screen_lines = 1; /* at least one */
	while(1) /* all chars in tptr */
		{
		i = 0;
		while(1) /* all chars in a line */
			{
			if(*cptr == '\n')
				{
				/* scip the '\n' */
				cptr++;

				/* force string termination */
				screen_text[screen_lines - 1][i] = 0;

				/* point to next screen line */
				screen_lines++;
				break;
				}

			/* copy character */
			screen_text[screen_lines - 1][i] = *cptr;

			/* test for end of string tptr */
			if(*cptr == 0) break;

			/* point to next char in screen_lines[][] */
			i++;

			/* point to next char in tptr */
			cptr++;
			} /* end while all characters in line terminated with LF */

		/* ready if end of tptr */
		if(*cptr == 0) break;

		} /* end while all lines in tptr */
	free(tptr);

	/* some limit */
	if(screen_lines > MAX_SCREEN_LINES) screen_lines = MAX_SCREEN_LINES;

	line_height = subtitle_current_font_descriptor -> height;
	window_top = window_bottom - (screen_lines * line_height);

//tc_log_msg(MOD_NAME, "WAS line_height=%d", line_height);

	if(debug_flag)
		{
		tc_log_msg(MOD_NAME, "screen_lines=%d", screen_lines);
		tc_log_msg(MOD_NAME, "line_h_start=%d line_h_end=%d",\
		line_h_start, line_h_end);
		tc_log_msg(MOD_NAME, "window_bottom=%d window_top=%d",\
		window_bottom, window_top);
		}

	/*
	to be able to draw a background' as in DVD like subs we need to calculate
	an aquare area encompassing where the formatted text is.
	*/
	text_start = INT_MAX;
	max_width = 0;
	/* print lines of text on screen in right position */
	for(i = 0; i < screen_lines; i++)
		{
		/* get text length */
		line_len = 0;
		j = 0;
		while(1)
			{
			c = screen_text[i][j];

			if(pa -> pfd == 0)
				{
				tc_log_msg(MOD_NAME, "subtitler: before get_h_pixels():  pa=%p pa -> fd=%p, aborting",\
				pa , pa -> pfd);

				return 0;
				}

			line_len += get_h_pixels(c, subtitle_current_font_descriptor);

			if(c == 0) break;
			j++;
			}

		x = screen_start[i];

		/* get size of bitmap as in bitmap.c */
		if(x < text_start) text_start = x;
		if(line_len > max_width) max_width = line_len;

		y = window_top + (i * line_height);

		if(debug_flag)
			{
			tc_log_msg(MOD_NAME,
			"screen_start[%d]=%d window_bottom=%d window_top=%d\n\
line_height=%d x=%d y=%d\n\
text=%s",\
			i, screen_start[i], window_bottom, window_top,\
			line_height, x, y,\
			screen_text[i]);
			}

		/* subtitle just behind frame counter */
		z = 65534;

		/* add the text to the structure list */
		pc = add_subtitle_object(\
		frame_nr, end_frame_nr, pa -> type,\
		x , y, z, screen_text[i]);

		/*
		remember the pointer for the first sub.
		Later, when diplaying lines of a formatted sub,
		in line 0 we can make a background if DVD type subs requested.
		*/
		pc -> line_number = i;
		if(i == 0)
			{
			pf = pc;
			}
		} /* end for all screen_lines */

	/* only the first line_number of a multiline sub has these parameters set! */

	/* just like in submux-dvd we clip the area, so it looks exactly like DVD */

	bg_height = screen_lines * line_height; // + 9;
//	a = bg_height % 4;
//	if(a) bg_height += 4 - a;

	bg_width = max_width; // + 6;
//	a = bg_width % 8;
//	if(a) bg_width += 8 - a;

	pf -> bg_y_start = window_top;
	pf -> bg_y_end = pf -> bg_y_start + bg_height;

	/* we have text_start and max_width */
	pf -> bg_x_start = text_start;
	pf -> bg_x_end = pf -> bg_x_start + bg_width;

	} /* end if object_type subtitle (=FORMATTED_TEXT) */

return 1;
} /* end function parse_frame_entry */

