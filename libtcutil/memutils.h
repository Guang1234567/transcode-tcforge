/*
 *  memutils.h - memory handling helpers functions for transcode (interface)
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


#ifndef MEMUTILS_H
#define MEMUTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>

/*************************************************************************/

/* this represents just an opaque piece of memory */
typedef struct tcmemchunk_ TCMemChunk;
struct tcmemchunk_ {
    void  *data;
    size_t size;
};

/*************************************************************************/

/*
 * tc_malloc: just a simple wrapper on libc's malloc(), with emits
 *            an additional warning, specifying calling context,
 *            if allocation fails
 * tc_zalloc: like tc_malloc, but zeroes all acquired memory before
 *             returning to the caller (this is quite common in
 *             transcode codebase)
 * tc_realloc: the same thing for realloc()
 * tc_free: the companion memory releasing wrapper.
 */
#define tc_malloc(size)    _tc_malloc(__FILE__, __LINE__, size)
#define tc_zalloc(size)    _tc_zalloc(__FILE__, __LINE__, size)
#define tc_realloc(p,size) _tc_realloc(__FILE__, __LINE__, p, size)
#define tc_free(ptr)       free(ptr)

/*
 * _tc_malloc:
 *     do the real work behind tc_malloc macro
 *
 * Parameters:
 *     file: name of the file on which call occurs
 *     line: line of above file on which call occurs
 *           (above two parameters are intended to be, and usually
 *           are, filled by tc_malloc macro)
 *     size: size of desired chunk of memory
 * Return Value:
 *     a pointer of acquired memory, or NULL if acquisition fails
 * Side effects:
 *     a message is printed on stderr  if acquisition fails
 * Preconditions:
 *     file param not null
 */
void *_tc_malloc(const char *file, int line, size_t size);

/*
 * _tc_zalloc:
 *     do the real work behind tc_zalloc macro
 *
 * Parameters:
 *     file: name of the file on which call occurs
 *     line: line of above file on which call occurs
 *           (above two parameters are intended to be, and usually
 *           are, filled by tc_malloc macro)
 *     size: size of desired chunk of memory
 * Return Value:
 *     a pointer of acquired memory, or NULL if acquisition fails
 * Side effects:
 *     a message is printed on stderr  if acquisition fails
 * Preconditions:
 *     file param not null
 * Postconditions:
 *     if call succeed, acquired memory contains all zeros
 */
void *_tc_zalloc(const char *file, int line, size_t size);

/*
 * _tc_realloc:
 *     do the real work behind tc_realloc macro
 *
 * Parameters:
 *     file: name of the file on which call occurs
 *     line: line of above file on which call occurs
 *           (above two parameters are intended to be, and usually
 *           are, filled by tc_malloc macro)
 *        p: pointer to reallocate
 *     size: size of desired chunk of memory
 * Return Value:
 *     a pointer of acquired memory, or NULL if acquisition fails
 * Side effects:
 *     a message is printed on stderr if acquisition fails
 * Preconditions:
 *     file param not null
 */
void *_tc_realloc(const char *file, int line, void *p, size_t size);

/*
 * Allocate a buffer aligned to the machine's page size, if known.  The
 * buffer must be freed with buffree() (not free()).
 */

#define tc_bufalloc(size) _tc_bufalloc(__FILE__, __LINE__, size)

/*
 * _tc_malloc:
 *     do the real work behind _tc_bufalloc macro
 *
 * Parameters:
 *     file: name of the file on which call occurs
 *     line: line of above file on which call occurs
 *           (above two parameters are intended to be, and usually
 *           are, filled by tc_malloc macro)
 *     size: size of desired chunk of memory
 * Return Value:
 *     a pointer of acquired, aligned, memory, or NULL if acquisition fails
 * Side effects:
 *     a message is printed on stderr (20051017)
 * Preconditions:
 *     file param not null
 */

void *_tc_bufalloc(const char *file, int line, size_t size);

/*
 * tc_buffree:
 *     release a memory buffer acquired using tc_bufalloc
 *
 * Parameters:
 *     ptr: pointer obtained as return value of a succesfull
 *          tc_bufalloc() call
 * Return Value:
 *     none
 * Preconditions:
 *     ptr is acquired via tc_bufalloc(). Really BAD things will happen
 *     if a buffer acquired via tc_bufalloc() is released using anything
 *     but tc_buffree(), or vice versa.
 */
void tc_buffree(void *ptr);

#ifdef __cplusplus
}
#endif

#endif  /* MEMUTILS_H */
