/*
 * tcaudio.c - audio processing library for transcode
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "tcaudio.h"

#include "libtc/libtc.h"

#include <math.h>

/*************************************************************************/

/* Internal data structure to hold various state information.  The
 * TCAHandle returned by tca_init() and passed by the caller to other
 * functions is a pointer to this structure. */

struct tcahandle_ {
    AudioFormat format;            /* Sample format */
    int bits, issigned, msbfirst;  /* Information about sample format */
};

/*************************************************************************/

/* Internal-use functions (defined at the bottom of the file). */

static int tca_get_format_info(AudioFormat format, int *bits_ret,
                               int *issigned_ret, int *msbfirst_ret);
static int tca_convert(const char *funcname, TCAHandle handle, void *buf,
                       int len, AudioFormat srcfmt, AudioFormat destfmt);

/*************************************************************************/
/*************************************************************************/

/* External interface functions. */

/*************************************************************************/

/**
 * tca_init:  Create and return a handle for use in other tcaudio
 * functions.  The handle should be freed with tca_free() when no longer
 * needed.
 *
 * Parameters: format: Audio sample format to use with tcaudio functions.
 * Return value: A handle to be passed to other tcaudio functions, or 0 on
 *               error.
 * Preconditions: None.
 * Postconditions: None.
 */

TCAHandle tca_init(AudioFormat format)
{
    TCAHandle handle;
    int bits, issigned, msbfirst;

    if (!tca_get_format_info(format, &bits, &issigned, &msbfirst)) {
        tc_log_error("libtcaudio", "tca_init: invalid audio format (%d)!",
                     format);
        return NULL;
    }
    handle = malloc(sizeof(*handle));
    if (!handle)
        return NULL;
    handle->format   = format;
    handle->bits     = bits;
    handle->issigned = issigned;
    handle->msbfirst = msbfirst;
    return handle;
}

/*************************************************************************/

/**
 * tca_free:  Free resources allocated for the given handle.
 *
 * Parameters: handle: tcaudio handle.
 * Return value: None.
 * Preconditions: handle != 0: handle was returned by tca_init()
 * Postconditions: None.
 */

void tca_free(TCAHandle handle)
{
    free(handle);
}

/*************************************************************************/

/**
 * tca_convert_from:  Convert the given audio buffer from another sample
 * format to the format given in tca_init().
 *
 * Parameters: handle: tcaudio handle.
 *                buf: Audio data buffer.
 *                len: Audio data length, in samples.
 *             srcfmt: Format of source audio samples.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tca_init()
 * Postconditions: None.
 */

int tca_convert_from(TCAHandle handle, void *buf, int len, AudioFormat srcfmt)
{
    return tca_convert("tca_convert_from", handle, buf, len, srcfmt,
                       handle->format);
}

/*************************************************************************/

/**
 * tca_convert_to:  Convert the given audio buffer from the format given in
 * tca_init() to another sample format.
 *
 * Parameters:  handle: tcaudio handle.
 *                 buf: Audio data buffer.
 *                 len: Audio data length, in samples.
 *             destfmt: Format to convert audio samples into.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tca_init()
 * Postconditions: None.
 */

int tca_convert_to(TCAHandle handle, void *buf, int len, AudioFormat destfmt)
{
    return tca_convert("tca_convert_to", handle, buf, len, handle->format,
                       destfmt);
}

/*************************************************************************/

/**
 * tca_amplify:  Amplify the given audio buffer by the given scale factor.
 * When increasing amplitude (scale factor greater than one), samples are
 * automatically clipped to the sample format's amplitude range; if
 * `nclip_ret' is not NULL, the number of clipped samples is stored there
 * (unmodified on error).
 *
 * Parameters:    handle: tcaudio handle.
 *                   buf: Audio data buffer.
 *                   len: Audio data length, in samples.
 *                 scale: Factor by which to scale audio data.
 *             nclip_ret: Variable to store number of clipped samples in,
 *                        or NULL if this value is not required.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tca_init()
 * Postconditions: None.
 */

int tca_amplify(TCAHandle handle, void *buf, int len, double scale,
                int *nclip_ret)
{
    int nclip;

    if (!handle || !buf || len < 0) {
        tc_log_error("libtcaudio", "tca_amplify: invalid parameters!");
        return 0;
    }
    nclip = 0;
    if (handle->bits == 8) {
        uint8_t *ptr = buf;
        int bias = handle->issigned ? 0 : 0x80;
        int i;
        for (i = 0; i < len; i++) {
            int32_t v = floor(((ptr[i]-bias) * scale) + 0.5);
            if (v > 0x7F) {
                v = 0x7F;
                nclip++;
            }
            if (v < -0x80) {
                v = -0x80;
                nclip++;
            }
            ptr[i] = v;
        }
    } else if (handle->bits == 16) {
        int8_t *ptr1 = buf + (handle->msbfirst ? 0 : 1);
        uint8_t *ptr2 = buf + (handle->msbfirst ? 1 : 0);
        int bias = handle->issigned ? 0 : 0x8000;
        int i;
        for (i = 0; i < len; i++) {
            int32_t v =
                floor((((ptr1[i*2]<<8 | ptr2[i*2]) - bias) * scale) + 0.5);
            if (v > 0x7FFF) {
                v = 0x7FFF;
                nclip++;
            }
            if (v < -0x8000) {
                v = -0x8000;
                nclip++;
            }
            ptr1[i*2] = v >> 8;
            ptr2[i*2] = v & 0xFF;
        }
    } else {
        tc_log_error("libtcaudio", "tca_amplify: %d-bit samples not supported",
                     handle->bits);
        return 0;
    }
    if (nclip_ret)
        *nclip_ret = nclip;
    return 1;
}

/*************************************************************************/

/**
 * tca_mono_to_stereo:  Convert monaural audio data to stereo by
 * duplicating the data into both stereo channels.
 *
 * Parameters: handle: tcaudio handle.
 *                buf: Audio data buffer.
 *                len: Audio data length, in stereo samples.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tca_init()
 * Postconditions: None.
 */

int tca_mono_to_stereo(TCAHandle handle, void *buf, int len)
{

    if (!handle || !buf || len < 0) {
        tc_log_error("libtcaudio", "tca_mono_to_stereo: invalid parameters!");
        return 0;
    }
    /* Be careful--we have to go backwards or we overwrite unconverted data */
    if (handle->bits == 8) {
        uint8_t *ptr = buf;
        int i;
        for (i = len-1; i >= 0; i--) {
            ptr[i*2] = ptr[i];
            ptr[i*2+1] = ptr[i];
        }
    } else if (handle->bits == 16) {
        uint16_t *ptr = buf;
        int i;
        for (i = len-1; i >= 0; i--) {
            ptr[i*2] = ptr[i];
            ptr[i*2+1] = ptr[i];
        }
    } else {
        tc_log_error("libtcaudio", "tca_mono_to_stereo: %d-bit samples not"
                     " supported", handle->bits);
        return 0;
    }
    return 1;
}

/*************************************************************************/

/**
 * tca_stereo_to_mono:  Convert stereo audio data to monaural by mixing the
 * two stereo channels.
 *
 * Parameters: handle: tcaudio handle.
 *                buf: Audio data buffer.
 *                len: Audio data length, in stereo samples.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tca_init()
 * Postconditions: None.
 */

int tca_stereo_to_mono(TCAHandle handle, void *buf, int len)
{
    if (!handle || !buf || len < 0) {
        tc_log_error("libtcaudio", "tca_stereo_to_mono: invalid parameters!");
        return 0;
    }
    if (handle->bits == 8) {
        uint8_t *ptr = buf;
        int i;
        for (i = 0; i < len; i++) {
            ptr[i] = ((int)ptr[i*2] + (int)ptr[i*2+1] + 1) / 2;
        }
    } else if (handle->bits == 16) {
        int8_t *ptr1 = buf + (handle->msbfirst ? 0 : 1);
        uint8_t *ptr2 = buf + (handle->msbfirst ? 1 : 0);
        int i;
        for (i = 0; i < len; i++) {
            int32_t v = ((ptr1[i*4]<<8 | ptr2[i*4])
                         + (ptr1[i*4+2]<<8 | ptr2[i*4+2])
                         + 1
                        ) / 2;
            ptr1[i*2] = v >> 8;
            ptr2[i*2] = v & 0xFF;
        }
    } else {
        tc_log_error("libtcaudio", "tca_stereo_to_mono: %d-bit samples not"
                     " supported", handle->bits);
        return 0;
    }
    return 1;
}

/*************************************************************************/
/*************************************************************************/

/* Internal-use helper functions. */

/*************************************************************************/

/**
 * tca_get_format_info:  Return information about the given audio format.
 *
 * Parameters:       format: Audio format to get information about.
 *                 bits_ret: Set to the number of bits in a sample.
 *             issigned_ret: Set to 1 if samples are signed, else 0.
 *             msbfirst_ret: Set to 1 if samples have the most significant
 *                           byte stored first, else 0.
 * Return value: Nonzero if the format is recognized, zero otherwise.
 * Preconditions: bits_ret != NULL
 *                issigned_ret != NULL
 *                msbfirst_ret != NULL
 * Postconditions: None.
 */

static int tca_get_format_info(AudioFormat format, int *bits_ret,
                               int *issigned_ret, int *msbfirst_ret)
{
    switch (format) {
      case TCA_S8:
        *bits_ret = 8;
        *issigned_ret = 1;
        *msbfirst_ret = 0;
        return 1;
      case TCA_U8:
        *bits_ret = 8;
        *issigned_ret = 0;
        *msbfirst_ret = 0;
        return 1;
      case TCA_S16BE:
        *bits_ret = 16;
        *issigned_ret = 1;
        *msbfirst_ret = 1;
        return 1;
      case TCA_S16LE:
        *bits_ret = 16;
        *issigned_ret = 1;
        *msbfirst_ret = 0;
        return 1;
      case TCA_U16BE:
        *bits_ret = 16;
        *issigned_ret = 0;
        *msbfirst_ret = 1;
        return 1;
      case TCA_U16LE:
        *bits_ret = 16;
        *issigned_ret = 0;
        *msbfirst_ret = 0;
        return 1;
    }
    return 0;
}

/*************************************************************************/

/**
 * tca_convert:  Convert from one audio sample format to another.
 * Implements tca_convert_from() and tca_convert_to().
 *
 * Parameters:  handle: tcaudio handle.
 *                 buf: Audio data buffer.
 *                 len: Audio data length, in samples.
 *              srcfmt: Format of source audio samples.
 *             destfmt: Format to convert audio samples into.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tca_init()
 * Postconditions: None.
 */

static int tca_convert(const char *funcname, TCAHandle handle, void *buf,
                       int len, AudioFormat srcfmt, AudioFormat destfmt)
{
    int src_bits = -1, src_issigned = -1, src_msbfirst = -1,
        dest_bits = -1, dest_issigned = -1, dest_msbfirst = -1;

    /* Parameter checks */
    if (!handle || !buf || len < 0
     || !tca_get_format_info(srcfmt,&src_bits,&src_issigned,&src_msbfirst)
     || !tca_get_format_info(destfmt,&dest_bits,&dest_issigned,&dest_msbfirst)
    ) {
        tc_log_error("libtcaudio", "%s: invalid parameters!", funcname);
        return 0;
    }

    /* Convert sample sizes and byte orders */
    if (src_bits == 8 && dest_bits == 16) {
        /* 8 bit -> 16 bit */
        const uint8_t *src8 = buf;
        uint8_t *dest8 = buf + (dest_msbfirst ? 0 : 1);
        int i;
        /* Go backwards so we don't overwrite unconverted data */
        for (i = len-1; i >= 0; i--)
            dest8[i*2] = src8[i];
    } else if (src_bits == 16 && dest_bits == 8) {
        /* 16 bit -> 8 bit */
        const uint8_t *src8 = buf + (src_msbfirst ? 0 : 1);
        uint8_t *dest8 = buf;
        int i;
        for (i = 0; i < len; i++)
            dest8[i] = src8[i*2];
    } else if (src_msbfirst != dest_msbfirst) {
        /* 16 bit -> 16 bit, endian swap */
        uint16_t *buf16 = buf;
        int i;
        for (i = 0; i < len; i++)
            buf16[i] = buf16[i]<<8 | buf16[i]>>8;
    } else {
        /* N bit -> N bit, no change */
    }

    /* Convert signed/unsigned */
    if (src_issigned != dest_issigned) {
        int sampsize = dest_bits / 8;
        uint8_t *buf8 = buf + (dest_msbfirst ? 0 : sampsize - 1);
        int i;
        for (i = 0; i < len * sampsize; i += sampsize)
            buf8[i] ^= 0x80;
    }

    /* All done */
    return 1;
}

/*************************************************************************/
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
