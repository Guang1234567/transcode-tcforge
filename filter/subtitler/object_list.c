/*
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
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "subtitler.h"

/* first element points to first entry,
   second element to last entry */
struct object *objecttab[2];

double subtitle_u, subtitle_v;
double subtitle_contrast = 100.0;
double subtitle_transparency = 0.0;
double subtitle_dxpos, subtitle_dypos, subtitle_dzpos;
font_desc_t *subtitle_pfd;
//double subtitle_extra_character_space = EXTRA_CHAR_SPACE;
double subtitle_font_factor;
double subtitle_font;

int subtitle_pattern = 0;
int subtitle_background = 1;
int subtitle_emphasis1 = 2;
int subtitle_emphasis2 = 3;

int subtitle_pattern_contrast = 0;
int subtitle_background_contrast = 15;
int subtitle_emphasis1_contrast = 15;
int subtitle_emphasis2_contrast = 0;

double outline_thickness;
double blur_radius;

int subtitle_symbols;

extern int add_background(struct object *pa);
extern int add_picture(struct object *pa);
extern int execute(char *);
extern int swap_position(struct object *ptop, struct object *pbottom);

struct object *lookup_object(char *name)
{
struct object *pa;

for(pa = objecttab[0]; pa != 0; pa = pa -> nxtentr)
	{
	if(strcmp(pa -> name, name) == 0) return(pa);
	}

return 0; /* not found */
}/* end function lookup_object */


struct object *install_object_at_end_of_list(char *name)
{
struct object *plast, *pnew;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "install_object_at_end_off_list(): arg name=%s", name);
	}

pnew = lookup_object(name);
if(pnew) return(pnew);/* already there */

/* create new structure */
pnew = (struct object *) calloc(1, sizeof(*pnew) );
if(! pnew) return(0);
pnew -> name = strsave(name);
if(! pnew -> name) return(0);

/* get previous structure */
plast = objecttab[1]; /* end list */

/* set new structure pointers */
pnew -> nxtentr = 0; /* new points top zero (is end) */
pnew -> prventr = plast; /* point to previous entry, or 0 if first entry */

/* set previous structure pointers */
if( !objecttab[0] ) objecttab[0] = pnew; /* first element in list */
else plast -> nxtentr = pnew;

/* set array end pointer */
objecttab[1] = pnew;

pnew -> saturation = 100.0;
pnew -> contrast = 100.0;

return(pnew);/* pointer to new structure */
}/* end function install_object_at_end_of_list */


int delete_object(char *name)
/* delete entry from double linked list */
{
struct object *pa, *pprev, *pdel, *pnext;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "delete_object(): arg name=%s", name);
	}

pa = objecttab[0];
while(1)
	{
	/* if end list, return not found */
	if(! pa) return(0);

	/* test for match in name */
	if(strcmp(name, pa -> name) != 0) /* no match */
		{
		/* point to next element in list */
		pa = pa -> nxtentr;

		/* loop for next element in list */
		continue;
		}

	/* we now know which struture to delete */
	pdel = pa;

	/* get previous and next structure */
	pnext = pa -> nxtentr;
	pprev = pa -> prventr;

	/* set pointers for previous structure */
	/* if first one, modify objecttab[0] */
	if(pprev == 0) objecttab[0] = pnext;
	else pprev -> nxtentr = pnext;

	/* set pointers for next structure */
	/* if last one, modify objecttab[1] */
	if(pnext == 0) objecttab[1] = pprev;
	else pnext -> prventr = pprev;

	/* delete structure */
/*
DO NOT DELETE THESE ARE POINTERS TO DATA IN FRAME LIST,
if you delete this, the next time the object is re-used, there is no data!
*/
//	free(pdel -> data);
//	free(pdel -> font_dir);

	free(pdel -> name);
	free(pdel); /* free structure */

	/* return OK deleted */
	return 1;
	}/* end for all structures */
}/* end function delete_object */


int delete_all_objects()
/* delete all entries from table */
{
struct object *pa;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "delete_all_objects() arg none");
	}

while(1)
	{
	pa = objecttab[0];
	if(! pa) break;
	objecttab[0] = pa -> nxtentr;

/*
DO NOT DELETE THESE ARE POINTERS TO DATA IN FRAME LIST,
if you delete this, the next time the object is re-used, there is no data!
*/
//	free(pa -> data);
//	free(pa -> font_dir);

	free(pa -> name);
	free(pa);/* free structure */
	}/* end while all structures */

objecttab[1] = 0;
return(1);
}/* end function delete_all_objects */


struct object *add_subtitle_object\
	(\
	int start_frame_nr, int end_frame_nr,\
	int type,\
	double xpos, double ypos, double zpos,\
	char *data\
	)
{
struct object *pa;
char name[TEMP_SIZE];

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "add_subtitle_object(): arg\n\
	start_frame_nr=%d end_frame_nr=%d\n\
	type=%d\n\
	xpos=%.2f ypos=%.2f zpos=%.2f\n\
	data=%lu",\
	start_frame_nr, end_frame_nr,\
	type,\
	xpos, ypos, zpos,\
	(unsigned long)data\
	);

	if(type == FORMATTED_TEXT) tc_log_msg(MOD_NAME, "type formatted text data=%s", data);

	}

/* argument check */
if(! data) return 0;

/* Need unique entry for each object */
tc_snprintf(name, sizeof(name), "%d %d %f %f %f %d",\
start_frame_nr, end_frame_nr, xpos, ypos, zpos, type);
pa = install_object_at_end_of_list(name);
if(! pa)
	{
	tc_log_msg(MOD_NAME, "subtitler: add_subtitle_object(): install_object_at_end_of_list %s failed",
	name);

	return 0;
	}

pa -> start_frame = start_frame_nr;
pa -> end_frame = end_frame_nr;

pa -> type = type;

pa -> xpos = xpos;
pa -> ypos = ypos;
pa -> zpos = zpos;

pa -> pfd = NULL;

pa -> data = strsave(data);
if(! pa -> data)
	{
	tc_log_msg(MOD_NAME, "subtitler(): add_subtitle_object():\n\
	could not allocate space for data, aborting");

	return 0;
	}

pa -> extra_character_space = extra_character_space;

pa -> status = OBJECT_STATUS_NEW;

/*
Sort by zaxis value,
put the object that is the most in front at the end of the list
*/
if(! sort_objects_by_zaxis() )
	{
	tc_log_msg(MOD_NAME, "subtitler(): add_subtitle_object():\n\
	could not sort objects by zaxis value, aborting");

	return 0;
	}

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "subtitler(): add_subtitle_object() return OK pa=%p", pa);
	}

return pa;
}/* end function add_subtitle_object */


int add_objects(int current_frame_nr)
{
int a, x, y;
struct object *pa, *pdel, *pnext, *pprev;
char temp[1024];
double dx = 0, dy, dd;
char *pc;
FILE *fptr;
int width, height;
char *temp_data;
double dtx, dty;
char *ptr;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "add_objects(): arg current_frame_nr=%d____", current_frame_nr);
	}

for(pa = objecttab[0]; pa != 0; pa = pa -> nxtentr)
	{
	/* remove stale entries */
	if(current_frame_nr == pa -> end_frame)
		{
//tc_log_msg(MOD_NAME, "DELETING STRUCTURE");
		/* we now know which struture to delete */
		pdel = pa;

		/* get previous and next structure */
		pnext = pa -> nxtentr;
		pprev = pa -> prventr;

		/* set pointers for previous structure */
		/* if first one, modify objecttab[0] */
		if(pprev == 0) objecttab[0] = pnext;
		else pprev -> nxtentr = pnext;

		/* set pointers for next structure */
		/* if last one, modify objecttab[1] */
		if(pnext == 0) objecttab[1] = pprev;
		else pnext -> prventr = pprev;

		/* delete structure */
		/*
		DO NOT DELETE THESE ARE POINTERS TO DATA IN FRAME LIST,
		if you delete this, the next time the object is re-used,
		there is no data!
		*/
//		free(pdel -> data);
//		free(pdel -> font_dir);

		free(pdel -> name);
		free(pdel); /* free structure */
		} /* end if timed out */
	else /* to display buffer */
		{
		if(debug_flag)
			{
			tc_log_msg(MOD_NAME, "pa->name=%s pa->start_frame=%d pa->end_frame=%d\n\
			pa->xpos=%.2f pa->ypos=%.2f pa->type=%d pa->data=%lu\n\
			pa->pfd=%lu",\
			pa->name, pa->start_frame, pa->end_frame,\
			pa->xpos, pa->ypos, pa->type, (unsigned long)pa->data,\
			(unsigned long)pa->pfd);

			tc_log_msg(MOD_NAME, "pa->data=%s", pa -> data);
			}

		/* functions working on variables deltas */
		/* heading */
		if( pa -> speed != 0)
			{
			/*
			definition of scroll direction variable:
                0
                |
        270 _      _ 90
                |
               180
           */
			/* get increments */
			pa -> dxpos = sin( pa -> heading * M_PI / 180.0 );
			pa -> dypos = -cos( pa -> heading * M_PI / 180.0 );

			/* correct for aspect ratio, these are globals */
			pa -> dxpos *=\
			(double)image_width / (double)image_height;
			} /* end if speed not zero */

		if(pa -> type == FORMATTED_TEXT)
			{
			/* move params from control object to all subtitle objects */
			/* U and V will handle saturation too */
			pa -> u = subtitle_u;
			pa -> v = subtitle_v;

			pa -> contrast = subtitle_contrast;
			pa -> transparency = subtitle_transparency;
			pa -> pfd = subtitle_pfd;
			pa -> extra_character_space = subtitle_extra_character_space;

			pa -> font_outline_thickness = outline_thickness;
			pa -> font_blur_radius = blur_radius;

			pa -> pattern = subtitle_pattern;
			pa -> background = subtitle_background;
			pa -> emphasis1 = subtitle_emphasis1;
			pa -> emphasis2 = subtitle_emphasis2;

			pa -> pattern_contrast = subtitle_pattern_contrast;
			pa -> background_contrast = subtitle_background_contrast;
			pa -> emphasis1_contrast = subtitle_emphasis1_contrast;
			pa -> emphasis2_contrast = subtitle_emphasis2_contrast;

			pa -> font_symbols = subtitle_symbols;

			if(pa -> line_number == 0)
				{
				add_background(pa);
				}

			add_text((int)pa -> xpos, (int)pa -> ypos, pa -> data,\
			pa, (int) pa -> u, (int)pa -> v,\
			pa -> contrast, pa -> transparency, pa -> pfd,\
			(int)pa -> extra_character_space);
			}
		else if(pa -> type == X_Y_Z_T_TEXT)
			{
			add_text( (int)pa -> xpos, (int)pa -> ypos, pa -> data,\
			pa, (int)pa -> u, (int)pa -> v,\
			pa -> contrast, pa -> transparency, pa -> pfd,\
			(int)pa -> extra_character_space);
			}
		else if(pa -> type == X_Y_Z_T_PICTURE)
			{
			if( (pa -> xsize != 0) && (pa -> ysize != 0) )
				{
				if(\
				(pa -> org_xsize != pa -> xsize) ||\
				(pa -> org_ysize != pa -> ysize) ||\
				(pa -> zrotation != 0.0) ||\
				(pa -> xshear != 0.0) ||\
				(pa -> yshear != 0)\
				)
					{
					/*
					done it this way because reformatting the yuv array
					over and over again seems to cause distortions in the
					picture.
   					Now we keep a copy of the original data and size, and
					always work from that.
					*/

					/*
					save x and y size, rotation will temporary alter these.
					*/
					dtx = pa -> xsize;
					dty = pa -> ysize;

					/*
					mofify the .ppm file
					*/
					if(pa -> mask_level)
						{
						border_luminance = pa -> mask_level;
						}
					else
						{
						border_luminance = default_border_luminance;
						}

					temp_data = pa -> data;
					pc =\
					change_picture_geometry(\
					temp_data, (int)pa -> org_xsize, (int)pa -> org_ysize,\
					&pa -> xsize, &pa -> ysize, (int)pa -> aspect,\
					pa -> zrotation,\
					pa -> xshear, pa -> yshear);
					if(pc)
						{
						pa -> data = pc;

						add_picture(pa);

						free(pc);

						/* restore sizes */
						pa -> xsize = dtx;
						pa -> ysize = dty;

						/* restore slice level to a safe value */
						border_luminance = 65535;

						} /* end if size changed */

					pa -> data = temp_data;
					} /* end if geometry changed */
				else /* original dimensions */
					{
					/* here pa -> data holds the picture in YUYV format */
					add_picture(pa);
					}
				} /* end if x and y size not zero */
			} /* end if type X_Y_Z_T_PICTURE */
		else if(pa -> type == X_Y_Z_T_MOVIE)
			{
			/* pa -> id holds the unique movie number */

			/*
			wait for the semaphore file to appear,
			indication the xxxx.ppm is ready
			*/
			tc_snprintf(temp, sizeof(temp), "%s/.subtitles/%d.sem", home_dir, pa -> id);
			while(1)
				{
				fptr = fopen(temp, "r");
				if(fptr)
					{
					fclose(fptr);

					break;
					}

				/* reduce processor load */
				usleep(10000); // 10 ms
				} /* end while wait for handshake */

			/* set the new size once the same, flag is status */
			if(! pa -> status & OBJECT_STATUS_INIT)
				{
				/* read the ppm file to get the size */
				tc_snprintf(temp, sizeof(temp), "%s/.subtitles/%d.ppm", home_dir, pa -> id);
				temp_data = ppm_to_yuv_in_char(temp, &width, &height);
				if(! temp_data)
   			        {
     			    tc_log_msg(MOD_NAME, "subtitler(): could not read file %s, aborting",\
					temp);

	    		    exit(1);
    				}

				/* this is the original movie size */
				pa -> org_xsize = (double)width;
				pa -> org_ysize = (double)height;

				pa -> xsize = (double)width;
				pa -> ysize = (double)height;

				pa -> status |= OBJECT_STATUS_INIT;
				} /* end if ! pa > status */

			/* apply resize to the picture if needed */
			if( (pa -> xsize != 0) && (pa -> ysize != 0) )
				{
				if(\
				(pa -> org_xsize != pa -> xsize) ||\
				(pa -> org_ysize != pa -> ysize) ||\
				(pa -> zrotation != 0.0) ||\
				(pa -> xshear != 0.0) ||\
				(pa -> yshear != 0)\
				)
					{
					/* work around mogrify bug by moving pic to home dir */
					tc_snprintf(temp, sizeof(temp),\
					"mv %s/.subtitles/%d.ppm %s/",\
					home_dir, pa -> id, home_dir);
					execute(temp);

					/*
					errors if structure directly used in sprintf,
					compiler?
					*/
					x = pa -> xsize;
					y = pa -> ysize;

					/* resize ppm */

					/* if '!' in mogrify, aspects is overruled */
					if(pa -> aspect) a = ' ';
					else a = '!';

					/*
					workaround bug in mogrify that causes exit if xshear
					is zero.
					*/
					if(pa -> yshear != 0)
						{
						dx = pa -> xshear;
						if(dx == 0.0) dx = 0.001;
						}

					if(\
					(dx != 0.0) || (pa -> yshear != 0.0) ||\
					(pa -> zrotation != 0.0)\
					)
						{
						/* load ppm */
						tc_snprintf(temp, sizeof(temp), "%s/%d.ppm", home_dir, pa -> id);
						ptr = ppm_to_yuv_in_char(temp, &width, &height);
						if(! ptr) return 0;

						/* change .ppm file */
						if(pa -> mask_level)
							{
							border_luminance = pa -> mask_level;
							}
						else
							{
							border_luminance = default_border_luminance;
							}

						/* save a modified ppm */
						a = yuv_to_ppm(ptr, width, height, temp);
						free(ptr);
						if(! a) return 0;

						/* restore the slice level to a safe value */
						border_luminance = 65535;

						} /* end if zrotation or shear */
					if( (dx != 0.0) || (pa -> yshear != 0.0) )
						{
						tc_snprintf(temp, sizeof(temp),\
"mogrify  -geometry %dx%d%c  -rotate %.2f  -shear %.2fx%.2f  %s/%d.ppm",\
						x, y, a,\
						pa -> zrotation,\
						dx, pa -> yshear,\
						home_dir, pa -> id);
						}
					else
						{
						tc_snprintf(temp, sizeof(temp),\
"mogrify  -geometry %dx%d%c  -rotate %.2f  %s/%d.ppm",\
						x, y, a,\
						pa -> zrotation,\
						home_dir, pa -> id);
						}

					execute(temp);

					/* back to normal dir */
					tc_snprintf(temp, sizeof(temp),\
					"mv %s/%d.ppm %s/.subtitles/",\
					home_dir, pa -> id, home_dir);
					execute(temp);
					} /* and if any size changed */

				/* rotation will alter these */
				dtx = pa -> xsize;
				dty = pa -> ysize;

				/* read the ppm file into the buffer */
				tc_snprintf(temp, sizeof(temp), "%s/.subtitles/%d.ppm", home_dir, pa -> id);
				pa -> data = ppm_to_yuv_in_char(temp, &width, &height);
		        if(! pa -> data)
    		        {
   		     	    tc_log_msg(MOD_NAME, "subtitler(): could not read file %s, aborting",\
					temp);

   		 	        exit(1);
	    			}

				/* use the modified sizes for display */
				pa -> xsize = width;
				pa -> ysize = height;

				/*
				remove the semaphore,
				so the other transcode can get the next frame.
				*/
				tc_snprintf(temp, sizeof(temp), "%s/.subtitles/%d.sem", home_dir, pa -> id);
				unlink(temp);

				/* here pa -> data holds the picture in YUYV format */
				/* get the next frame in YUYV format */

				add_picture(pa);

				/* release the memory */
				free(pa -> data);

				/* restore the sizes */
				pa -> xsize = dtx;
				pa -> ysize = dty;

//				/* restore the slice level to a safe value */
//				border_luminance = 65535;

				} /* end if not zero size */
			} /* end if type X_Y_Z_T_MOVIE */
		else if(pa -> type == MAIN_MOVIE)
			{
			set_main_movie_properties(pa);
			}
		else if(pa -> type == X_Y_Z_T_FRAME_COUNTER)
			{
			pa -> zpos = 65535;

			tc_snprintf(temp, sizeof(temp), "frame=%d", current_frame_nr);
			add_text( (int)pa -> xpos, (int)pa -> ypos, temp,\
			pa, (int)pa -> u, (int)pa -> v,\
			pa -> contrast, pa -> transparency, pa -> pfd,\
			(int)pa -> extra_character_space);
			}

		else if(pa -> type == SUBTITLE_CONTROL)
			{
			/* set the globals */
			subtitle_u = pa -> u;
			subtitle_v = pa -> v;
			subtitle_contrast = pa -> contrast;
			subtitle_transparency = pa -> transparency;
			subtitle_dxpos = pa -> dxpos;
			subtitle_dypos = pa -> dypos;
			subtitle_dzpos = pa -> dzpos;
			subtitle_pfd = pa -> pfd;

			subtitle_extra_character_space = pa -> extra_character_space;

			outline_thickness = pa -> font_outline_thickness;
			blur_radius = pa -> font_blur_radius;

			subtitle_pattern = pa -> pattern;
			subtitle_background = pa -> background;
			subtitle_emphasis1 = pa -> emphasis1;
			subtitle_emphasis2 = pa -> emphasis2;

			subtitle_pattern_contrast = pa -> pattern_contrast;
			subtitle_background_contrast = pa -> background_contrast;
			subtitle_emphasis1_contrast = pa -> emphasis1_contrast;
			subtitle_emphasis2_contrast = pa -> emphasis2_contrast;

			subtitle_symbols = pa -> font_symbols;

			}

		/* text color, this only works on TEXT */
		if(pa -> color)
			{
			pa -> u = 127.0 * sin(pa -> color * M_PI / 180.0);
			pa -> v = 127.0 * cos(pa -> color * M_PI / 180.0);

			pa -> u *= (pa -> saturation / 100.0);
			pa -> v *= (pa -> saturation / 100.0);

			} /* end if any color */
		else  /* no color */
 			{
			pa -> u = 0;
			pa -> v = 0;
			}

		/* apply speed correction */
		if(pa -> speed != 0)
			{
			pa -> dxpos *= pa -> speed;
			pa -> dypos *= pa -> speed;
			pa -> dzpos *= pa -> speed;
			}

		/*
		In goto mode, we substract the distance travelled each frame
		from the calculated distance, to get the remaining distance.
		If the remaining distance is smaller then zero, we have arrived.
		*/
		if(pa -> status & OBJECT_STATUS_GOTO)
			{
			/* true distance travelled in this frame */
			dx = pa -> dxpos;
			dy = pa -> dypos;

			/* correct for aspect ratio, these are globals */
			dx *=\
			(double)image_height / (double)image_width;

			dd = sqrt( (dx * dx) + (dy * dy) );

			/* substract travelled distance */
			pa -> distance -= dd;

#if 0
	tc_log_msg(MOD_NAME, "WAS GOTO x=%d y=%d dx=%.2f dy=%.2f dd=%.2f pa->distance=%.2f",\
	(int)pa -> xpos, (int)pa -> ypos, dx, dy, dd, pa -> distance);
#endif

			/* test if distance remaining */
			if(pa -> distance < 0.0)
				{
				/* stop if arrived ! */
				pa -> speed = 0.0;
				pa -> dspeed = 0.0;
				pa -> ddspeed = 0.0;

				/* set deltas to zero */
				pa -> dxpos = 0;
				pa -> dypos = 0;
//				pa -> dzpos = 0;

				/* arrived, no longer in GOTO mode */
				pa -> status &= ~OBJECT_STATUS_GOTO;
				}
			} /* end if in goto mode */

		/* add your functions here */

		/* update increments */
		pa -> xpos += pa -> dxpos;
		pa -> ypos += pa -> dypos;
		pa -> zpos += pa -> dzpos;

		if(pa -> type == FORMATTED_TEXT)
			{
			/* move params from control object to all subtitle objects */

			/*
			deltas only for x, y, z, because we do not want to disturb
			formatting.
			*/
			pa -> dxpos += subtitle_dxpos;
			pa -> dypos += subtitle_dypos;
			pa -> dzpos += subtitle_dzpos;
			}

		pa -> extra_character_space += pa -> dextra_character_space;
		if(pa -> extra_character_space < 0.0)
			{
//			pa -> extra_character_space = 0.0;
			}
		if(pa -> extra_character_space > image_width)
			{
			pa -> extra_character_space = image_width;
			}

		pa -> dspeed += pa -> ddspeed;
		pa -> speed += pa -> dspeed;
		pa -> heading += pa -> dheading;

		pa -> transparency += pa -> dtransparency;

		pa -> slice_level += pa -> dslice_level;
		pa -> mask_level += pa -> dmask_level;

		pa -> saturation += pa -> dsaturation;
		pa -> hue += pa -> dhue;
		pa -> contrast += pa -> dcontrast;
		pa -> brightness += pa -> dbrightness;

		pa -> xsize += pa -> dxsize;
		pa -> ysize += pa -> dysize;
		pa -> zsize += pa -> dzsize;

		pa -> xrotation += pa -> dxrotation;
		pa -> yrotation += pa -> dyrotation;
		pa -> zrotation += pa -> dzrotation;

		pa -> xshear += pa -> dxshear;
		pa -> yshear += pa -> dyshear;
		pa -> zshear += pa -> dzshear;

		/* chroma key only */
		pa -> chroma_key_color += pa -> dchroma_key_color;
		pa -> chroma_key_saturation += pa -> dchroma_key_saturation;
		pa -> chroma_key_window += pa -> dchroma_key_window;
		/* end chroma key only */

		pa -> u_shift += pa -> du_shift;
		pa -> v_shift += pa -> dv_shift;

		pa -> u += pa -> du;
		pa -> v += pa -> dv;

		pa -> color += pa -> dcolor;

		/* limit some variables */
		if(pa -> xsize < 0) pa -> xsize = 0;
		if(pa -> ysize < 0) pa -> ysize = 0;
		if(pa -> zsize < 0) pa -> zsize = 0;

		if(pa -> xshear >= 90.0) pa -> xshear = 89.0;
		if(pa -> yshear >= 90.0) pa -> yshear = 89.0;
		if(pa -> zshear >= 90.0) pa -> zshear = 89.0;

		if(pa -> xshear <= -90.0) pa -> xshear = -89.0;
		if(pa -> yshear <= -90.0) pa -> yshear = -89.0;
		if(pa -> zshear <= -90.0) pa -> zshear = -89.0;

		if(pa -> transparency > 100.0) pa -> transparency = 100.0;
		if(pa -> transparency < 0.0) pa -> transparency = 0.0;

		if(pa -> saturation > 100.0) pa -> saturation = 100.0;
		if(pa -> saturation < 0) pa -> saturation = 0.0;

		if(pa -> brightness > 255.0) pa -> brightness = 255.0;
		if(pa -> brightness < -255.0) pa -> brightness = -255.0;

		if(pa -> contrast > 100.0) pa -> contrast = 100.0;
		if(pa -> contrast < 0) pa -> contrast = 0;

		if(pa -> slice_level > 255.0) pa -> slice_level = 255.0;
		if(pa -> slice_level < 0) pa -> slice_level = 0;

		if(pa -> chroma_key_color > 360.0) pa -> chroma_key_color = 360.0;
		if(pa -> chroma_key_color < 0) pa -> chroma_key_color = 0;

		if(pa -> chroma_key_saturation > 100.0) pa -> chroma_key_saturation = 100.0;
		if(pa -> chroma_key_saturation < 0.0) pa -> chroma_key_saturation = 0.0;

		if(pa -> chroma_key_window > 255.0) pa -> chroma_key_window = 255.0;
		if(pa -> chroma_key_window < 0.0) pa -> chroma_key_window = 0.0;

		if(pa -> u_shift > 127.0) pa -> u_shift = 127.0;
		if(pa -> u_shift < -127.0) pa -> u_shift = -127.0;

		if(pa -> v_shift > 127.0) pa -> v_shift = 127.0;
		if(pa -> v_shift < -127.0) pa -> v_shift = -127.0;

		if(pa -> u > 127.0) pa -> u = 127.0;
		if(pa -> u < -127.0) pa -> u = -127.0;

		if(pa -> v > 127.0) pa -> v = 127.0;
		if(pa -> v < -127.0) pa -> v = -127.0;

		/*
		limit to prevent counter overflow, causing things that were
		forgotten (wrong endframe) to re-appear at strange locations.
		I know we use double, but screens are likely smaller then
		INT_MAX / 2 or so;
		*/
		if(pa -> ypos < -INT_MAX) pa -> ypos = -INT_MAX;
		if(pa -> ypos > INT_MAX) pa -> ypos = INT_MAX;
		if(pa -> xpos < -INT_MAX) pa -> xpos = -INT_MAX;
		if(pa -> xpos > INT_MAX) pa -> xpos = INT_MAX;

		/* if any zpos changed, sort the list for zpos */
		if(pa -> zpos != pa -> old_zpos)
			{
			sort_objects_by_zaxis();
			}
		pa -> old_zpos = pa -> zpos;

		} /* end print it */

	} /* end for all entries */

return 1;
} /* end function add_objects */


int sort_objects_by_zaxis()
/*
sorts the double linked list with as criterium that the lowest zaxis value,
that is the farthest away object, goes on top,
doing some sort of bubble sort.
*/
{
struct object *pa;
struct object *pb;
int swap_flag;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "subtitler(): sort_objects_by_zaxis(): arg none");
	}

while(1)/* go through list again and again */
	{
	if(debug_flag)
		{
		tc_log_msg(MOD_NAME, "SORTING OBJECT LIST");
		}/* end if debug_flag */

	swap_flag = 0;
	for(pa = objecttab[0]; pa != 0; pa = pa -> nxtentr)
		{
		if(debug_flag)
			{
			tc_log_msg(MOD_NAME, "sort_objects_by_zaxis(): sorting %s pa=%lu",\
			(const char *)pa->name, (unsigned long)pa);
			}

		pb = pa -> prventr;
		if(debug_flag)
			{
			tc_log_msg(MOD_NAME, "sort_objects_by_zaxis(): pb=pa->prventr=%lu", (unsigned long)pb);
			}

		if(pb)
			{
			/* compare */
			if( pa -> zpos < pb -> zpos )
				{
				swap_flag = swap_position(pa , pb);
				/* indicate position was swapped */
				if(debug_flag)
					{
					tc_log_msg(MOD_NAME, "swap_flag=%d", swap_flag);
					tc_log_msg(MOD_NAME, "AFTER SWAP pa->prventr=%lu pa->nxtentr=%lu\n\
					pb->prventr=%lu pb-nxtentrr=%lu",\
					(unsigned long)pa -> prventr, (unsigned long)pa -> nxtentr,\
					(unsigned long)pb -> prventr, (unsigned long)pb -> nxtentr);
					}
				}/* end if strcmp < 0 */
			}/* end if pb */
		}/* end for all entries */

	/* if no more swapping took place, ready, list is sorted */
	if(! swap_flag) break;
	}/* end while go through list again and again */

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "subtitler: sort_objects_by_zaxis(): return OK");
	}

return 1;
}/* end function sort_objects_by_zaxis */


int swap_position(struct object *ptop, struct object *pbottom)
{
struct object *punder;
struct object *pabove;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "swap_position(): swapping top=%lu bottom=%lu", (unsigned long)ptop, (unsigned long)pbottom);
	}

/* argument check */
if(! ptop) return 0;
if(! pbottom) return 0;

/* get one below the bottom */
punder = pbottom -> prventr;/* could be zero if first entry */
if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "swap_position(): punder=%lu", (unsigned long)punder);
	}

/* get the one above the top */
pabove = ptop -> nxtentr;/* could be zero if last entry */
if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "swap_position(): pabove=%lu", (unsigned long)pabove);
	}

/* the next pointer in punder (or objecttab[0]) must now point to ptop */
if(! punder)
	{
	objecttab[0] = ptop;
	}
else
	{
	punder -> nxtentr = ptop;
	}

/* the prev pointer in in ptop must now point to punder */
ptop -> prventr = punder;/* could be zero if first entry */

/* the next pointer in ptop must now point to pbottom */
ptop -> nxtentr = pbottom;

/* the next pointer in pbottom must now point to pabove */
pbottom -> nxtentr = pabove;

/* mark last one in objecttab */
if(! pabove)
	{
	objecttab[1] = pbottom;
	}
else
	{
	/* the prev pointer in pabove must now point to pbottom */
	pabove -> prventr = pbottom;
	}

/* the prev pointer in pbottom must now point to ptop */
pbottom -> prventr = ptop;

/* return swapped */
return 1;
}/* end function swap_position */

