/*
 * static_xio.h - static linkage helper for (lib)xio.
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef STATIC_XIO_H
#define STATIC_XIO_H

#include <fcntl.h>
#include <unistd.h>

#include "libtcutil/xio.h"

void dummy_xio(void);
void dummy_xio(void)
{
    int i;
    struct stat tmp;

    i = xio_open("", O_RDONLY);
    i = xio_read(i, NULL, 0);
    i = xio_write(i, NULL, 0);
    i = xio_ftruncate(i, 0);
    i = xio_lseek(i, 0, 0);
    i = xio_fstat(i, &tmp);
    i = xio_lstat("", &tmp);
    i = xio_stat("", &tmp);
    i = xio_rename("", "");
    i = xio_close(i);
}

#endif /* STATIC_XIO_H */
