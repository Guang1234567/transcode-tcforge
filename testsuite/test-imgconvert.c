/*
 * test-imgconvert.c - test/time image conversion routines
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#define WIDTH           768     /* Maximum/default width */
#define HEIGHT          512     /* Maximum/default height */
#define ITERATIONS      50      /* Minimum # of iterations */
#define MINTIME         100     /* Minimum msec to iterate */

/*************************************************************************/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "config.h"
#include "aclib/ac.h"
#include "aclib/imgconvert.h"

static void *old_SIGSEGV, *old_SIGILL;
static int sigsave;
static sigjmp_buf env;

/* Order of formats to test, with name strings */
static struct {
    ImageFormat fmt;
    const char *name;
    int width_unit, height_unit;  /* minimum meaningful unit in X and Y */
    int disabled;
} fmtlist[] = {
    { IMG_YUV420P, "420P", 2, 2 },
    { IMG_YV12,    "YV12", 2, 2, 1 },  /* disabled by default */
    { IMG_YUV411P, "411P", 4, 1 },
    { IMG_YUV422P, "422P", 2, 1 },
    { IMG_YUV444P, "444P", 1, 1 },
    { IMG_YUY2,    "YUY2", 2, 1 },
    { IMG_UYVY,    "UYVY", 2, 1 },
    { IMG_YVYU,    "YVYU", 2, 1 },
    { IMG_Y8,      " Y8 ", 1, 1 },
    { IMG_RGB24,   "RGB ", 1, 1 },
    { IMG_BGR24,   "BGR ", 1, 1 },
    { IMG_RGBA32,  "RGBA", 1, 1 },
    { IMG_ABGR32,  "ABGR", 1, 1 },
    { IMG_ARGB32,  "ARGB", 1, 1 },
    { IMG_BGRA32,  "BGRA", 1, 1 },
    { IMG_GRAY8,   "GRAY", 1, 1 },
    { IMG_NONE,    NULL }
};

/*************************************************************************/

static void sighandler(int sig)
{
    sigsave = sig;
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

/* Return value: >=0 is time/iteration in usec, <0 is error
 *   -1: unknown error
 *   -2: ac_init(0) failed
 *   -3: ac_init(accel) failed
 *   -4: ac_imgconvert(0) failed
 *   -5: ac_imgconvert(accel) failed
 *   -6: compare failed
 *   -7: SIGSEGV
 *   -8: SIGILL
 * If `check' is nonzero, just checks for accuracy and returns error or 0.
 */
static int testit(uint8_t *srcimage, ImageFormat srcfmt, ImageFormat destfmt,
                  int width, int height, int accel, int verbose, int check)
{
    static __attribute__((aligned(16))) uint8_t srcbuf[WIDTH*HEIGHT*4],
        destbuf[WIDTH*HEIGHT*4], cmpbuf[WIDTH*HEIGHT*4];
    uint8_t *src[3], *dest[3];
    long long tdiff;
    unsigned long long start, stop;
    struct rusage ru;
    int i, icnt;

    memset(cmpbuf, 0, sizeof(cmpbuf));
    memset(destbuf, 0, sizeof(destbuf));

    sigsave = 0;
    set_signals();
    if (sigsetjmp(env, 1)) {
        clear_signals();
        return sigsave==SIGILL ? -8 : -7;
    }

    if (!ac_init(0))
        return -2;
    ac_memcpy(srcbuf, srcimage, sizeof(srcbuf));
    src[0] = srcbuf;
    if (IS_YUV_FORMAT(srcfmt))
        YUV_INIT_PLANES(src, srcbuf, srcfmt, width, height);
    dest[0] = cmpbuf;
    if (IS_YUV_FORMAT(destfmt))
        YUV_INIT_PLANES(dest, cmpbuf, destfmt, width, height);
    if (!ac_imgconvert(src, srcfmt, dest, destfmt, width, height))
        return -4;

    if (!ac_init(accel))
        return -3;
    // currently src can get destroyed--see img_yuv_mixed.c
    ac_memcpy(srcbuf, srcimage, sizeof(srcbuf));
    dest[0] = destbuf;
    if (IS_YUV_FORMAT(destfmt))
        YUV_INIT_PLANES(dest, destbuf, destfmt, width, height);
    if (!ac_imgconvert(src, srcfmt, dest, destfmt, width, height))
        return -5;

    tdiff = 0;
    for (i = 0; i < sizeof(destbuf); i++) {
        int diff = (int)destbuf[i] - (int)cmpbuf[i];
        if (diff < -1 || diff > 1) {
            if (verbose) {
                fprintf(stderr, "*** compare error: at %d (want=%d have=%d)\n",
                        i, cmpbuf[i], destbuf[i]);
            }
            return -6;
        }
        tdiff += diff*diff;
    }
    if (tdiff >= width*height/2) {
        if (verbose) {
            fprintf(stderr,
                    "*** compare error: total difference too great (%lld)\n",
                    tdiff);
        }
        return -6;
    }

    if (check)
        return 0;

    getrusage(RUSAGE_SELF, &ru);
    start = ru.ru_utime.tv_sec * 1000000ULL + ru.ru_utime.tv_usec;
    icnt = 0;
    do {
        for (i = 0; i < ITERATIONS; i++)
            ac_imgconvert(src, srcfmt, dest, destfmt, width, height);
        getrusage(RUSAGE_SELF, &ru);
        stop = ru.ru_utime.tv_sec * 1000000ULL + ru.ru_utime.tv_usec;
        icnt += ITERATIONS;
    } while (stop-start < MINTIME*1000);

    clear_signals();
    return (stop-start + icnt/2) / icnt;
}

/*************************************************************************/

/* Check all routines, and return 1 (no failures) or 0 (some failures) */

#define TRYIT(w,h) \
            if (testit(srcimage, fmtlist[i].fmt, fmtlist[j].fmt,        \
                       (w), (h), accel, 1, 1) < -5                      \
            ) {                                                         \
                printf("FAILED: %s -> %s @ %dx%d\n",                    \
                       fmtlist[i].name, fmtlist[j].name, (w), (h));     \
                failures++;                                             \
            }

static int checkall(uint8_t *srcimage, int accel, const char *name)
{
    int i, j;
    int failures = 0;

    for (i = 0; fmtlist[i].fmt != IMG_NONE; i++) {
        for (j = 0; fmtlist[j].fmt != IMG_NONE; j++) {
            int oldfail = failures;
            int width_unit  = fmtlist[i].width_unit;
            int height_unit = fmtlist[i].height_unit;
            if (fmtlist[j].width_unit  > width_unit)
                width_unit  = fmtlist[j].width_unit;
            if (fmtlist[j].height_unit > height_unit)
                height_unit = fmtlist[j].height_unit;
            if (name) {
                printf("%s/%s-%s...", name, fmtlist[i].name, fmtlist[j].name);
                fflush(stdout);
            }
            TRYIT(WIDTH,            HEIGHT);
            TRYIT(WIDTH-width_unit, HEIGHT);
            TRYIT(WIDTH,            HEIGHT-height_unit);
            TRYIT(WIDTH-width_unit, HEIGHT-height_unit);
            if (name && failures == oldfail)
                printf("ok\n");
        }
    }
    if (failures) {
        if (name)
            printf("%s: %d conversions failed.\n", name, failures);
        return 0;
    } else {
        if (name)
            printf("%s: All conversions succeeded.\n", name);
        return 1;
    }
}

/*************************************************************************/

static const char *accel_flags(int accel)
{
    static char buf[1000];
    snprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s%s%s%s",
           !accel                ? " none"     : "",
           (accel & AC_IA32ASM ) ? " ia32asm"  : "",
           (accel & AC_AMD64ASM) ? " amd64asm" : "",
           (accel & AC_CMOVE   ) ? " cmove"    : "",
           (accel & AC_MMX     ) ? " mmx"      : "",
           (accel & AC_MMXEXT  ) ? " mmxext"   : "",
           (accel & AC_3DNOW   ) ? " 3dnow"    : "",
           (accel & AC_SSE     ) ? " sse"      : "",
           (accel & AC_SSE2    ) ? " sse2"     : "",
           (accel & AC_SSE3    ) ? " sse3"     : "");
    return buf;
}


int main(int argc, char **argv)
{
    static uint8_t srcbuf[WIDTH*HEIGHT*4];
    int check = 0, accel = 0, compare = 0, verbose = 0, width = WIDTH,
        height = HEIGHT;
    int i, j;

    while (argc > 1) {
        if (strcmp(argv[--argc],"-h") == 0) {
            fprintf(stderr,
"Usage: %s [-C] [-c] [-v] [=fmt-name[,fmt-name...]] [@WIDTHxHEIGHT] [accel-name...]\n",
                    argv[0]);
            fprintf(stderr,
"-C: check all testable accelerated routines and exit with success/failure\n"
"-c: compare with non-accelerated versions and report percentage speedup\n"
"-v: verbose (report details of comparison failures; with -C, print test names)\n"
"=: select formats to test\n"
"   fmt-name can be:");
            for (i = 0; fmtlist[i].fmt != IMG_NONE; i++) {
                char buf[16], *s;
                snprintf(buf, sizeof(buf), "%s", fmtlist[i].name);
                while (*buf && buf[strlen(buf)-1]==' ')
                    buf[strlen(buf)-1] = 0;
                s = buf + strspn(buf," ");
                fprintf(stderr," %s", s);
            }
            fprintf(stderr, "\n");
            fprintf(stderr, "@: set image size (default/max %dx%d)\n",
                    WIDTH, HEIGHT);
            fprintf(stderr, "accel-name can be ia32asm, amd64asm, cmove, mmx, ...\n");
            return 0;
        }
        if (strcmp(argv[argc],"-C") == 0)
            check = 1;
        else if (strcmp(argv[argc],"-c") == 0)
            compare = 1;
        else if (strcmp(argv[argc],"-v") == 0)
            verbose = 1;
        else if (strcmp(argv[argc],"ia32asm") == 0)
            accel |= AC_IA32ASM;
        else if (strcmp(argv[argc],"amd64asm") == 0)
            accel |= AC_AMD64ASM;
        else if (strcmp(argv[argc],"cmove") == 0)
            accel |= AC_CMOVE;
        else if (strcmp(argv[argc],"mmx") == 0)
            accel |= AC_MMX;
        else if (strcmp(argv[argc],"mmxext") == 0)
            accel |= AC_MMXEXT;
        else if (strcmp(argv[argc],"3dnow") == 0)
            accel |= AC_3DNOW;
        else if (strcmp(argv[argc],"3dnowext") == 0)
            accel |= AC_3DNOWEXT;
        else if (strcmp(argv[argc],"sse") == 0)
            accel |= AC_SSE;
        else if (strcmp(argv[argc],"sse2") == 0)
            accel |= AC_SSE2;
        else if (strcmp(argv[argc],"sse3") == 0)
            accel |= AC_SSE3;
        else if (argv[argc][0] == '=') {
            char *s = argv[argc]+1;
            for (i = 0; fmtlist[i].fmt != IMG_NONE; i++) {
                fmtlist[i].disabled = 1;
            }
            for (s = strtok(s,","); s; s = strtok(NULL,",")) {
                for (i = 0; fmtlist[i].fmt != IMG_NONE; i++) {
                    const char *t = fmtlist[i].name;
                    int l;
                    while (*t == ' ')
                        t++;
                    l = strlen(t);
                    while (l > 1 && t[l-1] == ' ')
                        l--;
                    if (strlen(s) == l && memcmp(s,t,l) == 0) {
                        fmtlist[i].disabled = 0;
                        break;
                    }
                }
                if (fmtlist[i].fmt == IMG_NONE) {
                    fprintf(stderr, "Unknown image format `%s'\n", s);
                    fprintf(stderr, "`%s -h' for help.\n", argv[0]);
                    return 1;
                }
            }
        } else if (argv[argc][0] == '@') {
            if (sscanf(argv[argc]+1, "%dx%d", &width, &height) != 2
             || width <= 0 || height <= 0
            ) {
                fprintf(stderr, "Invalid image size `%s'\n", argv[argc]+1);
                fprintf(stderr, "`%s -h' for help.\n", argv[0]);
                return 1;
            }
            if (width > WIDTH || height > HEIGHT) {
                fprintf(stderr, "Image size too large (max %dx%d)\n",
                        WIDTH, HEIGHT);
                fprintf(stderr, "`%s -h' for help.\n", argv[0]);
                return 1;
            }
        } else {
            fprintf(stderr, "Unknown accel type `%s'\n", argv[argc]);
            fprintf(stderr, "`%s -h' for help.\n", argv[0]);
            return 1;
        }
    }
    if (accel) {
        if (accel & ~ac_cpuinfo()) {
            fprintf(stderr, "Unavailable accel type(s):%s\n",
                    accel_flags(accel & ~ac_cpuinfo()));
            fprintf(stderr, "Supported on this machine:%s\n",
                    accel_flags(ac_cpuinfo()));
            return 1;
        }
    } else {
        accel = ac_cpuinfo();
    }

    srandom(0);  /* to give a standard "image" */
    for (i = 0; i < sizeof(srcbuf); i++)
        srcbuf[i] = random();

    if (check) {
        if (ac_cpuinfo() & (AC_IA32ASM | AC_AMD64ASM)) {
            if (!checkall(srcbuf, AC_IA32ASM | AC_AMD64ASM,
                          verbose ? "asm" : NULL))
                return 1;
        }
        if (ac_cpuinfo() & AC_MMX) {
            if (!checkall(srcbuf, AC_IA32ASM | AC_AMD64ASM | AC_MMX,
                          verbose ? "mmx" : NULL))
                return 1;
        }
        if (ac_cpuinfo() & AC_SSE2) {
            if (!checkall(srcbuf, AC_IA32ASM | AC_AMD64ASM | AC_CMOVE
                                | AC_MMX | AC_SSE | AC_SSE2,
                          verbose ? "sse2" : NULL))
                return 1;
        }
        return 0;
    }

    printf("Acceleration flags:%s\n", accel_flags(accel));
    if (compare)
        printf("Units: conversions/time (unaccelerated = 100)\n\n");
    else
        printf("Units: conversions/sec (frame size: %dx%d)\n\n", width, height);
    printf("    |");
    for (i = 0; fmtlist[i].fmt != IMG_NONE; i++) {
        if (!fmtlist[i].disabled)
            printf("%-4s|", fmtlist[i].name);
    }
    printf("\n----+");
    for (i = 0; fmtlist[i].fmt != IMG_NONE; i++) {
        if (!fmtlist[i].disabled)
            printf("----+");
    }
    printf("\n");

    for (i = 0; fmtlist[i].fmt != IMG_NONE; i++) {
        if (fmtlist[i].disabled)
            continue;
        printf("%-4s|", fmtlist[i].name);
        fflush(stdout);
        for (j = 0; fmtlist[j].fmt != IMG_NONE; j++) {
            if (fmtlist[j].disabled)
                continue;
            int res = testit(srcbuf, fmtlist[i].fmt, fmtlist[j].fmt,
                             width, height, accel, verbose, 0);
            switch (res) {
                case -1:
                case -2:
                case -3:
                case -4:
                case -5: printf("----|"); break;
                case -6: printf("BAD |"); break;
                case -7: printf("SEGV|"); break;
                case -8: printf("ILL |"); break;
                default:
                    if (compare) {
                        int res0 = testit(srcbuf, fmtlist[i].fmt,
                                          fmtlist[j].fmt, width, height,
                                          0, 0, 0);
                        if (res0 < 0)
                            printf("****|");
                        else
                            printf("%4d|", (100*res0 + res/2) / res);
                    } else {
                        printf("%4d|", (1000000+res/2)/res);
                    }
                    break;
            }
            fflush(stdout);
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
