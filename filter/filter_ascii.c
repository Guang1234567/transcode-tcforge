/*
 *  filter_ascii.c
 *
 *  Copyright (C) Julien Tierny <julien.tierny@wanadoo.fr> - October 2004
 *  Copyright (C) Thomas Oestreich - June 2001
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
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA].
 *
 */

#define MOD_NAME    		"filter_ascii.so"
#define MOD_VERSION 		"v0.5 (2004-12-08)"
#define MOD_CAP     		"Colored ascii-art filter plugin; render a movie into ascii-art."
#define MOD_AUTHOR		"Julien Tierny"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

/* For RGB->YUV conversion */
#include "libtcvideo/tcvideo.h"

#define MAX_LENGTH 		1024
#define TMP_FILE		"raw"
#define TMP_STRING_SIZE		7


/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/


typedef struct parameter_struct{
	char	aart_font[PATH_MAX];
	char	aart_pallete[PATH_MAX];
	int	aart_threads;
	int	aart_buffer;
	TCVHandle tcvhandle;
} parameter_struct;

static parameter_struct *parameters = NULL;

static void help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help\n"
"\n* Overview:\n"
"  This filter renders a video sample into colored ascii art, using the `aart` package.\n"
"  Both YUV and RGB formats are supported, in multithreaded mode.\n"
"\n* Warning:\n"
"  Rendering a video sample into colored ascii art might take a VERY LONG TIME for the moment.\n"
"  Please only consider short video samples for this very version of the filter.\n"
"\n"
"* Options:\n"
"  'font':    Valid PSF font file (provided with the `aart` package)\n"
"  'pallete': Valid PAL pallete file (provided with the `aart` package)\n"
"  'threads': Use multiple-threaded routine for picture rendering (recommended = 1)\n"
"  'buffer':  Use `aart` internal buffer for output (recommended off)\n"
		, MOD_CAP);
}

static int write_tmpfile(char* header, char* content, int content_size, int slot_id){
	FILE* 	tmp = NULL;
	int 	i = 0;
	char*	filename = NULL;

	filename = tc_malloc(sizeof(char)*(strlen(TMP_FILE) + TMP_STRING_SIZE));
	if (!filename){
		tc_log_error(MOD_NAME, "Out of memory !!!");
		return -1;
	}

	if (verbose & TC_DEBUG)
		tc_log_info(MOD_NAME, "Temporary filename correctly allocated.");
	tc_snprintf(filename, strlen(TMP_FILE) + TMP_STRING_SIZE, "%s-%d.tmp", TMP_FILE, slot_id);

	tmp = fopen(filename, "w");
	if (!tmp){
		tc_log_error(MOD_NAME, "Cannot write temporary file !");
		return -1;
	}
	for(i=0; i<strlen(header); i++)
		fputc(header[i], tmp);

	for(i=0; i< content_size; i++)
		fputc(content[i], tmp);

	fclose(tmp);
	free(filename);
	return 0;
}

static int parse_stream_header(FILE* stream, int width){
	char	cursor = 0;
	int		aart_width = 0;

	/* Purge the first line of the header */
	while (cursor!='\n')
		cursor = fgetc(stream);

	/* Purge additionnal commentary lines */
	while (cursor == '#')
		while ((cursor = fgetc(stream)) != '\n');
	cursor = fgetc(stream);

	/* Purge dimensions line */
	while (cursor!=' '){
		/* We have to check the width in case of re-size */
		aart_width = 10*aart_width + ((int) cursor - 48);
		cursor = fgetc(stream);
	}
	if ((aart_width != width) && (verbose & TC_DEBUG))
		tc_log_warn(MOD_NAME, "Picture has been re-sized by `aart`.");

	/* Purge the rest of the line */
	while (cursor!='\n')
		cursor = fgetc(stream);

	cursor = fgetc(stream);

	/* Purge dynamic line */
	while (cursor!='\n')
		cursor = fgetc(stream);

	return aart_width;
}

static int aart_render(char* buffer, int width, int height, int slot_id, char* font, char* pallete, int threads, int buffer_option){
	char 	pnm_header[255] = "",
			cmd_line[MAX_LENGTH] = "",
			buffer_option_string[PATH_MAX] = "";
	FILE* 	aart_output = NULL;
	int		i = 0,
			j = 0,
			resize = 0;

	if (verbose & TC_DEBUG)
		tc_log_info(MOD_NAME, "Formating buffer option string.");
	if (buffer_option != 1)
		tc_snprintf(buffer_option_string, strlen("--nobuffer"), "--nobuffer");
	if (verbose & TC_DEBUG)
		tc_log_info(MOD_NAME, "Buffer option string correctly formated.");


	tc_snprintf(cmd_line, MAX_LENGTH, "aart %s-%d.tmp --font %s --pallete %s --inmod=pnm --outmod=pnm %s --threads=%d", TMP_FILE, slot_id, font, pallete, buffer_option_string, threads);

	tc_snprintf(pnm_header, 255, "P6\n%d %d\n255\n", width, height);

	if (write_tmpfile(pnm_header, buffer, width*height*3, slot_id) == -1)
		return -1;

	if (!(aart_output = popen(cmd_line, "r"))){
		tc_log_error(MOD_NAME, "`aart` call failure !");
		return -1;
	}

	resize = parse_stream_header(aart_output, width);

	/* Now, let's fill the buffer */
	for (i=0; i<=(width*height*3); i++){
		if (j == width*3){
			/* We reached an end of row and skip aart additionnal pixels */
			for (j=0; j< 3*(resize - width); j++)
				fgetc(aart_output);
			j = 0;
		}
		buffer[i] = fgetc(aart_output);
		j++;
	}

	pclose(aart_output);
	return 0;
}

static int clean_parameter(char* parameter){
	/* Purges extra character from parameter string */
	int i=0;

	while (parameter[i] != '\0'){
		if (parameter[i] == '=') parameter[i] = '\0';
		i++;
	}
	if (verbose & TC_DEBUG)
		tc_log_info(MOD_NAME, "Extra-paramater correctly cleaned.");
	return 0;
}

static int init_slots(int slots[]){
	int i = 0;
	for (i=0; i<TC_FRAME_THREADS_MAX; i++)
		slots[i] = 0;
	return 0;
}

static int find_empty_slot(int frame_id, int *slots){
	int i = 0;
	while((slots[i]!=0)&&(i<TC_FRAME_THREADS_MAX))
		i++;
	if (i<TC_FRAME_THREADS_MAX)
		slots[i] = frame_id;
	if (verbose & TC_DEBUG)
		tc_log_info(MOD_NAME, "Found empty slot %d for frame %d.", i, frame_id);
	return i;
}

static int free_slot(int frame_id, int *slots){
	int i = 0;
	while ((slots[i]!=frame_id)&&(i<TC_FRAME_THREADS_MAX))
		i++;
	/*
	 * TODO:
	 * Provide a pthread_mutex lock system.
	 * Right now, 2 threads might be able to write
	 * in the same slot (case never encountered so far),
	 * which would cause issues at free step.
	 */
	if (i<TC_FRAME_THREADS_MAX)
		slots[i] = 0;
	if (verbose & TC_DEBUG)
		tc_log_info(MOD_NAME, "Slot %d correctly free.", i);
	return 0;
}

int tc_filter(frame_list_t *ptr_, char *options){
	vframe_list_t *		ptr = (vframe_list_t *)ptr_;
	int 			frame_slot = 0;
	static 			vob_t *vob=NULL;
	static int		slots[TC_FRAME_THREADS_MAX];

  if(ptr->tag & TC_FILTER_GET_CONFIG) {

	optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "Julien Tierny", "VRYMO", "1");
    optstr_param (options, "font", "Valid PSF font file (provided with the `aart` package)", "%s", "default8x9.psf");
	optstr_param (options, "pallete", "Valid pallete file (provided with the `aart` package)", "%s", "colors.pal");
	optstr_param(options, "threads", "Use multiple-threaded routine for picture rendering", "%d", "0", "1", "oo");

	/* Boolean parameter */
	optstr_param(options, "buffer", "Use `aart` internal buffer for output", "", "-1");

	return 0;
  }

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL)
		return(-1);

	/* aart sanity check */
	if (tc_test_program("aart") !=0 )
		return -1;

	/* Now, let's handle the options ... */
	if((parameters = tc_malloc (sizeof(parameter_struct))) == NULL){
		tc_log_error(MOD_NAME, "Out of memory !!!");
		return -1;
	}

	/* Filter default options */
	if (verbose & TC_DEBUG)
		tc_log_info(MOD_NAME, "Preparing default options.");
	strncpy(parameters->aart_font, "default8x9.psf", strlen("default8x9.psf"));
	if (verbose & TC_DEBUG)
		tc_log_info(MOD_NAME, "Default options correctly formated.");
	strncpy(parameters->aart_pallete, "colors.pal", strlen("colors.pal"));
	parameters->aart_threads 		= 1;
	parameters->aart_buffer 		= -1;
	parameters->tcvhandle			= 0;

	if (options){
		/* Get filter options via transcode core */
		if (verbose & TC_DEBUG)
			tc_log_info(MOD_NAME, "Merging options from transcode.");
		optstr_get(options, "font",			"%s",		parameters->aart_font);
		clean_parameter(parameters->aart_font);
		optstr_get(options, "pallete",		"%s",		parameters->aart_pallete);
		clean_parameter(parameters->aart_pallete);
		optstr_get(options, "threads",   	"%d",		&parameters->aart_threads);

		if (optstr_lookup(options, "buffer") != NULL)
			parameters->aart_buffer=1;
		if (optstr_lookup(options, "help") != NULL)
			help_optstr();
		if (verbose & TC_DEBUG)
			tc_log_info(MOD_NAME, "Options correctly merged.");
	}

	if (vob->im_v_codec == TC_CODEC_YUV420P){
		if (!(parameters->tcvhandle = tcv_init())) {
			tc_log_error(MOD_NAME, "Error at image conversion initialization.");
			return(-1);
		}
	}

	/* Init thread slots (multithread support)*/
	init_slots(slots);

	if(verbose)
		tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE) {

  	/*
	 * TODO :
	 * Provide a `aart` kill routine in case of cancel.
	 * For the moment, transcode waits for the `aart`
	 * process to finish before exiting.
	 */

	tcv_free(parameters->tcvhandle);

	/* Let's free the parameter structure */
	free(parameters);
	parameters = NULL;

    return(0);
  }

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------

	if(ptr->tag & TC_POST_M_PROCESS && ptr->tag & TC_VIDEO && !(ptr->attributes & TC_FRAME_IS_SKIPPED)) {

		frame_slot = find_empty_slot(ptr->id, slots);
		switch(vob->im_v_codec){
			case TC_CODEC_RGB24:
				return aart_render(ptr->video_buf, ptr->v_width, ptr->v_height, frame_slot, parameters->aart_font, parameters->aart_pallete, parameters->aart_threads, parameters->aart_buffer);
				break;

			case TC_CODEC_YUV420P:

				if (!tcv_convert(parameters->tcvhandle, ptr->video_buf, ptr->video_buf, ptr->v_width, ptr->v_height, IMG_YUV_DEFAULT, IMG_RGB24)){
					tc_log_error(MOD_NAME, "cannot convert YUV stream to RGB format !");
					return -1;
				}

				if (aart_render(ptr->video_buf, ptr->v_width, ptr->v_height, frame_slot, parameters->aart_font, parameters->aart_pallete, parameters->aart_threads, parameters->aart_buffer) == -1){return -1;}
				if (!tcv_convert(parameters->tcvhandle, ptr->video_buf, ptr->video_buf, ptr->v_width, ptr->v_height, IMG_RGB24, IMG_YUV_DEFAULT)){
					tc_log_error(MOD_NAME, "cannot convert RGB stream to YUV format !");
					return -1;
				}
				break;

			default:
				tc_log_error(MOD_NAME, "Internal video codec is not supported.");
				return -1;
		}
		free_slot(ptr->id, slots);
	}
	return(0);
}
