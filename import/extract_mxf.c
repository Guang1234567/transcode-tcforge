/*
 *  extract_mxf.c
 *
 *  Copyright (C) Tilmann Bitterberg - October 2003
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

#undef DDBUG
//#define DDBUG

#include "tccore/tcinfo.h"
#include "src/transcode.h"

#include "ioaux.h"
#include "aux_pes.h"
#include "tc.h"


/* ------------------------------------------------------------
 *
 * extract thread
 *
 * magic: TC_MAGIC_MXF
 *
 * ------------------------------------------------------------*/


void extract_mxf(info_t *ipipe)
{
    import_exit(0);
}

/* Probing */
void probe_mxf(info_t *ipipe)
{

    verbose = ipipe->verbose;

    ipipe->probe_info->frames = 0;
    ipipe->probe_info->width = 0;
    ipipe->probe_info->height = 0;
    ipipe->probe_info->fps = 0;
    ipipe->probe_info->num_tracks = 0;

    // for each audio track
    ipipe->probe_info->track[0].chan = 0;
    ipipe->probe_info->track[0].bits = 0;
    ipipe->probe_info->track[0].samplerate = 0;
    ipipe->probe_info->track[0].format = 0x1;

    ipipe->probe_info->magic = TC_MAGIC_MXF;
    ipipe->probe_info->codec = TC_CODEC_UNKNOWN;
    ipipe->probe_info->frc = 0;

}
