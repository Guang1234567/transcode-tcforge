/*
 *  tc_ogg.h -- transcode OGG/Xiph formats utilities.
 *  (C) 2007-2010 Francesco Romani <fromani at gmail dot com>
 *
 * This file is part of transcode, a video stream processing tool.
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TC_OGG_H
#define TC_OGG_H

#include <stdint.h>

#include <ogg/ogg.h>

#include "aclib/ac.h"

#include "libtc/libtc.h"
#include "libtc/tccodecs.h"


#ifndef PACKAGE
#define PACKAGE MOD_NAME
#endif
#ifndef VERSION
#define VERSION MOD_VERSION
#endif

/*
 * Xiph integration support functions.
 * We cheat a little bit here, by including some static (and short)
 * functions into this header.
 * The key point is that these functions will always be duplicated, even
 * moving them into a proper .c file. I'll probably end up moving them
 * in a .c file *and* adding a few more code soon.
 *
 * ---
 *
 * The following are utility functions for dealing with ogg packets
 * and extradata, needed by all ogg-related modules. See comments
 * in module to learn about quirks and gotchas.
 */

/*************************************************************************/

typedef struct oggextradata_ OGGExtraData;
struct oggextradata_ {
    int32_t granule_shift;
    ogg_packet header;
    ogg_packet comment;
    ogg_packet code;
};

#ifdef TC_ENCODER
void tc_ogg_del_packet(ogg_packet *op);
void tc_ogg_del_extradata(OGGExtraData *oxd);
int tc_ogg_dup_packet(ogg_packet *dst, const ogg_packet *src);
#endif /* TC_ENCODER */

#endif /* TC_OGG_H */

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

