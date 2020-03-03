/*compile-command
set -x
gcc -O3 -g -I. -I.. "$0" -DARCH_X86
exit $?
*/

/*
 * test-acmemcpy.c - test accelerated memcpy() implementations to check
 *                   that they work with all alignments and sizes
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#define _GNU_SOURCE  /* for strsignal */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "config.h"

#define ac_memcpy local_ac_memcpy  /* to avoid clash with libac.a */
#define ac_memcpy_init local_ac_memcpy_init  /* to avoid clash with libac.a */
#include "aclib/ac.h"

/* Include memcpy.c directly to get access to the particular implementations */
#include "../aclib/memcpy.c"
#undef ac_memcpy
/* Make sure all names are available, to simplify function table */
#if !defined(ARCH_X86) || !defined(HAVE_ASM_MMX)
# define memcpy_mmx memcpy
#endif
#if !defined(ARCH_X86) || !defined(HAVE_ASM_SSE)
# define memcpy_sse memcpy
#endif
#if !defined(ARCH_X86_64) || !defined(HAVE_ASM_SSE2)
# define memcpy_amd64 memcpy
#endif

/* Constant `spill' value for tests */
static const int SPILL = 8;

/*************************************************************************/

static void *old_SIGSEGV = NULL, *old_SIGILL = NULL;
static sigjmp_buf env;


static void sighandler(int sig)
{
    printf("*** %s\n", strsignal(sig));
    siglongjmp(env, 1);
}

static void set_signals(void)
{
    old_SIGSEGV = signal(SIGSEGV, sighandler);
    old_SIGILL  = signal(SIGILL , sighandler);
}

static void clear_signals(void)
{
    signal(SIGSEGV, old_SIGSEGV);
    signal(SIGILL , old_SIGILL );
}

/*************************************************************************/

/* Test the given function with the given data size.  Print error
 * information if verbose is nonzero.  Performs tests on all alignments
 * from 0 through block-1, and checks that `spill' bytes on either side of
 * the target region are not affected.  `block' is assumed to be a power
 * of 2. */

static int testit(void *(*func)(void *, const void *, size_t),
                  int size, int block, int spill, int verbose)
{
    char *chunk1, *chunk1_base, *chunk1_copy;
    char *chunk2, *chunk2_base, *chunk2_test;
    int align1, align2, result;

    chunk1_base = malloc(size + block-1);
    chunk1_copy = malloc(size + block-1);
    chunk2_base = malloc(size + block-1 + spill*2);
    chunk2_test = malloc(size + spill*2);
    memset(chunk1_base, 0x11, size + block-1);
    memset(chunk1_copy, 0x11, size + block-1);
    memset(chunk2_test, 0x22, size + spill*2);
    memset(chunk2_test + spill, 0x11, size);

    result = 1;
    for (align1 = 0; align1 < block-1; align1++) {
        for (align2 = 0; align2 < block-1; align2++) {
            chunk1 = chunk1_base + align1;
            chunk2 = chunk2_base + spill + align2;
            memset(chunk2-spill, 0x22, size+spill*2);
            set_signals();
            if (sigsetjmp(env, 1)) {
                result = 0;
            } else {
                (*func)(chunk2, chunk1, size);
                result = (memcmp(chunk2-spill, chunk2_test, size+spill*2) == 0
                       && memcmp(chunk1_base, chunk1_copy, size+block-1)==0);
            }
            clear_signals();
            if (!result) {
                if (verbose) {
                    printf("FAILED: size=%d align1=%d align2=%d\n",
                           size, align1, align2);
                }
                /* Just in case */
                memset(chunk1_base, 0x11, size+block-1);
            }
        }
    }

    free(chunk1_base);
    free(chunk1_copy);
    free(chunk2_base);
    free(chunk2_test);
    return result;
}

/*************************************************************************/

/* Turn presence/absence of #define into a number */
#if defined(ARCH_X86)
# define defined_ARCH_X86 1
#else
# define defined_ARCH_X86 0
#endif
#if defined(ARCH_X86_64)
# define defined_ARCH_X86_64 1
#else
# define defined_ARCH_X86_64 0
#endif
#if defined(HAVE_ASM_MMX)
# define defined_HAVE_ASM_MMX 1
#else
# define defined_HAVE_ASM_MMX 0
#endif
#if defined(HAVE_ASM_SSE)
# define defined_HAVE_ASM_SSE 1
#else
# define defined_HAVE_ASM_SSE 0
#endif
#if defined(HAVE_ASM_SSE2)
# define defined_HAVE_ASM_SSE2 1
#else
# define defined_HAVE_ASM_SSE2 0
#endif

/* List of routines to test, NULL-terminated */
static struct {
    const char *name;
    int arch_ok;  /* defined(ARCH_xxx), etc. */
    int acflags;  /* required ac_cpuinfo() flags */
    void *(*func)(void *, const void *, size_t);
} testfuncs[] = {
    { "mmx",   defined_ARCH_X86 && defined_HAVE_ASM_MMX,
               AC_MMX,           memcpy_mmx },
    { "sse",   defined_ARCH_X86 && defined_HAVE_ASM_SSE,
               AC_CMOVE|AC_SSE,  memcpy_sse },
    { "amd64", defined_ARCH_X86_64 && defined_HAVE_ASM_SSE2,
               AC_CMOVE|AC_SSE2, memcpy_amd64 },
    { NULL }
};

/* List of sizes to test, min==0 terminated */
static struct {
    const char *name;  /* Function to limit this test to, or NULL for all */
    int min, max;      /* Size range (inclusive) */
    int block;         /* Block alignment */
} testvals[] = {
    /* Test all small block sizes, with alignments 0..7 (for amd64's movq) */
    {NULL,    1, 63, 8},
    /* Test up to 2 medium blocks plus small sizes (MMX=64, SSE=8, SSE2=16) */
    {"mmx",   64, 191, 64},
    {"sse",   64, 71, 64},
    {"amd64", 64, 79, 64},
    /* Test large block size plus up to 2 cache lines minus 1 */
    {"sse",   0x10040, 0x100BF, 64},
    {"amd64", 0x38000, 0x3807F, 64},
    /* End of list */
    {NULL,0,0}
};

int main(int argc, char *argv[])
{
    int verbose = 1;
    int ch, i, failed;

    while ((ch = getopt(argc, argv, "hqv")) != EOF) {
        if (ch == 'q') {
            verbose = 0;
        } else if (ch == 'v') {
            verbose = 2;
        } else {
            fprintf(stderr,
                    "Usage: %s [-q | -v]\n"
                    "-q: quiet (don't print test names)\n"
                    "-v: verbose (print each block size as processed)\n",
                    argv[0]);
            return 1;
        }
    }

    failed = 0;
    for (i = 0; testfuncs[i].name; i++) {
        int j;
        int thisfailed = 0;
        if (verbose > 0) {
            printf("%s: ", testfuncs[i].name);
            fflush(stdout);
        }
        if (!testfuncs[i].arch_ok) {
            printf("WARNING: unable to test (wrong architecture or not"
                   " compiled in)\n");
            continue;
        }
        if ((ac_cpuinfo() & testfuncs[i].acflags) != testfuncs[i].acflags) {
            printf("WARNING: unable to test (no support in CPU)\n");
            continue;
        }
        for (j = 0; testvals[j].min > 0; j++) {
            int size;
            if (testvals[j].name
             && strcmp(testvals[j].name, testfuncs[i].name) != 0)
                continue;
            for (size = testvals[j].min; size <= testvals[j].max; size++) {
                if (verbose >= 2) {
                    printf("%-10d\b\b\b\b\b\b\b\b\b\b", size);
                    fflush(stdout);
                }
                if (!testit(testfuncs[i].func, size, testvals[j].block,
                            SPILL, verbose))
                    thisfailed = 1;
            } /* for each size */
        } /* for each size range */
        if (thisfailed) {
            /* FAILED message printed by testit() */
            failed = 1;
        } else {
            if (verbose > 0)
                printf("ok\n");
        }
    } /* for each function */

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
