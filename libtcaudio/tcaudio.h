/*
 * tcaudio.h - include file for audio processing library for transcode
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef LIBTCAUDIO_TCAUDIO_H
#define LIBTCAUDIO_TCAUDIO_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/*************************************************************************/

/* Handle for calling tcaudio functions, allocated by tca_init() and passed
 * to all other functions to hold internal state information.  Opaque to
 * the caller. */
typedef struct tcahandle_ *TCAHandle;

/* Audio sample formats, used by tca_convert(). */
typedef enum {
    TCA_S8 = 1,
    TCA_U8,
    TCA_S16BE,
    TCA_S16LE,
    TCA_U16BE,
    TCA_U16LE,
} AudioFormat;

typedef enum {
    TCA_S8_MAX    = 0x7F,
    TCA_U8_MAX    = 0xFF,
    TCA_S16BE_MAX = 0x7FFF,
    TCA_S16LE_MAX = 0x7FFF,
    TCA_U16BE_MAX = 0xFFFF,
    TCA_U16LE_MAX = 0xFFFF,
} AudioSampleMax;

/*************************************************************************/

TCAHandle tca_init(AudioFormat format);

void tca_free(TCAHandle handle);

int tca_convert_from(TCAHandle handle, void *buf, int len, AudioFormat srcfmt);

int tca_convert_to(TCAHandle handle, void *buf, int len, AudioFormat destfmt);

int tca_amplify(TCAHandle handle, void *buf, int len, double scale,
                int *nclip_ret);

int tca_mono_to_stereo(TCAHandle handle, void *buf, int len);

int tca_stereo_to_mono(TCAHandle handle, void *buf, int len);

/*************************************************************************/

#endif  /* LIBTCAUDIO_TCAUDIO_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
