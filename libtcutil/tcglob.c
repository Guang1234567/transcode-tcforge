/*
 * tcglob.c -- simple iterator over a path collection expressed through
 *             glob (7) semantic (implementation).
 * (C) 2007-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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


#include "common.h"
#include "memutils.h"
#include "strutils.h"
#include "logging.h"
#include "tcglob.h"

#include <string.h>
#include <glob.h>


struct tcglob_ {
    const char *pattern;
    glob_t glob;
    int current;
};

#define GLOB_NOERROR 0


TCGlob *tc_glob_open(const char *pattern, uint32_t flags)
{
    TCGlob *tcg = NULL;
    flags = GLOB_ERR; /* flags intentionally overridden */

    if (pattern != NULL && strlen(pattern) > 0) {
        tcg = tc_malloc(sizeof(TCGlob));

        if (tcg != NULL) {
            int err = 0;

            tcg->pattern = NULL;

            err = glob(pattern, flags, NULL, &(tcg->glob));

            switch (err) {
              case GLOB_NOMATCH:
                tcg->pattern = tc_strdup(pattern); // XXX
                tcg->current = -1;
                break;
              case GLOB_NOERROR:
                tcg->current = 0;
                break;
              default: /* any other error: clean it up */
                tc_log_error(__FILE__, "internal glob failed (code=%i)",
                             err);
                tc_free(tcg);
                tcg = NULL;
            }
        }
    }
    return tcg;
}


const char *tc_glob_next(TCGlob *tcg)
{
    const char *ret = NULL;
    if (tcg != NULL) {
        if (tcg->current == -1) {
            ret = tcg->pattern;
        }
        if (tcg->current < tcg->glob.gl_pathc) {
            ret = tcg->glob.gl_pathv[tcg->current];
        }
        tcg->current++;
    }
    return ret;
}

int tc_glob_has_more(TCGlob *tcg)
{
    if (tcg == NULL) {
        return 0;
    }
    return (tcg->current < tcg->glob.gl_pathc) + (tcg->pattern != NULL);
}


int tc_glob_close(TCGlob *tcg)
{
    if (tcg != NULL) {
        if (tcg->pattern != NULL) {
           tc_free((void*)tcg->pattern);
           tcg->pattern = NULL;
        }
        globfree(&(tcg->glob));
        tc_free(tcg);
    }
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
