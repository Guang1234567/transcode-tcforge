/*
 *  optstr.c
 *
 *  Copyright (C) Tilmann Bitterberg 2003
 *
 *  Description: A general purpose option string parser
 *
 *  Usage: see optstr.h, please
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* for vsscanf */
#ifdef HAVE_VSSCANF
#  define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "optstr.h"
#include "memutils.h"
#include "strutils.h"



const char* optstr_lookup(const char *haystack, const char *needle)
{
    const char *ch = haystack;
    int found = 0;
    size_t len = strlen(needle);

    while (!found) {
        ch = strstr(ch, needle);

        /* not in string */
        if (!ch) {
            break;
        }

        /* do we want this hit? ie is it exact? */
        if (ch[len] == '\0' || ch[len] == '=' || ch[len] == ARG_SEP) {
            found = 1;
        } else {
            /* go a little further */
            ch++;
        }
    }

    return ch;
}

int optstr_get(const char *options, const char *name, const char *fmt, ...)
{
    va_list ap;     /* points to each unnamed arg in turn */
    int num_args = 0, n = 0;
    size_t pos, fmt_len = strlen(fmt);
    const char *ch = NULL;

#ifndef HAVE_VSSCANF
    void *temp[ARG_MAXIMUM];
#endif

    ch = optstr_lookup(options, name);
    if (!ch) {
        return -1;
    }

    /* name IS in options */

    /* Find how many arguments we expect */
    for (pos = 0; pos < fmt_len; pos++) {
        if (fmt[pos] == '%') {
            ++num_args;
            /* is this one quoted  with '%%' */
            if (pos + 1 < fmt_len && fmt[pos + 1] == '%') {
                --num_args;
                ++pos;
            }
        }
    }

#ifndef HAVE_VSSCANF
    if (num_args > ARG_MAXIMUM) {
        fprintf (stderr,
            "(%s:%d) Internal Overflow; redefine ARG_MAXIMUM (%d) to something higher\n",
            __FILE__, __LINE__, ARG_MAXIMUM);
        return -2;
    }
#endif

    n = num_args;
    /* Bool argument */
    if (num_args <= 0) {
        return 0;
    }

    /* skip the `=' (if it is one) */
    ch += strlen( name );
    if( *ch == '=' )
        ch++;

    if( !*ch )
        return 0;

    va_start(ap, fmt);

#ifndef HAVE_VSSCANF
    while (--n >= 0) {
        temp[num_args - n - 1] = va_arg(ap, void *);
    }

    n = sscanf(ch, fmt,
            temp[0],  temp[1],  temp[2],  temp[3], temp[4],
            temp[5],  temp[6],  temp[7],  temp[8], temp[9],
            temp[10], temp[11], temp[12], temp[13], temp[14],
            temp[15]);

#else
    /* this would be very nice instead of the above,
     * but it does not seem portable
     */
     n = vsscanf(ch, fmt, ap);
#endif

    va_end(ap);

    return n;
}

static int optstr_is_string_arg(const char *fmt)
{
    if (!fmt) {
        return 0;
    }
    if (!strlen(fmt)) {
        return 0;
    }
    if (strchr(fmt, 's')) {
        return 1;
    }
    if (strchr(fmt, '[') && strchr(fmt, ']')) {
        return 1;
    }
    return 0;
}


int optstr_filter_desc(char *buf,
                       const char *filter_name,
                       const char *filter_comment,
                       const char *filter_version,
                       const char *filter_author,
                       const char *capabilities,
                       const char *frames_needed)
{
    int len = strlen(buf);
    if (tc_snprintf(buf + len, ARG_CONFIG_LEN - len,
                    "\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\"\n",
                    filter_name,filter_comment,filter_version,
                    filter_author, capabilities, frames_needed) <= 0) {
        return 1;
    }
    return 0;
}

int optstr_frames_needed(const char *filter_desc, int *needed_frames)
{
    const char *s = NULL;

    if ((s = strrchr(filter_desc, ',')) == NULL) {
        return 1;
    }
    if ((s = strchr(s, '\"')) == NULL) {
        return 1;
    }

    *needed_frames = strtol(s + 1, NULL, 0);
    return 0;
}

int optstr_param(char *buf,
                 const char *name,
                 const char *comment,
                 const char *fmt,
                 const char *val,
                 ...) /* char *valid_from1, char *valid_to1, ... */
{
    va_list ap;
    int n = 0, res = 0, num_args=0;
    size_t buf_len = strlen(buf), fmt_len = strlen(fmt), pos = 0;

    res = tc_snprintf(buf + buf_len, ARG_CONFIG_LEN - buf_len,
                           "\"%s\", \"%s\", \"%s\", \"%s\"",
                           name, comment, fmt, val);
    if(res <= 0) {
        return 1;
    }
    n += res;

    /* count format strings */
    for (pos = 0; pos < fmt_len; pos++) {
        if (fmt[pos] == '%') {
            ++num_args;
            /* is this one quoted  with '%%' */
            if (pos + 1 < fmt_len && fmt[pos + 1] == '%') {
                --num_args;
                ++pos;
            }
        }
    }
    num_args *= 2;

    if (num_args && optstr_is_string_arg(fmt)) {
        num_args = 0;
    }

    va_start(ap, val);
    while (num_args--) {
        res = tc_snprintf(buf + buf_len + n,
                          ARG_CONFIG_LEN - buf_len - n,
                          ", \"%s\"", va_arg(ap, char *));
        if (res <= 0) {
            return 1;
        }
        n += res;
    }
    va_end(ap);

    res = tc_snprintf(buf + buf_len + n, ARG_CONFIG_LEN - buf_len - n, "\n");
    if (res <= 0 ) {
        return 1;
    }

    return 0;
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
