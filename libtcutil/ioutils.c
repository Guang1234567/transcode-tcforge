/*
 *  ioutils.c - various I/O helper functions for transcode (implementation)
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
#include "ioutils.h"
#include "memutils.h"
#include "strutils.h"
#include "xio.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>


int tc_test_program(const char *name)
{
#ifndef NON_POSIX_PATH
    const char *path = getenv("PATH");
    char *tok_path = NULL;
    char *compl_path = NULL;
    char *tmp_path;
    char **strtokbuf;
    char done;
    size_t pathlen;
    long sret;
    int error = 0;

    if (name == NULL) {
        tc_warn("ERROR: Searching for a NULL program!\n");
        return ENOENT;
    }

    if (path == NULL) {
        tc_warn("The '%s' program could not be found. \n", name);
        tc_warn("Because your PATH environment variable is not set.\n");
        return ENOENT;
    }

    pathlen = strlen(path) + 1;
    tmp_path = tc_malloc(pathlen);
    strtokbuf = tc_malloc(pathlen);

    sret = strlcpy(tmp_path, path, pathlen);
    tc_test_string(__FILE__, __LINE__, pathlen, sret, errno);

    /* iterate through PATH tokens */
    for (done = 0, tok_path = strtok_r(tmp_path, ":", strtokbuf);
            !done && tok_path;
            tok_path = strtok_r((char *)0, ":", strtokbuf)) {
        pathlen = strlen(tok_path) + strlen(name) + 2;
        compl_path = tc_malloc(pathlen * sizeof(char));
        sret = tc_snprintf(compl_path, pathlen, "%s/%s", tok_path, name);

        if (access(compl_path, X_OK) == 0) {
            error   = 0;
            done    = 1;
        } else { /* access != 0 */
            if (errno != ENOENT) {
                done    = 1;
                error   = errno;
            }
        }

        tc_free(compl_path);
    }

    tc_free(tmp_path);
    tc_free(strtokbuf);

    if (!done) {
        tc_warn("The '%s' program could not be found. \n", name);
        tc_warn("Please check your installation.\n");
        return ENOENT;
    }

    if (error != 0) {
        /* access returned an unhandled error */
        tc_warn("The '%s' program was found, but is not accessible.\n", name);
        tc_warn("%s\n", strerror(errno));
        tc_warn("Please check your installation.\n");
        return error;
    }
#endif

    return 0;
}


/*************************************************************************/

ssize_t tc_pread(int fd, uint8_t *buf, size_t len)
{
    ssize_t n = 0;
    ssize_t r = 0;

    while (r < len) {
        n = xio_read(fd, buf + r, len - r);

        if (n == 0) {  /* EOF */
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        r += n;
    }
    return r;
}


ssize_t tc_pwrite(int fd, const uint8_t *buf, size_t len)
{
    ssize_t n = 0;
    ssize_t r = 0;

    while (r < len) {
        n = xio_write(fd, buf + r, len - r);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        r += n;
    }
    return r;
}

#ifdef PIPE_BUF
# define BLOCKSIZE PIPE_BUF /* 4096 on linux-x86 */
#else
# define BLOCKSIZE 4096
#endif

int tc_preadwrite(int fd_in, int fd_out)
{
    uint8_t buffer[BLOCKSIZE];
    ssize_t bytes;
    int error = 0;

    do {
        bytes = tc_pread(fd_in, buffer, BLOCKSIZE);

        /* error on read? */
        if (bytes < 0) {
            return -1;
        }

        /* read stream end? */
        if (bytes != BLOCKSIZE) {
            error = 1;
        }

        if (bytes) {
            /* write stream problems? */
            if (tc_pwrite(fd_out, buffer, bytes) != bytes) {
                error = 1;
            }
        }
    } while (!error);

    return 0;
}

int tc_file_check(const char *name)
{
    struct stat fbuf;

    if(xio_stat(name, &fbuf)) {
        tc_log_warn(__FILE__, "invalid file \"%s\"", name);
        return -1;
    }

    /* file or directory? */
    if(S_ISDIR(fbuf.st_mode)) {
        return 1;
    }
    return 0;
}

#ifndef major
# define major(dev)  (((dev) >> 8) & 0xff)
#endif

int tc_probe_path(const char *name)
{
    struct stat fbuf;

    if(name == NULL) {
        tc_log_warn(__FILE__, "invalid file \"%s\"", name);
        return TC_PROBE_PATH_INVALID;
    }

    if(xio_stat(name, &fbuf) == 0) {
        /* inode exists */

        /* treat DVD device as absolute directory path */
        if (S_ISBLK(fbuf.st_mode)) {
            return TC_PROBE_PATH_ABSPATH;
        }

        /* char device could be several things, depending on system */
        /* *BSD DVD device? v4l? bktr? sunau? */
        if(S_ISCHR(fbuf.st_mode)) {
            switch (major(fbuf.st_rdev)) {
#ifdef OS_BSD
# ifdef __OpenBSD__
                case 15: /* rcd */
                    return TC_PROBE_PATH_ABSPATH;
                case 42: /* sunau */
                    return TC_PROBE_PATH_SUNAU;
                case 49: /* bktr */
                    return TC_PROBE_PATH_BKTR;
# endif
# ifdef __FreeBSD__
                case 4: /* acd */
                    return TC_PROBE_PATH_ABSPATH;
                case 229: /* bktr */
                    return TC_PROBE_PATH_BKTR;
                case 0: /* OSS */
                    return TC_PROBE_PATH_OSS;
# endif
                default: /* libdvdread uses "raw" disk devices here */
                    return TC_PROBE_PATH_ABSPATH;
#else
                case 81: /* v4l (Linux) */
                    return TC_PROBE_PATH_V4L_VIDEO;
                case 14: /* OSS */
                    return TC_PROBE_PATH_OSS;
                default:
                    break;
#endif
            }
        }

        /* file or directory? */
        if (!S_ISDIR(fbuf.st_mode)) {
            return TC_PROBE_PATH_FILE;
        }

        /* directory, check for absolute path */
        if(name[0] == '/') {
            return TC_PROBE_PATH_ABSPATH;
        }

        /* directory mode */
        return TC_PROBE_PATH_RELDIR;
    } else {
        tc_log_warn(__FILE__, "invalid filename \"%s\"", name);
        return TC_PROBE_PATH_INVALID;
    }

    return TC_PROBE_PATH_INVALID;
}

/*************************************************************************/

/* 
 * clamp an unsigned value so it can be safely (without any loss) in
 * an another unsigned integer of <butsize> bits.
 */
static int32_t clamp(int32_t value, uint8_t bitsize)
{
    value = (value < 1) ?1 :value;
    value = (value > (1 << bitsize)) ?(1 << bitsize) :value;
    return value;
}

int tc_read_matrix(const char *filename, uint8_t *m8, uint16_t *m16)
{
    int i = 0;
    FILE *input = NULL;

    /* Open the matrix file */
    input = fopen(filename, "rb");
    if (!input) {
        tc_log_warn("read_matrix",
            "Error opening the matrix file %s",
            filename);
        return -1;
    }
    if (!m8 && !m16) {
        tc_log_warn("read_matrix", "bad matrix reference");
        return -1;
    }

    /* Read the matrix */
    for(i = 0; i < TC_MATRIX_SIZE; i++) {
        int value;

        /* If fscanf fails then get out of the loop */
        if(fscanf(input, "%d", &value) != 1) {
            tc_log_warn("read_matrix",
                "Error reading the matrix file %s",
                filename);
            fclose(input);
            return 1;
        }

        if (m8 != NULL) {
            m8[i] = clamp(value, 8);
        } else {
            m16[i] = clamp(value, 16);
        }
    }

    /* We're done */
    fclose(input);

    return 0;
}

void tc_print_matrix(uint8_t *m8, uint16_t *m16)
{
    int i;

    if (!m8 && !m16) {
        tc_log_warn("print_matrix", "bad matrix reference");
        return;
    }
   
    // XXX: magic number
    for(i = 0; i < TC_MATRIX_SIZE; i += 8) {
        if (m8 != NULL) {
            tc_log_info("print_matrix",
                        "%3d %3d %3d %3d "
                        "%3d %3d %3d %3d",
                        (int)m8[i  ], (int)m8[i+1],
                        (int)m8[i+2], (int)m8[i+3],
                        (int)m8[i+4], (int)m8[i+5],
                        (int)m8[i+6], (int)m8[i+7]);
        } else {
            tc_log_info("print_matrix",
                        "%3d %3d %3d %3d "
                        "%3d %3d %3d %3d",
                        (int)m16[i  ], (int)m16[i+1],
                        (int)m16[i+2], (int)m16[i+3],
                        (int)m16[i+4], (int)m16[i+5],
                        (int)m16[i+6], (int)m16[i+7]);
        }
    }
    return;
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

