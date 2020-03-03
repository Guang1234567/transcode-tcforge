/*
 *  tc_functions.c - various common functions for transcode
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
#include "config.h"
#endif

#ifndef OS_BSD
# ifdef HAVE_MALLOC_H
#  include <malloc.h>
# endif
#endif


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <string.h>
#include <pthread.h>


#include "xio.h"
#include "libtc.h"
#include "ratiocodes.h"

#include "tccore/tc_defaults.h"
#include "tccore/job.h"



/*************************************************************************/

/* frontend for lower level libtcutil code */
int libtc_init(int *argc, char ***argv)
{
    tc_log_init();
    return tc_log_open(TC_LOG_TARGET_CONSOLE, TC_LOG_MARK, argc, argv);
}

/*************************************************************************/

#define RESIZE_DIV      8
#define DIM_IS_OK(dim)  ((dim) % RESIZE_DIV == 0)

int tc_compute_fast_resize_values(void *_vob, int strict)
{
    int ret = -1;
    int dw = 0, dh = 0; /* delta width, height */
    vob_t *vob = _vob; /* adjust pointer */

    /* sanity checks first */
    if (vob == NULL) {
        return -1;
    }
    if (!DIM_IS_OK(vob->ex_v_width) || !DIM_IS_OK(vob->ex_v_width)) {
        return -1;
    }
    if (!DIM_IS_OK(vob->zoom_width) || !DIM_IS_OK(vob->zoom_width)) {
        return -1;
    }
    
    dw = vob->ex_v_width - vob->zoom_width;
    dh = vob->ex_v_height - vob->zoom_height;
    /* MORE sanity checks */
    if (!DIM_IS_OK(dw) || !DIM_IS_OK(dh)) {
        return -1;
    }
    if (dw == 0 && dh == 0) {
        /* we're already fine */
        ret = 0;
    } else  if (dw > 0 && dh > 0) {
        /* smaller destination frame -> -B */
        vob->resize1_mult = RESIZE_DIV;
        vob->hori_resize1 = dw / RESIZE_DIV;
        vob->vert_resize1 = dh / RESIZE_DIV;
        ret = 0;
    } else if (dw < 0 && dh < 0) {
        /* bigger destination frame -> -X */
        vob->resize2_mult = RESIZE_DIV;
        vob->hori_resize2 = -dw / RESIZE_DIV;
        vob->vert_resize2 = -dh / RESIZE_DIV;
        ret = 0;
    } else if (strict == 0) {
        /* always needed in following cases */
        vob->resize1_mult = RESIZE_DIV;
        vob->resize2_mult = RESIZE_DIV;
        ret = 0;
        if (dw <= 0 && dh >= 0) {
            vob->hori_resize2 = -dw / RESIZE_DIV;
            vob->vert_resize1 = dh / RESIZE_DIV;
        } else if (dw >= 0 && dh <= 0) {
            vob->hori_resize1 = dw / RESIZE_DIV;
            vob->vert_resize2 = -dh / RESIZE_DIV;
        }
    }

    if (ret == 0) {
        vob->zoom_width = 0;
        vob->zoom_height = 0;
    }
    return ret;
}

#undef RESIZE_DIV
#undef DIM_IS_OK

/*************************************************************************/


int tc_find_best_aspect_ratio(const void *_vob,
                              int *sar_num, int *sar_den,
                              const char *tag)
{
    const vob_t *vob = _vob; /* adjust pointer */
    int num, den;

    if (!vob || !sar_num || !sar_den) {
        return TC_ERROR;
    }

    /* Aspect Ratio Calculations (modified code from export_ffmpeg.c) */
    if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_PAR) {
        if (vob->ex_par > 0) {
            /* 
             * vob->ex_par MUST be guarantee to be in a sane range
             * by core (transcode/tcexport 
             */
            tc_par_code_to_ratio(vob->ex_par, &num, &den);
        } else {
            /* same as above */
            num = vob->ex_par_width;
            den = vob->ex_par_height;
        }
        tc_log_info(tag, "DAR value ratio calculated as %f = %d/%d",
                    (double)num/(double)den, num, den);
    } else {
        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_ASR) {
            /* same as above for PAR stuff */
            tc_asr_code_to_ratio(vob->ex_asr, &num, &den);
            tc_log_info(tag, "display aspect ratio calculated as %f = %d/%d",
                        (double)num/(double)den, num, den);

            /* ffmpeg FIXME:
             * This original code might lead to rounding/truncating errors
             * and maybe produces too high values for "den" and
             * "num" for -y ffmpeg -F mpeg4
             *
             * sar = dar * ((double)vob->ex_v_height / (double)vob->ex_v_width);
             * lavc_venc_context->sample_aspect_ratio.num = (int)(sar * 1000);
             * lavc_venc_context->sample_aspect_ratio.den = 1000;
             */

             num *= vob->ex_v_height;
             den *= vob->ex_v_width;
             /* I don't need to reduce since x264 does it itself :-) */
             tc_log_info(tag, "sample aspect ratio calculated as"
                              " %f = %d/%d",
                              (double)num/(double)den, num, den);

        } else { /* user did not specify asr at all, assume no change */
            tc_log_info(tag, "set display aspect ratio to input");
            num = 1;
            den = 1;
        }
    }

    *sar_num = num;
    *sar_den = den;
    return TC_OK;
}

/*************************************************************************/
/* system support (someday will be moved in a separate file)             */

#define PROCINFO_FILE       "/proc/cpuinfo"
#define PROCINFO_TAG        "processor"
#define PROCINFO_TAG_LEN    9

static int tc_sys_get_hw_threads_linux(int *nthreads)
{
    int ret = TC_ERROR;
    int procs = 0;

    FILE *f = fopen(PROCINFO_FILE, "r");
    if (f) {
        char buf[TC_BUF_MAX];
        while (fgets(buf, sizeof(buf), f)) {
            if(strncmp(buf, PROCINFO_TAG, PROCINFO_TAG_LEN) == 0) {
                procs++;
                ret = TC_OK;
                /* we declare success only if we found
                 * at least  one processor entry
                 */
            }
        }
        fclose(f);
    }
    if (ret == TC_OK) {
        *nthreads = procs;
    }
    return ret;
}

int tc_sys_get_hw_threads(int *nthreads)
{
    if (nthreads != NULL) {
#if defined OS_LINUX
        return tc_sys_get_hw_threads_linux(nthreads);
#else
        /* add here more platform-specific checks */
        return TC_ERROR;
#endif
    }
    return TC_ERROR;
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

