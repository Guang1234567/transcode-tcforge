/*
 *  ioutils.c - various I/O helper functions for transcode (interface)
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


#ifndef IOUTILS_H
#define IOUTILS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/param.h>
#include <string.h>

#ifndef OS_BSD
# ifdef HAVE_MALLOC_H
#  include <malloc.h>
# endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * tc_test_program:
 *     check if a given program is avalaible in current PATH.
 *     This function of course needs to read (and copy) the PATH
 *     environment variable
 *
 * Parameters:
 *     name: name of program to look for.
 * Return Value:
 *     0 if program was found in PATH.
 *     ENOENT if program was not found in PATH
 *     value of errno if program was found in PATH but it wasn't accessible
 *     for some reason.
 */
int tc_test_program(const char *name);


/*
 * tc_file_check:
 *     verify the type of a given file (path) this function will be
 *     deprecated very soon, replaced by a powered tc_probe_path().
 *
 * Parameters:
 *     file: the file (really: path) to verify.
 * Return Value:
 *     -1 if an internal error occur
 *     0  if given path is really a file
 *     1  if given path is a directory
 * Side effects:
 *     none
 * Preconditions:
 *     none
 * Postconditions:
 *     none
 */
int tc_file_check(const char *file);

/*
 * tc_pread:
 *     read an entire buffer from a file descriptor, restarting
 *     automatically if interrupted. This function is basically a wrapper
 *     around posix read(2); read(2) can be interrupted by a signal,
 *     so doesn't guarantee that all requested bytes are effectively readed
 *     when read(2) returns; this function ensures so, except for critical
 *     errors.
 * Parameters:
 *      fd: read data from this file descriptor
 *     buf: pointer to a buffer which will hold readed data
 *     len: how much data function must read from fd
 * Return Value:
 *     size of effectively readed data
 * Side effects:
 *     errno is readed internally
 * Postconditions:
 *     read exactly the requested bytes, if no *critical*
 *     (tipically I/O related) error occurs.
 */
ssize_t tc_pread(int fd, uint8_t *buf, size_t len);

/*
 * tc_pwrite:
 *     write an entire buffer from a file descriptor, restarting
 *     automatically if interrupted. This function is basically a wrapper
 *     around posix write(2); write(2) can be interrupted by a signal,
 *     so doesn't guarantee that all requested bytes are effectively writed
 *     when write(2) returns; this function ensures so, except for critical
 *     errors.
 * Parameters:
 *      fd: write data on this file descriptor
 *     buf: pointer to a buffer which hold data to be written
 *     len: how much data function must write in fd
 * Return Value:
 *     size of effectively written data
 * Side effects:
 *     errno is readed internally
 * Postconditions:
 *     write exactly the requested bytes, if no *critical* (tipically I/O
 *     related) error occurs.
 */
ssize_t tc_pwrite(int fd, const uint8_t *buf, size_t len);

/*
 * tc_preadwrite:
 *     read all data avalaible from a file descriptor, putting it on the
 *     other one.
 * Parameters:
 *      in: read data from this file descriptor
 *     out: write readed data on this file descriptor
 * Return Value:
 *     -1 if a read error happens
 *     0  if no error happens
 * Postconditions:
 *     move the entire content of 'in' into 'out', if no *critical*
 *     (tipically I/O related) error occurs.
 */
int tc_preadwrite(int in, int out);

enum {
    TC_PROBE_PATH_INVALID = 0,
    TC_PROBE_PATH_ABSPATH,
    TC_PROBE_PATH_RELDIR,
    TC_PROBE_PATH_FILE,
    TC_PROBE_PATH_BKTR,
    TC_PROBE_PATH_SUNAU,
    TC_PROBE_PATH_V4L_VIDEO,
    TC_PROBE_PATH_V4L_AUDIO,
    TC_PROBE_PATH_OSS,
    /* add more elements here */
};

/*
 * tc_probe_path:
 *     verify the type of a given path.
 *
 * Parameters:
 *     path: the path to probe.
 * Return Value:
 *     the probed type of path. Can be TC_PROBE_PATH_INVALID if given path
 *     doesn't exists or an internal error occur.
 * Side effects:
 *     if function fails, one or more debug message can be issued using
 *     tc_log*(). A name resolve request can be issued to system.
 */
int tc_probe_path(const char *name);

/*************************************************************************/

/*
 * XXX: add some general notes about quantization matrices stored
 * into files (format etc. etc.)
 *
 * tc_*_matrix GOTCHA:
 * Why _two_ allowed elements wideness? Why this mess?
 * The problem is that XviD and libavcodec wants elements for
 * quantization matrix in two different wideness. Obviously
 * we DON'T want to patch such sources, so we must handle in
 * some way this difference.
 * Of course we are looking for cleaner solutions.
 * -- fromani 20060305
 */

/*
 * Total size (=number of elements) of quantization matrix
 * for following two support functions
 */
#define TC_MATRIX_SIZE     (64)

/*
 * tc_read_matrix:
 *     read a quantization matrix from given file.
 *     Can read 8-bit wide or 16-bit wide matrix elements.
 *     Store readed matrix in a caller-provided buffer.
 *
 *     Caller can select the elements wideness just
 *     providing a not-NULL buffer for corresponding buffer.
 *     For example, if caller wants to read a quantization matrix
 *     from 'matrix.txt', and want 16-bit wide elements, it
 *     will call
 *
 *     uint16_t matrix[TC_MATRIX_SIZE];
 *     tc_read_matrix('matrix.txt', NULL, matrix);
 *
 * Parameters:
 *     filename: read quantization matrix from this file.
 *           m8: buffer for 8-bit wide elements quantization matrix
 *          m16: buffer for 16-bit wide elements quantization matrix
 *
 *     NOTE: if m8 AND m16 BOTH refers to valid buffers, 8-bit
 *     wideness is preferred.
 * Return value:
 *     -1 filename not found, or neither buffers is valid.
 *     +1 read error: matrix incomplete or badly formatted.
 *     0  no errors.
 * Side effects:
 *     a file on disk is open, readed, closed.
 * Preconditions:
 *     buffer provided by caller MUST be large enough to hold
 *     TC_MATRIX_SIZE elements of requested wideness.
 *     At least one given buffer is valid.
 */
int tc_read_matrix(const char *filename, uint8_t *m8, uint16_t *m16);

/*
 * tc_print_matrix:
 *     print (using tc_log*) a quantization matrix.
 *     Can print 8-bit wide or 16-bit wide matrix elements.
 *
 *     Caller must provide a valid pointer correspoinding to
 *     wideness of elements of matrix to be printed.
 *     Example: quantization matrix has 8-bit wide elements:
 *
 *     uint8_t matrix[TC_MATRIX_SIZE];
 *     // already filled with something useful
 *     tc_print_matrix(matrix, NULL);
 *
 * Parameters:
 *     m8: pointer to 8-bit wide elements quantization matrix.
 *     m16: pointer to 16-bit wide elements quantization matrix.
 *
 *     NOTE: if m8 AND m16 BOTH refers to valid buffers, 8-bit
 *     wideness is preferred.
 * Preconditions:
 *     At least one given pointer is valid.
 */
void tc_print_matrix(uint8_t *m8, uint16_t *m16);

#ifdef __cplusplus
}
#endif

#endif  /* IOUTILS_H */
