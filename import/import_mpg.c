/*
 *  import_mpg.c
 *
 *  Copyright (C) Francesco Romani - 2005-2010
 *
 *  This file would be part of transcode, a linux video stream  processing tool
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <mpeg2dec/mpeg2.h>
#include <mpeg2dec/mpeg2convert.h>

#include "libtcutil/optstr.h"
#include "src/transcode.h"
#include "aclib/ac.h"
#include "mpeglib/mpeglib.h"

#define MOD_NAME    "import_mpg.so"
#define MOD_VERSION "v0.1.5 (2009-12-02)"
#define MOD_CODEC   "(video) MPEG"

extern int tc_accel;

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB | TC_CAP_YUV | TC_CAP_VID;

#define MOD_PRE mpeg
#include "import_def.h"

static int rgb_mode = TC_FALSE; // flag
static mpeg_file_t *MFILE;
static mpeg_t *MPEG;
static mpeg2dec_t *decoder;
static const mpeg2_info_t *info;
static const mpeg2_sequence_t *sequence;
static mpeg2_state_t state;


static void copy_frame(const mpeg2_sequence_t *sequence,
                       const mpeg2_info_t *info,
                       transfer_t *param)
{
    size_t len = 0;
    // Y plane
    len = sequence->width * sequence->height;

    ac_memcpy(param->buffer, info->display_fbuf->buf[0], len);
    param->size = len;
    
    len = sequence->chroma_width * sequence->chroma_height; 
    // U plane
    ac_memcpy(param->buffer + param->size, 
              info->display_fbuf->buf[1], len);
    param->size += len;
    // V plane
    ac_memcpy(param->buffer + param->size, 
              info->display_fbuf->buf[2], len);
    param->size += len;
}

static uint32_t translate_accel(int accel)
{
    uint32_t mpeg_accel = 0; // NO accel
    
    switch(accel) {
        case AC_NONE:
            mpeg_accel = 0;
            break;
        case AC_MMX:
            mpeg_accel |= MPEG2_ACCEL_X86_MMX;
            break;
        case AC_MMXEXT:
            mpeg_accel |= MPEG2_ACCEL_X86_MMXEXT;
            mpeg_accel |= MPEG2_ACCEL_X86_MMX;
            break;
        case AC_3DNOW:
            mpeg_accel |= MPEG2_ACCEL_X86_3DNOW;
            mpeg_accel |= MPEG2_ACCEL_X86_MMX;
            break;
        case AC_ALL: // autodetect, fallthrough
        default:
            mpeg_accel = MPEG2_ACCEL_DETECT;
            break;
    }
    return mpeg_accel;
}

static void show_accel(uint32_t mp_ac)
{
    tc_log_info(__FILE__, "libmpeg2 acceleration: %s",
                (mp_ac & MPEG2_ACCEL_X86_3DNOW)  ? "3dnow" :
                (mp_ac & MPEG2_ACCEL_X86_MMXEXT) ? "mmxext" :
                (mp_ac & MPEG2_ACCEL_X86_MMX)    ? "mmx" :
                                                   "none (plain C)");
}

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
    uint32_t ac = 0, rac = 0; // ac == accel, rac == requested accel

    if(param->flag != TC_VIDEO) { 
        return TC_ERROR;
    }

    rac = translate_accel(tc_get_session()->acceleration);

    if (vob->im_v_codec == TC_CODEC_RGB24) {
        rgb_mode = TRUE;
    }
    
    mpeg_set_logging(mpeg_log_null, stderr);
    
    tc_log_info(MOD_NAME,
                "native MPEG1/2 import module using MPEGlib and libmpeg2");
    
    MFILE = mpeg_file_open(vob->video_in_file, "r");
    if (!MFILE) {
        tc_log_error(MOD_NAME, "unable to open: %s", vob->video_in_file);
        return TC_ERROR;
    }
    
    MPEG = mpeg_open(MPEG_TYPE_ANY, MFILE, MPEG_DEFAULT_FLAGS, NULL);
    if (!MPEG) {
        mpeg_file_close(MFILE);
        tc_log_error(MOD_NAME, "mpeg_open() failed");
        return TC_ERROR;
    }
    
    ac = mpeg2_accel(rac);

    decoder = mpeg2_init();
    if (decoder == NULL) {
        tc_log_error(MOD_NAME,
                     "failed to allocate a MPEG2 decoder object");
        return TC_ERROR;
    }
    info = mpeg2_info(decoder);

    param->fd = NULL;
    if (vob->ts_pid1 != 0) { // we doesn't support transport streams
        tc_log_error(MOD_NAME,
                     "this import module doesn't support TS streams."
                     " Use old import_mpeg2 module instead.");
        return TC_ERROR;
    }
    
    if (vob->verbose) { 
        show_accel(ac);
    }

    return TC_OK;
}

/* ------------------------------------------------------------ 
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

#define READS_MAX   (4096)

MOD_decode
{
    int decoding = TRUE;
    uint32_t reads = 0;
    const mpeg_pkt_t *pes = NULL;

    do {
        state = mpeg2_parse(decoder);
        sequence = info->sequence;
	
        switch (state) {
            case STATE_BUFFER:
                pes = mpeg_read_packet(MPEG, MPEG_STREAM_VIDEO(0));
                if (!pes) {
                    decoding = FALSE;
                    break;
                } else {
                    mpeg2_buffer(decoder, (uint8_t*)pes->data, 
                                 (uint8_t*)(pes->data + pes->size)); // FIXME
                    reads++;
                    if (reads > READS_MAX) {
                        tc_log_warn(MOD_NAME,
                                    "reached read limit. "
                                    "This should'nt happen. "
                                    "Check your input source");
                        return TC_ERROR;
                    }
                    mpeg_pkt_del(pes);
                }
                break;

            case STATE_SEQUENCE:
                if (rgb_mode == TRUE) {
                    mpeg2_convert(decoder, mpeg2convert_rgb24, NULL);
                }
                break;
            case STATE_SLICE:
            case STATE_END:
            case STATE_INVALID_END:
                if (info->display_fbuf) {
                    copy_frame(sequence, info, param);
                    reads = 0; // this is redundant?
                    return TC_OK;
                }
            default:
                break;
        }
    } while (decoding);
    
    return TC_ERROR;
}

/* ------------------------------------------------------------ 
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{  
    mpeg_close(MPEG);

    mpeg_file_close(MFILE);

    mpeg2_close(decoder);
        
    return TC_OK;
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
