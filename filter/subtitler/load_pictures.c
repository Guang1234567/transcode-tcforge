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

#define LIMIT(x) ((((x)>0xffffff)?0xff0000:(((x)<=0xffff)?0:(x)&0xff0000))>>16)


char *ppm_to_yuv_in_char(char *pathfilename, int *xsize, int *ysize)
{
/*
reads ppm file into buffer in yuyv format.
*/
char *out_ptr;
int c, i, j;
int r, g, b;
int width = 0, height = 0, maxval = 0;
double y, u, v;
double cr, cg, cb, cu, cv;
char temp[4096];
FILE *fptr;
int u_time;
char *buffer;
int comment_flag;

/* color standard */
/* Y spec */
cr = 0.3;
cg = 0.59;
cb = 0.11;

/* U spec */
cu = .5 / (1.0 - cb);

/* V spec */
cv = .5 / (1.0 - cr);

/* open the ppm format file */
fptr = fopen(pathfilename, "rb");
if(!fptr)
	{
	tc_log_msg(MOD_NAME, "subtitler(): ppm_to_yuv_in_char(): could not open file %s for read\n", pathfilename);

	strerror(errno);
	return 0;
	}

/*
cjpeg writes like this:
 "P6\n%ld %ld\n%d\n"
*/

/*
display writes like this:
P6
# CREATOR: XV Version 3.10a  Rev: 12/29/94

125 107
255
*/

/*
using fscanf here swallows control M after \n,
resulting in wrong color, perhaps a libc bug?
*/

/* parse the header */
i = 0;
j = 0;
comment_flag = 0;
while(1)
	{
	while(1)
		{
		errno = 0;
		c = getc(fptr);
		if(errno == EAGAIN) continue;
		if(errno == EINTR) continue;
		break;
		}
	if(c == EOF)
		{
		fclose(fptr);
		tc_log_msg(MOD_NAME, "ppm_to_yuv_in_char(): early EOF in header\n");
		return 0;
		}

	if(c == '#') comment_flag = 1;

	if( (c == '\n') || (c == '\r') ) comment_flag = 0;

	if(comment_flag) continue;

	temp[i] = c;
	if( (c == '\n') || (c == '\t') || (c == '\r') || (c == ' ') )
		{
		temp[i] = 0;
//tc_log_msg(MOD_NAME, "j=%d i=%d temp=%s\n", j, i, temp);

		/* test if a field present in line */
		if(i != 0)
			{
			if(j == 0) {}; /* scip the P6 */
			if(j == 1) width = atoi(temp);
			if(j == 2) height = atoi(temp);
			if(j == 3) maxval = atoi(temp);
			j++;
			}
		i = 0;
		}/* end if white space */
	else i++;
	if(j == 4) break;
	}/* end for all chars in header */

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "ppm_to_yuv_in_char(): width=%d height=%d maxval=%d\n",\
	width, height, maxval);
	}

*xsize = width;
*ysize = height;

buffer = (char *) malloc(width * height * 3); // should be 2 ?
if(! buffer)
	{
	tc_log_msg(MOD_NAME, "subtitler(): ppm_to_yuv_in_char(): malloc buffer failed\n");

	return 0;
	}

out_ptr = buffer;

for (i = 0; i < height; i++)
	{
	/* write Y, U, Y, V */
	if(debug_flag)
		{
		tc_log_msg(MOD_NAME, "ppm_to_yuv_in_char(): i=%d j=%d\n", i, j);
		}

	u_time = 1;
	for(j = 0; j < width; j++)
		{
		/* red */
		while(1)
			{
			errno = 0;
			r = getc(fptr);
			if(errno == EAGAIN) continue;
			if(errno == EINTR) continue;
			break;
			}

		if(r == EOF)
			{
			tc_log_msg(MOD_NAME, "ppm_to_yuv_in_char(): early EOF r\n");
			/*
			this is a hack to fix early EOF in the output from cjpeg
			Substitute black.
			*/
			r = 0;
/*			break;*/

			}

		/* green */
		while(1)
			{
			errno = 0;
			g = getc(fptr);
			if(errno == EAGAIN) continue;
			if(errno == EINTR) continue;
			break;
			}
		if(g == EOF)
			{
			tc_log_msg(MOD_NAME, "ppm_to_yuv_in_char(): early EOF g\n");
			g = 0;
/*			break;*/
			}

		/* blue */
		while(1)
			{
			errno = 0;
			b = getc(fptr);
			if(errno == EAGAIN) continue;
			if(errno == EINTR) continue;
			break;
			}
		if(b == EOF)
			{
			tc_log_msg(MOD_NAME, "ppm_to_yuv_in_char(): early EOF b\n");
			b = 0;
/*			break;*/
			}

		/* convert to YUV */

		/* test yuv coding here */
		y = cr * r + cg * g + cb * b;

		y = (219.0 / 256.0) * y + 16.5;  /* nominal range: 16..235 */

		*out_ptr = y;
		out_ptr++;

		if(u_time)
			{
			u = cu * (b - y);
			u = (224.0 / 256.0) * u + 128.5; /* 16..240 */

			*out_ptr = u;
			}
		else
			{
			v = cv * (r - y);
			v = (224.0 / 256.0) * v + 128.5; /* 16..240 */

			*out_ptr = v;
			}

		out_ptr++;
		u_time = 1 - u_time;
		}/* end for j horizontal */

	}/* end for i vertical */

/* close the input file */
fclose(fptr);

return buffer;
}/* end function ppm_to_yuv_in_char */


int yuv_to_ppm(char *data, int xsize, int ysize, char *filename)
{
/*
creates a .ppm format file from a YUYV character buffer.
*/
int x, y;
FILE *fptr;
char *py, *pu, *pv;
int cr, cg, cb, cy, cu = 0, cv = 0;
int r, g, b;
int u_time;
int odd_line;
int odd_xsize;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "subtitler(): yuv_to_ppm(): arg data=%lu\n\
	xsize=%d ysize=%d filename=%s\n",\
	(unsigned long)data, xsize, ysize, filename);
	}

/* open ppm file */
fptr = fopen(filename, "w");
if(! fptr)
	{
	tc_log_msg(MOD_NAME, "subtitler(): yuv_to_ppm(): could not open %s for write\n",\
	filename);

	return 0;
	}

/* write a ppm header */
fprintf(fptr, "P6\n%i %i\n255\n", xsize, ysize);

/* YUYVYUYV */
py = data;
pu = data + 1;
pv = data + 3;
u_time = 1;
odd_xsize = xsize % 2;

for(y = 0; y < ysize; y++)
	{
	odd_line = y % 2;

	/* get a line from buffer start */
	for(x = 0; x < xsize; x++)
		{
		/* calculate RGB */
		cy = ( (0xff & *py) - 16);

		/*
		prevent data to be cut away, by replacing it with the nearest.
		Normally the variable border_luminance is set to greater then 255,
		(so a value that will never happen) except in rotate and shear,
		where we use this luminance level to cut away the left over edges
		of the picture after mogrify processes it.
		*/
		/* do not use top white or greater for mask */
		if(cy != 255)
			{
			if(cy == YUV_MASK) cy += 1;
			}

		cy  *= 76310;

		/* increment Y pointer */
		py += 2;

		if(u_time)
			{
			if(odd_xsize)
				{
				if(! odd_line)
					{
					cu = (0xff & *pu) - 128;
					cv = (0xff & *pv) - 128;
					}
				else /* even line */
					{
					cu = (0xff & *pv) - 128;
					cv = (0xff & *pu) - 128;
					}
				}
			else /* even xsize */
				{
				cu = (0xff & *pu) - 128;
				cv = (0xff & *pv) - 128;
				}

			pu += 4;
			pv += 4;
			}

		cr = 104635 * cv;
		cg = -25690 * cu + -53294 * cv;
		cb = 132278 * cu;

		r = LIMIT(cr + cy);
		g = LIMIT(cg + cy);
		b = LIMIT(cb + cy);

		/* RGB to file */
		fprintf(fptr,\
		"%c%c%c", r, g, b);

		u_time = 1 - u_time;
		} /* end for all x */
	} /* end for all y */

/* close output file */
fclose(fptr);

return 1;
} /* end function yuv_to_ppm */

