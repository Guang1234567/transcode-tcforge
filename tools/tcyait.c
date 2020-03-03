/*
 *  tcyait.c
 *
 *  Copyright (C) Allan Snider - February 2007
 *
 *  This file is part of transcode, a video stream processing tool
 *
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
 */


#include <stdlib.h>
#include <string.h>

#include "libtc/libtc.h"

#include "yait.h"

/*
 *	tcyait:
 *		Yet Another Inverse Telecine filter.
 *
 *	Usage:
 *		tcyait [-d] [-l log] [-o ops] [-m mode]
 *	        	-d		print debug info to stdout
 *	        	-l log		specify input yait log file name
 *	        	-o ops		specify output yait frame operations file name
 *	        	-m mode		specify transcode de-interlace method to use
 *
 *		By default, reads "yait.log" and produces "yait.ops".
 *
 *	Description:
 *
 *		Read a yait log file (generated via -J yait=log), and analyze it to
 *	produce a yait frame operations file.  The frame operations file contains
 *	commands to the yait filter to drop, copy or save rows (to de-interlace),
 *	or blend frames.  This will convert from NTSC 29.97 to 23.976 fps.  The file
 *	generated is used as input for another transcode pass (-J yait=ops).
 */

/* frame information */
typedef struct fi_t Fi;
struct fi_t {
    double r;                   /* even/odd delta ratio, filtered */
    double ro;                  /* ratio, original value */
    double w;                   /* statistical strength */
    int fn;                     /* frame number */
    int ed;                     /* even row delta */
    int od;                     /* odd row delta */
    int gi;                     /* group array index */
    int ip;                     /* telecine pattern */
    int op;                     /* frame operation, nop, save/copy row */
    int drop;                   /* frame is to be dropped */
    int gf;                     /* group flag */
    Fi *next;
};


/*
 *	globals:
 */

char *Prog;                     /* argv[0] */
char *LogFn;                    /* log file name, default "yait.log" */
char *OpsFn;                    /* ops file name, default "yait.ops" */
int DeintMode;                  /* transcode de-interlace mode, (1-5) */
int DebugFi;                    /* dump debug frame info */

FILE *LogFp;                    /* log file */
FILE *OpsFp;                    /* ops file */

Fi *Fl;                         /* frame list */
Fi **Fa;                        /* frame array */
Fi **Ga;                        /* group array */
int Nf;                         /* number of frames */
int Ng;                         /* number of frames in group */
int Nd;                         /* number of frames dropped */
int Md;                         /* max delta */


/*
 *	protos:
 */

static void yait_parse_args(int, char **);
static void yait_chkac(int *);
static void yait_usage(void);
static void yait_read_log(void);
static double yait_calc_ratio(int, int);
static void yait_find_ip(void);
static void yait_chk_ip(int);
static void yait_chk_pairs(int);
static void yait_chk_tuplets(int);
static int yait_find_odd(double, int, double *);
static int yait_find_even(double, int, double *);
static int yait_ffmin(int);
static int yait_ffmax(int);
static int yait_m5(int);
static void yait_mark_grp(int, int, double);
static void yait_find_drops(void);
static int yait_cnt_drops(int);
static int yait_extra_drop(int);
static int yait_missing_drop(int);
static void yait_keep_frame(int);
static int yait_get_hdrop(int, int *);
static void yait_ivtc_keep(int);
static void yait_ivtc_grps(void);
static int yait_scan_bk(int);
static int yait_scan_fw(int);
static void yait_drop_frame(int);
static int yait_ivtc_grp(int, int, int);
static double yait_tst_ip(int, int);
static void yait_deint(void);
static void yait_write_ops(void);
static char *yait_write_op(Fi *);

static void yait_debug_fi(void);
static char *yait_op(int op);
static char *yait_drop(Fi * f);
static char *yait_grp(int flg);


/*
 *	main:
 */

int
main(int argc, char **argv)
{
    /* parse args */
    yait_parse_args(argc, argv);

    LogFp = fopen(LogFn, "r");
    if (!LogFp) {
        perror("fopen");
        fprintf(stderr, "Cannot open YAIT delta log file (%s)\n", LogFn);
        exit(1);
    }

    OpsFp = fopen(OpsFn, "w");
    if (!OpsFp) {
        perror("fopen");
        fprintf(stderr, "Cannot create YAIT frame ops file (%s)\n", OpsFn);
        exit(1);
    }

    /* read the log */
    yait_read_log();

    /* find interleave patterns */
    yait_find_ip();

    /* find drop frames */
    yait_find_drops();

    /* complete groups missing an interleave pattern */
    yait_ivtc_grps();

    /* let transcode de-interlace frames we missed */
    yait_deint();

    if (DebugFi)
        yait_debug_fi();

    /* print frame ops file */
    yait_write_ops();

    return (0);
}


/*
 *	yait_parse_args:
 */

static void
yait_parse_args(int argc, char **argv)
{
    int opt;
    char *p;

    LogFn = Y_LOG_FN;
    OpsFn = Y_OPS_FN;
    DeintMode = Y_DEINT_MODE;

    --argc;
    Prog = *argv++;
    while ((p = *argv)) {
        if (*p++ != '-')
            break;
        while ((opt = *p++))
            switch (opt) {
            case 'd':
                DebugFi = TC_TRUE;
                break;

            case 'l':
                yait_chkac(&argc);
                LogFn = *++argv;
                break;

            case 'o':
                yait_chkac(&argc);
                OpsFn = *++argv;
                break;

            case 'm':
                yait_chkac(&argc);
                DeintMode = atoi(*++argv);
                break;

            default:
                yait_usage();
                break;
            }
        --argc;
        argv++;
    }

    if (argc)
        yait_usage();
}


/*
 *	yait_chkac:
 */

static void
yait_chkac(int *ac)
{
    if (*ac < 1)
        yait_usage();
    --*ac;
}


/*
 *	yait_usage:
 */

static void
yait_usage(void)
{
    printf("Usage: %s [-d] [-l log] [-o ops] [-m mode]\n", Prog);
    printf("\t-d\t\tPrint debug information to stdout.\n");
    printf("\t-l log\t\tSpecify input yait log file name [yait.log].\n");
    printf
        ("\t-o ops\t\tSpecify output yait frame ops file name [yait.ops].\n");
    printf("\t-m mode\t\tSpecify transcode de-interlace method [3].\n\n");

    exit(1);
}


/*
 *	yait_read_log:
 */

static void
yait_read_log(void)
{
    Fi **fa, *pf, *f;
    int fn, ed, od;
    int s, n;

    s = 0;
    pf = NULL;
    for (Nf = 0;; Nf++) {
        n = fscanf(LogFp, "%d: e: %d, o: %d\n", &fn, &ed, &od);
        if (n != 3)
            break;

        /* starting frame number */
        if (!Nf)
            s = fn;

        if ((fn - s) != Nf) {
            printf("Broken log file, line %d\n", Nf);
            exit(1);
        }

        f = (Fi *) malloc(sizeof(Fi));
        if (!f) {
            perror("malloc");
            exit(1);
        }

        memset((void *) f, 0, sizeof(Fi));
        if (!Fl)
            Fl = f;
        if (pf)
            pf->next = f;
        pf = f;

        f->r = yait_calc_ratio(ed, od);
        f->ro = f->r;
        f->fn = fn;
        f->ed = ed;
        f->od = od;
        f->ip = -1;
    }

    if (!Fl) {
        fprintf(stderr, "Invalid log file.\n");
        exit(1);
    }

    Fa = (Fi **) malloc((Nf + 1) * sizeof(Fi *));
    Ga = (Fi **) malloc((Nf + 1) * sizeof(Fi *));
    if (!Fa || !Ga) {
        perror("malloc");
        exit(1);
    }

    fa = Fa;
    for (f = Fl; f; f = f->next)
        *fa++ = f;
    *fa = NULL;
}


/*
 *	yait_calc_ratio:
 *		Compute a ratio between even/odd row deltas.  A high ratio indicates an
 *	interlace present.  Use the sign of the ratio to indicate even row (<0), or odd
 *	row (>0) correlation.
 *
 *		If the magnitude of the ratio is > 1.1, this is usually enough to
 *	indicate interlacing.  A value around 1.0 indicates no row correlation at
 *	all.
 *
 * 		Assigning the ratios in this manner result in the following patterns
 * 	present for interlaced material.  Assume 0 for fabs(r)<thresh, else +/- 1:
 *
 * 	An odd interlace pattern (for a five frame group) would appear as:
 *
 *			frame:  1	2	3	4	5
 *			even:	a	a	b	c	d
 *			odd:	a	b	c	c	d
 *
 *			ratio:	0	-1	0	1	0
 *
 * 	If we detect this pattern, we assign the following frame operations:
 *
 *			frame:  1	2	3	4	5
 *			even:	a	a	b	c	d
 *			odd:	a	b	c	c	d
 *
 *			ratio:	0	-1	0	1	0
 *			op:		osd	oc
 *
 * 		osd = save odd rows and drop the frame
 * 		oc  = copy in saved odd rows
 *
 * 	This results with:
 *
 *			frame:  1	|2|	3	4	5
 *			even:	a	|a|	b	c	d
 *			odd:	a	|b|-->	b	c	d
 *                                     drop
 *
 *	For even interlace patterns, the signs are reversed, or simply:
 *
 *			ratio:	0	1	0	-1	0
 *					esd	ec
 *
 *	The entire approach of this tool depends on these specific ratio patterns
 *	to be present, and should be for 2:3 pulldown.  Lots of complications arise
 *	around still and abrupt scene changes.  Again, it might be useful for the
 *	filter to produce a combing co-efficient as well as the delta information.
 *
 *	Side note:
 *		For yuv, deltas based only on luminance yeilded much stronger
 *		interlace patterns, however, I suppose there are (rare) cases where
 *		chroma could be the only indicator, so chroma is included in the
 *		delta calculation, even though it results in weaker ratios.
 */

static double
yait_calc_ratio(int ed, int od)
{
    double r = 0.0;

    /* compute ratio, >1 odd, <-1 even */
    if (!ed && !od)
        /* duplicate frame */
        r = 0;

    if (ed && !od)
        r = 100;

    if (!ed && od)
        r = -100;

    if (ed && od) {
        r = (double) ed / (double) od;

        if (r < 1)
            r = -1.0 / r;

        if (r > 100)
            r = 100;
        if (r < -100)
            r = -100;
    }

    return (r);
}


/*
 *	yait_find_ip:
 *		- Mark isolated duplicate frames to be hard dropped.
 *		- Create the group array which is used to processes interleave
 *		  patterns without duplicate frames present.
 *		- Find the maximum frame delta value.  This is used to normalize
 *		  frame deltas to filter out weak frames (noise which may cause
 *		  erroneous interleave patterns to be detected).
 *		- Detect local interleave patterns.
 */

static void
yait_find_ip(void)
{
    Fi *f;
    double w;
    int m, p, i;

    /* mark obvious drop frames */
    for (i = 1; i < Nf - 1; i++) {
        f = Fa[i];
        if (f->r)
            continue;

        if (!Fa[i - 1]->r && !Fa[i + 1]->r)
            continue;

        f->drop = TC_TRUE;
    }

    /* create group array, ommiting drops */
    Ng = 0;
    for (i = 0; i < Nf; i++) {
        f = Fa[i];
        if (f->drop)
            continue;

        f->gi = Ng;
        Ga[Ng++] = f;
    }
    Ga[Ng] = NULL;

    /* find max row delta */
    m = 0;
    for (i = 0; i < Nf; i++) {
        f = Fa[i];
        if (f->ed > m)
            m = f->ed;
        if (f->od > m)
            m = f->od;
    }

    Md = m;
    if (!Md) {
        fprintf(stderr, "All empty frames?\n");
        exit(1);
    }

    /* filter out weak r values (noise) */
    for (i = 0; i < Ng; i++) {
        f = Ga[i];
        if ((f->ed + f->od) / (double) Md < Y_WEIGHT)
            f->r = 0;
    }

    /* adjust for incomplete interleave patterns */
    /* (indexing Fa[0,..,i+6]) */
    for (i = 0; i < Ng - 6; i++)
        yait_chk_ip(i);

    /* find interleave patterns */
    for (i = 0; i < Ng; i++) {
        f = Ga[i];
        if (f->op & Y_OP_COPY) {
            /* finish this group before looking for another pattern */
            i++;
            continue;
        }

        p = yait_find_odd(Y_THRESH, i, &w);
        if (p != -1) {
            yait_mark_grp(p, i, w);
            continue;
        }

        p = yait_find_even(Y_THRESH, i, &w);
        if (p != -1)
            yait_mark_grp(p + 10, i, w);
    }
}


/*
 *	yait_chk_ip:
 *		Two cases to look for.  An isolated pair of high r's, and an
 *	isolated tuplet of high r's.  These can be caused by interlacing over
 *	still and abrupt scene changes.
 */

static void
yait_chk_ip(int n)
{
    yait_chk_pairs(n);
    yait_chk_tuplets(n);
}


/*
 *	yait_chk_pairs:
 *		Look for patterns of the type:
 *			i:      0  1  2  3  4  5
 *			odd:	0  0 -1  1  0  0
 *			even:	0  0  1 -1  0  0
 *
 *	If detected, force the drop of the (single) interlaced frame.
 *	De-interlacing would just incur a redundant copy operation.
 */

static void
yait_chk_pairs(int n)
{
    Fi *fa[6];
    double ra[6];
    int i;

    for (i = 0; i < 6; i++) {
        fa[i] = Ga[n + i];
        ra[i] = fabs(fa[i]->r);
    }

    for (i = 2; i < 4; i++)
        if (ra[i] < Y_THRESH)
            return;

    /* adjacent frames to the tuplet must be <thresh */
    if (ra[1] > Y_THRESH || ra[4] > Y_THRESH)
        return;

    /* we only need one edge frame to be <thresh */
    if (ra[0] > Y_THRESH && ra[5] > Y_THRESH)
        return;

    if (fa[2]->r > 0 && fa[3]->r > 0)
        return;

    if (fa[2]->r < 0 && fa[3]->r < 0)
        return;

    /* two isolated high r values of opposite sign */
    /* drop the interlaced frame, erase the pattern */
    fa[2]->r = 0;
    fa[3]->r = 0;

    fa[2]->drop = TC_TRUE;
}


/*
 *	yait_chk_tuplets:
 *		Look for patterns of the type:
 *			i:      0  1  2   3    4  5  6
 *			odd:	0  0 -1  +/-2  1  0  0
 *			even:	0  0  1  +/-2 -1  0  0
 *
 *	and complete to:
 *
 *			odd:	0  0 -1   0    1  0  0
 *			even:	0  0  1   0   -1  0  0
 */

static void
yait_chk_tuplets(int n)
{
    Fi *fa[7];
    double ra[7];
    int i;

    for (i = 0; i < 7; i++) {
        fa[i] = Ga[n + i];
        ra[i] = fabs(fa[i]->r);
    }

    for (i = 2; i < 5; i++)
        if (ra[i] < Y_THRESH)
            return;

    /* adjacent frames to the tuplet must be <thresh */
    if (ra[1] > Y_THRESH || ra[5] > Y_THRESH)
        return;

    /* we only need one edge frame to be <thresh */
    if (ra[0] > Y_THRESH && ra[6] > Y_THRESH)
        return;

    if (fa[2]->r > 0 && fa[4]->r > 0)
        return;

    if (fa[2]->r < 0 && fa[4]->r < 0)
        return;

    /* isolated tuplet of high r values of opposite sign */
    if (ra[3] > ra[2] || ra[3] > ra[4])
        fa[3]->r = 0;
}


/*
 *	yait_find_odd:
 */

static int
yait_find_odd(double thresh, int n, double *w)
{
    double re, ro;
    int me, mo;
    int p;

    /* find max even/odd correlations */
    /* (r<0 - even, r>0 - odd) */
    me = yait_ffmin(n);
    mo = yait_ffmax(n);

    p = -1;
    if (yait_m5(mo - 2) == yait_m5(me)) {
        re = fabs(Ga[me]->r);
        ro = fabs(Ga[mo]->r);
        if (re > thresh && ro > thresh) {
            p = yait_m5(mo - 4);
            *w = re + ro;
        }
    }

    return (p);
}


/*
 *	yait_find_even:
 */

static int
yait_find_even(double thresh, int n, double *w)
{
    double re, ro;
    int me, mo;
    int p;

    me = yait_ffmin(n);
    mo = yait_ffmax(n);

    p = -1;
    if (yait_m5(me - 2) == yait_m5(mo)) {
        re = fabs(Ga[me]->r);
        ro = fabs(Ga[mo]->r);
        if (re > thresh && ro > thresh) {
            p = yait_m5(me - 4);
            *w = re + ro;
        }
    }

    return (p);
}


/*
 *	yait_ffmin:
 */

static int
yait_ffmin(int n)
{
    Fi *f;
    int m, i;
    double r;

    r = 0;
    m = 0;
    for (i = n; i < n + 4; i++) {
        if (i < 0)
            break;

        f = Ga[i];
        if (!f)
            break;

        if (f->r < r) {
            r = f->r;
            m = i;
        }
    }

    return (m);
}


/*
 *	yait_ffmax:
 */

static int
yait_ffmax(int n)
{
    Fi *f;
    int m, i;
    double r;

    r = 0;
    m = 0;
    for (i = n; i < n + 4; i++) {
        if (i < 0)
            break;

        f = Ga[i];
        if (!f)
            break;

        if (f->r > r) {
            r = f->r;
            m = i;
        }
    }

    return (m);
}


/*
 *	yait_m5:
 */

static int
yait_m5(int n)
{
    while (n < 0)
        n += 5;
    return (n % 5);
}


/*
 *	yait_mark_grp:
 */

static void
yait_mark_grp(int p, int n, double w)
{
    Fi *f;
    int t, i;

    if (n % 5 != (p + 2) % 5)
        return;

    /* only overwrite an existing pattern if weight is greater */
    f = Ga[n];
    if (w <= f->w)
        return;

    /* this frame and next are interlaced */
    t = (p < 10) ? Y_OP_ODD : Y_OP_EVEN;
    f->op = t | Y_OP_SAVE | Y_OP_DROP;
    f = Ga[n + 1];
    f->op = t | Y_OP_COPY;

    /* assume 1 progressive on either side of the tuplet */
    for (i = n - 1; i < n + 4; i++) {
        if (i < 0 || i > Ng - 1)
            continue;

        f = Ga[i];
        f->ip = p;
        f->w = w;
    }
}


/*
 *	yait_find_drops:
 *		For every group of 5 frames, make sure we drop a frame.  Allow up to a
 *	4 group lookahead to make up for extra or missing drops.  (The duplicated frames
 *	generated by --hard_fps can be quite early or late in the sequence).  If a group
 *	requires a drop, but none exists, mark the group as requiring de-interlacing.
 *	Finally, consequetive marked groups inherit surrounding interleave patterns.
 *
 *	Each group will receive one of the following flags:
 *
 * 		Y_HAS_DROP		- group has a single drop frame
 * 		Y_BANK_DROP		- extra drop, can be used forward
 * 		Y_WITHDRAW_DROP		- missing drop, use banked drop from behind
 * 		Y_RETURN_DROP		- extra drop, can be used behind
 * 		Y_BORROW_DROP		- missing drop, use future extra drop
 * 		Y_FORCE_DEINT		- force de-interlacing, (produces a drop)
 * 		Y_FORCE_DROP		- missing drop, no extras and no interleave found
 * 		Y_FORCE_KEEP		- extra drop, no consumer so have to keep it
 *
 *	For any flags other than FORCE, no action is required.  Eeach group already has
 *	an available frame to drop, whether a marked duplicate, or a locally detected
 *	interleave pattern (which produces a drop).
 *
 *	For Y_FORCE_DEINT, assemble consecutive groups of this type and try to inherit
 *	adjacent interleave patterns.  If no pattern is available, mark them as
 *	Y_FORCE_DROP.
 */

static void
yait_find_drops(void)
{
    Fi *f;
    int ed;
    int d, n;

    /* running count of extra drops */
    ed = 0;

    /* process by groups of 5 */
    for (n = 0; n < Nf; n += 5) {
        f = Fa[n];

        /* get number of drops */
        d = yait_cnt_drops(n);

        /* we can't really handle this well, so force the keep of frames */
        /* until we have only two extra drops */
        while (d > 2) {
            yait_keep_frame(n);
            d = yait_cnt_drops(n);
        }

        /* no drops in group */
        if (!d) {
            if (ed > 0) {
                /* an extra drop was available */
                f->gf = Y_WITHDRAW_DROP;
                --ed;
                continue;
            }

            /* look ahead for an extra drop */
            d = yait_extra_drop(n);
            if (d) {
                /* consume the next extra drop */
                f->gf = Y_BORROW_DROP;
                --ed;
                continue;
            }

            /* mark group to be de-interlaced */
            f->gf = Y_FORCE_DEINT;

            continue;
        }

        /* extra drop exists */
        if (d > 1) {
            if (ed < 0) {
                /* we needed it */
                f->gf = Y_RETURN_DROP;
                ed++;
                continue;
            }

            /* look ahead for a missing drop */
            d = yait_missing_drop(n);
            if (d) {
                /* we can use it later */
                f->gf = Y_BANK_DROP;
                ed++;
                continue;
            }

            /* we can't use an extra drop, keep one */
            f->gf = Y_FORCE_KEEP;
            yait_keep_frame(n);

            continue;
        }

        /* group has a single drop frame */
        f->gf = Y_HAS_DROP;
    }
}


/*
 *	yait_cnt_drops:
 */

static int
yait_cnt_drops(int n)
{
    Fi *f;
    int d, i;

    d = 0;
    for (i = n; i < n + 5 && i < Nf; i++) {
        f = Fa[i];
        if (f->drop || f->op & Y_OP_DROP)
            d++;
    }

    return (d);
}


/*
 *	yait_extra_drop:
 *		Scan four groups ahead for an extra drop.
 */

static int
yait_extra_drop(int n)
{
    int da[4], d, e, g, i;

    d = 0;
    for (g = 0; g < 4; g++) {
        i = n + (g + 1) * 5;
        da[g] = yait_cnt_drops(i);
        d += da[g];
    }

    if (d < 5)
        return (TC_FALSE);

    /* find group with the extra drop */
    for (e = 0; e < 4; e++)
        if (da[e] > 1)
            break;

    /* make sure extra drop wouldn't be accounted for */
    d = 0;
    for (g = 0; g < 3; g++) {
        i = n + ((e + 1) + (g + 1)) * 5;
        d += yait_cnt_drops(i);
    }

    if (d < 3)
        return (TC_FALSE);

    return (TC_TRUE);
}


/*
 *	yait_missing_drop:
 *		Scan four groups ahead for a missing drop.
 */

static int
yait_missing_drop(int n)
{
    int d, g, i;

    d = 0;
    for (g = 0; g < 4; g++) {
        i = n + (g + 1) * 5;
        d += yait_cnt_drops(i);
    }

    if (d > 3)
        return (TC_FALSE);

    return (TC_TRUE);
}


/*
 *	yait_keep_frame:
 *		Multiple drops exist.  Pick the best frame to keep.  This can be difficult,
 *	as we do not want to keep a duplicate of an interlaced frame.  First, try to find
 *	a hard dropped frame which does not follow an interlace.  If one can be found, then
 *	simply negate the drop flag.  If we are duplicating an interlace, alter the frame
 *	operations for the group to produce a non-interlaced duplicate.
 */

static void
yait_keep_frame(int n)
{
    Fi *f;
    int da[6], bd, d, i;

    d = yait_get_hdrop(n, da);

    if (!d) {
        /* no hard drop frames were found, so ... */
        /* two interlace drops exist, keep one, but blend it */
        for (i = n; i < n + 5 && i < Nf; i++) {
            f = Fa[i];
            if (f->op & Y_OP_DROP) {
                f->op &= ~Y_OP_DROP;
                f->op |= Y_OP_DEINT;
                return;
            }
        }

        /* sanity check */
        f = Fa[n];
        fprintf(stderr, "No drop frame can be found, frame: %d\n", f->fn);
        exit(1);
    }

    /* try to use a drop frame that isn't an interlace duplicate */
    bd = -1;
    for (i = 0; i < 5; i++) {
        d = da[i];
        if (!d)
            /* can't access before Fa[0] */
            continue;

        if (d < 0)
            /* end of drop list */
            break;

        f = Fa[d - 1];
        if (f->drop)
            /* sheesh, two dups in a row */
            f = Fa[d - 2];

        if (!f->op) {
            /* good */
            f = Fa[d];
            f->drop = TC_FALSE;
            return;
        }

        if (f->op & Y_OP_COPY)
            bd = d;
    }

    /* keeping a duplicate of an interlace, try to use one which duplicates the */
    /* second of an interlace pair, as that is cleaner to deal with */
    /* bd (best drop) was set earlier in the loop if such a frame was found */
    if (bd < 0)
        bd = da[0];

    yait_ivtc_keep(bd);
}


/*
 *	yait_get_hdrop:
 *		Populate an index array of the hard dropped frames, and return
 *	the count of how many were found.
 */

static int
yait_get_hdrop(int n, int *da)
{
    Fi *f;
    int d, i;

    d = 0;
    for (i = n; i < n + 5 && i < Nf; i++) {
        f = Fa[i];
        if (f->drop) {
            *da++ = i;
            d++;
        }
    }
    *da = -1;

    return (d);
}


/*
 *	yait_ivtc_keep
 *		Depending upon the position of the DROP in the pattern, alter the
 *	frame ops to generate a non-interlaced frame, and keep it.
 *
 *	Case 1:
 *		If the duplicated frame is the second of the interlaced pair, then
 *		simply repeat the row copy operation and keep the frame.
 *
 *		Original (odd pattern):
 *				 	sd	c	 	 
 *			even:	2	2	3	3	4
 *			odd:	2	3	4	4	4
 *					drop		DROP
 *
 *		    yeilds (bad keep frame):
 *			even:	2		3	3	4
 *			odd:	2		3	4	4
 *							KEEP
 *		Revised:
 *				 	sd	c	c	 
 *			even:	2	2	3	3	4
 *			odd:	2	3	4	4	4
 *					drop		DROP
 *		    yeilds:
 *			even:	2		3	3	4
 *			odd:	2		3	3	4
 *							KEEP
 *
 *	Case 2:
 *		If the duplicated frame copies the first of the interlaced pair, more
 *		work must be done:
 *
 *		Original (odd pattern):
 *				 	sd		c	 
 *			even:	2	2	2	3	4
 *			odd:	2	3	3	4	4
 *					drop	DROP
 *
 *		    yeilds (bad keep frame):
 *			even:	2		2	3	4
 *			odd:	2		3	3	4
 *						KEEP
 *		Revised:
 *				s	c	sd	c	 
 *			even:	2	2	2	3	4
 *			odd:	2	3	3	4	4
 *						drop
 *		    yeilds:
 *			even:	2	2		3	4
 *			odd:	2	2		3	4
 *					(keep)
 */

static void
yait_ivtc_keep(int d)
{
    Fi *fd, *fp;
    int t;

    fd = Fa[d];
    fp = Fa[d - 1];

    if (fp->op & Y_OP_COPY) {
        /* case 1 */
        fd->op = fp->op;
        fd->drop = TC_FALSE;
        return;
    }

    /* case 2 */
    if (d < 2) {
        /* can't access before Fa[0] */
        /* (unlikely we would see this the first two frames of a film) */
        fd->drop = TC_FALSE;
        return;
    }

    fd->op = fp->op;
    fd->drop = TC_FALSE;

    t = fp->op & Y_OP_PAT;
    fp->op = t | Y_OP_COPY;
    fp = Fa[d - 2];
    fp->op = t | Y_OP_SAVE;
}


/*
 *	yait_ivtc_grps:
 *		For each group missing an interleave pattern, scan backward and forward
 *	for an adjacent pattern.  Consider hard dropped frames as barriers.  If two
 *	different patterns exist, test the pattern against the original r values to find
 *	the best match.  For consecutive (forced) interleave groups, use the previously
 *	found pattern values, until the forward scan value is used, which is then
 *	propagated to the rest of the sequence.  (This avoids an O(n^2) search).
 *
 *		If no pattern can be found, force a drop of a frame in the group.
 *
 *	TODO:
 *		I should really be detecting scene changes as well, and consider them
 *		barriers.
 */

static void
yait_ivtc_grps(void)
{
    Fi *f;
    int pb, pf, fg;
    int p, n;

    /* process by groups of 5 */
    fg = TC_TRUE;
    pb = -1;
    pf = -1;
    for (n = 0; n < Nf; n += 5) {
        f = Fa[n];
        if (f->gf != Y_FORCE_DEINT) {
            fg = TC_TRUE;
            continue;
        }

        if (fg) {
            /* this is the first group of a sequence, scan */
            fg = TC_FALSE;
            pb = yait_scan_bk(n);
            pf = yait_scan_fw(n);
        }

        if (pb < 0 && pf < 0) {
            /* no pattern exists */
            f->gf = Y_FORCE_DROP;
            yait_drop_frame(n);
            continue;
        }

        /* deinterlace the group with one of the given patterns */
        /* if the pattern used is forward, keep it from now on */
        p = yait_ivtc_grp(n, pb, pf);
        if (p < 0) {
            /* no pattern will match */
            f->gf = Y_FORCE_DROP;
            yait_drop_frame(n);
            continue;
        }

        if (p == pf)
            pb = -1;
    }
}


/*
 *	yait_scan_bk:
 */

static int
yait_scan_bk(int n)
{
    Fi *f;
    int i;

    for (i = n - 1; i >= 0; --i) {
        f = Fa[i];
        if (!f)
            return (-1);

        if (f->drop)
            return (-1);

        if (f->ip != -1)
            return (f->ip);
    }

    return (-1);
}


/*
 *	yait_scan_fw:
 */

static int
yait_scan_fw(int n)
{
    Fi *f;
    int i;

    for (i = n + 5; i < Nf; i++) {
        f = Fa[i];

        if (f->drop)
            return (-1);

        if (f->ip != -1)
            return (f->ip);
    }

    return (-1);
}


/*
 *	yait_drop_frame:
 *		Choose a frame to drop.  We want the frame with the highest fabs(r) value,
 *	as it is likely an interlaced frame.  Do not use a frame which follows an assigned
 *	ip pattern, (it is the trailing element of a tuplet).  If no r values exceed the
 *	threshold, choose the frame with the minimum delta.
 *
 *		Frame:	0   1   2   3   4   |   5   6   7   8   9  
 *		Ratio:	0   0   0  -1   0   |	1   0   0   0   0
 *		Op:		   sd	c   |
 *				      group boundary
 *
 *	In the above example, the first frame of the second group (5) may have the highest
 *	ratio value, but is the worst choice because it is part of the detected pattern and
 *	is a unique progressive frame.
 */

static void
yait_drop_frame(int n)
{
    Fi *f;
    double mr, r;
    int md, d;
    int fr, fd;
    int i;

    mr = 0;
    md = 0;
    fr = n;
    fd = n;

    for (i = n; i < n + 5 && i < Nf - 1; i++) {
        if (!i)
            /* can't access before Fa[0] */
            continue;

        if (Fa[i - 1]->drop || Fa[i + 1]->drop)
            /* avoid two consequetive drops */
            continue;

        if (Fa[i - 1]->op & Y_OP_PAT)
            /* trailing tuplet element */
            continue;

        f = Fa[i];

        r = fabs(f->ro);
        if (r > mr) {
            mr = r;
            fr = i;
        }

        d = f->ed + f->od;
        if (!md || d < md) {
            md = d;
            fd = i;
        }
    }

    Fa[(mr > Y_THRESH) ? fr : fd]->drop = TC_TRUE;
}


/*
 *	yait_ivtc_grp:
 *		We need to de-interlace this group.  Given are two potential patterns.
 *	If both are valid, test both and keep the one with the best r value matches.
 *	For the pattern used, mark the group, set the frame ops accordingly, and return
 *	it as the function value.
 */

static int
yait_ivtc_grp(int n, int p1, int p2)
{
    Fi *f;
    double thresh, m1, m2;
    int p, t, i;

    m1 = (p1 < 0) ? -1 : yait_tst_ip(n, p1);
    m2 = (p2 < 0) ? -1 : yait_tst_ip(n, p2);

    /* yait_tst_ip() returns the sum of two ratios */
    /* we want both ratios > Y_MTHRESH */
    thresh = Y_MTHRESH * 2;
    if (m1 < thresh && m2 < thresh)
        /* neither pattern matches, force a drop instead */
        return (-1);

    p = (m1 > m2) ? p1 : p2;

    /* sanity check */
    if (p < 0) {
        f = Fa[n];
        fprintf(stderr,
                "Impossible interlace pattern computed (%d), frame: %d\n",
                p, f->fn);
        exit(1);
    }

    /* we have a pattern, mark group */
    for (i = n; i < n + 5 && i < Nf; i++) {
        f = Fa[i];
        if (f->drop) {
            fprintf(stderr,
                    "De-interlace, horribly confused now, frame: %d.\n",
                    f->fn);
            exit(1);
        }
        f->ip = p;
    }

    f = Fa[n];
    n = f->gi;

    /* sanity check */
    if (Ga[n] != f) {
        fprintf(stderr, "Lost our frame in the group array, frame: %d\n",
                f->fn);
        exit(1);
    }

    t = (p < 10) ? Y_OP_ODD : Y_OP_EVEN;
    for (i = n; i < n + 5 && i < Ng - 1; i++) {
        if (i % 5 == (p + 2) % 5) {
            f = Ga[i];
            f->op = t | Y_OP_SAVE | Y_OP_DROP;

            /* don't overwrite an existing frame drop */
            f = Ga[i + 1];
            if (!(f->op & Y_OP_DROP))
                f->op = t | Y_OP_COPY;

            break;
        }
    }

    return (p);
}


/*
 *	yait_tst_ip:
 */

static double
yait_tst_ip(int n, int p)
{
    double rs, r;
    int s, i;

    s = (p < 10) ? 1 : -1;
    rs = 0;

    n = Fa[n]->gi;
    for (i = n; i < n + 5 && i < Ng - 2; i++) {
        if (i % 5 != (p + 2) % 5)
            continue;

        /* strong pattern would have r[i]<-thresh and r[i+2]>thresh */
        r = s * Ga[i]->ro;
        if (r < 0)
            rs += fabs(r);

        r = s * Ga[i + 2]->ro;
        if (r > 0)
            rs += r;

        break;
    }

    return (rs);
}


/*
 *	yait_deint:
 *		For non 3/2 telecine patterns, we may have let interlaced frames
 *	through.  Tell transcode to de-interlace (blend) these.  This is the case for
 *	any frame having a high ratio with no interlace pattern detected.
 *
 *	TODO:
 *		This was an afterthought.  Perhaps we can avoid a 32detect pass on
 *	the video by performing this, although it is difficult to detect out of
 *	pattern interlace frames solely on row delta information.  Perhaps we should
 *	have built 32detect into the log generation, and added an extra flag field if
 *	we thought the frame was interlaced.  This also would help when trying to
 *	assign ambiguous ip patterns.  Unfortunately, the affect I see when I tell
 *	transcode to de-interlace are horribly encoded frames, as though the bit
 *	rate drops down to nothing.  So, more investigation is required.
 *
 *		Also, sequences of requested frame blending usually indicate a
 *	problem.  The ip pattern was not detected correctly.  This is especially
 *	true when a progressive frame is missing.  For example:
 *
 * 		Normal (odd) case:
 *			0 -1 0 1 0	0 -1 0 1 0
 *			  odd             odd
 *
 * 		Missing frame:
 * 			0 -1 0 1 0	-1 0 1 0
 * 			  odd  even          deint
 *
 *		Because a frame was missing, an even interlace pattern was
 *	erroneously determined, which then causes the last two frames of the
 *	sequence to be blended.  I'm currently stumped here.  I usually examine
 *	the original video and edit the .ops file directly to correct the problem.
 */

static void
yait_deint(void)
{
    Fi *f1, *f2, *f;
    int os, i;

    for (i = 1; i < Ng - 2; i++) {
        f = Ga[i];

        if (f->op & Y_OP_PAT || f->drop)
            /* already being de-interlaced or dropped */
            continue;

        if (fabs(f->r) < Y_FTHRESH)
            /* it isn't interlaced (we think) */
            continue;

        if ((f->ed + f->od) / (double) Md < Y_FWEIGHT)
            /* delta is too weak, interlace is likely not visible */
            continue;

        f1 = Ga[i + 1];
        f2 = Ga[i + 2];

        /* kludge: if this is the trailing frame of an ip tuplet, then */
        /* only de-interlace if a high ratio exists within the next two */
        /* frames and are not accounted for */
        if (Ga[i - 1]->op & Y_OP_PAT) {
            if (fabs(f1->r) < Y_THRESH && fabs(f2->r) < Y_THRESH)
                continue;

            if (f1->op & Y_OP_PAT || f2->op & Y_OP_PAT)
                continue;

            /* looks like we made a bad choice for the ip pattern */
            /* too late now, so just blend frames */
        }

        /* set os true if next frame has opposite sign ratio */
        os = (f->r * f1->r < 0) ? TC_TRUE : TC_FALSE;

        /* only reject now if next frame has same sign > thresh */
        if (!os && fabs(f1->r) > Y_THRESH)
            continue;

        /* this frame is interlaced with no operation assigned */
        f->op = Y_OP_DEINT;

        /* if the next frame ratio < thresh, it is similar and */
        /* therefore interlaced as well (probably) */
        if (fabs(f1->r) < Y_FTHRESH)
            if (!(f1->op & Y_OP_PAT) && !f1->drop)
                f1->op = Y_OP_DEINT;

        /* skip next */
        i++;
    }
}


/*
 *	yait_write_ops:
 */

static void
yait_write_ops(void)
{
    Fi *f;

    for (f = Fl; f; f = f->next)
        fprintf(OpsFp, "%d: %s\n", f->fn, yait_write_op(f));
}


/*
 *	yait_write_op:
 */

static char *
yait_write_op(Fi * f)
{
    static char buf[10];
    char *p;
    int op;

    p = buf;
    if (f->drop) {
        *p++ = 'd';
        *p = 0;
        Nd++;
        return (buf);
    }

    op = f->op;
    if (op & Y_OP_ODD)
        *p++ = 'o';
    if (op & Y_OP_EVEN)
        *p++ = 'e';
    if (op & Y_OP_SAVE)
        *p++ = 's';
    if (op & Y_OP_COPY)
        *p++ = 'c';
    if (op & Y_OP_DROP) {
        *p++ = 'd';
        Nd++;
    }
    if (op & Y_OP_DEINT)
        *p++ = '0' + DeintMode;
    *p = 0;

    return (buf);
}


/*
 *	Output debug information to stdout
 */

static void
yait_debug_fi(void)
{
    Fi *f;
    int i;

    i = 0;
    for (f = Fl; f; f = f->next, i++) {
        if (i && !(i % 5))
            printf("\n");

        printf("Frame %6d: e: %8d, o: %8d, r: %7.3f, ro: %7.3f, w: %8.4f, "
               "ip: %2d, gi: %6d, op: %-4s d: %s   %s\n",
               f->fn, f->ed, f->od, f->r, f->ro, f->w, f->ip, f->gi,
               yait_op(f->op), yait_drop(f), yait_grp(f->gf));
    }
}

static char *
yait_op(int op)
{
    static char buf[10];
    char *p;

    p = buf;
    *p = 0;
    if (!op)
        return (buf);

    if (op & Y_OP_ODD)
        *p++ = 'o';
    if (op & Y_OP_EVEN)
        *p++ = 'e';
    if (op & Y_OP_SAVE)
        *p++ = 's';
    if (op & Y_OP_COPY)
        *p++ = 'c';
    if (op & Y_OP_DROP)
        *p++ = 'd';
    if (op & Y_OP_DEINT)
        *p++ = '0' + DeintMode;
    *p = 0;

    return (buf);
}

static char *
yait_drop(Fi * f)
{
    if (f->drop)
        return ("DROP");

    if (f->op & Y_OP_ODD && f->op & Y_OP_DROP)
        return ("odd ");

    if (f->op & Y_OP_EVEN && f->op & Y_OP_DROP)
        return ("even");

    return ("    ");
}

static char *
yait_grp(int flg)
{
    switch (flg) {
    case Y_HAS_DROP:
        return ("has drop");
    case Y_BANK_DROP:
        return ("bank");
    case Y_WITHDRAW_DROP:
        return ("withdraw");
    case Y_BORROW_DROP:
        return ("borrow");
    case Y_RETURN_DROP:
        return ("return");
    case Y_FORCE_DEINT:
        return ("force deint");
    case Y_FORCE_DROP:
        return ("force drop");
    case Y_FORCE_KEEP:
        return ("force keep");
    }
    return ("");
}
