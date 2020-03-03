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


//#define NEW_DESC

/*
Byte swapping routines taken directly from ffmpeg/libavcodec/bswap.h.
Should rewritten to put data directly in struct font_desc_t,
and also make a windows 4 bit bitmap, really.
*/

/*
Renders antialiased fonts for mplayer using freetype library.
Should work with TrueType, Type1 and any other font supported
by libfreetype.
Can generate font.desc for any encoding.

Artur Zaprzala <zybi@fanthom.irc.pl>
*/


#include <iconv.h>
#include <math.h>
#include <string.h>
#include <libgen.h>

// FreeType specific includes
#include <ft2build.h>
#include FT_FREETYPE_H

#include <freetype/ftglyph.h>

/**
 * @file bswap.h
 * byte swap.
 */

#ifndef __BSWAP_H__
#define __BSWAP_H__

#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#else

#ifdef ARCH_X86_64
#  define LEGACY_REGS "=Q"
#else
#  define LEGACY_REGS "=q"
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static inline uint16_t ByteSwap16(uint16_t x)
{
  __asm("xchgb %b0,%h0"	:
        LEGACY_REGS (x)	:
        "0" (x));
    return x;
}
#define bswap_16(x) ByteSwap16(x)

static inline uint32_t ByteSwap32(uint32_t x)
{
#if __CPU__ > 386
 __asm("bswap	%0":
      "=r" (x)     :
#else
 __asm("xchgb	%b0,%h0\n"
      "	rorl	$16,%0\n"
      "	xchgb	%b0,%h0":
      LEGACY_REGS (x)		:
#endif
      "0" (x));
  return x;
}
#define bswap_32(x) ByteSwap32(x)

static inline uint64_t ByteSwap64(uint64_t x)
{
#ifdef ARCH_X86_64
  __asm("bswap	%0":
        "=r" (x)     :
        "0" (x));
  return x;
#else
  register union { __extension__ uint64_t __ll;
          uint32_t __l[2]; } __x;
  asm("xchgl	%0,%1":
      "=r"(__x.__l[0]),"=r"(__x.__l[1]):
      "0"(bswap_32((uint32_t)x)),"1"(bswap_32((uint32_t)(x>>32))));
  return __x.__ll;
#endif
}
#define bswap_64(x) ByteSwap64(x)

#elif defined(ARCH_SH4)

static inline uint16_t ByteSwap16(uint16_t x) {
	__asm__("swap.b %0,%0":"=r"(x):"0"(x));
	return x;
}

static inline uint32_t ByteSwap32(uint32_t x) {
	__asm__(
	"swap.b %0,%0\n"
	"swap.w %0,%0\n"
	"swap.b %0,%0\n"
	:"=r"(x):"0"(x));
	return x;
}

#define bswap_16(x) ByteSwap16(x)
#define bswap_32(x) ByteSwap32(x)

static inline uint64_t ByteSwap64(uint64_t x)
{
    union {
        uint64_t ll;
        struct {
           uint32_t l,h;
        } l;
    } r;
    r.l.l = bswap_32 (x);
    r.l.h = bswap_32 (x>>32);
    return r.ll;
}
#define bswap_64(x) ByteSwap64(x)

#else

#define bswap_16(x) (((x) & 0x00ff) << 8 | ((x) & 0xff00) >> 8)


// code from bits/byteswap.h (C) 1997, 1998 Free Software Foundation, Inc.
#define bswap_32(x) \
     ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
      (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))

static inline uint64_t ByteSwap64(uint64_t x)
{
    union {
        uint64_t ll;
        uint32_t l[2];
    } w, r;
    w.ll = x;
    r.l[0] = bswap_32 (w.l[1]);
    r.l[1] = bswap_32 (w.l[0]);
    return r.ll;
}
#define bswap_64(x) ByteSwap64(x)

#endif	/* !ARCH_X86 */

#endif	/* !HAVE_BYTESWAP_H */

// be2me ... BigEndian to MachineEndian
// le2me ... LittleEndian to MachineEndian

#ifdef WORDS_BIGENDIAN
#define be2me_16(x) (x)
#define be2me_32(x) (x)
#define be2me_64(x) (x)
#define le2me_16(x) bswap_16(x)
#define le2me_32(x) bswap_32(x)
#define le2me_64(x) bswap_64(x)
#else
#define be2me_16(x) bswap_16(x)
#define be2me_32(x) bswap_32(x)
#define be2me_64(x) bswap_64(x)
#define le2me_16(x) (x)
#define le2me_32(x) (x)
#define le2me_64(x) (x)
#endif

#endif /* __BSWAP_H__ */


int sub_unicode = 0;


raw_file* load_raw(char *name, int verbose)
{
int bpp;
raw_file* raw = malloc( sizeof(raw_file) );
unsigned char head[32];
FILE *f = fopen(name, "rb");

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "load_raw(): arg name=%s verbose=%d\n", name, verbose);
	}

if(!f)return NULL;							// can't open

if(fread(head, 32, 1, f) < 1) return NULL;	// too small
if(memcmp(head, "mhwanh", 6)) return NULL;	// not raw file

raw->w = head[8] * 256 + head[9];
raw->h = head[10] * 256 + head[11];
raw->c = head[12] * 256 + head[13];
if(raw->c > 256) return NULL;				// too many colors!?
if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "RAW: %s %d x %d, %d colors\n", name, raw->w, raw->h, raw->c);
	}
if(raw->c)
	{
	raw->pal = malloc(raw->c * 3);
	if (fread(raw->pal, 3, raw->c, f) != raw->c)
	    return NULL;
	bpp = 1;
	}
else
	{
	raw->pal = NULL;
	bpp = 3;
	}
raw->bmp = malloc(raw->h * raw->w * bpp);
if (fread(raw->bmp, raw->h * raw->w * bpp, 1, f) != 1)
    return NULL;

fclose(f);

return raw;
} /* end function load_raw */


font_desc_t* read_font_desc(char* fname, float factor, int verbose)
{
unsigned char sor[1024];
unsigned char sor2[1024];
font_desc_t *desc;
FILE *f;
char section[64];
int i, j;
int chardb = 0;
int fontdb = -1;
int version = 0;
char temp[4096];
char *ptr;

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "read_font_desc(): arg fname=%s factor=%.2f verbose=%d\n",\
	fname, factor, verbose);
	}

desc = malloc( sizeof(font_desc_t) );
if(!desc) return NULL;
memset(desc, 0, sizeof(font_desc_t) );

f = fopen(fname, "r");
if(!f)
	{
	tc_log_msg(MOD_NAME, "read_font_desc(): font: can't open file: %s\n", fname);
	return NULL;
	}

strlcpy(temp, fname, sizeof(temp));
ptr = strstr(temp, "font.desc");
if(! ptr)
	{
	tc_log_msg(MOD_NAME, "subtitler: read_font_descr(): no font.desc found in %s aborting.\n", fname);

	exit(1);
	}
*ptr = 0;
desc->fpath = strsave(temp);

if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "subtitler: read_font_desc(): read_font_desc(): fname=%s path=%s\n",\
	fname, desc->fpath);
	}

// set up some defaults, and erase table
desc->charspace = 2;
desc->spacewidth = 12;
desc->height = 0;
for(i = 0; i < 512; i++)
	{
	desc->start[i] = desc->width[i] = desc->font[i] = -1;
	}
section[0] = 0;

while( fgets(sor, 1020, f) )
	{
	unsigned char* p[8];
	int pdb = 0;
	unsigned char *s = sor;
	unsigned char *d = sor2;
	int ec = ' ';
	int id = 0;
	sor[1020] = 0;
	p[0] = d;
	++pdb;
	while(1)
		{
		int c = *s++;
		if(c == 0 || c == 13 || c == 10) break;
		if(!id)
			{
			if(c == 39 || c == 34)
				{
				id = c;
				continue;
				} // idezojel
			if(c == ';' || c == '#') break;
			if(c == 9) c = ' ';
			if(c == ' ')
				{
				if(ec == ' ') continue;
				*d = 0;
				++d;
				p[pdb] = d;
				++pdb;
				if(pdb >= 8) break;
				continue;
				}
			}
		else
			{
			if(id == c)
				{
				id = 0;
				continue;
				} // idezojel
			}
		*d = c;
		d++;
		ec = c;
		}

	if(d == sor2) continue; // skip empty lines
	*d = 0;

	//tc_log_msg(MOD_NAME, "params=%d sor=%s\n", pdb, sor);
	//for(i = 0; i < pdb; i++) tc_log_msg(MOD_NAME, " param %d = '%s'\n", i, p[i]);

	if(pdb == 1 && p[0][0] == '[')
		{
		int len = strlen(p[0]);
		if(len && len < 63 && p[0][len - 1] == ']')
			{
			strlcpy(section, p[0], sizeof(section));
			if(debug_flag)
				{
				tc_log_msg(MOD_NAME, "font: Reading section: %s\n",section);
				}
			if(strcmp(section, "[files]") == 0)
				{
				++fontdb;
				if(fontdb >= 16)
					{
					tc_log_msg(MOD_NAME, "font: Too many bitmaps defined!\n");
					return NULL;
					}
				}
			continue;
			}
		}

	if(strcmp(section, "[fpath]") == 0)
		{
		if(pdb == 1)
			{
			desc->fpath = tc_strdup(p[0]);
			continue;
			}
		}
	else if(strcmp(section,"[files]") == 0)
		{
		if(pdb == 2 && strcmp(p[0], "alpha") == 0)
			{
			char *cp;
			if (! (cp = malloc(strlen(desc->fpath) + strlen(p[1]) + 2)))
				{
				return NULL;
				}
			tc_snprintf(cp, strlen(desc->fpath) + strlen(p[1]) + 2, "%s/%s",
			desc->fpath, p[1]);
			if(!((desc->pic_a[fontdb] = load_raw(cp, verbose))))
				{
				tc_log_msg(MOD_NAME, "Can't load font bitmap: %s\n", p[1]);
				free(cp);
				return NULL;
				}
			free(cp);
			continue;
			}
		if(pdb == 2 && strcmp(p[0], "bitmap") == 0)
			{
			char *cp;
			if (!(cp = malloc(strlen(desc->fpath) + strlen(p[1]) + 2)))
				{
				return NULL;
				}
			tc_snprintf(cp, strlen(desc->fpath) + strlen(p[1]) + 2, "%s/%s",
			desc->fpath, p[1]);
			if(!((desc->pic_b[fontdb] = load_raw(cp, verbose))))
				{
				tc_log_msg(MOD_NAME, "Can't load font bitmap: %s\n", p[1]);
				free(cp);
				return NULL;
				}
			free(cp);
			continue;
			}
		}
	else if(strcmp(section, "[info]") == 0)
		{
		if(pdb == 2 && strcmp(p[0], "name") == 0)
			{
			desc->name = tc_strdup(p[1]);
			continue;
			}
		if(pdb == 2 && strcmp(p[0], "descversion") == 0)
			{
			version = atoi(p[1]);
			continue;
			}
		if(pdb == 2 && strcmp(p[0], "spacewidth") == 0)
			{
			desc->spacewidth = atoi(p[1]);
			continue;
			}
		if(pdb == 2 && strcmp(p[0], "charspace") == 0)
			{
			desc->charspace = atoi(p[1]);
			continue;
			}
		if(pdb == 2 && strcmp(p[0], "height") == 0)
			{
			desc->height = atoi(p[1]);
			continue;
			}
		}
	else if(strcmp(section, "[characters]") == 0)
		{
		if(pdb == 3)
			{
			int chr = p[0][0];
			int start = atoi(p[1]);
			int end = atoi(p[2]);
			if(sub_unicode && (chr>=0x80)) chr = (chr << 8) + p[0][1];
			else if(strlen(p[0]) != 1) chr = strtol(p[0], NULL, 0);
			if(end < start)
				{
				tc_log_msg(MOD_NAME, "error in font desc: end<start for char '%c'\n", chr);
				}
			else
				{
				desc->start[chr] = start;
				desc->width[chr] = end-start + 1;
				desc->font[chr] = fontdb;
#if 0
				tc_log_msg(MOD_NAME, "char %d '%c' start=%d width=%d\n",
				chr, chr, desc->start[chr], desc->width[chr]);
				++chardb;
#endif
				}
			continue;
			}
		}
	tc_log_msg(MOD_NAME, "Syntax error in font desc: %s\n", sor);
	}
fclose(f);

//tc_log_msg(MOD_NAME, "font: pos of U = %d\n", desc->start[218]);

for(i = 0; i <= fontdb; i++)
	{
	if(!desc->pic_a[i] || !desc->pic_b[i])
		{
		tc_log_msg(MOD_NAME, "font: Missing bitmap(s) for sub-font #%d\n", i);
		return NULL;
		}
//	if(factor != 1.0f)
		{
//		re-sample alpha
		int f = factor * 256.0f;
		int size = desc->pic_a[i]->w * desc->pic_a[i]->h;
		int j;
		if(verbose)
			{
			tc_log_msg(MOD_NAME, "font: resampling alpha by factor %5.3f (%d) ",\
			factor, f);
			}
		fflush(stderr);
		for(j = 0; j < size; j++)
			{
			int x = desc->pic_a[i]->bmp[j];	// alpha
			int y = desc->pic_b[i]->bmp[j];	// bitmap

#ifdef FAST_OSD
			x = (x < (255 - f)) ? 0 : 1;
#else
			x = 255 - ((x * f) >> 8); // scale
			//if(x < 0) x = 0;else if(x>255) x=255;
			//x ^= 255; // invert

			if(x + y > 255) x = 255 - y; // to avoid overflows

			//x=0;
			//x=((x*f*(255-y))>>16);
			//x=((x*f*(255-y))>>16)+y;
			//x=(x*f)>>8;if(x<y) x=y;

			if(x < 1) x = 1;
			else if(x >= 252) x = 0;
#endif

			desc->pic_a[i]->bmp[j] = x;
//			desc->pic_b[i]->bmp[j]=0; // hack
			}
		if(verbose) tc_log_msg(MOD_NAME, "DONE!\n");
		}
	if(!desc->height) desc->height = desc->pic_a[i]->h;
	}

j = '_';
if(desc->font[j] < 0) j = '?';

for(i = 0; i < 512; i++)
	{
	if(desc->font[i] < 0)
		{
		desc->start[i] = desc->start[j];
		desc->width[i] = desc->width[j];
		desc->font[i] = desc->font[j];
		}
	}

desc->font[' '] = -1;
desc->width[' '] = desc->spacewidth;
if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "Font %s loaded successfully! (%d chars)\n", fname, chardb);
	}
return desc;
} /* end function read_font_desc */



/* default values */
char *encoding = "iso-8859-15";/* target encoding */
char *charmap = "ucs-4";	/* font charmap encoding, I hope ucs-4 is always big endian */
	/* gcc 2.1.3 doesn't support ucs-4le, but supports ucs-4 (==ucs-4be) */
float ppem = 22;			/* font size in pixels */
char* font_desc = "font.desc";
char *outdir = ".";

/* constants */
int const colors = 256;
int const maxcolor = 255;
unsigned const base = 256;
unsigned const first_char = 33;

#define max_charset_size 60000

unsigned charset_size = 0;

char *command;
char *font_path;
char *encoding_name;
int append_mode = 0;
int unicode_desc = 0;
unsigned char *bbuffer, *abuffer;
int	width, height;
int	padding;
static FT_ULong	charset[max_charset_size]; /* characters we want to render; Unicode */
static FT_ULong	charcodes[max_charset_size]; /* character codes in 'encoding' */
iconv_t cd;	// iconv conversion descriptor


#define ERROR_(msg, ...)	(tc_log_error(MOD_NAME, "%s: " msg "\n", command, __VA_ARGS__), return 0)
#define WARNING_(msg, ...)	tc_log_warn(MOD_NAME, "%s: " msg "\n", command, __VA_ARGS__)
#define ERROR(...)		ERROR_(__VA_ARGS__, NULL)
#define WARNING(...)		WARNING_(__VA_ARGS__, NULL)

#define f266ToInt(x)		(((x)+32)>>6)	// round fractional fixed point number to integer
				// coordinates are in 26.6 pixels (i.e. 1/64th of pixels)

#define f266CeilToInt(x)	(((x)+63)>>6)	// ceiling
#define f266FloorToInt(x)	((x)>>6)	// floor
#define f1616ToInt(x)		(((x)+0x8000)>>16)	// 16.16
#define floatTof266(x)		((int)((x)*(1<<6)+0.5))

#define ALIGN8(x)		(((x)+7)&~7)	// 8 byte align


static void paste_bitmap(FT_Bitmap *bitmap, int x, int y)
{
int drow = x + y * width;
int srow = 0;
int sp, dp, w, h;

if (bitmap -> pixel_mode == ft_pixel_mode_mono)
	for (h = bitmap -> rows; h > 0; --h, drow += width, srow += bitmap -> pitch)
	    for (w = bitmap -> width, sp = dp = 0; w > 0; --w, ++dp, ++sp)
		    bbuffer[drow + dp] = (bitmap->buffer[srow + sp / 8] & (0x80 >> (sp % 8))) ? 255 : 0;
else
	for (h = bitmap -> rows; h > 0; --h, drow += width, srow += bitmap -> pitch)
	    for (w = bitmap -> width, sp = dp = 0; w > 0; --w, ++dp, ++sp)
		    bbuffer[drow + dp] = bitmap -> buffer[srow + sp];
} /* end function paste_bitmap */


int write_header(FILE *f)
{
static unsigned char   header[800] = "mhwanh";
int i;
header[7] = 4;

if (width < 0x10000)
	{
	/* are two bytes enough for the width? */
	header[8] = width>>8;
	header[9] = (unsigned char)width;
    }
else
	{
	/* store width using 4 bytes at the end of the header */
    header[8] = header[9] = 0;
    header[28] = (width >> 030) & 0xFF;
    header[29] = (width >> 020) & 0xFF;
    header[30] = (width >> 010) & 0xFF;
    header[31] = (width       ) & 0xFF;
    }

header[10] = height>>8;	header[11] = (unsigned char)height;
//header[12] = colors>>8;	header[13] = (unsigned char)colors;
header[12] = colors>>8;
header[13] = (unsigned char)(colors&0xff); // patch AMD64 by Tilmann Bitterberg

for (i = 32; i<800; ++i) header[i] = (i - 32) / 3;

return fwrite(header, 1, 800, f) == 800;
} /* end function write_header */


int write_bitmap(void *buffer, char type)
{
FILE *f;
int const max_name = 128;
char name[max_name];

tc_snprintf(name, max_name, "%s/%s-%c.raw", outdir, encoding_name, type);
f = fopen(name, "wb");
if(! f)
	{
	tc_log_msg(MOD_NAME, "subtitler(): write_bitmap(): could not open %s for write\n", name);

	return 0;
	}

if (!write_header(f)
 || fwrite(buffer, 1, width * height, f) != width * height)
	{
	tc_log_msg(MOD_NAME, "subtitler(): write_bitmap(): could not write to %s\n", name);

	return 0;
	}

fclose(f);

return 1;
} /* end function write_bitmap */


int render()
{
FT_Library	library;
FT_Face	face;
FT_Error	error;
FT_Glyph	*glyphs;
FT_BitmapGlyph glyph = 0;
FILE	*f;
int	const	load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING;
int		pen_x = 0, pen_xa;
int		ymin = INT_MAX, ymax = INT_MIN;
int		i, uni_charmap = 1;
int		baseline, space_advance = 20;
int		glyphs_count = 0;


    /* initialize freetype */
    error = FT_Init_FreeType(&library);
    if (error)
		{
		tc_log_msg(MOD_NAME, "subtitler: render(): Init_FreeType failed.");

		return 0;
		}

    error = FT_New_Face(library, font_path, 0, &face);
    if (error)
		{
		tc_log_msg(MOD_NAME, "subtitler: render(): New_Face failed. Maybe the font path `%s' is wrong.", font_path);

		return 0;
		}

    /*
    if (font_metrics) {
	error = FT_Attach_File(face, font_metrics);
	if (error) tc_log_msg(MOD_NAME, "subtitler: render(): FT_Attach_File failed.");
    }
    */


#if 0
    /************************************************************/
    tc_log_msg(MOD_NAME, "Font encodings:\n");
    for (i = 0; i<face->num_charmaps; ++i)
	tc_log_msg(MOD_NAME, "'%.4s'\n", (char*)&face->charmaps[i]->encoding);

    //error = FT_Select_Charmap(face, ft_encoding_unicode);
    //error = FT_Select_Charmap(face, ft_encoding_adobe_standard);
    //error = FT_Select_Charmap(face, ft_encoding_adobe_custom);
    //error = FT_Set_Charmap(face, face->charmaps[1]);
    //if (error) tc_log_msg(MOD_NAME, "subtitler: render(): FT_Select_Charmap failed.");
#endif


#if 0
    /************************************************************/
    if (FT_HAS_GLYPH_NAMES(face)) {
	int const max_gname = 128;
	char gname[max_gname];
	for (i = 0; i<face->num_glyphs; ++i) {
	    FT_Get_Glyph_Name(face, i, gname, max_gname);
	    tc_log_msg(MOD_NAME, "%02x `%s'\n", i, gname);
	}

    }
#endif


    if (face->charmap==NULL || face->charmap->encoding!=ft_encoding_unicode) {
	tc_log_msg(MOD_NAME, "subtitler: render(): Unicode charmap not available for this font. Very bad!");
	uni_charmap = 0;
	error = FT_Set_Charmap(face, face->charmaps[0]);
	if (error) tc_log_msg(MOD_NAME, "subtitler: render(): No charmaps! Strange.");
    }



    /* set size */
    if (FT_IS_SCALABLE(face)) {
	error = FT_Set_Char_Size(face, floatTof266(ppem), 0, 0, 0);
	if (error) tc_log_msg(MOD_NAME, "subtitler: render(): FT_Set_Char_Size failed.");
    } else {
	int j = 0;
	int jppem = face->available_sizes[0].height;
	/* find closest size */
	for (i = 0; i<face->num_fixed_sizes; ++i) {
	    if (fabs(face->available_sizes[i].height - ppem) < abs(face->available_sizes[i].height - jppem)) {
		j = i;
		jppem = face->available_sizes[i].height;
	    }
	}
	tc_log_msg(MOD_NAME, "subtitler: render(): Selected font is not scalable. Using ppem=%i.", face->available_sizes[j].height);
	error = FT_Set_Pixel_Sizes(face, face->available_sizes[j].width, face->available_sizes[j].height);
	if (error) tc_log_msg(MOD_NAME, "subtitler: render(): FT_Set_Pixel_Sizes failed.");
    }


    if (FT_IS_FIXED_WIDTH(face))
	tc_log_msg(MOD_NAME, "subtitler: render(): Selected font is fixed-width.");


    /* compute space advance */
    error = FT_Load_Char(face, ' ', load_flags);
//face->glyph->advance.x = 100;

    if (error) tc_log_msg(MOD_NAME, "subtitler: render(): spacewidth set to default.");
    else space_advance = f266ToInt(face->glyph->advance.x);


    /* create font.desc */

	{
    int const max_name = 128;
    char name[max_name];

    tc_snprintf(name, max_name, "%s/%s", outdir, font_desc);
    f = fopen(name, append_mode ? "a":"w");
	if(! f)
		{
		tc_log_msg(MOD_NAME, "xste(): render(): could not open file %s for write\n", name);

		return 0;
		}
	}

    /* print font.desc header */
    if (append_mode) {
	fprintf(f, "\n\n# ");
    } else {
	fprintf(f,  "# This file was generated with subfont for Mplayer.\n"
		    "# Subfont by Artur Zaprzala <zybi@fanthom.irc.pl>.\n\n");
	fprintf(f, "[info]\n");
    }

    fprintf(f, "name 'Subtitle font for %s %s, \"%s%s%s\" face, size: %.1f pixels.'\n",
	    encoding_name,
	    unicode_desc ? "charset, Unicode encoding":"encoding",
	    face->family_name,
	    face->style_name ? " ":"", face->style_name ? face->style_name:"",
	    ppem);

    if (!append_mode) {
#ifdef NEW_DESC
	fprintf(f, "descversion 2\n");
#else
	fprintf(f, "descversion 1\n");
#endif
	fprintf(f, "spacewidth %i\n",	2 * padding + space_advance);
#ifndef NEW_DESC
	fprintf(f, "charspace %i\n", -2 * padding);
#endif
	fprintf(f, "height %lu\n", (padding * 2) + f266ToInt(face->size->metrics.height));
#ifdef NEW_DESC
	fprintf(f, "ascender %i\n",	f266CeilToInt(face->size->metrics.ascender));
	fprintf(f, "descender %i\n",	f266FloorToInt(face->size->metrics.descender));
#endif
    }
    fprintf(f, "\n[files]\n");
    fprintf(f, "alpha %s-a.raw\n",	encoding_name);
    fprintf(f, "bitmap %s-b.raw\n",	encoding_name);
    fprintf(f, "\n[characters]\n");


    // render glyphs, compute bitmap size and [characters] section
    glyphs = (FT_Glyph*)malloc(charset_size*sizeof(FT_Glyph*));
    for (i= 0; i<charset_size; ++i) {
	FT_GlyphSlot	slot;
	FT_ULong	character, code;
	FT_UInt		glyph_index;

	character = charset[i];
	code = charcodes[i];

	// get glyph index
	if (character==0)
	    glyph_index = 0;
	else {
	    glyph_index = FT_Get_Char_Index(face, uni_charmap ? character:code);
	    if (glyph_index == 0)
			{
			if(debug_flag)
				{
				tc_log_msg(MOD_NAME, "subtitler: render(): Glyph for char 0x%02x|U+%04X|%c not found.",\
				(unsigned int)code, (unsigned int)character, (char)(code<' '|| code > 255 ? '.' : code));
				}

			continue;
		    }
	}

	// load glyph
	error = FT_Load_Glyph(face, glyph_index, load_flags);
	if (error) {
	    tc_log_msg(MOD_NAME, "subtitler: render(): FT_Load_Glyph 0x%02x (char 0x%02x|U+%04X) failed.", (unsigned int)glyph_index, (unsigned int)code, (unsigned int)character);
	    continue;
	}
	slot = face->glyph;

	// render glyph
	if (slot->format != ft_glyph_format_bitmap) {
	    error = FT_Render_Glyph(slot, ft_render_mode_normal);
	    if (error) {
		tc_log_msg(MOD_NAME, "subtitler: render(): FT_Render_Glyph 0x%04x (char 0x%02x|U+%04X) failed.", (unsigned int)glyph_index, (unsigned int)code, (unsigned int)character);
		continue;
	    }
	}

	// extract glyph image
	{
		void * tmp_glyph = (void *)glyph;

	    error = FT_Get_Glyph(slot, (void *)&tmp_glyph);
	    if (error) {
	        tc_log_msg(MOD_NAME, "subtitler: render(): FT_Get_Glyph 0x%04x (char 0x%02x|U+%04X) failed.", glyph_index, (unsigned int)code, (unsigned int)character);
	        continue;
	    }
    }

	glyphs[glyphs_count++] = (FT_Glyph)glyph;

#ifdef NEW_DESC
	// max height
	if (glyph->bitmap.rows > height) height = glyph->bitmap.rows;

	// advance pen
	pen_xa = pen_x + glyph->bitmap.width + 2*padding;

	// font.desc
	fprintf(f, "0x%04x %i %i %i %i %i %i;\tU+%04X|%c\n", unicode_desc ? character:code,
		pen_x,						// bitmap start
		glyph->bitmap.width + 2*padding,		// bitmap width
		glyph->bitmap.rows + 2*padding,			// bitmap height
		glyph->left - padding,				// left bearing
		glyph->top + padding,				// top bearing
		f266ToInt(slot->advance.x),			// advance
		character, code<' '||code>255 ? '.':code);
#else
	// max height
	if (glyph->top > ymax) {
	    ymax = glyph->top;
	    //tc_log_msg(MOD_NAME, "%3i: ymax %i (%c)\n", code, ymax, code);
	}
	if (glyph->top - glyph->bitmap.rows < ymin) {
	    ymin = glyph->top - glyph->bitmap.rows;
	    //tc_log_msg(MOD_NAME, "%3i: ymin %i (%c)\n", code, ymin, code);
	}

	/* advance pen */
	pen_xa = pen_x + f266ToInt(slot->advance.x) + 2*padding;

	/* font.desc */
	fprintf(f, "0x%04x %i %i;\tU+%04X|%c\n", (unsigned int)(unicode_desc ? character:code),
		pen_x,						// bitmap start
		pen_xa-1,					// bitmap end
		(unsigned int)character, (unsigned int)(code<' '||code>255 ? '.':code));
#endif
	pen_x = ALIGN8(pen_xa);
    }


    width = pen_x;
    pen_x = 0;
#ifdef NEW_DESC
    if (height<=0)
		{
		tc_log_msg(MOD_NAME, "subtitler: render(): Something went wrong. Use the source!");

		return 0;
		}

    height += 2*padding;
#else
    if (ymax<=ymin)
		{
		tc_log_msg(MOD_NAME, "subtitler: render(): Something went wrong. Use the source!");

		return 0;
		}

	height = ymax - ymin + 2*padding;
    baseline = ymax + padding;
#endif

    // end of font.desc
    if (debug_flag) tc_log_msg(MOD_NAME, "bitmap size: %ix%i\n", width, height);
    fprintf(f, "# bitmap size: %ix%i\n", width, height);
    fclose(f);

    bbuffer = (unsigned char*)malloc(width*height);
    if (bbuffer==NULL)
		{
		tc_log_msg(MOD_NAME, "subtitler: render(): malloc failed.");

		return 0;
		}
    memset(bbuffer, 0, width*height);


    /* paste glyphs */
    for (i= 0; i<glyphs_count; ++i) {
	glyph = (FT_BitmapGlyph)glyphs[i];
#ifdef NEW_DESC
	paste_bitmap(&glyph->bitmap,
	    pen_x + padding,
	    padding);

	/* advance pen */
	pen_x += glyph->bitmap.width + 2*padding;
#else
	paste_bitmap(&glyph->bitmap,
	    pen_x + padding + glyph->left,
	    baseline - glyph->top);

	/* advance pen */
//tc_log_msg(MOD_NAME, "WAS glyph->root.advance.x = %.2f\n", (float)glyph->root.advance.x);
//glyph->root.advance.x /= 2;

	pen_x += f1616ToInt(glyph->root.advance.x) + 2*padding;
#endif
	pen_x = ALIGN8(pen_x);

	FT_Done_Glyph((FT_Glyph)glyph);
    }
    free(glyphs);

    error = FT_Done_FreeType(library);
    if (error)
		{
		tc_log_msg(MOD_NAME, "subtitler: render(): FT_Done_FreeType failed.");

		return 0;
		}
return 1;
} /* end function render */


/* decode from 'encoding' to unicode */
static FT_ULong decode_char(char c)
{
#if 0  /* this code is completely broken (o is never initialized, etc) --AC */
FT_ULong o;
/* patch for AMD 64 by Tilmann Bitterberg */
size_t outbytesleft = sizeof(FT_ULong);

/* convert unicode BigEndian -> MachineEndian */
o = be2me_32(o);

// if (count == -1) o = 0; // not OK, at least my iconv() returns E2BIG for all
if (outbytesleft != 0) o = 0;

/* we don't want control characters */
if (o >= 0x7f && o < 0xa0) o = 0;

return o;
#else /* 0 */
return c;
#endif /* 0 */
} /* end function decode_char */


int prepare_charset()
{
FILE *f;
FT_ULong i;

f = fopen(encoding, "r"); // try to read custom encoding
if (f == NULL)
	{
	int count = 0;

	// check if ucs-4 is available
	cd = iconv_open(charmap, charmap);
	if (cd==(iconv_t)-1)
		{
		tc_log_msg(MOD_NAME, "subtitler: prepare_charset(): iconv doesn't know %s encoding. Use the source!", charmap);

		return 0;
		}

	iconv_close(cd);

	cd = iconv_open(charmap, encoding);
	if(cd == (iconv_t) - 1)
		{
		tc_log_msg(MOD_NAME, "subtitler: prepare_charset(): Unsupported encoding `%s', use iconv --list to list character sets known on your system.",\
		encoding);

		return 0;
		}

	charset_size = 256 - first_char;
	for (i = 0; i<charset_size; ++i)
		{
	    charcodes[count] = i+first_char;
	    charset[count] = decode_char(i+first_char);
	    //tc_log_msg(MOD_NAME, "%04X U%04X\n", charcodes[count], charset[count]);
	    if (charset[count]!=0) ++count;
		}
	charcodes[count] = charset[count] = 0; ++count;
	charset_size = count;

	iconv_close(cd);
    }
else
	{
	unsigned int character, code;
	int count;

	tc_log_msg(MOD_NAME, "Reading custom encoding from file '%s'.\n", encoding);

    while ((count = fscanf(f, "%x%*[ \t]%x", &character, &code)) != EOF)
		{
	    if (charset_size==max_charset_size)
			{
			tc_log_msg(MOD_NAME, "subtitler: prepare_charset(): There is no place for  more than %i characters. Use the source!", max_charset_size);
			break;
		    }
	    if (count == 0)
			{
			tc_log_msg(MOD_NAME, "subtitler: prepare_charset(): Unable to parse custom encoding file.");

			return 0;
			}

	    if (character < 32) continue;	// skip control characters
	    charset[charset_size] = character;
	    charcodes[charset_size] = count==2 ? code : character;
	    ++charset_size;
		}

	fclose(f);
//	encoding = basename(encoding);
    }

if (charset_size==0)
	{
	tc_log_msg(MOD_NAME, "subtitler: prepare_charset(): No characters to render!");

	return 0;
	}

return 1;
} /* end function prepare_charset */


// general outline
void outline(\
unsigned char *s, unsigned char *t, int width, int height,
int *m, int r, int mwidth)
{
int x, y;

for (y = 0; y < height; ++y)
	{
	for (x = 0; x < width; ++x, ++s, ++t)
		{
	    unsigned max = 0;
	    unsigned *mrow = m + r;
	    unsigned char *srow = s -r * width;
	    int x1 = (x < r) ? -x : -r;
	    int x2 = (x + r >= width) ? (width - x - 1) : r;
	    int my;

	    for(my = -r; my <= r; ++my, srow += width, mrow += mwidth)
			{
			int mx;

			if(y + my < 0) continue;
			if(y + my >= height) break;

			for(mx = x1; mx <= x2; ++mx)
				{
		    	unsigned v = srow[mx] * mrow[mx];
		    	if(v > max) max = v;
				}
	   		}
	    *t = (max + base / 2) / base;
		}
    }
} /* end function outline */


// 1 pixel outline
void outline1(unsigned char *s, unsigned char *t, int width, int height)
{
int x, y;

for (x = 0; x<width; ++x, ++s, ++t) *t = *s;
for (y = 1; y<height-1; ++y)
	{
	*t++ = *s++;
	for (x = 1; x<width-1; ++x, ++s, ++t)
		{
	    unsigned v = \
			(
		    s[-1 - width]+
		    s[-1 + width]+
		    s[+1 - width]+
		    s[+1 + width]\
			) / 2 + \
			(
		    s[-1]+
		    s[+1]+
		    s[-width]+
		    s[+width]+
		    s[0]
			);
	    *t = v > maxcolor ? maxcolor : v;
		}
	*t++ = *s++;
    }
for (x = 0; x < width; ++x, ++s, ++t) *t = *s;

} /* end function outline1 */


// gaussian blur
void blur(
unsigned char *buffer,\
unsigned char *tmp,\
int width,\
int height,\
int *m,\
int r,\
int mwidth,\
unsigned volume\
)
{
int x, y;
unsigned char *s = buffer - r;
unsigned char *t = tmp;

for (y = 0; y < height; ++y)
	{
	for (x = 0; x < width; ++x, ++s, ++t)
		{
	    unsigned sum = 0;
	    int x1 = (x < r) ? r - x : 0;
	    int x2 = (x + r >= width) ? (r + width - x) : mwidth;
	    int mx;
	    for(mx = x1; mx < x2; ++mx) sum += s[mx] * m[mx];
	    *t = (sum + volume / 2) / volume;
	    //*t = sum;
		}
	}

tmp -= r * width;
for(x = 0; x < width; ++x, ++tmp, ++buffer)
	{
	s = tmp;
	t = buffer;
	for (y = 0; y < height; ++y, s += width, t += width)
		{
	    unsigned sum = 0;
	    int y1 = (y < r) ? r - y : 0;
	    int y2 = (y + r >= height) ? (r + height - y) : mwidth;
	    unsigned char *smy = s + y1 * width;
	    int my;
	    for (my = y1; my < y2; ++my, smy += width)
		sum += *smy * m[my];
	    *t = (sum + volume / 2) / volume;
		}
    }
} /* end function blur */


// Gaussian matrix
// Maybe for future use.
unsigned gmatrix(unsigned *m, int r, int w, double const A)
{
unsigned volume = 0; // volume under Gaussian area is exactly -pi * base / A
int mx, my;

for (my = 0; my < w; ++my)
	{
	for (mx = 0; mx < w; ++mx)
		{
	    m[mx + my * w] = \
		(unsigned)(exp(A * ((mx - r) * (mx - r) + \
		(my - r) * (my - r))) * base + .5);
	    volume += m[mx + my * w];
	    if (debug_flag) tc_log_msg(MOD_NAME, "%3i ", m[mx + my * w]);
		}
	if (debug_flag) tc_log_msg(MOD_NAME, "\n");
    }
if (debug_flag)
	{
	tc_log_msg(MOD_NAME, "A= %f\n", A);
	tc_log_msg(MOD_NAME, "volume: %i; exact: %.0f; volume/exact: %.6f\n\n", volume, -M_PI*base/A, volume/(-M_PI*base/A));
    }
return volume;
} /* end function gmatrix */


int alpha(double outline_thickness, double blur_radius)
{
int const g_r = ceil(blur_radius);
int const o_r = ceil(outline_thickness);
int const g_w = 2 * g_r + 1;		// matrix size
int const o_w = 2 * o_r + 1;		// matrix size
double const A = log(1.0 / base) / (blur_radius * blur_radius * 2);

int mx, my, i;
unsigned volume = 0; // volume under Gaussian area is exactly -pi * base / A

unsigned *g = (unsigned*)malloc(g_w * sizeof(unsigned));
unsigned *om = (unsigned*)malloc(o_w * o_w * sizeof(unsigned));
if (g == NULL || om == NULL)
	{
	tc_log_msg(MOD_NAME, "subtitler: alpha(): malloc failed.");

	return 0;
	}

if(blur_radius == 0)
	{
	tc_log_msg(MOD_NAME, "subtitler: alpha(): radius is zero, set subtitle fonts to default\n");

	return 0;
	}

// gaussian curve
for (i = 0; i < g_w; ++i)
	{
	g[i] = (unsigned)(exp(A * (i - g_r) * (i - g_r) ) * base + .5);
	volume += g[i];
	if (debug_flag) tc_log_msg(MOD_NAME, "%3i ", g[i]);
    }

//volume *= volume;
if (debug_flag) tc_log_msg(MOD_NAME, "\n");

/* outline matrix */
for (my = 0; my < o_w; ++my)
	{
	for (mx = 0; mx < o_w; ++mx)
		{
	    // antialiased circle would be perfect here, but this one is good enough
	    double d = \
		outline_thickness + 1 - \
		sqrt( (mx - o_r) * (mx - o_r) + (my - o_r) * (my - o_r) );
	    om[mx + my * o_w] = d >= 1 ? base : d <= 0 ? 0 : (d * base + .5);
	    if (debug_flag) tc_log_msg(MOD_NAME, "%3i ", om[mx + my * o_w]);
		}
	if (debug_flag) tc_log_msg(MOD_NAME, "\n");
    }
if (debug_flag) tc_log_msg(MOD_NAME, "\n");

if(outline_thickness == 1.0)
	outline1(bbuffer, abuffer, width, height);	// FAST solid 1 pixel outline
else
	outline(bbuffer, abuffer, width, height, om, o_r, o_w);	// solid outline

//	outline(bbuffer, abuffer, width, height, gm, g_r, g_w);	// Gaussian outline

blur(abuffer, bbuffer, width, height, g, g_r, g_w, volume);

free(g);
free(om);

return 1;
} /* end function alpha */


font_desc_t *make_font(\
	char *font_name, int font_symbols, int font_size, int iso_extention,\
	double outline_thickness, double blur_radius)
{
font_desc_t *pfontd;
char temp[4096];
FILE *pptr;
FILE *fptr;

//if(debug_flag)
	{
	tc_log_msg(MOD_NAME, "make_font(): arg font_name=%s font_symbols=%d font_size=%d iso_extention=%d\n\
	outline_thickness=%.2f blur_radius=%.2f\n",\
	font_name, font_symbols, font_size, iso_extention, outline_thickness, blur_radius);
	}

/* argument check */
if(! font_name) return 0;
if(! font_size) return 0;
if(! iso_extention) return 0;

/* pathfilename of true type font */
if(font_path) free(font_path);
tc_snprintf(temp, sizeof(temp), "%s/.xste/fonts/%s", home_dir, font_name);
font_path = strsave(temp);
if(! font_path) return 0;

/* test if font present in this system (rframes.dat could have been imported) */
fptr = fopen(font_path, "r");
if(! fptr)
	{
	tc_log_msg(MOD_NAME, "subtitler: make_font(): cannot open file %s for read, aborting.\n", font_path);

	exit(1);
	} /* end if font_path not valid (font_path is actually pathfilename) */
fclose(fptr);

/* create font data directory */
tc_snprintf(temp, sizeof(temp), "mkdir %s/.subtitler 2> /dev/zero", home_dir);
pptr = popen(temp, "w");
pclose(pptr);

/* directory where to put the temp files */
tc_snprintf(temp, sizeof(temp), "%s/.subtitler", home_dir);
outdir = strsave(temp);
if(! outdir) return 0;

/* encoding string */
tc_snprintf(temp, sizeof(temp), "iso-8859-%d", iso_extention);
encoding = strsave(temp);
if(! encoding) return 0;

encoding_name = encoding;

ppem = font_size;
append_mode = 0;
unicode_desc = 0;

padding = ceil(blur_radius) + ceil(outline_thickness);

if(! prepare_charset() ) return 0;

if(! render() ) return 0;

if(! write_bitmap(bbuffer, 'b') ) return 0;

abuffer = (unsigned char*)malloc(width * height);
if(! abuffer) return 0;

if(! alpha(outline_thickness, blur_radius) ) return 0;

if(! write_bitmap(abuffer, 'a') ) return 0;

free(bbuffer);
free(abuffer);

/* reload the font ! */

/* read in font (also needed for frame counter) */
tc_snprintf(temp, sizeof(temp), "%s/font.desc", outdir);

pfontd = read_font_desc(temp, 1, 0);
if(! pfontd)
	{
	tc_log_msg(MOD_NAME, "subtitler: make_font(): could not load font %s for read, aborting.\n", temp);

	return 0;
	}

pfontd -> outline_thickness = outline_thickness;
pfontd -> blur_radius = blur_radius;

return pfontd;
} /* end function make_font */

