/*
 *  libtc.h - include file for utilities library for transcode
 *
 *  Copyright (C) Thomas Oestreich - August 2003
 *  Copyright (C) Transcode Team - 2005-2010
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

#ifndef LIBTC_H
#define LIBTC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "libtcutil/tcutil.h"
#include "mediainfo.h"


/*
 * libtc_init:
 *     tune up some libtc settings.
 *     It's safe to call libtc_setup multiple times BEFORE to call any other
 *     libtc function.
 *
 * Parameters:
 *     WRITEME
 * Return Value:
 *     TC_ERROR on error, TC_OK if succesfull.
 * Side effects:
 *     various. See description of flags above.
 * Preconditions:
 *     call this function BEFORE any other libtc function.
 */
int libtc_init(int *argc, char ***argv);

/*************************************************************************/

/*
 * tc_compute_fast_resize_values:
 *     compute internal values needed for video frame fast resize (-B/-X)
 *     given base resolution (ex_v_{width,height}) and target one
 * 	   (zoom_{width,height}).
 *     WARNING: at moment of writing there are some back compatibility
 *     constraints, nevethless this function interface (notabley I/O
 *     parameters passing) needs a SERIOUS rethink.
 * 
 * Parameters:
 *      _vob: pointer to a structure on which read/store values for
 *            computation.
 *            Should ALWAYS really be a pointer to a vob_t structure,
 *            but vob_t pointer isn't used (yet) in order to avoid
 *            libtc/transcode.h interdependency.
 *            I'm not yet convinced that those informations should go
 *            in TCExportInfo because only transcode core needs them.
 *            Perhaps the cleanest solution is to introduce yet
 *            another structure :\.
 *            If anyone has a better solution just let me know -- FR.
 *            vob_t fields used:
 *                ex_v_{width, height}: base resolution (In)
 *                zoom_{width, height}: target resolution (In)
 *                resize{1,2}_mult, vert_resize{1,2}, hori_resize{1,2}:
 *                                   computed parameters (Out)
 *    strict: if !0, allow only enlarging and shrinking of frame in
 *            both dimensions, and fail otherwise.
 * Return Value:
 *      0 succesfull
 *     -1 error, computation failed 
 *        (i.e. width or height not multiple of 8)
 * Side effects:
 *     if succesfull, zoom_{width,height} will be set to 0.
 */
int tc_compute_fast_resize_values(void *_vob, int strict);

/*************************************************************************/

/**
 * tc_find_best_aspect_ratio:
 * 	set sar_num and sar_den to the sample aspect ratio (also called
 * 	pixel aspect ratio) described by vob->ex_par,
 * 	vob->ex_par_width, vob->ex_par_height and vob->ex_asr.
 *
 * This function might return quite high values in sar_num and
 * sar_den. Depending on what codec these parameters are given to,
 * eventually a common factor should be reduced first. In case of x264
 * this is not needed, because it's done in x264's code.
 *
 * Parameters:
 *         vob: constant pointer to vob structure.
 *     sar_num: integer to store SAR-numerator in.
 *     sar_den: integer to store SAR-denominator in.
 *         tag: tag to use in log messages (if any).
 *
 * Return Value:
 *     0 on success, nonzero otherwise (this means bad parameters).
 */
int tc_find_best_aspect_ratio(const void *_vob,
                              int *sar_num, int *sar_den,
			      const char *tag);

/*************************************************************************/

/**
 * tc_sys_get_hw_threads:
 *       get the number of thread that the system can run in parallel
 *       in hardware (aka real concurrency, aka number of CPUs|cores).
 *
 * Parameters:
 *       nthreads: pointer to integer. If succesfull, store here the
 *                 number of threads.
 *
 * Return Value:
 *       TC_OK if succesfull
 *       TC_ERROR otherwise, or if `nthreads' is NULL.
 *
 * Postconditions:
 *       `nthreads' is changed only if succesfull.
 */
int tc_sys_get_hw_threads(int *nthreads);


/*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif  /* _LIBTC_H */
