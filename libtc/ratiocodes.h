/*
 * ratiocodes.h -- database for all ratio/codes (asr, sar, dar, frc...)
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


#ifndef RATIOCODES_H
#define RATIOCODES_H

/* 
 * same ratio codes (i.e.: code=3) have different meaning in
 * different contexts, so we have this enum to let the
 * tc_code_{from,to}_ratio functions distinguish operational
 * context.
 */
typedef enum tccoderatio_ TCRatioCode;
enum tccoderatio_ {
    TC_FRC_CODE = 1, /* frame ratio */
    TC_ASR_CODE,     /* (display?) aspect ratio */
    TC_PAR_CODE,     /* pixel aspect ratio */
};

typedef struct tcpair_ TCPair;
struct tcpair_ {
    int a; /* numerator, width... */
    int b; /* denominator, height... */
};

#define TC_FRC_FPS_TO_RATIO(fps, pn, pd) \
do { \
    *(pn) = (int)((fps) * 1000.0); \
    *(pd) = 1000; \
} while(0)

/*
 * TCPair VS int/int dilemma:
 *
 * Why don't use out TCPair instead of int/int couples in
 * functions interface below?
 * this code (and the whole transcode in general) need to interact
 * with a broad variety of foreign code with various conventions,
 * interface, styles and so on. I've no *YET* found a clean way
 * to use a custom struct (TCPair) for those situations without
 * requiring to use a temporary variable, that looks quite
 * clumsy to me. So I've chosen to fall back to minimum
 * common denominator, that is a couple of int or pointer
 * to ints.
 *
 * Of course this can change if a better and cleanest solution
 * pops out :)
 *
 * BTW, TCPair is extensively used in internal processing and
 * in private code.
 */

/*************************************************************************/

/*
 * tc_asr_code_describe:
 *    provides a descriptive, human readable string of given ASR code.
 *
 * Parameters:
 *    asr_code: the ASR code.
 * Return Value:
 *    A constant string describing the given ASR code.
 *    There is NO NEED (and it is WRONG to do so) to free() it.
 *    Returned string is guaranteed to be valid at least until
 *    the next call to this function.
 */
const char *tc_asr_code_describe(int asr_code);

/*
 * tc_asr_code_from_value:
 *    detect the right aspect ratio code (asr) given an aspect ratio value
 *    as real number.
 *
 * Parameters:
 *    asr_code: pointer to integer where detected asr code will be stored.
 *              Can be NULL: if so, asr code will be detected but not
 *              stored.
 *       ratio: value of frame rate, as real number.
 * Return Value:
 *    TC_NULL_MATCH if input value isn't known
 *    >= 0 otherwise
 */
int tc_asr_code_from_value(int *asr_code, double ratio);

/*
 * tc_frc_code_from_value:
 *    detect the right frame ratio code (frc) given a frame rate value as
 *    real number.
 *
 * Parameters:
 *    frc_code: pointer to integer where detected frc code will be stored.
 *              Can be NULL: if so, frc code will be detected but not
 *              stored.
 *         fps: value of frame rate, as real number.
 * Return Value:
 *    TC_NULL_MATCH if input value isn't known
 *    >= 0 otherwise
 */
int tc_frc_code_from_value(int *frc_code, double fps);

/*
 * tc_frc_code_to_value:
 *    detect the right frame ratio value as real number given a frame rate
 *    code (frc).
 *
 * Parameters:
 *    frc_code: frame rate code.
 *         fps: pointer to double where detected frc value will be stored.
 *              Can be NULL: if so, frc value will be detected but not
 *              stored.
 * Return Value:
 *    TC_NULL_MATCH if input value isn't known
 *    >= 0 otherwise
 */
int tc_frc_code_to_value(int frc_code, double *fps);

/*
 * tc_code_from_ratio:
 *    detect the right code in a specificied domain given a fraction as
 *    pair of integers.
 *
 * Parameters:
 *        rc: select operational domain. See definition of
 *            TCRatioCode above to see avalaible domains.
 *      code: pointer to integer where detected code will be stored.
 *            Can be NULL: if so, code will be detected but not
 *            stored.
 *         n: numerator of given frame ratio fraction.
 *         d: denominator of given frame ratio fraction.
 * Return Value:
 *    TC_NULL_MATCH if input value isn't known
 *    >= 0 otherwise
 */
int tc_code_from_ratio(TCRatioCode rc, int *out_code, int in_n, int in_d);

/*
 * tc_code_to_ratio:
 *    detect the right ratio fraction in a specified domain as pair of
 *    integers given a ratio code.
 *
 * Parameters:
 *        rc: select operational domain. See definition of
 *            TCRatioCode above to see avalaible domains.
 *	code: code to be converted in fraction.
 *         n: pointer to integer where numerator of rate fraction
 *            will ne stored. Can be NULL: if so, fraction will be
 *            detected but not stored.
 *         d: pointer to integer where denominator of frate fraction
 *            will ne stored. Can be NULL: if so, fraction will be
 *            detected but not stored.
 * Return Value:
 *    TC_NULL_MATCH if input value isn't known
 *    >= 0 otherwise
 */
int tc_code_to_ratio(TCRatioCode rc, int in_code, int *out_n, int *out_d);

/* macro goodies */
#define tc_frc_code_from_ratio(frc, n, d) \
	tc_code_from_ratio(TC_FRC_CODE, frc, n, d)
#define tc_frc_code_to_ratio(frc, n, d) \
	tc_code_to_ratio(TC_FRC_CODE, frc, n, d)

#define tc_asr_code_from_ratio(asr, n, d) \
	tc_code_from_ratio(TC_ASR_CODE, asr, n, d)
#define tc_asr_code_to_ratio(asr, n, d) \
	tc_code_to_ratio(TC_ASR_CODE, asr, n, d)

#define tc_par_code_from_ratio(par, n, d) \
	tc_code_from_ratio(TC_PAR_CODE, par, n, d)
#define tc_par_code_to_ratio(par, n, d) \
	tc_code_to_ratio(TC_PAR_CODE, par, n, d)


#endif  /* RATIOCODES_H */
