/*
 *  strutils.c - string handling helpers for transcode (implementation)
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

#include "common.h"
#include "logging.h"
#include "memutils.h"
#include "strutils.h"

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <errno.h>


#ifndef OS_BSD
# ifdef HAVE_MALLOC_H
#  include <malloc.h>
# endif
#endif




int tc_test_string(const char *file, int line, int limit, long ret, int errnum)
{
    if (ret < 0) {
        fprintf(stderr, "[%s:%d] string error: %s\n",
                        file, line, strerror(errnum));
        return 1;
    }
    if (ret >= limit) {
        fprintf(stderr, "[%s:%d] truncated %ld characters\n",
                        file, line, (ret - limit) + 1);
        return 1;
    }
    return 0;
}

/*************************************************************************/

/*
 * These versions of [v]snprintf() return -1 if the string was truncated,
 * printing a message to stderr in case of truncation (or other error).
 */

int _tc_vsnprintf(const char *file, int line, char *buf, size_t limit,
                  const char *format, va_list args)
{
    int res = vsnprintf(buf, limit, format, args);
    return tc_test_string(file, line, limit, res, errno) ? -1 : res;
}


int _tc_snprintf(const char *file, int line, char *buf, size_t limit,
                 const char *format, ...)
{
    va_list args;
    int res;

    va_start(args, format);
    res = _tc_vsnprintf(file, line, buf, limit, format, args);
    va_end(args);
    return res;
}

char *_tc_strndup(const char *file, int line, const char *s, size_t n)
{
    char *pc = NULL;

    if (s != NULL) {
        pc = _tc_malloc(file, line, n + 1);
        if (pc != NULL) {
            memcpy(pc, s, n);
            pc[n] = '\0';
        }
    }
    return pc;
}

void tc_strstrip(char *s) 
{
    char *start;

    if (s == NULL) {
        return;
    }
    
    start = s;
    while ((*start != 0) && isspace(*start)) {
        start++;
    }
    
    memmove(s, start, strlen(start) + 1);
    if (strlen(s) == 0) {
        return;
    }
    
    start = &s[strlen(s) - 1];
    while ((start != s) && isspace(*start)) {
        *start = 0;
        start--;
    }
}

char **tc_strsplit(const char *str, char sep, size_t *pieces_num)
{
    const char *begin = str, *end = NULL;
    char **pieces = NULL, *pc = NULL;
    size_t i = 0, n = 2;
    int failed = TC_FALSE;

    if (!str || !strlen(str)) {
        return NULL;
    }

    while (begin != NULL) {
        begin = strchr(begin, sep);
        if (begin != NULL) {
            begin++;
            n++;
        }
    }

    pieces = tc_malloc(n * sizeof(char*));
    if (!pieces) {
        return NULL;
    }

    begin = str;
    while (begin != NULL) {
        size_t len;

        end = strchr(begin, sep);
        if (end != NULL) {
            len = (end - begin);
        } else {
            len = strlen(begin);
        }
        if (len > 0) {
            pc = tc_strndup(begin, len);
            if (pc == NULL) {
                failed = TC_TRUE;
                break;
            } else {
                pieces[i] = pc;
                i++;
            }
        }
        if (end != NULL) {
            begin = end + 1;
        } else {
            break;
        }
    }

    if (failed) {
        /* one or more copy of pieces failed */
        tc_free(pieces);
        pieces = NULL;
    } else { /* i == n - 1 -> all pieces copied */
        pieces[n - 1] = NULL; /* end marker */
        if (pieces_num != NULL) {
            *pieces_num = i;
        }
    }
    return pieces;
}

void tc_strfreev(char **pieces)
{
    if (pieces != NULL) {
        int i = 0;
        for (i = 0; pieces[i] != NULL; i++) {
            tc_free(pieces[i]);
        }
        tc_free(pieces);
    }
}

/*************************************************************************/

int tc_mangle_cmdline(int *argc, char ***argv,
                      const char *opt, const char **optval)
{
    int i = 0, skew = (optval == NULL) ?1 :2, err = -1;

    if (argc == NULL || argv == NULL || opt == NULL) {
        return err;
    }

    err = 1;
    /* first we looking for our option (and it's value) */
    for (i = 1; i < *argc; i++) {
        if ((*argv)[i] && strcmp((*argv)[i], opt) == 0) {
            if (optval == NULL) {
                err = 0; /* we're set */
            } else {
                /* don't peek after the end... */
                if (i + 1 >= *argc || (*argv)[i + 1][0] == '-') {
                    tc_log_warn(__FILE__, "wrong usage for option '%s'", opt);
                    err = 1; /* no option and/or value found */
                } else {
                    *optval = (*argv)[i + 1];
                    err = 0;
                }
            }
            break;
        }
    }

    /*
     * if we've found our option, now we must shift back all
     * the other options after the ours and we must also update argc.
     */
    if (!err) {
        for (; i < (*argc - skew); i++) {
            (*argv)[i] = (*argv)[i + skew];
        }
        (*argc) -= skew;
    }

    return err;
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

