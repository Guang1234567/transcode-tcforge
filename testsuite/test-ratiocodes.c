/*
 * test-ratiocodes.c -- testsuite for to/from ratio utility conversion
 *                      functions. Everyone feel free to add more tests
 *                      and improve existing ones.
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

#include "tccore/tc_defaults.h"
#include "libtc/libtc.h"
#include "libtc/tcframes.h"
#include "libtc/ratiocodes.h"

#ifndef PACKAGE
#define PACKAGE __FILE__
#endif

#define DELTA (0.0005)

static int test_autoloop_from_fps(double fps)
{
    int ret = 0, frc;
    double myfps;

    ret = tc_frc_code_from_value(&frc, fps);
    if (ret == TC_NULL_MATCH) {
        tc_log_warn(PACKAGE, "from_fps: failed conversion_from for fps=%f",
                             fps);
        return 1;
    }

    ret = tc_frc_code_to_value(frc, &myfps);
    if (ret == TC_NULL_MATCH) {
        tc_log_warn(PACKAGE, "from_fps: failed conversion_to for fps=%f",
                             fps);
        return 2;
    }

    if (myfps - DELTA < fps && fps < myfps + DELTA) {
        tc_log_msg(PACKAGE, "from_fps: test for fps=%f -> OK",
                             fps);
    } else {
        tc_log_warn(PACKAGE, "from_fps: test for fps=%f -> FAILED (%f)",
                             fps, myfps);
        ret = -1;
    }
    return ret;
}

static int test_autoloop_to_fps(int frc)
{
    int ret = 0, myfrc;
    double fps;

    ret = tc_frc_code_to_value(frc, &fps);
    if (ret == TC_NULL_MATCH) {
        tc_log_warn(PACKAGE, "to_fps: failed conversion_to for frc=%i",
                             frc);
        return 1;
    }

    ret = tc_frc_code_from_value(&myfrc, fps);
    if (ret == TC_NULL_MATCH) {
        tc_log_warn(PACKAGE, "to_fps: failed conversion_from for frc=%i",
                             frc);
        return 2;
    }

    if (frc == myfrc) {
        tc_log_msg(PACKAGE, "to_fps: test for frc=%i -> OK", frc);
    } else {
        tc_log_warn(PACKAGE, "to_fps: test for frc=%i -> FAILED (%i)",
                             frc, myfrc);
        ret = -1;
    }
    return ret;
}

static int test_autoloop_to_ratio(int dom, int code)
{
    int ret = 0, mycode;
    TCPair pair;

    ret = tc_code_to_ratio(dom, code, &pair.a, &pair.b);
    if (ret == TC_NULL_MATCH) {
        tc_log_warn(PACKAGE, "to_ratio: failed conversion_to for "
                             "code=%i (dom=%i)", code, dom);
        return 1;
    }

    ret = tc_code_from_ratio(dom, &mycode, pair.a, pair.b);
    if (ret == TC_NULL_MATCH) {
        tc_log_warn(PACKAGE, "to_ratio: failed conversion_from for "
                             "code=%i (dom=%i)", code, dom);
        return 2;
    }

    if (code == mycode) {
        tc_log_msg(PACKAGE, "to_ratio: test for code=%i (dom=%i) -> OK",
                             code, dom);
    } else {
        tc_log_warn(PACKAGE, "to_ratio: test for code=%i (dom=%i) -> FAILED"
                             " (%i)", code, dom, mycode);
        ret = -1;
    }
    return ret;
}

static int test_autoloop_from_ratio(int dom, TCPair pair)
{
    int ret = 0, code;
    TCPair mypair;

    ret = tc_code_from_ratio(dom, &code, pair.a, pair.b);
    if (ret == TC_NULL_MATCH) {
        tc_log_warn(PACKAGE, "from_ratio: failed conversion_from for "
                             "ratio=%i/%i (dom=%i)", pair.a, pair.b, dom);
        return 2;
    }

    ret = tc_code_to_ratio(dom, code, &mypair.a, &mypair.b);
    if (ret == TC_NULL_MATCH) {
        tc_log_warn(PACKAGE, "from_ratio: failed conversion_to for "
                             "ratio=%i/%i (dom=%i)", pair.a, pair.b, dom);
        return 1;
    }

    if (pair.a == mypair.a && pair.b == mypair.b) {
        tc_log_msg(PACKAGE, "from_ratio: test for ratio=%i/%i (dom=%i)"
                             " -> OK", pair.a, pair.b, dom);
    } else {
        tc_log_warn(PACKAGE, "from_ratio: test for ratio=%i/%i (dom=%i)"
                             " -> FAILED (%i/%i)", pair.a, pair.b, dom,
                             mypair.a, mypair.b);
        ret = -1;
    }
    return ret;
}


struct p_struct {
    int code;
    TCPair ratio;
};

/* intentionally random order */
static const struct p_struct frc_ratios[] = {
    {  0, { 0    ,    0 } },
    { 10, {  5000, 1000 } },
    {  4, { 30000, 1001 } },
    {  2, { 24000, 1000 } },
    {  3, { 25000, 1000 } },
    {  9, {  1000, 1000 } },
    {  6, { 50000, 1000 } },
    { 11, { 10000, 1000 } },
    {  5, { 30000, 1000 } },
    {  8, { 60000, 1000 } },
    {  1, { 24000, 1001 } },
    { 12, { 12000, 1000 } },
    {  7, { 60000, 1001 } },
    { 13, { 15000, 1000 } },
};

/* intentionally random order */
static const struct p_struct asr_ratios[] = {
    { 2, {   4,   3 } },
    { 3, {  16,   9 } },
    { 1, {   1,   1 } },
    { 0, {   0,   0 } },
    { 4, { 221, 100 } },
};

/* intentionally random order */
static const struct p_struct par_ratios[] = {
    { 3, { 1000, 1100 } },
    { 0, {    1,    1 } },
    { 5, { 4000, 3300 } },
    { 4, { 1600, 1100 } },
    { 2, { 1200, 1100 } },
};


struct fr_struct {
    int frc;
    double fps;
};

/*
 * testing frc/fps pairs, picked not-so-randomly, but
 * intentionally left in random order here
 */
static const struct fr_struct fps_pairs[] = {
    { 7, (2*NTSC_VIDEO) },
    { 8, 60.0 },
    { 1, NTSC_FILM },
    { 4, NTSC_VIDEO },
    { 0, 0.0 },
    { 13, 15 },
    { 3, 25.0 },
//    { 15, 0 },
//    known issue: aliasing isn't handled properly
};


#define TABLE_LEN(tab) (sizeof((tab))/sizeof((tab)[0]))

int main(int argc, char *argv[])
{
    int i = 0;

    libtc_init(&argc, &argv);

    tc_log_info(PACKAGE, "testing frc <=> fps ...");
    for (i = 0; i < TABLE_LEN(fps_pairs); i++) {
        test_autoloop_from_fps(fps_pairs[i].fps);
        test_autoloop_to_fps(fps_pairs[i].frc);
    }

    tc_log_info(PACKAGE, "testing frc <=> ratio ...");
    for (i = 0; i < TABLE_LEN(frc_ratios); i++) {
        test_autoloop_from_ratio(TC_FRC_CODE, frc_ratios[i].ratio);
        test_autoloop_to_ratio(TC_FRC_CODE, frc_ratios[i].code);
    }

    tc_log_info(PACKAGE, "testing asr <=> ratio ...");
    for (i = 0; i < TABLE_LEN(asr_ratios); i++) {
        test_autoloop_from_ratio(TC_ASR_CODE, asr_ratios[i].ratio);
        test_autoloop_to_ratio(TC_ASR_CODE, asr_ratios[i].code);
    }

    tc_log_info(PACKAGE, "testing par <=> ratio ...");
    for (i = 0; i < TABLE_LEN(par_ratios); i++) {
        test_autoloop_from_ratio(TC_PAR_CODE, par_ratios[i].ratio);
        test_autoloop_to_ratio(TC_PAR_CODE, par_ratios[i].code);
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
