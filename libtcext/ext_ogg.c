/*
 *  ext_ogg.c - glue code for interfacing transcode with libogg.
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
#ifdef HAVE_OGG
#include "tc_ogg.h"
#endif



#ifdef HAVE_OGG

/*************************************************************************/
/* OGG support                                                           */
/*************************************************************************/

/* watch out the 'n' here */
/* FIXME */
#ifndef TC_ENCODER
void tc_ogg_del_packet(ogg_packet *op);
void tc_ogg_del_extradata(OGGExtraData *oxd);
int tc_ogg_dup_packet(ogg_packet *dst, const ogg_packet *src);
#endif /* TC_ENCODER */

void tc_ogg_del_packet(ogg_packet *op)
{
    tc_free(op->packet);
    memset(op, 0, sizeof(*op));
}

void tc_ogg_del_extradata(OGGExtraData *oxd)
{
    oxd->granule_shift = 0;
    tc_ogg_del_packet(&oxd->header);
    tc_ogg_del_packet(&oxd->comment);
    tc_ogg_del_packet(&oxd->code);
}

int tc_ogg_dup_packet(ogg_packet *dst, const ogg_packet *src)
{
    int ret = TC_ERROR;

    ac_memcpy(dst, src, sizeof(ogg_packet));
    dst->packet = tc_malloc(src->bytes);
    if (dst->packet) {
        ac_memcpy(dst->packet, src->packet, src->bytes);
        ret = TC_OK;
    }
    return ret;
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

