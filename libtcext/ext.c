/*
 *  ext.c - glue code for interfacing transcode with external libraries.
 *  Written by Thomas Oestreich, Francesco Romani, Andrew Church, and others
 *
 *  This file is part of transcode, a video stream processing tool.
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif


#include "libtcutil/tcthread.h"
#include "libtcutil/tcutil.h"
#include "libtc/libtc.h"
#include "aclib/ac.h"

#include "tc_ext.h"
#ifdef HAVE_FFMPEG
#include "tc_avcodec.h"
#endif
#ifdef HAVE_OGG
#include "tc_ogg.h"
#endif
#ifdef HAVE_LZO
#include "tc_lzo.h"
#endif
#ifdef HAVE_GRAPHICSMAGICK
#include "tc_magick.h"
#endif



#ifdef HAVE_FFMPEG

/*************************************************************************/
/* libav* support                                                        */
/*************************************************************************/

/*
 * libavcodec lock. Used for serializing initialization/open of library.
 * Other libavcodec routines (avcodec_{encode,decode}_* should be thread
 * safe (as ffmpeg crew said) if each thread uses it;s own AVCodecContext,
 * as we do.
 */
static TCMutex tc_libavcodec_mutex;

void tc_lock_libavcodec(void)
{
    tc_mutex_lock(&tc_libavcodec_mutex);
}

void tc_unlock_libavcodec(void)
{
    tc_mutex_unlock(&tc_libavcodec_mutex);
}


#endif

#ifdef HAVE_GRAPHICSMAGICK


/*************************************************************************/
/* GraphicsMagick support / core                                         */
/*************************************************************************/

static TCMutex tc_magick_mutex;
static int magick_refcount = 0;

int tc_ref_graphicsmagick(void)
{
    int ref = 0;
    tc_mutex_lock(&tc_magick_mutex);
    ref = ++magick_refcount;
    tc_mutex_unlock(&tc_magick_mutex);
    return ref;
}


int tc_unref_graphicsmagick(void)
{
    int ref = 0;
    tc_mutex_lock(&tc_magick_mutex);
    ref = --magick_refcount;
    tc_mutex_unlock(&tc_magick_mutex);
    return ref;
}

#endif


int tc_ext_init(void)
{
#ifdef HAVE_FFMPEG
    tc_mutex_init(&tc_libavcodec_mutex);
#endif
#ifdef HAVE_GRAPHICSMAGICK
    tc_mutex_init(&tc_magick_mutex);
    magick_refcount = 0;
#endif
    return TC_OK;
}


/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */

