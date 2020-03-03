/*
 *  filter_yait.c
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

#define MOD_NAME    "filter_yait.so"
#define MOD_VERSION "v0.1.1 (2007-12-04)"
#define MOD_CAP     "Yet Another Inverse Telecine filter"
#define MOD_AUTHOR  "Allan Snider"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include <stdlib.h>

#include "yait.h"


/*
 *  yait:
 *      Yet Another Inverse Telecine filter.
 *
 *  Usage:
 *      -J yait=log[=file] (-y null)
 *      -J yait=ops[=file]
 *
 *  Description:
 *
 *      This filter is designed specifically to handle mixed progressive and NTSC
 *  telecined data (2:3 pulldown), converting from NTSC_VIDEO (29.97 fps) to NTSC_FILM
 *  (23.976 fps).  It uses row save and copy operations to reconstruct progressive
 *  frames.  It is provided as an alternative to the -J ivtc,32detect,decimate method.
 *
 *      For those who don't care how much cpu is used but are interested only
 *  in trying to achieve the best quality rendering, then read on.  If not, then stop
 *  reading right now, as this filter requires a complete separate pass on the video
 *  stream and an external analysis tool to be run.
 *
 *      The main advantage of using a separate pass is that it provides a much
 *  larger window of frames to examine when deciding what frames need to be dropped
 *  or de-interlaced.  The video stream is read at 30 fps (--hard_fps).  Duplicate
 *  frames are inserted by the demuxer to keep the frame rate at 30 when progressive
 *  data is encountered.  These frames can appear quite early or late, far beyond the
 *  five frame local window used by ivtc, etc.  This approach allows drop frames to
 *  be credited, and debited, (up to a point), making better drop frame choices.  The
 *  result is a (noticeably) smoother video.
 *
 *      Another advantage of using a large frame window is to provide context
 *  for determining interlace patterns.  Local interlace patterns (eg. a 5 frame
 *  window) can sometimes be impossible to determine.  When able to look ahead or
 *  behind for existing patterns, usually the correct pattern can be inherited.
 *
 *      The filter guarantees one drop frame per 5 frames.  No more, no less.
 *
 *  Using the filter:
 *
 *      Pass 1:
 *
 *      -J yait=log -y null
 *
 *      The first pass is used only to generate row (even/odd) delta information.
 *      This is written as a text log file (called yait.log by default).  (The
 *      file name can be specified using yait=log=file).  This is the only data
 *      required by this pass, so no video (or audio) frames need to be encoded.
 *      You do need to specify the demuxer_sync method however.  (It must be 2).
 *
 *      Alternatively (for debug purposes), you may want to generate a frame
 *      labeled video file, then compare the yait analysis to the original video.
 *      In this case use something like:
 *          -H 10 -x vob -i ...
 *          --export_fps 0,4 --demuxer_sync 2 -y xvid4,null -o label.ogm
 *          -J yait=log -J text=frame:posdef=8
 *
 *      Running the tcyait tool:
 *
 *      Pass 1 created a yait.log file.  The analysis tool 'tcyait' is then run
 *      which reads the log file and determines which areas are telecined and
 *      progressive and detects the telecine patterns.  A yait frame operations
 *      file is then written (yait.ops by default).  It is a text file containing
 *      instructions for each frame, such as nop, save even or odd rows, copy rows,
 *      drop frames, or blend a frame.  The usage of the tool is as follows:
 *
 *      tcyait [-d] [-l log] [-o ops] [-m mode]
 *              -d              Print debug information to stdout.
 *              -l log          Specify input yait log file name [yait.log].
 *              -o ops          Specify output yait frame ops file name [yait.ops].
 *              -m mode         Specify transcode de-interlace method [3].
 *
 *      I typically run it as:
 *          tcyait -d > yait.info
 *
 *      One could query why pass 1 doesn't just create the .ops file directly.  That
 *      is, run the analysis at TC_FILTER_CLOSE time and save the user from having
 *      to run the tool directly.  The main reason is that the tcyait code is alpha
 *      and still undergoing a lot of fine tuning.  I would not want to have to
 *      regenerate the row delta information every time I tweaked the analysis
 *      portion.  So, it exists as a separate tool for now.
 *
 *      Pass 2 (and 3):         
 *
 *      -J yait=ops -y ...
 *
 *      The second pass (or third for -R 1 and -R 2), reads the frame operations file
 *      generated by tcyait, (called yait.ops by default).  (The file name can be
 *      specified using yait=ops=file).  This file instructs the filter to save or
 *      copy rows, skip frames, or de-interlace frames, and causes the pre filtering
 *      to reduce the frame rate to 24 fps.  Hence, you must specify the export fps
 *      as 24, or will get truncated audio, (ie. --export_fps 24,1).  The frame
 *      sequence seen by the filter must match exactly what pass 1 saw.  That is, you
 *      cannot specify a frame range or audio track in pass 1, but not pass 2, and
 *      visa versa.  Here is an example invocation:
 *
 *          transcode -H 10 -a 0 -x vob -i ... -w 1800,50 -b 192,0,0 -Z ...
 *              -R 1 -y xvid4,null --demuxer_sync 2 --export_fps 24,1
 *              -J yait=ops --progress_rate 25
 *
 *          transcode -H 10 -a 0 -x vob -i ... -w 1800,50 -b 192,0,0 -Z ...
 *              -R 2 -y xvid4 --demuxer_sync 2 --export_fps 24,1 -o ...
 *              -J yait=ops --progress_rate 25
 *
 *      The import frame rate and --hard_fps flags are forced by the filter and
 *      need not be specified.
 *
 *  DISCLAIMER:
 *
 *      This is a work in progress.  For non-NTSC telecine patterns, PAL, or purely
 *  interlaced material, you are going to get nonsense results.  Best stick to 'ivtc' or
 *  'smartyuv' for those.
 *
 *      For some video, remarkably good results were obtained.  (I was quite pleased and
 *  hence felt obliged to distribute this).  In a few cases I had video constantly switching
 *  frame rates, with single or small grouped telecine, both even and odd patterns, and was
 *  able to reconstruct the original 24 fps progressive film for the entire file, without
 *  blending a single frame.  For others, not so good.  The analysis tool can sometimes
 *  generate a lot of false positives for interlace detection and specify needless frame
 *  blending.  Generally, wherever a frame blend is specified, something went wrong.  I
 *  usually step frame by frame in the original (frame labelled) .ogm and edit/correct
 *  the .ops file manually.
 *
 *      There is much work to be done still (especially documentation), but here it
 *  is, such as it is.
 *                      Allan
 */


/*
 *  Prototypes:
 */

static int yait_get_config(char *opt);
static int yait_init(char *opt);
static int yait_fini(void);
static int yait_process(vframe_list_t * ptr);

static void yait_compare(vframe_list_t * ptr, uint8_t * lv, int fn);
typedef void (*yait_cmp_fn) (uint8_t * lv, uint8_t * cv,
                             int w, int h, int *ed, int *od);
static void yait_cmp_rgb(uint8_t * lv, uint8_t * cv, int w, int h,
                         int *ed, int *od);
static void yait_cmp_yuv(uint8_t * lv, uint8_t * cv, int w, int h,
                         int *ed, int *od);
static int yait_ops(vframe_list_t *);
static int yait_ops_chk(void);
static int yait_ops_get(char *, int, int *);
static int yait_ops_decode(char *, int *);
static void yait_put_rows(uint8_t *, uint8_t *, int, int, int);

/*
 *  Globals:
 */

static FILE *Log_fp;            /* output log file */
static FILE *Ops_fp;            /* input frame ops file */

static uint8_t *Fbuf;           /* video frame buffer */
static int Codec;               /* internal codec */
static int Fn;                  /* frame number */
static yait_cmp_fn cmp_hook;


/*
 *  tc_filter:
 *      YAIT filter main entry point.  Single instance.
 */
int tc_filter(frame_list_t * ptr_, char *opt)
{
    vframe_list_t *ptr = (vframe_list_t *) ptr_;

    if (ptr->tag & TC_AUDIO)
        return TC_OK;

    if (ptr->tag & TC_FILTER_GET_CONFIG)
        return yait_get_config(opt);

    if (ptr->tag & TC_FILTER_INIT)
        return yait_init(opt);

    if (ptr->tag & TC_FILTER_CLOSE)
        return yait_fini();

    if (ptr->tag & TC_PRE_S_PROCESS)
        return yait_process(ptr);

    return TC_OK;
}


/*
 *  yait_get_config:
 */

static int yait_get_config(char *opt)
{
    optstr_filter_desc(opt, MOD_NAME, MOD_CAP,
                       MOD_VERSION, MOD_AUTHOR, "VRYE", "1");
    optstr_param(opt, "log",
                 "Compute and write yait delta log file", "%s", "");
    optstr_param(opt, "ops",
                 "Read and apply yait frame operation file", "%s", "");

    return TC_OK;
}


/*
 *  yait_init:
 */

static int yait_init(char *opt)
{
    vob_t *vob = tc_get_vob();
    char buf[256], *fn = NULL;
    const char *p = NULL;
    int n;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
        tc_log_info(MOD_NAME, "options=%s", opt);
    }

    Codec = vob->im_v_codec;

    /* log file */
    p = optstr_lookup(opt, "log");
    if (p) {
        fn = Y_LOG_FN;
        n = optstr_get(opt, "log", "%[^:]", buf);
        if (n > 0)
            fn = buf;

        Log_fp = fopen(fn, "w");
        if (!Log_fp) {
            tc_log_error(MOD_NAME, "cannot create log file, '%s'", buf);
            return TC_ERROR;
        }
    }

    /* ops file */
    p = optstr_lookup(opt, "ops");
    if (p) {
        fn = Y_OPS_FN;
        n = optstr_get(opt, "ops", "%[^:]", buf);
        if (n > 0)
            fn = buf;

        Ops_fp = fopen(fn, "r");
        if (!Ops_fp) {
            tc_log_error(MOD_NAME, "cannot open yait ops file, '%s'", buf);
            return TC_ERROR;
        }

        if (!yait_ops_chk()) {
            tc_log_error(MOD_NAME, "invalid yait ops file");
            return TC_ERROR;
        }
    }

    if (!Log_fp && !Ops_fp) {
        tc_log_error(MOD_NAME,
                     "at least one operation (log|ops) must be specified");
        return TC_ERROR;
    }

    if (Log_fp && Ops_fp) {
        tc_log_error(MOD_NAME,
                     "only one operation (log|ops) may be specified");
        return TC_ERROR;
    }

    /* common settings */
    vob->hard_fps_flag = TC_TRUE;
    vob->im_frc = 4;
    vob->fps = NTSC_VIDEO;

    if (Log_fp) {
        tc_log_info(MOD_NAME, "Generating YAIT delta log file '%s'", fn);
        tc_log_info(MOD_NAME,
                    "Forcing --hard_fps, -f 30,4, --export_fps 30,4");

        /* try to lock everything in at 30 fps */
        vob->ex_frc = 4;
        vob->ex_fps = NTSC_VIDEO;
    }

    if (Ops_fp) {
        tc_log_info(MOD_NAME, "Applying YAIT frame operations file '%s'",
                    fn);
        tc_log_info(MOD_NAME,
                    "Forcing --hard_fps, -f 30,4, --export_fps 24,1");

        /* try to lock import at 30 fps, export at 24 fps */
        vob->ex_frc = 1;
        vob->ex_fps = NTSC_FILM;
    }

    Fbuf = tc_zalloc(SIZE_RGB_FRAME);
    if (!Fbuf) {
        tc_log_error(MOD_NAME, "cannot allocate frame buffer");
        return TC_ERROR;
    }

    Fn = -1;

    if (Codec == TC_CODEC_RGB24)
        cmp_hook = yait_cmp_rgb;
    else
        cmp_hook = yait_cmp_yuv;

    return TC_OK;
}


/*
 *  yait_fini:
 */

static int yait_fini(void)
{
    if (Log_fp) {
        fclose(Log_fp);
        Log_fp = NULL;
    }
    if (Ops_fp) {
        fclose(Ops_fp);
        Ops_fp = NULL;
    }
    if (Fbuf) {
        tc_free(Fbuf);
        Fbuf = NULL;
    }

    return TC_OK;
}


/*
 *  yait_process:
 */

static int yait_process(vframe_list_t * ptr)
{
    if (Fn == -1) {
        Fn = ptr->id;
        ac_memcpy(Fbuf, ptr->video_buf, ptr->video_size);
    }

    if (ptr->id != Fn) {
        tc_log_error(MOD_NAME, "inconsistent frame numbers");
        yait_fini();
        return TC_ERROR;
    }

    if (Log_fp) {
        yait_compare(ptr, Fbuf, Fn);
        ac_memcpy(Fbuf, ptr->video_buf, ptr->video_size);
    }

    if (Ops_fp)
        if (!yait_ops(ptr)) {
            yait_fini();
            return TC_ERROR;
        }

    Fn++;
    return TC_OK;
}


/*
 *  yait_compare:
 */

static void yait_compare(vframe_list_t * ptr, uint8_t * lv, int fn)
{
    uint8_t *cv = ptr->video_buf;
    int w = ptr->v_width, h = ptr->v_height;
    int ed, od;

    cmp_hook(lv, cv, w, h, &ed, &od);

    fprintf(Log_fp, "%d: e: %d, o: %d\n", fn, ed, od);

    /* BUG: until the blocked tcdecode pipe problem is fixed... */
    if (!(fn % 5))
        fflush(Log_fp);
}


/*
 *  yait_cmp_rgb:
 */

static void yait_cmp_rgb(uint8_t * lv, uint8_t * cv, int w, int h, int *ed, int *od)
{
    uint8_t *lp, *cp;
    int x, y, p;
    int e, o;

    /* even row delta */
    e = 0;
    for (y = 0; y < h; y += 2) {
        p = y * w * 3;
        lp = lv + p;
        cp = cv + p;
        for (x = 0; x < w; x++) {
            e += abs(*lp++ - *cp++);
            e += abs(*lp++ - *cp++);
            e += abs(*lp++ - *cp++);
        }
    }

    /* odd row delta */
    o = 0;
    for (y = 1; y < h; y += 2) {
        p = y * w * 3;
        lp = lv + p;
        cp = cv + p;
        for (x = 0; x < w; x++) {
            o += abs(*lp++ - *cp++);
            o += abs(*lp++ - *cp++);
            o += abs(*lp++ - *cp++);
        }
    }

    *ed = e;
    *od = o;
}


/*
 *  yait_cmp_yuv:
 */

static void yait_cmp_yuv(uint8_t * lv, uint8_t * cv, int w, int h, int *ed, int *od)
{
    uint8_t *lp, *cp;
    int x, y, p;
    int e, o;

    /* even row delta */
    e = 0;
    for (y = 0; y < h; y += 2) {
        /* y */
        p = y * w;
        lp = lv + p;
        cp = cv + p;
        for (x = 0; x < w; x++)
            e += abs(*lp++ - *cp++);

        /* uv */
        p = w * h + y * w / 2;
        lp = lv + p;
        cp = cv + p;
        for (x = 0; x < w / 2; x++)
            e += abs(*lp++ - *cp++);
    }

    /* odd row delta */
    o = 0;
    for (y = 1; y < h; y += 2) {
        /* y */
        p = y * w;
        lp = lv + p;
        cp = cv + p;
        for (x = 0; x < w; x++)
            o += abs(*lp++ - *cp++);

        /* uv */
        p = w * h + y * w / 2;
        lp = lv + p;
        cp = cv + p;
        for (x = 0; x < w / 2; x++)
            o += abs(*lp++ - *cp++);
    }

    *ed = e;
    *od = o;
}


/*
 *  yait_ops:
 */

static int yait_ops(vframe_list_t * ptr)
{
    char buf[256];
    uint8_t *v;
    int mode, op;
    int w, h;

    v = ptr->video_buf;
    w = ptr->v_width;
    h = ptr->v_height;

    if (!fgets(buf, 256, Ops_fp))
	return (TC_FALSE);

    op = yait_ops_get(buf, Fn, &mode);

    if (op < 0)
        return (TC_FALSE);

    if (op & Y_OP_SAVE)
        yait_put_rows(Fbuf, v, w, h, op & Y_OP_PAT);

    if (op & Y_OP_COPY)
        yait_put_rows(v, Fbuf, w, h, op & Y_OP_PAT);

    if (op & Y_OP_DROP)
        ptr->attributes |= TC_FRAME_IS_SKIPPED;

    if (op & Y_OP_DEINT) {
        ptr->attributes |= TC_FRAME_IS_INTERLACED;
        ptr->deinter_flag = mode;
    }

    return (TC_TRUE);
}


/*
 *  yait_ops_chk:
 */

static int yait_ops_chk(void)
{
    char buf[256], *p;
    int fn, op;

    if (fscanf(Ops_fp, "%d:", &fn) != 1)
	return (TC_FALSE);

    rewind(Ops_fp);
    for (;;) {
        p = fgets(buf, 256, Ops_fp);
        if (!p)
            break;

        op = yait_ops_get(buf, fn, NULL);
        if (op < 0)
            return (TC_FALSE);
        fn++;
    }

    rewind(Ops_fp);
    return (TC_TRUE);
}


/*
 *  yait_ops_get:
 */

static int yait_ops_get(char *buf, int fn, int *mode)
{
    char str[256];
    int op, f, n;

    f = -1;
    str[0] = 0;

    n = sscanf(buf, "%d: %s\n", &f, str);
    if (n < 1) {
        if (feof(Ops_fp))
            tc_log_error(MOD_NAME, "truncated yait ops file, frame: %d",
                         fn);
        else
            tc_log_error(MOD_NAME, "invalid yait ops format, frame: %d",
                         fn);
        return TC_ERROR;
    }

    if (f != fn) {
        tc_log_error(MOD_NAME, "invalid yait ops frame number, frame: %d",
                     fn);
        return TC_ERROR;
    }

    op = yait_ops_decode(str, mode);
    if (op < 0) {
        tc_log_error(MOD_NAME, "invalid yait ops code, frame: %d", fn);
        return TC_ERROR;
    }

    return (op);
}


/*
 *  yait_ops_decode:
 */

static int yait_ops_decode(char *str, int *mode)
{
    int op, c;

    op = 0;
    while (*str) {
        c = *str++;
        if (c >= '1' && c <= '5') {
            op |= Y_OP_DEINT;
            if (mode)
                *mode = c - '0';
            continue;
        }

        switch (c) {
        case 'o':
            op |= Y_OP_ODD;
            break;
        case 'e':
            op |= Y_OP_EVEN;
            break;
        case 's':
            op |= Y_OP_SAVE;
            break;
        case 'c':
            op |= Y_OP_COPY;
            break;
        case 'd':
            op |= Y_OP_DROP;
            break;
        default:
            return TC_ERROR;
            break;
        }
    }

    return (op);
}


/*
 *  yait_put_rows:
 */

static void yait_put_rows(uint8_t * dst, uint8_t * src, int w, int h, int flg)
{
    int y, o;

    y = (flg == Y_OP_EVEN) ? 0 : 1;

    if (Codec == TC_CODEC_RGB24) {
        for (; y < h; y += 2) {
            o = y * w * 3;
            ac_memcpy(dst + o, src + o, w * 3);
        }
    }
    else {
        for (; y < h; y += 2) {
            /* y (luminance) */
            o = y * w;
            ac_memcpy(dst + o, src + o, w);

            /* 2 * h/2 blocks (u and v) = h */
            o = w * h + y * w / 2;
            ac_memcpy(dst + o, src + o, w / 2);
        }
    }
}
