/*
 *  decode_mpeg2.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Copyright (C) 1999-2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 *  This file is part of transcode, a video stream  processing tool
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
 *
 */

#include "tccore/tcinfo.h"

#include "src/transcode.h"
#include "libtc/libtc.h"

#include "ioaux.h"
#include "tc.h"

#if defined(HAVE_LIBMPEG2) && defined(HAVE_LIBMPEG2CONVERT)

#include <mpeg2dec/mpeg2.h>
#include <mpeg2dec/mpeg2convert.h>

#define BUFFER_SIZE 262144
static uint8_t buffer[BUFFER_SIZE];

/* ------------------------------------------------------------
 * helper functions
 * ------------------------------------------------------------*/

typedef void (*WriteDataFn)(decode_t *decode, const mpeg2_info_t *info,
                            const mpeg2_sequence_t *sequence);

static void show_accel(uint32_t mp_ac)
{
    tc_log_info(__FILE__, "libmpeg2 acceleration: %s",
                (mp_ac & MPEG2_ACCEL_X86_3DNOW)  ? "3dnow" :
                (mp_ac & MPEG2_ACCEL_X86_MMXEXT) ? "mmxext" :
                (mp_ac & MPEG2_ACCEL_X86_MMX)    ? "mmx" :
                                                   "none (plain C)");
}

static uint32_t conv_accel(int ac)
{
    uint32_t mp_ac = 0;
    if (ac == AC_ALL) {
        mp_ac = MPEG2_ACCEL_DETECT;
    } else {
        if (ac & AC_MMX)
            mp_ac |= MPEG2_ACCEL_X86_MMX;
        if (ac & AC_MMXEXT)
            mp_ac |= MPEG2_ACCEL_X86_MMXEXT;
        if (ac & AC_3DNOW)
            mp_ac |= MPEG2_ACCEL_X86_3DNOW;
    }
    return mp_ac;
}

#define WRITE_DATA(PBUF, LEN, TAG) do { \
    int ret = tc_pwrite(decode->fd_out, PBUF, LEN); \
    if(LEN != ret) { \
        tc_log_error(__FILE__, "failed to write %s data" \
                               " of frame (len=%i)", \
                               TAG, ret); \
        import_exit(1); \
    } \
} while (0)


static void write_rgb24(decode_t *decode, const mpeg2_info_t *info,
                        const mpeg2_sequence_t *sequence)
{
    int len = 0;
    /* FIXME: move to libtc/tcframes routines? */

    len = 3 * info->sequence->width * info->sequence->height;
    WRITE_DATA(info->display_fbuf->buf[0], len, "RGB"); 
}

static void write_yuv420p(decode_t *decode, const mpeg2_info_t *info,
                          const mpeg2_sequence_t *sequence)
{
    static const char *plane_id[] = { "Y", "U", "V" };
    int len = 0;
    /* FIXME: move to libtc/tcframes routines? */

    len = sequence->width * sequence->height;
    WRITE_DATA(info->display_fbuf->buf[0], len, plane_id[0]);
                
    len = sequence->chroma_width * sequence->chroma_height;
    WRITE_DATA(info->display_fbuf->buf[1], len, plane_id[1]);
    WRITE_DATA(info->display_fbuf->buf[2], len, plane_id[2]);
}


/* ------------------------------------------------------------
 * decoder entry point
 * ------------------------------------------------------------*/

void decode_mpeg2(decode_t *decode)
{
    mpeg2dec_t *decoder = NULL;
    const mpeg2_info_t *info = NULL;
    const mpeg2_sequence_t *sequence = NULL;
    mpeg2_state_t state;
    size_t size = 0;
    uint32_t ac = 0, mp_ac = 0;

    WriteDataFn writer = write_yuv420p;
    if (decode->format == TC_CODEC_RGB24) {
        tc_log_info(__FILE__, "using libmpeg2convert"
                              " RGB24 conversion");
        writer = write_rgb24;
    }

    mp_ac = conv_accel(decode->accel);
    ac = mpeg2_accel(mp_ac);
    show_accel(ac);

    decoder = mpeg2_init();
    if (decoder == NULL) {
        tc_log_error(__FILE__, "Could not allocate a decoder object.");
        import_exit(1);
    }
    info = mpeg2_info(decoder);

    size = (size_t)-1;
    do {
        state = mpeg2_parse(decoder);
        sequence = info->sequence;
        switch (state) {
          case STATE_BUFFER:
            size = tc_pread(decode->fd_in, buffer, BUFFER_SIZE);
            mpeg2_buffer(decoder, buffer, buffer + size);
            break;
          case STATE_SEQUENCE:
            if (decode->format == TC_CODEC_RGB24) {
                mpeg2_convert(decoder, mpeg2convert_rgb24, NULL);
            }
            break;
          case STATE_SLICE:
          case STATE_END:
          case STATE_INVALID_END:
            if (info->display_fbuf) {
                writer(decode, info, sequence);
            }
            break;
          default:
            /* can't happen */
            break;
        }
    } while (size);

    mpeg2_close(decoder);
    import_exit(0);
}

#else /* defined(HAVE_LIBMPEG2) && defined(HAVE_LIBMPEG2CONVERT) */

void decode_mpeg2(decode_t *decode)
{
    tc_log_error(__FILE__, "No support for MPEG2 configured -- exiting");
    import_exit(1);
}


#endif /* defined(HAVE_LIBMPEG2) && defined(HAVE_LIBMPEG2CONVERT) */


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
