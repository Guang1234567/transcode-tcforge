/*
 *  memutils.c - memory handling helpers functions for transcode (implementation)
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

#include <unistd.h>

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#ifndef OS_BSD
# ifdef HAVE_MALLOC_H
#  include <malloc.h>
# endif
#endif

#include "memutils.h"

/*************************************************************************/

/* simple malloc wrapper with failure guard. */

void *_tc_malloc(const char *file, int line, size_t size)
{
    void *p = malloc(size);
    if (p == NULL) {
        fprintf(stderr, "[%s:%d] tc_malloc(): can't allocate %lu bytes\n",
                        file, line, (unsigned long)size);
    }
    return p;
}

/* allocate a chunk of memory (like tc_malloc), but zeroes memory before
 * returning. */

void *_tc_zalloc(const char *file, int line, size_t size)
{
    void *p = malloc(size);
    if (p == NULL) {
        fprintf(stderr, "[%s:%d] tc_zalloc(): can't allocate %lu bytes\n",
                        file, line, (unsigned long)size);
    } else {
        memset(p, 0, size);
    }
    return p;
}

/* realloc() wrapper. */

void *_tc_realloc(const char *file, int line, void *p, size_t size)
{
    p = realloc(p, size);
    if (p == NULL && size > 0) {
        fprintf(stderr, "[%s:%d] tc_realloc(): can't allocate %lu bytes\n",
                        file, line, (unsigned long)size);
    }
    return p;
}


/*** FIXME ***: find a clean way to refactorize above functions */

/* Allocate a buffer aligned to the machine's page size, if known.  The
 * buffer must be freed with buffree() (not free()). */

void *_tc_bufalloc(const char *file, int line, size_t size)
{
#ifdef HAVE_GETPAGESIZE
    unsigned long pagesize = getpagesize();
    int8_t *base = malloc(size + sizeof(void *) + pagesize);
    int8_t *ptr = NULL;
    unsigned long offset = 0;

    if (base == NULL) {
        fprintf(stderr, "[%s:%d] tc_bufalloc(): can't allocate %lu bytes\n",
                        file, line, (unsigned long)size);
    } else {
        ptr = base + sizeof(void *);
        offset = (unsigned long)ptr % pagesize;

        if (offset)
            ptr += (pagesize - offset);
        ((void **)ptr)[-1] = base;  /* save the base pointer for freeing */
    }
    return ptr;
#else  /* !HAVE_GETPAGESIZE */
    return malloc(size);
#endif
}

/* Free a buffer allocated with tc_bufalloc(). */
void tc_buffree(void *ptr)
{
#ifdef HAVE_GETPAGESIZE
    if (ptr)
	free(((void **)ptr)[-1]);
#else
    free(ptr);
#endif
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

