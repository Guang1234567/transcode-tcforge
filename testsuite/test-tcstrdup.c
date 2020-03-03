/*
 * test-tcstrdup.c -- testsuite for tc_*strdup* family (tc_functions.c);
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


#include "config.h"

#define _GNU_SOURCE 1

#include "libtc/libtc.h"
#include <stdlib.h>
#include <stdio.h>

// test case 1

#define TEST_STRING "testing tc_str*dup()"

static int test_strdup(void)
{
    const char *s1 = TEST_STRING;
    char *s2 = NULL, *s3 = NULL;

    tc_info("test_strdup() begin");

    s2 = strdup(s1);
    s3 = tc_strdup(s1);

    if (strlen(s1) != strlen(s2)) {
        tc_error("string length mismatch: '%s' '%s'", s1, s2);
    }
    if (strlen(s1) != strlen(s3)) {
        tc_error("string length mismatch: '%s' '%s'", s1, s3);
    }
    if (strlen(s2) != strlen(s3)) {
        tc_error("string length mismatch: '%s' '%s'", s2, s3);
    }

    if (strcmp(s1, s2) != 0) {
        tc_error("string mismatch: '%s' '%s'", s1, s2);
    }
    if (strcmp(s1, s3) != 0) {
        tc_error("string mismatch: '%s' '%s'", s1, s3);
    }
    if (strcmp(s2, s3) != 0) {
        tc_error("string mismatch: '%s' '%s'", s2, s3);
    }

    free(s2);
    tc_free(s3);

    tc_info("test_strdup() end");
    return 0;
}

static int test_strndup(size_t n)
{
    const char *s1 = TEST_STRING;
    char *s2 = NULL, *s3 = NULL;

    tc_info("test_strndup(%lu) begin", (unsigned long)n);

    s2 = malloc(n+1);
    if (n > 0) {
        strncpy(s2, s1, n);
    }
    s2[n] = 0;
    s3 = tc_strndup(s1, n);

    if (strlen(s2) != strlen(s3)) {
        tc_error("string length mismatch: '%s' '%s'", s2, s3);
    }

    if (strcmp(s2, s3) != 0) {
        tc_error("string mismatch: '%s' '%s'", s2, s3);
    }

    free(s2);
    tc_free(s3);

    tc_info("test_strndup() end");
    return 0;
}

int main(int argc, char *argv[])
{
    libtc_init(&argc, &argv);

    test_strdup();

    test_strndup(0);
    test_strndup(1);
    test_strndup(5);

    test_strndup(strlen(TEST_STRING)-2);
    test_strndup(strlen(TEST_STRING)-1);

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
