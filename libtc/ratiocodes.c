/*
 * ratiocodes.c -- database for all ratio/codes (asr, sar, dar, frc...)
 *                 used in transcode
 * (C) 2005-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "libtc.h"
#include "ratiocodes.h"

#include <stdlib.h>
#include <math.h>


#define TABLE_LEN(tab) (sizeof((tab))/sizeof((tab)[0]))

/* WARNING: this table MUST BE in frc order */
static const double frc_table[16] = {
    0,
    (24000.0/1001.0),
    24,
    25,
    (30000.0/1001.0),
    30,
    50,
    (2*(30000.0/1001.0)),
    60,
    1,
    5,
    10,
    12,
    15,
    0,
    0
};

/* WARNING: this table MUST BE in asr order */
static const double asr_table[8] = {
    0.0,
    1.0,
    (4.0/3.0),
    (16.0/9.0),
    (221.0/100.0),
    0.0,
    0.0,
    0.0,
};


/* WARNING: this table MUST BE in frc order */
static const TCPair frc_ratios[16] = {
    {     0,    0 },
    { 24000, 1001 },
    { 24000, 1000 },
    { 25000, 1000 },
    { 30000, 1001 },
    { 30000, 1000 },
    { 50000, 1000 },
    { 60000, 1001 },
    { 60000, 1000 },
    /* XXX */
    {  1000, 1000 },
    {  5000, 1000 },
    { 10000, 1000 },
    { 12000, 1000 },
    { 15000, 1000 },
    /* XXX  */
    {     0,    0 },
    {     0,    0 },
};

/* WARNING: this table MUST BE in asr order */
static const TCPair asr_ratios[8] = {
    {   0,   0 },
    {   1,   1 },
    {   4,   3 },
    {  16,   9 },
    { 221, 100 },
    {   0,   0 },
    {   0,   0 },
    {   0,   0 },
    /* 
     * XXX: import/tcprobe.c also claims that
     * asr == 8 and asr == 12 are 4:3.
     * Need further investigation.
     */
};

static const TCPair par_ratios[8] = {
    {    1,    1 },
    {    1,    1 },
    { 1200, 1100 },
    { 1000, 1100 },
    { 1600, 1100 },
    { 4000, 3300 },
    {    1,    1 },
    {    1,    1 }
};

/*************************************************************************/


const char *tc_asr_code_describe(int asr_code)
{
    switch (asr_code) {
      case  1:
        return "encoded @ 1:1";
      case  2:
        return "encoded @ 4:3";
      case  3:
        return "encoded @ 16:9";
      case  4:
        return "encoded @ 2.21:1";
      case  8:
        return "encoded @ 4:3";
      case 12:
        return "encoded @ 4:3";
    }
    return "encoded @ UNKNOWN";
}


#define DELTA 0.0005
static int tc_guess_code_from_value(const double *pairs, size_t len,
                                    int *code, double val)
{
    int idx = TC_NULL_MATCH, i = 0;
    double mindiff = DELTA;

    for (i = 0; i < len; i++) {
        double diff = fabs(pairs[i] - val);
        if (diff < mindiff) {
            mindiff = diff;
            idx = i;
        }
    }
    if (code != NULL && idx != TC_NULL_MATCH) {
        *code = idx;
    }
    return idx;
}
#undef DELTA


int tc_asr_code_from_value(int *asr_code, double ratio)
{
    return tc_guess_code_from_value(asr_table, TABLE_LEN(frc_table),
                                    asr_code, ratio);
}


int tc_frc_code_from_value(int *frc_code, double fps)
{
    return tc_guess_code_from_value(frc_table, TABLE_LEN(frc_table),
                                    frc_code, fps);
}


int tc_frc_code_to_value(int frc_code, double *fps)
{
    if ((fps != NULL && frc_code >= 0)
      && frc_code <= TABLE_LEN(frc_table)) {
        *fps = frc_table[frc_code];
        return 0;
    }
    return TC_NULL_MATCH;
}

/*
 * match_ratio:
 *      helper for various detection functions. Scans a ratio
 *      table (that MUST be in frc order) looking for corrispondences
 *      between a ratio and a ratio code.
 *
 * Parameters:
 *     pairs: pointer to an array of TCPair to scan
 *       len: number of pairs to consider
 *         n: numerator of ratio to look for. Use TC_NULL_MATCH
 *            if this function must look for a corrispondency of code
 *            and not for a corrispondency of ratio.
 *         d: denominator of ratio to look for. Use TC_NULL_MATCH
 *            if this function must look for a corrispondency of code
 *            and not for a corrispondency of ratio.
 *      code: code of ratio to look for. Use TC_NULL_MATCH if this function
 *            must look for a corrispondency of ratio.
 * Return Value:
 *     TC_NULL_MATCH if input parameter(s) isn't known.
 *     >= 0 index in table of given corrispondency.
 * Precondintions:
 *     given pairs table MUST BE in code (frc, asr) order.
 *     pairs != NULL.
 */
static int match_ratio(const TCPair *pairs, size_t len,
                       int n, int d, int code)
{
    int i = 0, r = TC_NULL_MATCH;
    for (i = 0; i < len; i++) {
        if (i == code) {
            r = i;
            break;
        }
        if (n == pairs[i].a && d == pairs[i].b) {
            r = i;
            break;
        }
    }
    return r;
}

static int select_table(TCRatioCode rc, const TCPair **table, size_t *len)
{
    int ret = 0;

    switch (rc) {
      case TC_FRC_CODE:
        *table = frc_ratios;
        *len = TABLE_LEN(frc_ratios);
        break;
      case TC_ASR_CODE:
        *table = asr_ratios;
        *len = TABLE_LEN(asr_ratios);
        break;
      case TC_PAR_CODE:
        *table = par_ratios;
        *len = TABLE_LEN(par_ratios);
        break;
      default:
        *table = NULL;
        *len = 0;
        ret = TC_NULL_MATCH;
    }
    return ret;
}

int tc_code_from_ratio(TCRatioCode rc, int *out_code, int in_n, int in_d)
{
    int code = TC_NULL_MATCH;
    const TCPair *table = NULL;
    size_t len = 0;

    select_table(rc, &table, &len);
    if (table != NULL) {
        code = match_ratio(table, len, in_n, in_d, TC_NULL_MATCH);
        if (out_code != NULL && code != TC_NULL_MATCH) {
            *out_code = code;
        }
    }
    return code;
}


int tc_code_to_ratio(TCRatioCode rc, int in_code, int *out_n, int *out_d)
{
    int code = TC_NULL_MATCH;
    const TCPair *table = NULL;
    size_t len = 0;

    select_table(rc, &table, &len);
    if (table != NULL) {
        code = match_ratio(table, len,
                           TC_NULL_MATCH, TC_NULL_MATCH, in_code);
        if ((out_n != NULL && out_d != NULL) && code != TC_NULL_MATCH) {
            *out_n = table[code].a;
            *out_d = table[code].b;
        }
    }
    return code;
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
