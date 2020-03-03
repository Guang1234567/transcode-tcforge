/*compile-command
set -x
gcc -O3 -g -I. -I.. "$0" -DARCH_X86
exit $?
*/

/*
 * test-acmemcpy-speed.c - time all accelerated memcpy() implementations
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "config.h"

#define _GNU_SOURCE  /* for strsignal */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>

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

/* Default copy size and test length */
#define DEF_BLOCKSIZE    0x10000
#define DEF_TESTTIME  2000  /* milliseconds */

/*************************************************************************/

static void *old_SIGSEGV = NULL, *old_SIGILL = NULL, *old_SIGVTALRM = NULL;
static sigjmp_buf env;


static void sighandler(int sig)
{
    if (sig != SIGVTALRM)
        printf("*** %s\n", strsignal(sig));
    siglongjmp(env, sig);
}

static void set_signals(void)
{
    old_SIGSEGV   = signal(SIGSEGV  , sighandler);
    old_SIGILL    = signal(SIGILL   , sighandler);
    old_SIGVTALRM = signal(SIGVTALRM, sighandler);
}

static void clear_signals(void)
{
    signal(SIGSEGV  , old_SIGSEGV  );
    signal(SIGILL   , old_SIGILL   );
    signal(SIGVTALRM, old_SIGVTALRM);
}

/*************************************************************************/

/* align1 and align2 are 0..63 */
static void testit(void *(*func)(void *, const void *, size_t), int size,
                   int align1, int align2, int msec, uint64_t *iterations)
{
    char *chunk1, *chunk1_base;
    char *chunk2, *chunk2_base;
    int sig;
    volatile uint64_t iter = 0;

    chunk1_base = malloc(size+128);
    chunk2_base = malloc(size+128);
    chunk1 = (char *)(((long)chunk1_base+63) & -64) + align1;
    chunk2 = (char *)(((long)chunk2_base+63) & -64) + align2;
    memset(chunk1, 0x11, size);
    memset(chunk2, 0x22, size);

    set_signals();
    if ((sig = sigsetjmp(env, 1)) != 0) {
        if (sig == SIGVTALRM)
            *iterations = iter;
        else
            *iterations = 0;
    } else {
        struct itimerval timer;

        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = 0;
        timer.it_value.tv_sec = msec/1000;
        timer.it_value.tv_usec = msec%1000;
        if (setitimer(ITIMER_VIRTUAL, &timer, NULL) < 0) {
            perror("setitimer");
            exit(1);
        }
        for (;;) {
            (*func)(chunk2, chunk1, size);
            iter++;
        }
    }
    clear_signals();

    free(chunk1_base);
    free(chunk2_base);
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
    const char *name;  /* centered in 5 chars */
    int arch_ok;       /* defined(ARCH_xxx) */
    int acflags;       /* required ac_cpuinfo() flags */
    void *(*func)(void *, const void *, size_t);
} testfuncs[] = {
    { "libc ", 1,                   
               0,                memcpy },
    { " mmx ", defined_ARCH_X86 && defined_HAVE_ASM_MMX,
               AC_MMX,           memcpy_mmx },
    { " sse ", defined_ARCH_X86 && defined_HAVE_ASM_SSE,
               AC_CMOVE|AC_SSE,  memcpy_sse },
    { "amd64", defined_ARCH_X86_64 && defined_HAVE_ASM_SSE2,
               AC_CMOVE|AC_SSE2, memcpy_amd64 },
    { NULL }
};

/* Alignments/sizes to test */
static struct {
    int align1, align2;
} tests[] = {
    {  0,  0 },
    {  0,  1 },
    {  0,  4 },
    {  0,  8 },
    {  0, 63 },
    {  1,  0 },
    {  1,  1 },
    {  1,  4 },
    {  1,  8 },
    {  1, 63 },
    {  4,  0 },
    {  4,  1 },
    {  8,  0 },
    {  8,  1 },
    { 63,  0 },
    { 63,  1 },
    {  0,  0 },
    {  0,  1 },
    {  0,  4 },
    {  0,  8 },
    {  0, 63 },
    {  1,  0 },
    {  1,  1 },
    {  1,  4 },
    {  1,  8 },
    {  1, 63 },
    { -1, -1 }
};

int main(int argc, char *argv[])
{
    int size = DEF_BLOCKSIZE, testtime = DEF_TESTTIME;
    int ch, i;

    while ((ch = getopt(argc, argv, "hs:t:")) != EOF) {
        if (ch == 's') {
            size = atoi(optarg);
        } else if (ch == 't') {
            testtime = atoi(optarg);
        } else {
          usage:
            fprintf(stderr,
                    "Usage: %s [-s blocksize] [-t msec-per-test]\n"
                    "Defaults: -s %d -t %d\n",
                    argv[0], DEF_BLOCKSIZE, DEF_TESTTIME);
            return 1;
        }
    }
    if (size <= 0 || testtime <= 0)
        goto usage;

    for (i = 0; testfuncs[i].name; i++) {
        if ((ac_cpuinfo() & testfuncs[i].acflags) != testfuncs[i].acflags)
            testfuncs[i].arch_ok = 0;
    }

    printf("Size: %d  msec/test: %d    Table entries in MB/s\n",
           size, testtime);
    printf("Align ");
    for (i = 0; testfuncs[i].name; i++) {
        if (testfuncs[i].arch_ok)
            printf("|%s", testfuncs[i].name);
    }
    printf("\n------");
    for (i = 0; testfuncs[i].name; i++) {
        if (testfuncs[i].arch_ok)
            printf("+-----");
    }
    printf("\n");

    for (i = 0; tests[i].align1 >= 0; i++) {
        int j;
        printf("%2d/%2d ", tests[i].align1, tests[i].align2);
        fflush(stdout);
        for (j = 0; testfuncs[j].name; j++) {
            if (testfuncs[j].arch_ok) {
                uint64_t iterations;
                testit(testfuncs[j].func, size,
                       tests[i].align1, tests[i].align2, testtime,
                       &iterations);
                if (iterations == 0)
                    printf("|-ERR-");
                else
                    printf("|%5d",
                           (int)(iterations*size*1000 / testtime / (1<<20)));
                fflush(stdout);
            }
        }
        printf("\n");
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
