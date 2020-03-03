/*
 *  filter_text
 *
 *  Copyright (C) Tilmann Bitterberg - April 2003
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
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
 * 	v0.1.4 -> v0.1.5:	(Allan Snider)
 * 		- change posdef to keypad location
 * 		- changed default font path, new standard location
 * 		- add "frame" option, similar to tstamp, but just
 * 			writes a frame number (ptr->id)
 */

#define MOD_NAME    "filter_text.so"
#define MOD_VERSION "v0.1.5 (2007-02-14)"
#define MOD_CAP     "write text in the image"
#define MOD_AUTHOR  "Tilmann Bitterberg"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include "video_trans.h"

// FreeType specific includes
#include <ft2build.h>
#ifdef FT_FREETYPE_H
#include FT_FREETYPE_H


// basic parameter

/* use keypad naviation */
enum POS { NONE=0,
	TOP_LEFT=7, TOP_CENTER=8, TOP_RIGHT=9,
	CTR_LEFT=4, CTR_CENTER=5, CTR_RIGHT=6,
	BOT_LEFT=1, BOT_CENTER=2, BOT_RIGHT=3 };

#define MAX_OPACITY 100

static unsigned char yuv255to224[] = {
 16,  17,  18,  19,  20,  20,  21,  22,  23,  24,  25,  26,  27,  27,  28,
 29,  30,  31,  32,  33,  34,  34,  35,  36,  37,  38,  39,  40,  41,  41,
 42,  43,  44,  45,  46,  47,  48,  49,  49,  50,  51,  52,  53,  54,  55,
 56,  56,  57,  58,  59,  60,  61,  62,  63,  63,  64,  65,  66,  67,  68,
 69,  70,  70,  71,  72,  73,  74,  75,  76,  77,  77,  78,  79,  80,  81,
 82,  83,  84,  85,  85,  86,  87,  88,  89,  90,  91,  92,  92,  93,  94,
 95,  96,  97,  98,  99,  99, 100, 101, 102, 103, 104, 105, 106, 106, 107,
108, 109, 110, 111, 112, 113, 114, 114, 115, 116, 117, 118, 119, 120, 121,
121, 122, 123, 124, 125, 126, 127, 128, 128, 129, 130, 131, 132, 133, 134,
135, 135, 136, 137, 138, 139, 140, 141, 142, 142, 143, 144, 145, 146, 147,
148, 149, 150, 150, 151, 152, 153, 154, 155, 156, 157, 157, 158, 159, 160,
161, 162, 163, 164, 164, 165, 166, 167, 168, 169, 170, 171, 171, 172, 173,
174, 175, 176, 177, 178, 179, 179, 180, 181, 182, 183, 184, 185, 186, 186,
187, 188, 189, 190, 191, 192, 193, 193, 194, 195, 196, 197, 198, 199, 200,
200, 201, 202, 203, 204, 205, 206, 207, 207, 208, 209, 210, 211, 212, 213,
214, 215, 215, 216, 217, 218, 219, 220, 221, 222, 222, 223, 224, 225, 226,
227, 228, 229, 229, 230, 231, 232, 233, 234, 235, 236, 236, 237, 238, 239, 240
};

typedef struct MyFilterData {
    /* public */
	unsigned int start;  /* start frame */
	unsigned int end;    /* end frame */
	unsigned int step;   /* every Nth frame */
	unsigned int dpi;    /* dots per inch resolution */
	unsigned int points; /* pointsize */
	char *font;          /* full path to font to use */
	int posx;            /* X offset in video */
	int posy;            /* Y offset in video */
	enum POS pos;        /* predifined position */
	char *string;        /* text to display */
	int fade;            /* fade in/out (speed) */
	int transparent;     /* do not draw a black bounding box */
	int tstamp;          /* */
	int frame;           /* string is "Frame: %06d", ptr->id */
	int antialias;       /* do sub frame anti-aliasing (not done) */
	int R, G, B;         /* color to apply in RGB */
	int Y, U, V;         /* color to apply in YUV */
	int flip;

    /* private */
	int opaque;          /* Opaqueness of the text */
	int boolstep;
	int top_space;
	int do_time;
	int start_fade_out;
	int boundX, boundY;
	int fade_in, fade_out;

	FT_Library  library;
	FT_Face     face;
	FT_GlyphSlot  slot;

} MyFilterData;

static MyFilterData *mfd = NULL;

static void help_optstr(void)
{
   tc_log_info (MOD_NAME, "(%s) help\n"
"* Overview\n"
"    This filter renders text into the video stream\n"
"* Options\n"
"         'range' apply filter to [start-end]/step frames [0-oo/1]\n"
"           'dpi' dots-per-inch resolution [96]\n"
"        'points' point size of font in 1/64 [25]\n"
"          'font' full path to font file [/usr/X11R6/.../arial.ttf]\n"
"        'string' text to print [date]\n"
"          'fade' Fade in and/or fade out [0=off, 1=slow, 10=fast]\n"
" 'notransparent' disable transparency\n"
"           'pos' Position (0-width x 0-height) [0x0]\n"
"        'posdef' Position (keypad number, 0=None) [0]\n"
"        'tstamp' add timestamp to each frame (overridden by string)\n"
"        'frame'  add frame number to each frame (overridden by string)\n"
		, MOD_CAP);
}

static void font_render(int width, int height, int codec, int w, int h, int i, uint8_t *p, uint8_t *q, uint8_t *buf)
{
    int error;

    //render into temp buffer
    if (codec == TC_CODEC_YUV420P || codec == TC_CODEC_YUV422P) {

        memset (buf, 16, height*width);
        memset (buf+height*width, 128, height*width/2);
	p = buf+mfd->posy*width+mfd->posx;

	for (i=0; i<strlen(mfd->string); i++) {

	    error = FT_Load_Char( mfd->face, mfd->string[i], FT_LOAD_RENDER);
	    mfd->slot = mfd->face->glyph;

	    if (verbose > 1) {
		// see http://www.freetype.org/freetype2/docs/tutorial/metrics.png
		/*
		tc_log_msg(MOD_NAME, "`%c\': rows(%2d) width(%2d) pitch(%2d) left(%2d) top(%2d) "
			   "METRIC: width(%2d) height(%2d) bearX(%2d) bearY(%2d)\n",
			   mfd->string[i], mfd->slot->bitmap.rows, mfd->slot->bitmap.width,
			   mfd->slot->bitmap.pitch, mfd->slot->bitmap_left, mfd->slot->bitmap_top,
			   mfd->slot->metrics.width>>6, mfd->slot->metrics.height>>6,
			   mfd->slot->metrics.horiBearingX>>6, mfd->slot->metrics.horiBearingY>>6);
			*/
	    }

	    for (h=0; h<mfd->slot->bitmap.rows; h++) {
		for (w=0; w<mfd->slot->bitmap.width; w++)  {
		    unsigned char c = mfd->slot->bitmap.buffer[h*mfd->slot->bitmap.width+w] &0xff;
		    c = yuv255to224[c];
		    // make it transparent
		    if (mfd->transparent && c==16) continue;

		    p[width*(h+mfd->top_space - mfd->slot->bitmap_top) +
			w+mfd->slot->bitmap_left] = c&0xff;

		    }
		}
	    p+=((mfd->slot->advance.x >> 6) - (mfd->slot->advance.y >> 6)*width);
	}

    } else if (codec == TC_CODEC_RGB24) {

        memset (buf, 0, height*width*3);
	p = buf + 3*(height-mfd->posy)*width + 3*mfd->posx;

	for (i=0; i<strlen(mfd->string); i++) {

	    // render the char
	    error = FT_Load_Char( mfd->face, mfd->string[i], FT_LOAD_RENDER);
	    mfd->slot = mfd->face->glyph; // shortcut

	    for (h=0; h<mfd->slot->bitmap.rows; h++) {
		for (w=0; w<mfd->slot->bitmap.width; w++)  {
		    unsigned char c = mfd->slot->bitmap.buffer[h*mfd->slot->bitmap.width+w];
		    c = c>254?254:c;
		    c = c<16?16:c;
		    // make it transparent
		    if (mfd->transparent && c==16) continue;

		    p[3*(width*(-(h+mfd->top_space - mfd->slot->bitmap_top)) +
			    w+mfd->slot->bitmap_left)-2] = c&0xff;
		    p[3*(width*(-(h+mfd->top_space - mfd->slot->bitmap_top)) +
			    w+mfd->slot->bitmap_left)-1] = c&0xff;
		    p[3*(width*(-(h+mfd->top_space - mfd->slot->bitmap_top)) +
			    w+mfd->slot->bitmap_left)-0] = c&0xff;

		    }
		}

	    p+=3*((mfd->slot->advance.x >> 6) - (mfd->slot->advance.y >> 6));
	}
    }
}

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  static vob_t *vob=NULL;

  static int width=0, height=0;
  static int codec=0;
  static int w, h, i;
  int error;
  static time_t mytime=0;
  static int hh, mm, ss, ss_frame;
  static float elapsed_ss;
  static uint8_t *buf = NULL;
  static uint8_t *p, *q;
  char *default_font = "/usr/share/fonts/corefonts/arial.ttf";
  extern int flip; // transcode.c

  if (ptr->tag & TC_AUDIO)
      return 0;

  if(ptr->tag & TC_FILTER_GET_CONFIG) {
      char b[128];
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYO", "1");

      tc_snprintf(b, 128, "%u-%u/%d", mfd->start, mfd->end, mfd->step);
      optstr_param (options, "range", "apply filter to [start-end]/step frames",
	      "%u-%u/%d", b, "0", "oo", "0", "oo", "1", "oo");

      optstr_param (options, "string", "text to display (no ':') [defaults to `date`]",
	      "%s", mfd->string);

      optstr_param (options, "font", "full path to font file [defaults to arial.ttf]",
	      "%s", mfd->font);

      tc_snprintf(b, 128, "%d", mfd->points);
      optstr_param (options, "points", "size of font (in points)",
	      "%d", b, "1", "100");

      tc_snprintf(b, 128, "%d", mfd->dpi);
      optstr_param (options, "dpi", "resolution of font (in dpi)",
	      "%d", b, "72", "300");

      tc_snprintf(b, 128, "%d", mfd->fade);
      optstr_param (options, "fade", "fade in/out (0=off, 1=slow, 10=fast)",
	      "%d", b, "0", "10");

      tc_snprintf(b, 128, "%d", mfd->antialias);
      optstr_param (options, "antialias", "Anti-Alias text (0=off 1=on)",
	      "%d", b, "0", "10");

      optstr_param (options, "pos", "Position (0-width x 0-height)",
	      "%dx%d", "0x0", "0", "width", "0", "height");

      optstr_param (options, "posdef", "Position (keypad number, 0=None)",
		"%d", "0", "0", "9");

      optstr_param (options, "notransparent", "disable transparency (enables block box)",
		"", "0");

      optstr_param (options, "tstamp", "add timestamps (overridden by string)",
		"", "0");

      optstr_param (options, "frame", "add frame numbers (overridden by string)",
		 "", "0");

      return 0;
  }

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL) return(-1);

    // MallocZ  just to be safe
    mfd = tc_zalloc (sizeof(MyFilterData));
    if(mfd == NULL)
        return (-1);

    mfd->start=0;
    mfd->end=(unsigned int)-1;
    mfd->step=1;

    mfd->points=25;
    mfd->dpi = 96;
    mfd->font = tc_strdup(default_font);
    mfd->string = NULL;

    mfd->fade = 0;
    mfd->posx=0;
    mfd->posy=0;
    mfd->pos=NONE;
    mfd->transparent=1;
    mfd->antialias=1;

    mfd->do_time=1;
    mfd->tstamp=0;
    mfd->frame=0;
    mfd->opaque=MAX_OPACITY;
    mfd->fade_in = 0;
    mfd->fade_out = 0;
    mfd->start_fade_out=0;
    mfd->top_space = 0;
    mfd->boundX=0;
    mfd->boundY=0;

    mfd->R = mfd->B = mfd->G = 0xff; // white
    mfd->Y = 240; mfd->U = mfd->V = 128;



    if (options != NULL) {
	char string[PATH_MAX] = { '\0', };
	char font[PATH_MAX] = { '\0', };

	if(verbose) tc_log_info(MOD_NAME, "options=%s", options);

	optstr_get (options, "range",  "%u-%u/%d", &mfd->start, &mfd->end, &mfd->step);
	optstr_get (options, "dpi",    "%u",       &mfd->dpi);
	optstr_get (options, "points", "%u",       &mfd->points);
	optstr_get (options, "font",   "%[^:]",    font);
	optstr_get (options, "posdef", "%d",       (int *)&mfd->pos);
	optstr_get (options, "pos",    "%dx%d",    &mfd->posx,  &mfd->posy);
	optstr_get (options, "string", "%[^:]",    string);
	optstr_get (options, "fade",   "%d",       &mfd->fade);
	optstr_get (options, "antialias",   "%d",       &mfd->antialias);
	optstr_get (options, "color",   "%2x%2x%2x",  &mfd->R, &mfd->G, &mfd->B);
        mfd->Y =  (0.257 * mfd->R) + (0.504 * mfd->G) + (0.098 * mfd->B) + 16;
        mfd->U =  (0.439 * mfd->R) - (0.368 * mfd->G) - (0.071 * mfd->B) + 128;
        mfd->V = -(0.148 * mfd->R) - (0.291 * mfd->G) + (0.439 * mfd->B) + 128;

	if (optstr_lookup (options, "notransparent") ) {
	    mfd->transparent = !mfd->transparent;
        }

	if (strlen(font)>0) {
	    free (mfd->font);
	    mfd->font=tc_strdup(font);
	}

	if (strlen(string)>0) {
	    mfd->string=tc_strdup(string);
	    mfd->do_time=0;
        } else if (optstr_lookup (options, "tstamp") ) {
            mfd->string = "[ timestamp ]";
	    mfd->do_time = 0;
	    mfd->tstamp = 1;
	} else if (optstr_lookup (options, "frame") ) {
	    mfd->string=strdup("Frame: dddddd");
	    mfd->do_time = 0;
	    mfd->frame = 1;
	} else {
	    // do `date` as default
	    mytime = time(NULL);
	    mfd->string = ctime(&mytime);
	    mfd->string[strlen(mfd->string)-1] = '\0';
	}
    }

    if (verbose) {
	tc_log_info (MOD_NAME, " Text Settings:");
	tc_log_info (MOD_NAME, "            string = \"%s\"", mfd->string);
	tc_log_info (MOD_NAME, "             range = %u-%u", mfd->start, mfd->end);
	tc_log_info (MOD_NAME, "              step = %u", mfd->step);
	tc_log_info (MOD_NAME, "               dpi = %u", mfd->dpi);
	tc_log_info (MOD_NAME, "            points = %u", mfd->points);
	tc_log_info (MOD_NAME, "              font = %s", mfd->font);
    tc_log_info (MOD_NAME, "            posdef = %d", mfd->pos);
	tc_log_info (MOD_NAME, "               pos = %dx%d", mfd->posx, mfd->posy);
	tc_log_info (MOD_NAME, "       color (RGB) = %x %x %x", mfd->R, mfd->G, mfd->B);
	tc_log_info (MOD_NAME, "       color (YUV) = %x %x %x", mfd->Y, mfd->U, mfd->V);
    }

    if (options)
	if (optstr_lookup (options, "help")) {
	    help_optstr();
	}

    if (mfd->start % mfd->step == 0) mfd->boolstep = 0;
    else mfd->boolstep = 1;

    width  = vob->ex_v_width;
    height = vob->ex_v_height;
    codec  = vob->im_v_codec;

    if (codec == TC_CODEC_RGB24)
	buf = tc_malloc(width*height*3);
    else if (codec == TC_CODEC_YUV422P)
	buf = tc_malloc(width*height + ((width/2)*height)*2);
    else
	buf = tc_malloc(width*height + ((width/2)*(height/2))*2);

    if(buf == NULL)
        return (-1);

    if (codec == TC_CODEC_RGB24)
	memset(buf, 0, height*width*3);
    else {
	memset(buf, 16, height*width);
	memset(buf + height*width, 128,
	       (codec==TC_CODEC_YUV422P ? height : height/2) * (width/2) * 2);
    }

    // init lib
    error = FT_Init_FreeType (&mfd->library);
    if (error) { tc_log_error(MOD_NAME, "init FreeType lib!"); return -1;}

    error = FT_New_Face (mfd->library, mfd->font, 0, &mfd->face);
    if (error == FT_Err_Unknown_File_Format) {
	tc_log_error(MOD_NAME, "Unsupported font format");
	return -1;
    } else if (error) {
	tc_log_error(MOD_NAME, "Cannot handle file");
	return -1;
    }

    error = FT_Set_Char_Size(
              mfd->face,      /* handle to face object           */
              0,              /* char_width in 1/64th of points  */
              mfd->points*64, /* char_height in 1/64th of points */
              mfd->dpi,       /* horizontal device resolution    */
              mfd->dpi );     /* vertical device resolution      */

    if (error) { tc_log_error(MOD_NAME, "Cannot set char size"); return -1; }

    // guess where the the groundline is
    // find the bounding box
    for (i=0; i<strlen(mfd->string); i++) {

	error = FT_Load_Char( mfd->face, mfd->string[i], FT_LOAD_RENDER);
	mfd->slot = mfd->face->glyph;

	if (mfd->top_space < mfd->slot->bitmap_top)
	    mfd->top_space = mfd->slot->bitmap_top;

	// if you think about it, its somehow correct ;)
	/*
	if (mfd->boundY < 2*mfd->slot->bitmap.rows - mfd->slot->bitmap_top)
	    mfd->boundY = 2*mfd->slot->bitmap.rows - mfd->slot->bitmap_top;
	    */
	if (mfd->boundY < 2*(mfd->slot->bitmap.rows) - mfd->slot->bitmap_top)
	    mfd->boundY = 2*(mfd->slot->bitmap.rows) - mfd->slot->bitmap_top;

	/*
	tc_log_msg(MOD_NAME, "`%c\': rows(%2d) width(%2d) pitch(%2d) left(%2d) top(%2d) "
		   "METRIC: width(%2d) height(%2d) bearX(%2d) bearY(%2d)",
		   mfd->string[i], mfd->slot->bitmap.rows, mfd->slot->bitmap.width,
		   mfd->slot->bitmap.pitch, mfd->slot->bitmap_left, mfd->slot->bitmap_top,
		   mfd->slot->metrics.width>>6, mfd->slot->metrics.height>>6,
		   mfd->slot->metrics.horiBearingX>>6, mfd->slot->metrics.horiBearingY>>6);
		*/

	mfd->boundX += mfd->slot->advance.x >> 6;
    }

    switch (mfd->pos) {
	case NONE: /* 0 */
	    break;

	case TOP_LEFT:
	    mfd->posx = 0;
	    mfd->posy = 0;
	    break;

	case TOP_CENTER:
	    mfd->posx = (width - mfd->boundX)/2;
	    mfd->posy = 0;
	    if (mfd->posx&1)
		mfd->posx++;
	    break;

	case TOP_RIGHT:
	    mfd->posx = width  - mfd->boundX;
	    mfd->posy = 0;
	    break;

	case CTR_LEFT:
	    mfd->posx = 0;
	    mfd->posy = (height- mfd->boundY)/2;
	    if (mfd->posy&1)
		mfd->posy++;
	    break;

	case CTR_CENTER:
	    mfd->posx = (width - mfd->boundX)/2;
	    mfd->posy = (height- mfd->boundY)/2;

	    /* align to not cause color disruption */
	    if (mfd->posx&1)
		mfd->posx++;
	    if (mfd->posy&1)
		mfd->posy++;
	    break;

	case CTR_RIGHT:
	    mfd->posx = width  - mfd->boundX;
	    mfd->posy = (height- mfd->boundY)/2;
	    if (mfd->posy&1)
		mfd->posy++;
	    break;

	case BOT_LEFT:
	    mfd->posx = 0;
	    mfd->posy = height - mfd->boundY;
	    break;

	case BOT_CENTER:
	    mfd->posx = (width - mfd->boundX)/2;
	    mfd->posy = height - mfd->boundY;
	    if (mfd->posx&1)
		mfd->posx++;
	    break;

	case BOT_RIGHT:
	    mfd->posx = width  - mfd->boundX;
	    mfd->posy = height - mfd->boundY;
	    break;
    }

    if ( mfd->posy < 0 || mfd->posx < 0 ||
	    mfd->posx+mfd->boundX > width ||
	    mfd->posy+mfd->boundY > height) {
	tc_log_error(MOD_NAME, "invalid position");
	return (-1);
    }

    font_render(width,height,codec,w,h,i,p,q,buf);

    // filter init ok.
    if (verbose) tc_log_info(MOD_NAME, "%s %s %dx%d-%d", MOD_VERSION, MOD_CAP,
	    mfd->boundX, mfd->boundY, mfd->top_space);


    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE) {

    if (mfd) {
	FT_Done_Face (mfd->face );
	FT_Done_FreeType (mfd->library);
	free(mfd->font);
	if (!mfd->do_time)
	    free(mfd->string);
	free(mfd);
	free(buf);

    }
    mfd=NULL;
    buf=NULL;

    return(0);

  } /* filter close */

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------


  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  if((ptr->tag & TC_POST_M_PROCESS) && (ptr->tag & TC_VIDEO) && !(ptr->attributes & TC_FRAME_IS_SKIPPED))  {

    char tstampbuf[256];

    if (mfd->start <= ptr->id && ptr->id <= mfd->end && ptr->id%mfd->step == mfd->boolstep) {

	if(mfd->do_time && time(NULL)!=mytime) {
	    mytime = time(NULL);
	    mfd->string = ctime(&mytime);
	    mfd->string[strlen(mfd->string)-1] = '\0';
	    font_render(width,height,codec,w,h,i,p,q,buf);
	}

	else if (mfd->tstamp) {
	    elapsed_ss = ptr->id / vob->fps;
	    hh = elapsed_ss / 3600;
	    mm = (elapsed_ss - (3600 * hh)) / 60;
	    ss = (elapsed_ss - (3600 * hh) - (60 * mm));
	    ss_frame = (ptr->id - (((hh * 3600) + (mm * 60) + ss) * vob->fps));
	    tc_snprintf(tstampbuf, sizeof(tstampbuf),
			"%02i:%02i:%02i.%02i", hh, mm, ss, ss_frame);
	    mfd->string = tstampbuf;
	    font_render(width,height,codec,w,h,i,p,q,buf);
	}

	else if (mfd->frame) {
	    sprintf(mfd->string, "Frame: %06d", ptr->id);  
	    font_render(width,height,codec,w,h,i,p,q,buf);
	}

	if (mfd->start == ptr->id && mfd->fade) {
	    mfd->fade_in = 1;
	    mfd->fade_out= 0;
	    mfd->opaque  = 0;
	    mfd->start_fade_out = mfd->end - MAX_OPACITY/mfd->fade - 1;
	    //if (mfd->start_fade_out < mfd->start) mfd->start_fade_out = mfd->start;
	}

	if (ptr->id == mfd->start_fade_out && mfd->fade) {
	    mfd->fade_in = 0;
	    mfd->fade_out = 1;
	}


	if (codec == TC_CODEC_YUV420P) {
	    uint8_t *vbuf, *U, *V;
	    int Bpl;

	    if (flip) {
		vbuf = ptr->video_buf + (height-1)*width;
		Bpl = (-width);
		U = ptr->video_buf + ptr->v_width*ptr->v_height
		                   + ((height/2)-1)*(width/2);
	    } else {
		vbuf = ptr->video_buf;
		Bpl = width;
		U = ptr->video_buf + ptr->v_width*ptr->v_height;
	    }
	    p = vbuf + mfd->posy*Bpl   + mfd->posx;
	    q =  buf + mfd->posy*width + mfd->posx;
	    U = U + (mfd->posy/2)*(Bpl/2) + mfd->posx/2;
	    V = U + (ptr->v_width/2)*(ptr->v_height/2);

	    for (h=0; h<mfd->boundY; h++) {
		for (w=0; w<mfd->boundX; w++)  {

		    unsigned int c = q[h*width+w]&0xff;
		    unsigned int d = p[h*Bpl+w]&0xff;
		    unsigned int e = 0;

		    // transparency
		    if (mfd->transparent && (c <= 16)) continue;

		    // opacity
		    e = ((MAX_OPACITY-mfd->opaque)*d + mfd->opaque*c)/MAX_OPACITY;
		    //e &= (mfd->Y&0xff);

		    // write to image
		    p[h*width+w] = e&0xff;

		    U[(h/2)*(Bpl/2)+w/2] = mfd->U&0xff;
		    V[(h/2)*(Bpl/2)+w/2] = mfd->V&0xff;
		}
	    }


	} else if (codec == TC_CODEC_YUV422P) { // FIXME untested
	    uint8_t *vbuf, *U, *V;
	    int Bpl;

	    if (flip) {
		vbuf = ptr->video_buf + (height-1)*width;
		Bpl = (-width);
		U = ptr->video_buf + ptr->v_width*ptr->v_height
		                   + (height-1)*(width/2);
	    } else {
		vbuf = ptr->video_buf;
		Bpl = width;
		U = ptr->video_buf + ptr->v_width*ptr->v_height;
	    }
	    p = vbuf + mfd->posy*Bpl   + mfd->posx;
	    q =  buf + mfd->posy*width + mfd->posx;
	    U = U + (mfd->posy)*(Bpl/2) + mfd->posx/2;
	    V = U + (ptr->v_width/2)*(ptr->v_height/2);

	    for (h=0; h<mfd->boundY; h++) {
		for (w=0; w<mfd->boundX; w++)  {

		    unsigned int c = q[h*width+w]&0xff;
		    unsigned int d = p[h*Bpl+w]&0xff;
		    unsigned int e = 0;

		    // transparency
		    if (mfd->transparent && (c <= 16)) continue;

		    // opacity
		    e = ((MAX_OPACITY-mfd->opaque)*d + mfd->opaque*c)/MAX_OPACITY;
		    //e &= (mfd->Y&0xff);

		    // write to image
		    p[h*width+w] = e&0xff;

		    U[h*(Bpl/2)+w/2] = mfd->U&0xff;
		    V[h*(Bpl/2)+w/2] = mfd->V&0xff;
		}
	    }


	} else if (codec == TC_CODEC_RGB24) { // FIXME
	    uint8_t *vbuf;
	    int Bpl;

	    if (flip) {
		vbuf = ptr->video_buf + (height-1)*width*3;
		Bpl = (-width)*3;
	    } else {
		vbuf = ptr->video_buf;
		Bpl = width*3;
	    }

	    p = vbuf +   (height-mfd->posy)*Bpl   + 3*mfd->posx;
	    q =  buf + 3*(height-mfd->posy)*width + 3*mfd->posx;

	    //ac_memcpy(ptr->video_buf, buf, 3*width*height);

	    for (h=0; h>-mfd->boundY; h--) {
		for (w=0; w<mfd->boundX; w++)  {
		    for (i=0; i<3; i++) {
			unsigned int c = q[3*(h*width+w)-(2-i)]&0xff;
			unsigned int d = p[3*(h*Bpl+w)-(2-i)]&0xff;
			unsigned int e;
			if (mfd->transparent && c <= 16) continue;

			// opacity
			e = ((MAX_OPACITY-mfd->opaque)*d + mfd->opaque*c)/MAX_OPACITY;

			switch (i){
			    case 0: e &= (mfd->G); break;
			    case 1: e &= (mfd->R); break;
			    case 2: e &= (mfd->B); break;
			}

			// write to image
			p[3*(h*width+w)-(2-i)] =  e&0xff;
		    }
		}
	    }

	}

	if (mfd->fade && mfd->opaque>0 && mfd->fade_out) {
	    mfd->opaque -= mfd->fade;
	    if (mfd->opaque<0) mfd->opaque=0;
	}

	if (mfd->fade && mfd->opaque<MAX_OPACITY && mfd->fade_in) {
	    mfd->opaque += mfd->fade;
	    if (mfd->opaque>MAX_OPACITY) mfd->opaque=MAX_OPACITY;
	}

    }
  }

  return(0);
}

#else
int tc_filter(vframe_list_t *ptr, char *options)
{
    tc_log_error(MOD_NAME, "Your freetype installation is missing header files");
    return -1;
}
#endif // FT_FREETYPE_H
