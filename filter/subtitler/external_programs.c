#include "subtitler.h"

extern int execute(char *);

char subtitles_dir[] = "";


char *change_picture_geometry(\
char *data, int xsize, int ysize, double *new_xsize, double *new_ysize,\
int keep_aspect,\
double zrotation,\
double xshear, double yshear)
{
int a, x, y;
char temp[1024];
char *ptr;

/*
returns new data adjusted for geometry, calls Imagemagick package mogrify.
*/
if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "change_picture_geometry(): data=%lu xsize=%d ysize=%d\n\
	new_xsize=%.2f new_ysize=%.2f keep_aspect=%d\n\
	zrotation=%.2f xshear=%.2f yshear=%.2f\n",\
	(unsigned long)data, xsize, ysize,\
	*new_xsize, *new_ysize, keep_aspect,\
	zrotation,\
	xshear, yshear);
	}

/* write data as a temp .ppm file */
tc_snprintf(temp, sizeof(temp), "%s/%s/temp.ppm", home_dir, subtitles_dir);
if(! yuv_to_ppm(data, xsize, ysize, temp) )
	{
	tc_log_msg(MOD_NAME, "subtitler(): change_picture_geometry(): yuv_to_ppm() error return\n");

	return 0;
	}

/*
NOTE to programmers: it seems mogrify (and possibly the other ImageMagic
programs) gets confused if you attempt ~/.subtitles/temp.ppm
It then thinks .subtitles is a picture type and reports:

panteltje:/video/test# mogrify -geometry 352x288 /root/.subtitles/temp.ppm
mogrify: no encode delegate for this image format (SUBTITLES/MAGICRMGWIV).

So, I am using home_dir now, although that is not a good way of doing this,
but /temp needs special write permissions....
*/

/* if '!' in mogrify, aspect is overruled */
if(keep_aspect) a = ' ';
else a = '!';

/* change geometry temp file, this overwrites the temp file */

if(xshear == 0.0)

/* workaround bug in mogrify that causes exit if xshear is zero */
if(yshear != 0)
	{
	if(xshear == 0.0) xshear = 0.001;
	}

if( (xshear != 0.0) || (yshear != 0.0) )
	{
	tc_snprintf(temp, sizeof(temp),\
"mogrify -geometry %dx%d%c  -rotate %.2f  -shear %.2fx%.2f  %s/%s/temp.ppm",\
	(int) *new_xsize, (int) *new_ysize, a,\
	zrotation,\
	xshear, yshear,\
	home_dir, subtitles_dir);
	}
else
	{
	tc_snprintf(temp, sizeof(temp),\
	"mogrify -geometry %dx%d%c  -rotate %.2f  %s/%s/temp.ppm",\
	(int) *new_xsize, (int) *new_ysize, a,\
	zrotation,\
	home_dir, subtitles_dir);
	}

if(!execute(temp) ) return 0;

/* load temp .ppm file */
tc_snprintf(temp, sizeof(temp), "%s/%s/temp.ppm", home_dir, subtitles_dir);
ptr = ppm_to_yuv_in_char(temp, &x, &y);

*new_xsize = (double)x;
*new_ysize = (double)y;

#if 0
	tc_log_msg(MOD_NAME, "WAS RELOAD x=%d y=%d *new_xsize=%.2f *new_ysize=%.2f\n",\
	x, y, *new_xsize, *new_ysize);
#endif

return ptr;
} /* end function change_picture_geometry */


int execute(char *command)
{
FILE *pptr;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "subtitler() execute(): arg command=%s\n", command);
	}

pptr = popen(command, "r");
if(pptr <= 0)
	{
	tc_log_perror(MOD_NAME, "command");

	return 0;
	}

pclose(pptr);

return 1;
} /* end function execute */

