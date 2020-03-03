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

struct frame *frametab[FRAME_HASH_SIZE];
struct subtitle_fontname *subtitle_fontnametab[2];

extern int parse_frame_entry(struct frame *pa);

int hash(s)/* form hash value for string s */
char *s;
{
int hashval;

//for(hashval = 0; *s != '\0';) hashval += *s++;
/* sum of ascii value of characters divided by tablesize */

/* vector into structure list, a row for each frame number */
hashval = atoi(s);
return(hashval % FRAME_HASH_SIZE);
}


char *strsave(char *s) /*save char array s somewhere*/
{
char *p;
if((p = malloc( strlen(s) +  1))) strlcpy(p, s, strlen(s) + 1);
return(p);
}


struct frame *lookup_frame(char *name)
{
struct frame *pa;

for(pa = frametab[hash(name)]; pa != 0; pa = pa -> nxtentr)
	{
	if(strcmp(pa -> name, name) == 0) return(pa);
	}

return 0; /* not found */
}/* end function lookup_frame */


struct frame *install_frame(char *name)
{
struct frame *pa, *pnew, *pnext;
int hashval;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "installframe(): arg name=%s\n", name);
	}

/* allow multiple entries with the same name */
//pa = lookup_frame(name);
pa = 0;

if(!pa) /* not found */
	{
	/* create new structure */
	pnew = (struct frame *) calloc(1, sizeof(*pnew) );
	if(! pnew) return 0;
	pnew -> name = strsave(name);
	if(! pnew -> name) return 0;

	/* get next structure */
	hashval = hash(name);
	pnext = frametab[hashval];/* may be zero, if there was nothing */

	/* insert before next structure (if any, else at start) */
	frametab[hashval] = pnew;

	/* set pointers for next structure */
	if(pnext) pnext -> prventr = pnew;

	/* set pointers for new structure */
	pnew -> nxtentr = pnext;
	pnew -> prventr = 0;/* always inserting at start of chain of structures */

	return pnew;/* pointer to new structure */
	}/* end if not found */

return pa;
}/* end function install_frame */


int delete_all_frames()
{
struct frame *pa;
int i;

for(i = 0; i < FRAME_HASH_SIZE; i++)/* for all structures at this position */
	{
	while(1)
		{
		pa = frametab[i];
		if(! pa) break;
		frametab[i] = pa -> nxtentr;
					/* frametab entry points to next one,
					this could be 0
					*/
		free(pa -> name);/* free name */
		free(pa -> data);
		free(pa);/* free structure */
		}/* end while all structures hashing to this value */
	}/* end for all entries in frametab */

return(0);/* not found */
}/* end function delete_all_frames */


int add_frame(\
	char *name, char *data, int object_type,\
	int xsize, int ysize, int zsize, int id)
{
struct frame *pa;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "add_frame(): arg name=%s\n\
	data=%lu\n\
	object_type=%d\n\
	xsize=%d ysize=%d zsize=%d\n\
	id=%d\n",\
	name,\
	(unsigned long)data,\
	object_type,\
	xsize, ysize, zsize,\
	id);
	}

/* argument check */
if(! name) return 0;
if(! data) return 0;

pa = install_frame(name);
if(! pa) return(0);

pa -> data = strsave(data);
if(! pa -> data) return(0);

pa -> type = object_type;

pa -> xsize = xsize;
pa -> ysize = ysize;
pa -> zsize = zsize;

pa -> id = id;

pa -> pfd = vo_font;

pa -> status = NEW_ENTRY;

return 1;
}/* end function add_frame */


int process_frame_number(int frame_nr)
{
struct frame *pa;
char temp[80];

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "subtitler(): process_frame_number(): arg frame_nr=%d\n",\
	frame_nr);
	}

tc_snprintf(temp, sizeof(temp), "%d", frame_nr);
for(pa = frametab[hash(temp)]; pa != 0; pa = pa -> nxtentr)
	{
	if(strcmp(pa -> name, temp) == 0)
		{
		/* parse data here */
		parse_frame_entry(pa);
		} /* end if frame number matches */
	} /* end for all entries that hash to this frame number */

return 1;
} /* end function process_frame_number */


int set_end_frame(int frame_nr, int end_frame)
{
struct frame *pa;
char temp[80];

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "set_end_frame(): frame_nr=%d end_frame=%d\n",\
	frame_nr, end_frame);
	}

tc_snprintf(temp, sizeof(temp), "%d", frame_nr);
for(pa = frametab[hash(temp)]; pa != 0; pa = pa -> nxtentr)
	{
#if 0
	tc_log_msg(MOD_NAME, "WAS pa->type=%d pa->name=%s frame_nr=%d end_frame=%d\n",\
	pa -> type, pa -> name, frame_nr, end_frame);
#endif

	if(pa -> type == FORMATTED_TEXT)
		{
		if(atoi(pa -> name) == frame_nr)
			{
			pa -> end_frame = end_frame;

			return 1;
			}
		} /* end if type FORMATTED_TEXT */
	} /* end for all entries that hash to this frame number */

/* not found */
return 0;
} /* end function set_end_frame */


static struct subtitle_fontname *lookup_subtitle_fontname(char *name)
{
struct subtitle_fontname *pa;

for(pa = subtitle_fontnametab[0]; pa != 0; pa = pa -> nxtentr)
	{
	if(strcmp(pa -> name, name) == 0) return(pa);
	}

return 0; /* not found */
}/* end function lookup_subtitle_fontname */


static struct subtitle_fontname *install_subtitle_fontname_at_end_of_list(char *name)
{
struct subtitle_fontname *plast, *pnew;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "install_subtitle_fontname_at_end_off_list(): arg name=%s\n", name);
	}

pnew = lookup_subtitle_fontname(name);
if(pnew) return(pnew);/* already there */

/* create new structure */
pnew = (struct subtitle_fontname *) calloc(1, sizeof(*pnew) );
if(! pnew) return 0;
pnew -> name = strsave(name);
if(! pnew -> name) return 0;

/* get previous structure */
plast = subtitle_fontnametab[1]; /* end list */

/* set new structure pointers */
pnew -> nxtentr = 0; /* new points top zero (is end) */
pnew -> prventr = plast; /* point to previous entry, or 0 if first entry */

/* set previous structure pointers */
if( !subtitle_fontnametab[0] ) subtitle_fontnametab[0] = pnew; /* first element in list */
else plast -> nxtentr = pnew;

/* set array end pointer */
subtitle_fontnametab[1] = pnew;

return(pnew);/* pointer to new structure */
}/* end function install_subtitle_fontname */


#if 0  /* unused --AC */
int delete_subtitle_fontname(int subtitle_fontnamenr)/* delete entry from double linked list */
{
struct subtitle_fontname *pa, *pprev, *pdel, *pnext;
char name[80];

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "delete_subtitle_fontname(): arg subtitle_fontnamenr=%d\n", subtitle_fontnamenr);
	}

tc_snprintf(name, sizeof(name), "%d", subtitle_fontnamenr);
pa = subtitle_fontnametab[0];
while(1)
	{
	/* if end list, return not found */
	if(! pa) return 0;

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
	/* if first one, modify subtitle_fontnametab[0] */
	if(pprev == 0) subtitle_fontnametab[0] = pnext;
	else pprev -> nxtentr = pnext;

	/* set pointers for next structure */
	/* if last one, modify subtitle_fontnametab[1] */
	if(pnext == 0) subtitle_fontnametab[1] = pprev;
	else pnext -> prventr = pprev;

	/* delete structure */
	free(pdel -> name);
	free(pdel); /* free structure */

	/* return OK deleted */
	return 1;
	}/* end for all structures */
}/* end function delete_subtitle_fontname */
#endif /* 0 */


#if 0  /* unused --AC */
int delete_all_subtitle_fontnames(void)/* delete all entries from table */
{
struct subtitle_fontname *pa;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "delete_all_subtitle_fontnames() arg none\n");
	}

while(1)
	{
	pa = subtitle_fontnametab[0];
	if(! pa) break;
	subtitle_fontnametab[0] = pa -> nxtentr;

	free(pa -> name);
	free(pa);/* free structure */
	}/* end while all structures */

subtitle_fontnametab[1] = 0;
return 1;
}/* end function delete_all_subtitle_fontnames */
#endif /* 0 */


font_desc_t *add_font(\
	char *name, int symbols, int size, int iso_extension, double outline_thickness, double blur_radius)
{
struct subtitle_fontname *ps;
font_desc_t *pfd;
char temp[4096];

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "add_font(): arg name=%s symbols=%d size=%d iso_extension=%d outline_thickness=%.2f blur_radius=%.2f\n",\
	name, symbols, size, iso_extension, outline_thickness, blur_radius);
	}

tc_snprintf(temp, sizeof(temp), "%s_%d_%d_%d_%.2f_%.2f", name, symbols, size, iso_extension, outline_thickness, blur_radius);
ps = lookup_subtitle_fontname(temp);
if(ps) /* found in list */
	{
	/* get pointer from list */

	pfd = ps -> pfd;

	return pfd;
	} /* end if found in list */
else /* not in fontname_list */
	{
	/* if not there yet, create this font and add to list */

	pfd = make_font(\
		name, symbols, size, iso_extension, outline_thickness, blur_radius);
	if(! pfd)
		{
		/* try the default font settings */
		tc_log_msg(MOD_NAME, "subtitler(): add_font(): could not create requested font %s, trying default font\n", temp);

		pfd = make_font(\
			default_subtitle_font_name, default_subtitle_symbols,
			default_subtitle_font_size, default_subtitle_iso_extention,\
			default_subtitle_radius, default_subtitle_thickness);
		if(! pfd)
			{
			tc_log_msg(MOD_NAME, "subtitler(): add_font(): could not create any font for %s\n", temp);

			return 0;
			}

		tc_snprintf(temp, sizeof(temp), "%s_%d_%d_%d_%.2f_%.2f",\
			default_subtitle_font_name,\
			default_subtitle_symbols,\
			default_subtitle_font_size,\
			default_subtitle_iso_extention,\
			default_subtitle_radius,\
			default_subtitle_thickness);

		} /* end if default font failed */
	} /* end if not in fontname_list */

/* font created OK */

/* add to list */
ps = install_subtitle_fontname_at_end_of_list(temp);
if(! ps)
	{
	tc_log_msg(MOD_NAME, "subtitler(): add_font(): could not add subtitle font %s to subtitle_fontname_list\n", temp);

	return 0;
	}

/* set pointer to font in fontname_list */
ps -> pfd = pfd;

return pfd;
} /* end function add_font */


