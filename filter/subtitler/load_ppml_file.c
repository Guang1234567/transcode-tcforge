/*
part of xste subtitle editor
xste  is registered Copyright (C) 2001 Jan Panteltje
email: panteltje@zonnet.nl

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

pthread_t *movie_thread[MAX_MOVIES];

int line_number;

int load_ppml_file(char *pathfilename)
{
FILE *fptr;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "load_ppml_file(): arg pathfilename=%s", pathfilename);
	}

/* argument check */
if(! pathfilename) return 0;

fptr = fopen(pathfilename, "r");
if(! fptr)
	{
	tc_log_msg(MOD_NAME, "Could not open file %s for read", pathfilename);

	return 0;
	}

/* load the .xste file into the structure list */
if(! read_in_ppml_file(fptr) )
	{
	tc_log_msg(MOD_NAME, "subtitler(): read_in_ppml_file(): failed");

	return 0;
	}

return 1;
} /* end  function load_ppml_file */

extern int readline_ppml(FILE *, char *);
extern int set_end_frame(int, int);

int read_in_ppml_file(FILE *finptr)
{
int a;
char temp[READSIZE];
char arg0[1024];
char arg1[1024];
char arg2[1024];
char arg3[1024];
char *ptr;
char *ptr1 = 0;
int object_type;
int start_frame;
int old_start_frame = 0;
char *cptr;
int xsize, ysize, zsize;
int arguments, arguments_read;
char subtitler_args[1024];
int movie_number = 0;
int id = 0;
char *thread_arg;
FILE *fptr2;

/* clear the frame_list */
delete_all_frames();

/* start reading lines from the xste file */
line_number = 0;
while(1)
	{
	temp[0] = 0;
	/* read a line from the file */
	a = readline_ppml(finptr, temp); /* closes file if EOF */
	if(a == EOF)
		{
		return 1;
		}
//tc_log_msg(MOD_NAME, "WAS readline_ppml temp=%s strlen(temp)=%d", temp, strlen(temp) );

	if(debug_flag)
	{
		tc_log_msg(MOD_NAME, "read_in_ppml_file(): line read=%s", temp);
		}

	/* scip emty lines */
	if(temp[0] == 0) continue;

	/* scip lines starting with ';' these are comments */
	if(temp[0] == ';') continue;

	arg0[0] = 0;
	arg1[0] = 0;
	arg2[0] = 0;
	arg3[0] = 0;
	arguments_read = sscanf(temp, "%s %s %s %s", arg0, arg1, arg2, arg3);

	start_frame = atoi(arg0);

	/* set defaults in case parameters are omitted */
	xsize = 0;
	ysize = 0;
	zsize = 0;
	object_type = 0;

	if(arguments_read > 1) ptr = strstr(temp, arg1); // point to text
	else /* empty line with only a line number*/
		{
		ptr = strsave("");

		if(! ptr)
			{
			tc_log_msg(MOD_NAME, "subtitler(): strsave() malloc failed");

			exit(1);
			}
		}
	if(temp[0] == '*')
		{
		if(strcmp(arg1, "subtitle") == 0)
			{
			arguments = 1;
			object_type = SUBTITLE_CONTROL;
			ptr = strsave("");
			if(! ptr)
				{
				tc_log_msg(MOD_NAME, "subtitler(): load_ppml_file(): strsave() failed, aborting");

				exit(1);
				}
			}
		else if(strcmp(arg1, "text") == 0)
			{
			arguments = 3;
			object_type = X_Y_Z_T_TEXT;
			ptr = strstr(temp, arg2); // point to real text
			}
		else if(strcmp(arg1, "picture") == 0)
			{
			arguments = 3;
			object_type = X_Y_Z_T_PICTURE;
			ptr = strstr(temp, arg2); // point to filename
			}
		else if(strcmp(arg1, "movie") == 0)
			{
			arguments = 3;
			object_type = X_Y_Z_T_MOVIE;
			ptr = strstr(temp, arg2); // point to filename

			/* test if move file exists! */
			fptr2 = fopen(ptr, "r");
			if(!fptr2)
				{
				tc_log_msg(MOD_NAME, "subtitler(): file %s not found, aborting",\
				ptr);

				exit(1);
				}

			fclose(fptr2);
			}
		else if(strcmp(arg1, "main_movie") == 0)
			{
			arguments = 1;
			object_type = MAIN_MOVIE;

			/* empty, but NULL would cause error return in add_frame() */
			ptr = strsave("");
			if(! ptr)
				{
				tc_log_msg(MOD_NAME, "subtitler(): load_ppml_file(): strsave() failed, aborting");

				exit(1);
				}
			}
		else if(strcmp(arg1, "frame_counter") == 0)
			{
			arguments = 1;
			object_type = X_Y_Z_T_FRAME_COUNTER;
			ptr = strsave(""); // no arguments
			if(! ptr)
				{
				tc_log_msg(MOD_NAME, "subtitler(): strsave() malloc failed");

				exit(1);
				}
			}
		else /* syntax error?, fatal */
			{
			tc_log_msg(MOD_NAME, "subtitler(): ppml file: line %d\n\
			unknow object type referenced: %s, aborting",\
			line_number, arg1);

			exit(1);
			}

		if(arguments_read < arguments)
			{
			tc_log_msg(MOD_NAME, "subtitler(): read_in_ppml_file(): parse error in line %d\n\
			arguments required=%d, arguments_read=%d",\
			line_number, arguments, a);

			exit(1);
			}

		} /* end if object */

	/* NOTE ptr points to start arguments */
	if(object_type == MAIN_MOVIE)
		{
		/* no arguments, identified by type */
		}
	if(object_type == X_Y_Z_T_PICTURE)
		{
		/* load ppm file in buffer (ptr) */
		cptr = ppm_to_yuv_in_char(ptr, &xsize, &ysize);
		if(! cptr)
			{
			tc_log_msg(MOD_NAME, "subtitler(): could not read file %s", ptr1);

			exit(1);
			}

		/* point to yuv data */
		ptr = cptr;

//yuv_to_ppm(ptr, xsize, ysize, "/root/.subtitles/temp2.ppm");

		} /* end if object_type X_Y_Z_T_PICTURE */

	if(object_type == X_Y_Z_T_MOVIE)
		{
		/*
		start transcode for this filename, calling subtitler
		with write_ppm, this will cause transcode to write
		home_dir/.subtitles/movie_number.ppm, and set semaphore
		file home_dir/.subtitles/movie_number.sem.
		transcode will then wait until this semaphore file is
		removed again.
		ptr points to filename
		*/

		/* arguments to subtitler, no subtitles! (for now...) */

		/*
		note the leading space!!!!!!!!!!! else parsing fails in
		subtitler!
		For reasons unknow to me, when called from a thread, transcode
		leaves the ' in, when called from a shell not...... why?
		*/
		tc_snprintf(subtitler_args, sizeof(subtitler_args), " no_objects write_ppm movie_id=%d",\
		movie_number);

		/* arguments to transcode */
		tc_snprintf(temp, sizeof(temp),\
	" -i %s -x mpeg2,null -y null,null -V -J subtitler=%c%s%c",\
		ptr, '"', subtitler_args, '"');

		/*
		must stay allocated !, temp may change before the thread executes
		*/
		thread_arg = strsave(temp);
		if(! thread_arg)
			{
			tc_log_msg(MOD_NAME, "subtitler(): read_in_ppml_file():\n\
			malloc thread_arg failed, aborting");

			exit(1);
			}

		/* create a thread with a new instance of transcode */
		pthread_create(\
		(pthread_t *)&movie_thread[movie_number],\
		NULL,\
		(void *)&movie_routine,\
		thread_arg);

		/* movie_number to structure */
		id = movie_number;

		/*
		increment the movie_number, for the next movie definition
		*/
		movie_number++;
		} /* end if object_type X_Y_Z_T_MOVIE */

	start_frame += frame_offset;
	if(start_frame < 1)
		{
		tc_log_msg(MOD_NAME, "subtitler(): read_in_ppml_file(): WARNING:\n\
	line %d frame %d frame_offset %d causes frame values < 1",\
		line_number, start_frame, frame_offset);

		/* no exit here, also alert on *main etc */
		}

	/* update any decimal integer start frame8 to real frame */
	if( isdigit(arg0[0]) )
		{
		/* a subtitle, or an object manipulation */
		tc_snprintf(arg0, sizeof(arg0), "%d", start_frame);

		if(ptr[0] != '*')
			{
			/* only a subtitle */
			object_type = FORMATTED_TEXT;
			}

		/* one could look up the object type here, but later, parser */
		}

	if(object_type == FORMATTED_TEXT)
		{
		/*
		now we know the end frame of the previous entry, it is this start
		frame.
		look the entry up and modify that end_frame
		*/
		if(! set_end_frame(old_start_frame, start_frame) )
			{
			tc_log_msg(MOD_NAME, "subtitler(): could not set end_frame=%d for frame=%d",\
			start_frame, old_start_frame);
			}

		/* remember start_frame */
		old_start_frame = start_frame;

		} /* end if object_type FORMATTED_TEXT */

	/* add entry to linked list from here */


	/* add entry to structure */
	if(! add_frame(\
	arg0,\
	ptr,\
	object_type,\
	xsize, ysize, zsize,\
	id) )
		{
		tc_log_msg(MOD_NAME, "subtitler(): could not add_frame start_frame=%d, aborting",\
		start_frame);

		/* close input file */
		fclose(finptr);

		exit(1);
		}

	} /* end while all lines in .xste file */

return 1;
} /* end function read_in_ppml_file */


void *movie_routine(char *helper_flags)
{
int a, c, i, j, k;
char temp[4096];
pid_t pid;
char execv_args[51][1024];
char *flip[51];/* this way I do not have to free anything */
				/* arguments to ececlv are copied */
char helper_program[512];
int quote_flag;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "movie_routine(): arg helper_flags=%s", helper_flags);
	}

strlcpy(helper_program, "transcode", sizeof(helper_program)); /* program name */

/*
fill in the multidimensional argument array for execv()
the first one is the helper program its self
*/
strlcpy(execv_args[0], helper_program, 1024);
k = 0;
i = 1;
quote_flag = 0;
while(1)
	{
	if(helper_flags[k] == ' ')
		{
		k++;
		continue;
		}
	j = 0;
	while(1)
		{
		c = helper_flags[k];
		if(c == '"')
			{
			quote_flag = 1 - quote_flag;
			}

		if(! quote_flag)
			{
			if(c == ' ') c = 0;
			}

		execv_args[i][j] = c;
		if(c == 0) break;
		j++;
		k++;
		}
	i++;
	if(helper_flags[k] == 0) break;
	}
execv_args[i][0] = 0;

/*
the reason I am using char *flip[] is that I cannot assign a NULL to
execv_args[i].
The reason I copy the (temporary stack variable) execv_args[][] into a
*ptr[] is that, by not using strsave(), I do not have to free later.
ececlv() will copy its arguments, flip[] may be destroyed after the call to
execlv().
*/
temp[0] = 0;
i = 0;
while(1)
	{
	flip[i] = execv_args[i];
	if(execv_args[i][0] == 0)
		{
		flip[i] = temp;
		flip[i + 1] = NULL;
		break;
		}
	i++;
	}

if(debug_flag)
	{
	for(i = 0; execv_args[i][0] != 0; i++)
		{
		tc_log_msg(MOD_NAME, "i=%d execv_args[i]=%s flip[i]=%s",\
		i, execv_args[i], flip[i]);
		}
	}

/* report to user */
if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "Starting helper program %s %s",\
	helper_program, temp);
	}

/* fork */
pid = fork();
if(pid == 0)/* in child */
	{
	/*
	start the helper program
	NOTE: helper program  must be in the path or in the current directory.
	*/

	/* a NULL pointer is required as the last argument */
	a = execvp(helper_program, flip );
	if(a < 0)
		{
		if(debug_flag)
			{
			tc_log_msg(MOD_NAME, "Cannot start helper program execvp failed: %s %s errno=%d",\
			helper_program, temp, errno);
			}
		return(0);
		}
	}
else if(pid < 0)/* fork failed */
	{
	tc_log_msg(MOD_NAME, "subtitler(): Helper program fork failed");

	return(0);
	}
else/* parent */
	{
	}

return(0);
} /* end function movie_routine */

int readline_ppml(FILE *file, char *contents)
{
int a, c, i;
int status;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "readline_ppml(): arg file=%lu\n", (long)file);
	}

/* a back-slash before a \n signals a continuation of the current line */
#define HAVE_BACKSLASH		1

status = 0;
i = 0;
while(1)
	{
	if(i > READSIZE - 1) break;

	while(1)
		{
		c = getc(file);
		a = ferror(file);
		if(a)
			{
			tc_log_perror(MOD_NAME, "readline():");
			continue;
			}
		break;
		}
	if(feof(file) )
		{
		fclose(file);
		contents[i] = 0;/* EOF marker */

		line_number++;
		return(EOF);
		}

	if(c == '\\') status = HAVE_BACKSLASH;
	else if(c == '\n')
		{
		line_number++;
		if(status == HAVE_BACKSLASH)
			{
			/* continue line */
			status = 0;

			/* scip the back-slash AND the \n */
			if(i > 0) i--;
			continue;
			}
		else
			{
			contents[i] = 0; /* string termination */

			return 1; /* end of line */
			}
		}
	else
		{
		status = 0;
		}

	contents[i] = c;
	i++;
	} /* end for all chars */
/*
mmm since we are here, the line must be quite long, possibly something
is wrong.
Since I do not always check for a return 0 in the use uf this function,
just to be safe, gona force a string termination.
This prevents stack overflow, and variables getting overwritten.
*/
contents[i] = 0;/* force string termination */

line_number++;


if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "readline_ppml(): line %d to long, returning 0 contents=%s",\
	line_number, contents);
	}

return 0;
}/* end function readline_ppml */


