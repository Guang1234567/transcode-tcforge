/*
 * test-bufalloc.c -- testsuite for tc_*bufalloc* family (tc_functions.c)
 *                    everyone feel free to add more tests and improve
 *                    existing ones.
 * (C) 2006-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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


#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "libtc/libtc.h"

#ifndef PACKAGE
#define PACKAGE __FILE__
#endif

#define MY_PAGE_SZ  (4096)
#define HOW_MUCH    (144000)
#define BIG_SIZE    (64*1024*1024)

static int test_alloc(int size)
{
    int ret = 0;
    uint8_t *mem = tc_bufalloc(size);

    if (mem == NULL) {
        tc_error("test_alloc(%i): FAILED (mem == NULL)", size);
        ret = 1;
    } else {
        tc_info("test_alloc(%i): PASSED", size);
        ret = 1;
    }
    tc_buffree(mem);
    return ret;
}

static int test_alloc_memset(int size)
{
    int ret = 0;
    uint8_t *mem = tc_bufalloc(size);

    if (mem == NULL) {
        tc_error("test_alloc_memset(%i): FAILED (mem == NULL)", size);
        ret = 1;
    } else {
        memset(mem, 0, size);
        tc_info("test_alloc_memset(%i): PASSED", size);
        ret = 1;
    }
    tc_buffree(mem);
    return ret;
}

int main(int argc, char *argv[])
{
    libtc_init(&argc, &argv);

    test_alloc(0);
    test_alloc(1);
    test_alloc(MY_PAGE_SZ);
    test_alloc(MY_PAGE_SZ-1);
    test_alloc(MY_PAGE_SZ+1);
    test_alloc(HOW_MUCH);
    test_alloc(HOW_MUCH-1);
    test_alloc(HOW_MUCH+1);
    test_alloc(BIG_SIZE);
    test_alloc(BIG_SIZE-1);
    test_alloc(BIG_SIZE+1);
    
    test_alloc_memset(0);
    test_alloc_memset(1);
    test_alloc_memset(MY_PAGE_SZ);
    test_alloc_memset(MY_PAGE_SZ-1);
    test_alloc_memset(MY_PAGE_SZ+1);
    test_alloc_memset(HOW_MUCH);
    test_alloc_memset(HOW_MUCH-1);
    test_alloc_memset(HOW_MUCH+1);
    test_alloc_memset(BIG_SIZE);
    test_alloc_memset(BIG_SIZE-1);
    test_alloc_memset(BIG_SIZE+1);

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
