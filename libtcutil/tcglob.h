/*
 * tcglob.h -- simple iterator over a path collection expressed through
 *             glob (7) semantic.
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


#ifndef TCGLOB_H
#define TCGLOB_H

/*
 * Quick Summary:
 *   this code iterates over a collection of pathnames expressed through
 *   glob (7) syntax, in a compact, consolidated and efficient way.
 *   The intended usage of this code is as iterator/pathname generator.
 *
 */

#include <stdint.h>

/* opaque type */
typedef struct tcglob_ TCGlob;


/*
 * tc_glob_open:
 *    create a new TCGlob structure based on a given pathname
 *    glob expression.
 *
 * Parameters:
 *    pattern: glob pattern expression to be expanded
 *      flags: currently unused (use 0 (zero)).
 * Return Value:
 *    if succesfull, a newly-allocated TCGlob structure ready
 *    to be used through tc_glob_* functions;
 *    (use tc_glob_close() to dispose it)
 *    NULL if failed.
 */
TCGlob *tc_glob_open(const char *pattern, uint32_t flags);


/*
 * tc_glob_next:
 *    get new expanded pathname from given tcglob structure.
 *
 * Parameters:
 *    tcg: pointer to TCGlob structure to be used.
 * Return Value:
 *    if succesfull, a constant pointer to the next expanded pathname.
 *    there is NO NEED to free() it explicitely after usage.
 *    The returned pointer is guaranteed to be valid AT LEAST until
 *    next tc_glob_next() call, but not after.
 *    if failed, returns NULL.
 *    PLEASE NOTE that this function returns NULL also if all pathnames
 *    are been expanded, so there is NO MORE pathname to get.
 *    You can safely think that returning NULL acts as 'guard condition'
 *    meaning something like 'iteration must end here'.
 *    (see also tc_glob_has_more)
 */
const char *tc_glob_next(TCGlob *tcg);


/*
 * tc_glob_has_more:
 *    tell if current glob expression has at least one more pathname to
 *    be expanded (= is terminated) or not.
 *
 * Parameters:
 *    tcg: pointer to TCGlob structure to be checked.
 * Return Value:
 *    > 0: pathname expansion not yet ended (= there is at least one more
 *         pathname to get with tc_glob_next)
 *      0: pathname expansion ended.
 */
int tc_glob_has_more(TCGlob *tcg);


/*
 * tc_glob_close:
 *    finalize a TCGlob structure and release all resources acquired via
 *    tc_glob_open. Subsequent tc_glob_* calls using this TCGlob structure
 *    will lead to undefined behaviour.
 *
 * Parameters:
 *    tcg: pointer to TCGlob structure to be finalized.
 * Return Value:
 *     !0: succesfull.
 *      0: otherwise.
 */
int tc_glob_close(TCGlob *tcg);

#endif /* TCGLOB_H */
