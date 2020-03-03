/*
 * tomsmocompfilter.h
 *
 *  Filter access code (c) by Matthias Hopf - July 2004
 *  Base code taken from DScaler's tomsmocomp filter (c) 2002 Tom Barry,
 *  ported by Dirk Ziegelmeier for kdetv.
 *  Code base kdetv-cvs20040727.
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

#include "dscaler_interface.h"
#define false 0
#define true  1

/* Only accessed inside this module, but in several functions... */
static MEMCPY_FUNC* pMyMemcpy;
// these look unused but are not, sigh... EMS
static int  IsOdd;
static const unsigned char* pWeaveSrc;
static const unsigned char* pWeaveSrcP;
static unsigned char* pWeaveDest;
static const unsigned char* pCopySrc;
static const unsigned char* pCopySrcP;
static unsigned char* pCopyDest;
static int src_pitch;
static int dst_pitch;
static int rowsize;
static int FldHeight;

static inline int Fieldcopy(void *dest, const void *src, size_t count,
			    int rows, int dst_pitch, int src_pitch) {
    unsigned char* pDest = (unsigned char*) dest;
    unsigned char* pSrc = (unsigned char*) src;
    int i;

    for (i=0; i < rows; i++) {
	pMyMemcpy(pDest, pSrc, count);
	pSrc += src_pitch;
	pDest += dst_pitch;
    }
    return 0;
}


/* Kludge:
 * gcc-2.95 wont compile postprocess_template in -fPIC mode
 * gcc-3.2.3 does work -- tibit
 */
#if defined(PIC) && (__GNUC__ == 2)
#undef HAVE_ASM_MMX
#undef HAVE_ASM_MMX2
#undef HAVE_ASM_SSE
#undef HAVE_ASM_3DNOW
#warning All optimizations disabled because of gcc-2.95
#endif
