#include "subtitler.h"


void adjust_color(int *u, int *v, double degrees, double saturation)
{
double du, dv;
double dcolor, dsaturation;
double dsine;
double dsat;
int tmp_int;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "subtitler(): adjust_color(): arg\n\
	*u=%d *v=%d degrees=%.3f saturation=%.3f\n",\
	*u, *v, degrees, saturation);
	}

/*
The U and V signals were intended for quadrature modulation on the 4.43
PAL carrier, lets do it:
       color vector, angle alpha sets color, amplitude sets saturation
     u /
     |/ alpha
-v -- -- v
     |
    -u
*/

/* no color no action, prevent nan */
if( (*u == 0) && (*v == 0) ) return;

/* calculate the vector amplitude (hypotenusa) */
du = (double)*u;
dv = (double)*v;
dsaturation = sqrt( (du * du) + ( dv * dv ) );

/* calc multiplyer from percentage 0-100 */
dsat  = saturation / 100.0;

/*
I wont apply a flipping vector each line, as in PAL, so as to keep our
quadrature modulator simple, we have no phase errors here to correct.
Phase errors in PAL cancel, but leave small saturation (vector amplitude)
errors.
*/

/* calculate the sine */
dsine = (du / dsaturation);

/* dsine must be in the range -1 to +1, else errno. */

/* get the vector angle */
errno = 0;
dcolor = asin(dsine);
if(errno == EDOM)
	{
	tc_log_perror(MOD_NAME, "subtitler(): rotate_color(): asin NOT A NUMBER :-)");

	/* abort */
	exit(1);
	}

/* if V is negative, we move to the other 2 quadrants */
if(dv < 0) dcolor = M_PI - dcolor;

/* add the hue to the vector angle, -PI/2 to PI/2 (inclusive) */
dcolor += (degrees * M_PI) / 180.0;

/* change saturation by changing the vector amplitude */
dsaturation *= dsat;

/* demodulate :) our quadrature demodulator */
tmp_int = sin(dcolor) * dsaturation;
*u = tmp_int;
tmp_int = cos(dcolor) * dsaturation;
*v = tmp_int;

/* and do this for each pixel...... */

return;
} /* end function adjust_color */


int chroma_key(int u, int v, double color,\
double color_window, double saturation)
{
double da, du, dv, ds;
double dcolor, dvector;
double dsine;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "subtitler(): chroma_key(): arg\n\
	u=%d v=%d color=%.3f color_window=%.3f saturation=%.3f\n",\
	u, v, color, color_window, saturation);
	}

/*
The U and V signals were intended for quadrature modulation on the 4.43
PAL carrier, lets do it:
       color vector, angle alpha sets color, amplitude sets saturation
     u /
     |/ alpha
-v -- -- v
     |
    -u
*/

/* no color no action, prevent nan */
if( (u == 0) && (v == 0) ) return 0;

/* calculate the vector amplitude (hypotenusa) */
du = (double)u;
dv = (double)v;
dvector = sqrt( (du * du) + ( dv * dv ) );

/* calculate if enough saturation (chroma level) */

/* saturation is specified as 0-100% */
/* range 0-1 */
ds = saturation / 100.0;

/* multiply by maximum vector amplitude possible */
ds *= dmax_vector; // set in init

/* if not this much color, return no match */
if(dvector < ds) return 0;

/* calculate the sine */
dsine = (du / dvector);

/* dsine must be in the range -1 to +1, else errno. */

/* get the vector angle */
errno = 0;
dcolor = asin(dsine);
if(errno == EDOM)
	{
	tc_log_perror(MOD_NAME, "subtitler(): rotate_color(): asin NOT A NUMBER :-)");

	/* abort */
	exit(1);
	}

/* if V is negative, we move to the other 2 quadrants */
if(dv < 0) dcolor = M_PI - dcolor;

dcolor *= 180.0 / M_PI;

da = dcolor - color;

/* if color in range, return match */
if( fabs(da) < color_window) return 1;

return 0;
} /* end function chroma_key */


