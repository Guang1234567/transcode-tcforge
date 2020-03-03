/*
 * test-framecode.c -- test tclib framecode handling
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

/* We define our own dummy free() for testing, so avoid string.h declaring
 * it over us */
#define free string_h_free
#include <string.h>
#undef free

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

#include "libtc/libtc.h"
#include "libtc/framecode.h"

/* Global verbosity level (0: silent, 1: test list, 2: debug info) */
static int verbose = 1;

/*************************************************************************/

/* Fake free() function, to enable testing of free_fc_time(). */

#define FREED_POINTER ((struct fc_time *)1)

void free(void *fct);  /* prototype, to avoid warnings */
void free(void *fct)
{
    ((struct fc_time *)fct)->next = FREED_POINTER;
}

/*************************************************************************/

/* Helper routines for test_new_fc_time_from_string() to extract an integer
 * or a framecode from a string and advance the string pointer.  Both
 * routines return nonzero on success, zero on failure. */

static int get_uint(char **sptr, unsigned int *valptr)
{
    char *s = *sptr;
    unsigned long lval;

    if (*s < '0' || *s > '9')
        return 0;
    errno = 0;
    lval = strtoul(s, sptr, 10);
    if (errno == ERANGE
#if ULONG_MAX > UINT_MAX
     || lval > UINT_MAX
#endif
    ) {
        return 0;
    }
    *valptr = (unsigned int)lval;
    return 1;
}

static int get_fc(char **sptr, unsigned int *frameptr, double fps)
{
    char *s = *sptr;
    unsigned int frame, temp;
    int is_time = 0;

    if (!get_uint(&s, &frame))
        return 0;
    if (*s == ':') {
        is_time = 1;
        s++;
        if (!get_uint(&s, &temp))
            return 0;
        frame = frame*60 + temp;
        if (*s == ':') {
            s++;
            if (!get_uint(&s, &temp))
                return 0;
            frame = frame*60 + temp;
        }
    }
    if (*s == '.' || is_time) {
        frame = (unsigned int)floor(frame * fps);
    }
    if (*s == '.') {
        s++;
        if (!get_uint(&s, &temp))
            return 0;
        frame += temp;
    }
    *sptr = s;
    *frameptr = frame;
    return 1;
}

/*************************************************************************/
/*************************************************************************/

/* Check that new_fc_time properly clears the fields of the allocated
 * fc_time structure. */

static int test_new_fc_time(void)
{
    struct fc_time *fct;

    fct = new_fc_time();
    return fct->next  == NULL
        && fct->fps   == 0.0
        && fct->stepf == 0
        && fct->vob_offset == 0
        && fct->sh    == 0
        && fct->sm    == 0
        && fct->ss    == 0
        && fct->sf    == 0
        && fct->stf   == 0
        && fct->eh    == 0
        && fct->em    == 0
        && fct->es    == 0
        && fct->ef    == 0
        && fct->etf   == 0;
}

/*************************************************************************/

/* Check that free_fc_time() properly frees all elements in a list. */

static int test_free_fc_time(void)
{
    struct fc_time fct1, fct2, fct3;

    fct1.next = &fct2;
    fct2.next = &fct3;
    fct3.next = NULL;
    free_fc_time(&fct1);
    return fct1.next == FREED_POINTER
        && fct2.next == FREED_POINTER
        && fct3.next == FREED_POINTER;
}

/*************************************************************************/

/* Check that set_fc_time with the given parameters sets the fields of the
 * fc_time structure properly. */

static int test_set_fc_time(int start, int end, double fps)
{
    struct fc_time fct;

    fct.next = NULL;
    fct.fps = fps;
    fct.stepf = 0;
    fct.vob_offset = 0;
    fct.sh = fct.sm = fct.ss = fct.sf = fct.stf = ~0;
    fct.eh = fct.em = fct.es = fct.ef = fct.etf = ~0;
    set_fc_time(&fct, start, end);
    if (verbose >= 2) {
        printf("[%d->%u:%u:%u.%u|%u - %d->%u:%u:%u.%u|%u @ %.1f] ",
               start, fct.sh, fct.sm, fct.ss, fct.sf, fct.stf,
               end,   fct.eh, fct.em, fct.es, fct.ef, fct.etf,
               fps);
    }
    return fct.sh  == (int)floor(start/fps) / 3600
        && fct.sm  == (int)floor(start/fps) / 60 % 60
        && fct.ss  == (int)floor(start/fps) % 60
        && fct.sf  == floor(start - ((int)floor(start/fps))*fps)
        && fct.stf == start
        && fct.eh  == (int)floor(end/fps) / 3600
        && fct.em  == (int)floor(end/fps) / 60 % 60
        && fct.es  == (int)floor(end/fps) % 60
        && fct.ef  == floor(end - ((int)floor(end/fps))*fps)
        && fct.etf == end;
}

/*************************************************************************/

/* Check that fc_time_contains properly determines whether a given frame
 * number is contained in a list of up to three fc_time structures.  When
 * testing with less than three fc_time structures, use -1 for the start
 * and end values as follows:
 *     Two structures -> test(frame, fps, start1, end1, start2, end2, -1, -1)
 *     One structure  -> test(frame, fps, start1, end1, -1, -1, -1, -1)
 * Assumes that set_fc_time() works correctly. */

static int test_fc_time_contains(int frame, double fps,
                                 int start1, int end1,
                                 int start2, int end2,
                                 int start3, int end3)
{
    struct fc_time fct1, fct2, fct3;
    int expected;  /* Do we expect it to be found or not? */
    int result;    /* What we actually got out of the function */

    fct1.next = NULL;
    fct1.fps = fps;
    fct1.stepf = 0;
    fct1.vob_offset = 0;
    fct2.next = NULL;
    fct2.fps = fps;
    fct2.stepf = 0;
    fct2.vob_offset = 0;
    fct3.next = NULL;
    fct3.fps = fps;
    fct3.stepf = 0;
    fct3.vob_offset = 0;

    set_fc_time(&fct1, start1, end1);
    expected = (frame >= start1 && frame < end1);
    if (start2 >= 0 && end2 >= 0) {
        fct1.next = &fct2;
        set_fc_time(&fct2, start2, end2);
        expected |= (frame >= start2 && frame < end2);
        if (start3 >= 0 && end3 >= 0) {
            fct2.next = &fct3;
            set_fc_time(&fct3, start3, end3);
            expected |= (frame >= start3 && frame < end3);
        }
    }

    result = fc_time_contains(&fct1, frame);
    return (expected && result) || (!expected && !result);
}

/*************************************************************************/

/* Check that new_fc_time_from_string() properly parses the given string.
 * Assumes that new_fc_time() and set_fc_time() work correctly. */

static int test_new_fc_time_from_string(const char *string,
                                        const char *separator,
                                        double fps)
{
    struct fc_time *fctret, *fctexpect, *tail, *fct;
    char strsave[1000], *s;

    if (strlen(string) > sizeof(strsave)-1) {
        fprintf(stderr, "*** test_new_fc_time_from_string(): string too long"
                " (max %u chars)\n", (unsigned int)sizeof(strsave)-1);
        return 0;
    }

    /* Call the function itself */
    fctret = new_fc_time_from_string(string, separator, fps,
                                     verbose>=2 ? 1 : -1);

    /* Figure out what we're supposed to get; if we're supposed to get an
     * error, return success or failure at that point */
    fctexpect = tail = NULL;
    snprintf(strsave, sizeof(strsave), "%s", string);
    for (s = strtok(strsave,separator); s; s = strtok(NULL,separator)) {
        unsigned int start = 0, end = 0, stepf = 1;
        if (!get_fc(&s, &start, fps)
         || *s++ != '-'
         || !get_fc(&s, &end, fps)
        ) {
            return fctret == NULL;
        }
        if (*s == '/') {
            s++;
            if (!get_uint(&s, &stepf))
                return fctret == NULL;
        }
        if (*s)
            return fctret == NULL;
        fct = new_fc_time();
        if (!fct) {
            fprintf(stderr, "*** Out of memory\n");
            exit(-1);
        }
        fct->fps = fps;
        fct->stepf = stepf;
        set_fc_time(fct, start, end);
        if (!fctexpect) {
            fctexpect = fct;
        } else {
            tail->next = fct;
        }
        tail = fct;
    }

    /* Compare the returned list against the expected one */
    for (fct = fctexpect; fct; fct = fct->next) {
        if (verbose >= 2) {
            printf("\n[[%u:%u:%u.%u|%u - %u:%u:%u.%u|%u / %u @ %.1f]]"
                   "\n<<%u:%u:%u.%u|%u - %u:%u:%u.%u|%u / %u @ (%d)>>\n",
                   fct->sh, fct->sm, fct->ss, fct->sf, fct->stf,
                   fct->eh, fct->em, fct->es, fct->ef, fct->etf,
                   fct->stepf, fct->fps,
                   fctret->sh, fctret->sm, fctret->ss, fctret->sf, fctret->stf,
                   fctret->eh, fctret->em, fctret->es, fctret->ef, fctret->etf,
                   fctret->stepf, (fctret->fps == fct->fps));
        }
        if (!fctret
         || fctret->fps   != fct->fps
         || fctret->stepf != fct->stepf
         || fctret->sh    != fct->sh
         || fctret->sm    != fct->sm
         || fctret->ss    != fct->ss
         || fctret->sf    != fct->sf
         || fctret->stf   != fct->stf
         || fctret->eh    != fct->eh
         || fctret->em    != fct->em
         || fctret->es    != fct->es
         || fctret->ef    != fct->ef
         || fctret->etf   != fct->etf
        ) {
            return 0;
        }
        fctret = fctret->next;
    }

    /* Everything succeeded */
    return 1;
}

/*************************************************************************/
/*************************************************************************/

/* Helper macro to print a test name and result status, and set the
 * `failed' variable to nonzero if the test failed.  Usage:
 *     DO_TEST("name", test_function(param1,param2));
 * The test function is assumed to return a true value for success, a
 * false value for failure  (the return type does not matter).
 */
#define DO_TEST(name,test) do {   \
    if (verbose > 0) {            \
        printf("%s... ", (name)); \
        fflush(stdout);           \
    }                             \
    if (test) {                   \
        if (verbose > 0)          \
            printf("ok\n");       \
    } else {                      \
        if (verbose > 0)          \
            printf("FAILED\n");   \
        failed = 1;               \
    }                             \
} while (0)

/* Shorthand for test_new_fc_time_from_string() */
#define DO_TEST_FC_STRING(str) \
    DO_TEST("new_fc_time_from_string(" str ")", \
            test_new_fc_time_from_string(str, ",", 10.0))

/*************************************************************************/

/* Main program. */

int main(int argc, char *argv[])
{
    int failed = 0;
    int opt;

    libtc_init(&argc, &argv);
 
    /* Option processing */
    while ((opt = getopt(argc, argv, "hqv")) != EOF) {
        if (opt == 'q') {
            verbose = 0;
        } else if (opt == 'v') {
            verbose = 2;
        } else {
            fprintf(stderr,
                    "Usage: %s [-q | -v]\n"
                    "-q: quiet (don't print list of tests)\n"
                    "-v: verbose (print debugging info)\n", argv[0]);
            return 1;
        }
    }

    /* Check that new_fc_time properly clears fields. */
    DO_TEST("new_fc_time", test_new_fc_time());

    /* Check that free_fc_time properly frees a multi-struct list. */
    DO_TEST("free_fc_time", test_free_fc_time());

    /* Check set_fc_time() using various frame ranges and fps.
     * First check simple frame counts within the first second; then
     * move on to values that require splitting between H/M/S/F; and
     * finally check that rounding of fractional frames (downward) is
     * performed correctly. */
    DO_TEST("set_fc_time(0-1/10)", test_set_fc_time(0, 1, 10));
    DO_TEST("set_fc_time(1-2/10)", test_set_fc_time(1, 2, 10));
    DO_TEST("set_fc_time(0-10/10)", test_set_fc_time(0, 10, 10));
    DO_TEST("set_fc_time(10-20/10)", test_set_fc_time(10, 20, 10));
    DO_TEST("set_fc_time(0-600/10)", test_set_fc_time(0, 600, 10));
    DO_TEST("set_fc_time(600-1200/10)", test_set_fc_time(600, 1200, 10));
    DO_TEST("set_fc_time(0-36000/10)", test_set_fc_time(0, 36000, 10));
    DO_TEST("set_fc_time(36000-72000/10)", test_set_fc_time(36000,72000,10));
    DO_TEST("set_fc_time(0-37234/10)", test_set_fc_time(0, 37234, 10));
    DO_TEST("set_fc_time(37234-74468/10)", test_set_fc_time(37234,74468,10));
    DO_TEST("set_fc_time(0-10/8.8)", test_set_fc_time(0, 10, 8.8));
    DO_TEST("set_fc_time(10-20/8.8)", test_set_fc_time(10, 20, 8.8));
    DO_TEST("set_fc_time(0-10/8.2)", test_set_fc_time(0, 10, 8.2));
    DO_TEST("set_fc_time(10-20/8.2)", test_set_fc_time(10, 20, 8.2));

    /* Everything from here on down depends on set_fc_time() (and on
     * new_fc_time() in the case of new_fc_time_from_string()), so abort
     * now if we've failed somewhere. */
    if (failed) {
        fprintf(stderr, "*** Aborting due to test failures.\n");
        return 1;
    }

    /* Test various cases with fc_time_contains():
     *     A: 1 less than the starting frame in a large range
     *     B: Equal to the starting frame in a large range
     *     C: Midway between the starting and ending frames in a large range
     *     D: 1 less than the ending frame in a large range
     *     E: Equal to the ending frame in a large range
     *     F: 1 less than the only frame in a 1-frame range
     *     G: Equal to the only frame in a 1-frame range
     *     H: 1 more than the only frame in a 1-frame range
     * for various types of lists:
     *     1: Only one fc_time in the list (first structure)
     *     2: The second of a list of 2 fc_times (last structure)
     *     3: The second of a list of 3 fc_times (middle structure)
     */
    DO_TEST("fc_time_contains(1A)",
            test_fc_time_contains( 9, 10.0, 10, 20, -1, -1, -1, -1));
    DO_TEST("fc_time_contains(1B)",
            test_fc_time_contains(10, 10.0, 10, 20, -1, -1, -1, -1));
    DO_TEST("fc_time_contains(1C)",
            test_fc_time_contains(15, 10.0, 10, 20, -1, -1, -1, -1));
    DO_TEST("fc_time_contains(1D)",
            test_fc_time_contains(19, 10.0, 10, 20, -1, -1, -1, -1));
    DO_TEST("fc_time_contains(1E)",
            test_fc_time_contains(20, 10.0, 10, 20, -1, -1, -1, -1));
    DO_TEST("fc_time_contains(1F)",
            test_fc_time_contains( 9, 10.0, 10, 11, -1, -1, -1, -1));
    DO_TEST("fc_time_contains(1G)",
            test_fc_time_contains(10, 10.0, 10, 11, -1, -1, -1, -1));
    DO_TEST("fc_time_contains(1H)",
            test_fc_time_contains(11, 10.0, 10, 11, -1, -1, -1, -1));
    DO_TEST("fc_time_contains(2A)",
            test_fc_time_contains( 9, 10.0,  1,  2, 10, 20, -1, -1));
    DO_TEST("fc_time_contains(2B)",
            test_fc_time_contains(10, 10.0,  1,  2, 10, 20, -1, -1));
    DO_TEST("fc_time_contains(2C)",
            test_fc_time_contains(15, 10.0,  1,  2, 10, 20, -1, -1));
    DO_TEST("fc_time_contains(2D)",
            test_fc_time_contains(19, 10.0,  1,  2, 10, 20, -1, -1));
    DO_TEST("fc_time_contains(2E)",
            test_fc_time_contains(20, 10.0,  1,  2, 10, 20, -1, -1));
    DO_TEST("fc_time_contains(2F)",
            test_fc_time_contains( 9, 10.0,  1,  2, 10, 11, -1, -1));
    DO_TEST("fc_time_contains(2G)",
            test_fc_time_contains(10, 10.0,  1,  2, 10, 11, -1, -1));
    DO_TEST("fc_time_contains(2H)",
            test_fc_time_contains(11, 10.0,  1,  2, 10, 11, -1, -1));
    DO_TEST("fc_time_contains(3A)",
            test_fc_time_contains( 9, 10.0,  1,  2, 10, 20, 30, 40));
    DO_TEST("fc_time_contains(3B)",
            test_fc_time_contains(10, 10.0,  1,  2, 10, 20, 30, 40));
    DO_TEST("fc_time_contains(3C)",
            test_fc_time_contains(15, 10.0,  1,  2, 10, 20, 30, 40));
    DO_TEST("fc_time_contains(3D)",
            test_fc_time_contains(19, 10.0,  1,  2, 10, 20, 30, 40));
    DO_TEST("fc_time_contains(3E)",
            test_fc_time_contains(20, 10.0,  1,  2, 10, 20, 30, 40));
    DO_TEST("fc_time_contains(3F)",
            test_fc_time_contains( 9, 10.0,  1,  2, 10, 11, 30, 40));
    DO_TEST("fc_time_contains(3G)",
            test_fc_time_contains(10, 10.0,  1,  2, 10, 11, 30, 40));
    DO_TEST("fc_time_contains(3H)",
            test_fc_time_contains(11, 10.0,  1,  2, 10, 11, 30, 40));

    /* See whether new_fc_time_from_string() works with a simple string */
    DO_TEST_FC_STRING("10-20");
    DO_TEST_FC_STRING("10-20/3");
    /* Try some invalid variations */
    DO_TEST_FC_STRING("10-");
    DO_TEST_FC_STRING("-20");
    DO_TEST_FC_STRING("10-20/");
    DO_TEST_FC_STRING("a-20");
    DO_TEST_FC_STRING("10-b");
    DO_TEST_FC_STRING("10a-20");
    DO_TEST_FC_STRING("10-20b");
    DO_TEST_FC_STRING("10-20/c");
    DO_TEST_FC_STRING("10-20/30c");
    /* Try with multiple entries */
    DO_TEST_FC_STRING("10-20,30-40");
    DO_TEST_FC_STRING(",10-20,,30-40,");  // extra commas should be ignored
    DO_TEST_FC_STRING("10-20,30-40/5,60-70");
    DO_TEST_FC_STRING("10-20,30-40b,50-60");
    /* Try timecodes instead of frames */
    DO_TEST_FC_STRING("1.0-20");
    DO_TEST_FC_STRING("10-2.0");
    DO_TEST_FC_STRING("1:1-2000");
    DO_TEST_FC_STRING("1-2:2");
    DO_TEST_FC_STRING("1:08-2000");  // make sure it doesn't try to parse octal
    DO_TEST_FC_STRING("10-2:08");
    DO_TEST_FC_STRING("1:1:1-200000");
    DO_TEST_FC_STRING("10-2:2:2");
    DO_TEST_FC_STRING("1:1:1.1-200000");
    DO_TEST_FC_STRING("10-2:2:2.2");
    DO_TEST_FC_STRING("1:1:1.1-200000/3");
    DO_TEST_FC_STRING("10-2:2:2.2/3");
    /* Test invalid timecodes as well */
    DO_TEST_FC_STRING("1:1:1:1-200000");
    DO_TEST_FC_STRING("10-2:2:2:2");
    DO_TEST_FC_STRING("1.1.1-200000");
    DO_TEST_FC_STRING("10-2.2.2");
    DO_TEST_FC_STRING("1:1:1.1.1-200000");
    DO_TEST_FC_STRING("10-2:2:2.2.2");

    /* All done, exit with appropriate status */
    return failed ? 1 : 0;
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
