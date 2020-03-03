/*
 *  probe_im.c
 *
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
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "tccore/tcinfo.h"
#include "src/transcode.h"
#include "ioaux.h"
#include "tc.h"
#include "ioaux.h"
#include "libtc/libtc.h"


#ifdef HAVE_GRAPHICSMAGICK
#include "libtcext/tc_magick.h"


void probe_im(info_t *ipipe)
{
    TCMagickContext magick;
    int ret = TC_ERROR;
    
    ret = tc_magick_init(&magick, TC_MAGICK_QUALITY_DEFAULT);
    if (ret != TC_OK) {
        tc_log_error(__FILE__, "cannot create magick context");
        ipipe->error = 1;
        return;
    }

    ret = tc_magick_filein(&magick, ipipe->name);
    if (ret != TC_OK) {
        tc_log_error(__FILE__, "cannot read image file");
        ipipe->error = 1;
        return;
    }

	ipipe->probe_info->width  = TC_MAGICK_GET_WIDTH(&magick);
	ipipe->probe_info->height = TC_MAGICK_GET_HEIGHT(&magick);
	/* slide show? */
	ipipe->probe_info->frc    = 9;   /* FRC for 1 fps */
	ipipe->probe_info->fps    = 1.0;

	ipipe->probe_info->codec  = TC_CODEC_RGB24;
	ipipe->probe_info->magic  = ipipe->magic;

    tc_magick_fini(&magick);

	return;
}

#else   // HAVE_GRAPHICSMAGICK

void probe_im(info_t *ipipe)
{
	tc_log_error(__FILE__, "no support for GraphicsMagick compiled - exit.");
	ipipe->error = 1;
	return;
}

#endif

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
