/*
 *  strutils.h - string handling helpers for transcode (interface)
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

#ifndef STRUTILS_H
#define STRUTILS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Safer string functions from OpenBSD, because these are not in every
 * libc implementations.
 */

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size);
#endif

#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t size);
#endif

/*
 * tc_strsplit:
 *      split a given string into tokens using given separator character.
 *      Return NULL-terminated array of splitted tokens, and optionally
 *      return (via a out parameter) size of returned array.
 *
 * Parameters:
 *         str: string to split
 *         sep: separator CHARACTER: cut string when sep is found
 *  pieces_num: if not NULL, store here the size of returned array
 * Return value:
 *      NULL-terminated array of splitted pieces.
 *      You must explicitely free this returned array by using tc_strfreev
 *      (see below) in order to avoid memleaks.
 */
char **tc_strsplit(const char *str, char sep, size_t *pieces_num);

/*
 * tc_strfreev:
 *      return an array of strings as returned by tc_strsplit
 *
 * Parameters:
 *      pieces: return value of tc_strsplit to be freed.
 * Return value:
 *      None.
 */
void tc_strfreev(char **pieces);

/*
 * tc_strstrip:
 * 	remove IN PLACE heading and trailing whitespaces from a given
 * 	C-string. This means that given string will be mangled to
 * 	remove such whitespace while moving pointer to first element
 * 	and terminating '\0'.
 * 	It's safe to supply a NULL string.
 * Parameters:
 *      s: string to strip.
 * Return Value:
 *      None
 */
void tc_strstrip(char *s);

/*
 * tc_test_string:
 *	check the return value of snprintf, strlcpy, and strlcat.
 *      If an error is detected, prints reason.
 *
 * Parameters:
 *        file: name of source code file on which this function is called
 *              (this parameter is usually equal to __FILE__).
 *        line: line of source code file on which this function is called
 *              (this parameter is usually equal to __LINE__).
 *       limit: maximum size of char buffer previously used.
 *         ret: return code of one of above function.
 *      errnum: error code (this parameter is usually equal to errno)
 * Return Value:
 * 	< 0 is an internal error.
 *      >= limit means characters were truncated.
 *      0 if not problems.
 *      1 if error.
 */
int tc_test_string(const char *file, int line, int limit,
                   long ret, int errnum);


/*
 * These versions of [v]snprintf() return -1 if the string was truncated,
 * printing a message to stderr in case of truncation (or other error).
 */
#define tc_vsnprintf(buf,limit,format,args...) \
    _tc_vsnprintf(__FILE__, __LINE__, buf, limit, format , ## args)
#define tc_snprintf(buf,limit,format,args...) \
    _tc_snprintf(__FILE__, __LINE__, buf, limit, format , ## args)

int _tc_vsnprintf(const char *file, int line, char *buf, size_t limit,
                  const char *format, va_list args);
int _tc_snprintf(const char *file, int line, char *buf, size_t limit,
                 const char *format, ...);

/*
 * tc_strdup: a macro wrapper on top of _tc_strndup, like tc_malloc, above
 * tc_strndup: like tc_strdup, but copies only N byte of given string
 *
 * This function does the same thing of libc's standard function
 * strdup(3) and the GNU extension strndup(3), but using libtc's
 * tc_malloc features.
 */
#define tc_strdup(s) \
            _tc_strndup(__FILE__, __LINE__, s, strlen(s))
#define tc_strndup(s, n) \
            _tc_strndup(__FILE__, __LINE__, s, n)

/*
 * _tc_strndup:
 *     do the real work behind tc_strdup/tc_strndup macro. This function
 *     adds automatically and implicitely a '\0' terminator at end of
 *     copied string.
 *
 * Parameters:
 *     file: name of the file on which call occurs
 *     line: line of above file on which call occurs (above two parameters
 *           are intended to be, and usually are, filled by tc_malloc macro)
 *        s: null-terminated string to copy
 *        n: copy at most 'n' characters of original string.
 * Return Value:
 *     a pointer to a copy of given string. This pointer must be freed using
 *     tc_free() to avoid memory leaks
 * Side effects:
 *     a message is printed on stderr (20051017)
 * Preconditions:
 *     file param not null
 * Postconditions:
 *     none
 */
char *_tc_strndup(const char *file, int line, const char *s, size_t n);

/*************************************************************************/

/*
 * tc_mangle_cmdline:
 *      parse a command line option array looking for a given option.
 *      Given option can be short or long but must be given literally.
 *      So, if you want to mangle "--foobar", give "--foobar" not
 *      "foobar". Same story for short options "-V": use "-V" not "V".
 *      If given option isn't found in string option array, do nothing
 *      and return succesfull (see below). If option is found but
 *      its argument isn't found, don't mangle string options array
 *      but return failure.
 *      If BOTH option and its value is found, store a pointer to
 *      option value into "optval" parameter and remove both option
 *      and value from string options array.
 * Parameters:
 *      argc: pointer to number of values present into option string
 *            array. This parameter must be !NULL and it's updated
 *            by a succesfull call of this function.
 *      argv: pointer to array of option string items. This parameter
 *            must be !NULL and it's updated by a succesfull call of
 *            this function
 *       opt: option to look for.
 *    optval: if !NULL, this function will expect a value for given option;
 *            if such value is found, `optval' will point to it.
 * Return value:
 *      1: no option found
 *      0: succesfull
 *     -1: bad parameter(s) (NULL)
 *     -2: bad usage: expected value for option, but not found,
 * Postconditions:
 *      this function must operate trasparently by always leaving
 *      argc/argv in an usable and consistent state.
 */
int tc_mangle_cmdline(int *argc, char ***argv,
                      const char *opt, const char **optval);


#ifdef __cplusplus
}
#endif

#endif  /* STRUTILS_H */
