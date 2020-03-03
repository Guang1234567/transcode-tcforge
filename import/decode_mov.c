/*
 *  decode_mov.c
 *
 *  Copyright (C) Malanchini Marzio - April 2003
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
 *
 */
#include "tccore/tcinfo.h"

#include "src/transcode.h"
#include "libtc/libtc.h"

#include "ioaux.h"
#include "magic.h"
#include "tc.h"

#include <stdint.h>

#ifdef HAVE_LIBQUICKTIME
#include <quicktime.h>
#endif


/* ------------------------------------------------------------
 *
 * decoder thread
 *
 * ------------------------------------------------------------*/

#define QT_ABORT(MSG) do { \
    if (qt_handle != NULL) { \
        quicktime_close(qt_handle); \
    } \
    tc_log_error(__FILE__, "%s",(MSG)); \
    import_exit(1); \
} while (0)

#define QT_WRITE(FD, BUF, SIZE) do { \
    ssize_t w = tc_pwrite((FD), (BUF), (SIZE)); \
    if (w != (ssize_t)(SIZE)) { /* XXX: watch here */ \
        /* XXX: what about memleaks? */ \
        QT_ABORT("error while writing output data"); \
    } \
} while (0)   


#ifdef HAVE_LIBQUICKTIME
void decode_mov(decode_t *decode)
{

    quicktime_t *qt_handle=NULL;
    unsigned char **p_raw_buffer;
    char *p_v_codec=NULL,*p_a_codec=NULL,*p_buffer=NULL,*p_tmp=NULL;
    int s_width=0,s_height=0,s_channel=0,s_bits=0,s_buff_size=0,s_audio_size=0,s_video_size=0,s_sample=0;
    int s_cont,s_frames;
    double s_fps=0;
    long s_audio_rate,s_qt_pos;
    uint16_t *p_mask1, *p_mask2;
    char msgbuf[TC_BUF_MIN];


    qt_handle = quicktime_open((char * )decode->name, 1, 0);
    if (qt_handle == NULL) {
        QT_ABORT("can't open quicktime!");
    }
    quicktime_set_preload(qt_handle, 10240000);
    s_fps = quicktime_frame_rate(qt_handle, 0);
    if (decode->format == TC_CODEC_PCM) {
        if (quicktime_audio_tracks(qt_handle) == 0) {
            QT_ABORT("no audio track in quicktime found!");
        }
        s_channel = quicktime_track_channels(qt_handle, 0);
        s_audio_rate = quicktime_sample_rate(qt_handle, 0);
        s_bits = quicktime_audio_bits(qt_handle, 0);
        s_audio_size = quicktime_audio_length(qt_handle,0);
        p_a_codec = quicktime_audio_compressor(qt_handle, 0);

        if (decode->frame_limit[1] < s_audio_size) {
            s_audio_size = decode->frame_limit[1] - decode->frame_limit[0];
        } else {
            s_audio_size -= decode->frame_limit[0];
        }

        if (decode->verbose) {
            tc_log_info(__FILE__, "Audio codec=%s, rate=%ld Hz, bits=%d, channels=%d",
                                  p_a_codec, s_audio_rate, s_bits, s_channel);
        }

        if ((s_bits != 8) && (s_bits != 16)) {
            tc_snprintf(msgbuf, sizeof(msgbuf), "unsupported %d bit rate"
                        " in quicktime!", s_bits);
            QT_ABORT(msgbuf);
        }
        if (s_channel > 2) {
            tc_snprintf(msgbuf, sizeof(msgbuf), "too many audio tracks "
                        "(%d) found in quicktime!", s_channel);
            QT_ABORT(msgbuf);
        }
        if (strlen(p_a_codec) == 0) {
            QT_ABORT("unsupported codec (empty!) in quicktime!");
        }
        if (quicktime_supported_audio(qt_handle, 0) != 0) {
            s_qt_pos = quicktime_audio_position(qt_handle,0);
            s_sample = (1.00 * s_channel * s_bits *s_audio_rate)/(s_fps * 8);
            s_buff_size = s_sample * sizeof(uint16_t);
            p_buffer = tc_malloc(s_buff_size);
            if (s_bits == 16)
                s_sample /= 2;
            if (s_channel == 1) {
                p_mask1=(uint16_t *)p_buffer;
                quicktime_set_audio_position(qt_handle, s_qt_pos + decode->frame_limit[0], 0);
                for (; s_audio_size > 0; s_audio_size -= s_sample) {
                    if (quicktime_decode_audio(qt_handle, p_mask1, NULL, s_sample, 0) < 0) {
                        QT_ABORT("error reading quicktime audio frame");
                    }
                    QT_WRITE(decode->fd_out, p_buffer, s_buff_size);
                }
            } else {
                s_sample /= 2;
                p_mask1 = (uint16_t *)p_buffer;
                p_mask2 = tc_malloc(s_sample * sizeof(uint16_t));
                s_qt_pos += decode->frame_limit[0];
                quicktime_set_audio_position(qt_handle, s_qt_pos, 0);
                for (; s_audio_size > 0; s_audio_size -= s_sample) {
                    if (quicktime_decode_audio(qt_handle, p_mask1, NULL, s_sample, 0) < 0) {
                        QT_ABORT("error reading quicktime audio frame");
                    }
                    quicktime_set_audio_position(qt_handle, s_qt_pos, 0);
                    if (quicktime_decode_audio(qt_handle,p_mask2, NULL,s_sample, 1) < 0) {
                        QT_ABORT("error reading quicktime audio frame");
                    }
                    for (s_cont = s_sample - 1; s_cont >= 0; s_cont--)
                        p_mask1[s_cont<<1] = p_mask1[s_cont];
                    for (s_cont = 0; s_cont < s_sample; s_cont++)
                        p_mask1[1+(s_cont<<1)] = p_mask2[s_cont];
                    s_qt_pos += s_sample;
                    QT_WRITE(decode->fd_out, p_buffer, s_buff_size >> 1);
                }
                free(p_mask2);
            }
            free(p_buffer);
        }
#if !defined(LIBQUICKTIME_000904)
        else if ((strcasecmp(p_a_codec, QUICKTIME_RAW) == 0)
              || (strcasecmp(p_a_codec, QUICKTIME_TWOS) == 0)) {
            s_sample = (1.00 * s_channel * s_bits *s_audio_rate)/(s_fps * 8);
            s_buff_size = s_sample * sizeof(uint16_t);
            p_buffer = tc_malloc(s_buff_size);
            s_qt_pos = quicktime_audio_position(qt_handle, 0);
            quicktime_set_audio_position(qt_handle, s_qt_pos + decode->frame_limit[0], 0);
            for (; s_audio_size > 0; s_audio_size -= s_buff_size) {
                if (quicktime_read_audio(qt_handle,p_buffer, s_buff_size, 0) < 0) {
                    QT_ABORT("error reading quicktime audio frame");
                }
                QT_WRITE(decode->fd_out, p_buffer, s_buff_size);
            }
            quicktime_close(qt_handle);
            free(p_buffer);
        }
#endif
        else {
            tc_snprintf(msgbuf, sizeof(msgbuf), "quicktime audio codec '%s'"
                        " not supported!", p_a_codec);
            QT_ABORT(msgbuf);
        }
    } else {
        if (quicktime_video_tracks(qt_handle) == 0) {
            QT_ABORT("no video track in quicktime found!");
        }
        p_v_codec = quicktime_video_compressor(qt_handle, 0);
        if (strlen(p_v_codec) == 0) {
            tc_snprintf(msgbuf, sizeof(msgbuf), "quicktime video codec '%s'"
                        " not supported!", p_v_codec);
            QT_ABORT(msgbuf);
        }
        s_width = quicktime_video_width(qt_handle, 0);
        s_height = quicktime_video_height(qt_handle, 0);
        s_video_size = quicktime_video_length(qt_handle,0);
        s_frames = quicktime_video_length(qt_handle, 0);
        if (decode->frame_limit[1] < s_frames)
            s_video_size = decode->frame_limit[1] - decode->frame_limit[0];
        else
            s_video_size -= decode->frame_limit[0];
        if (decode->verbose)
            tc_log_info(__FILE__, "Video codec=%s, fps=%6.3f,"
                                  " width=%d, height=%d",
                                  p_v_codec, s_fps, s_width, s_height);
        if (strcasecmp(p_v_codec,QUICKTIME_DV) == 0) {
            if ( s_fps == 25.00 )
                s_buff_size = TC_FRAME_DV_PAL;
            else
                s_buff_size = TC_FRAME_DV_NTSC;
            p_buffer = tc_malloc(s_buff_size);
            if (p_buffer == NULL) {
                QT_ABORT("can't allocate buffer");
            }
            s_qt_pos = quicktime_video_position(qt_handle, 0);
            quicktime_set_video_position(qt_handle, s_qt_pos + decode->frame_limit[0], 0);
            for (s_cont = 0; s_cont < s_video_size; s_cont++) {
                if (quicktime_read_frame(qt_handle, p_buffer, 0) < 0) {
                    free(p_buffer);
                    QT_ABORT("error reading quicktime video frame");
                }
                QT_WRITE(decode->fd_out, p_buffer, s_buff_size);
            }
        } else if (decode->format == TC_CODEC_RGB24) {
            if (quicktime_supported_video(qt_handle, 0) == 0) {
                tc_snprintf(msgbuf, sizeof(msgbuf), "quicktime video codec"
                            " '%s' not supported for RGB", p_v_codec);
                QT_ABORT(msgbuf);
            }
            p_raw_buffer = tc_malloc(s_height * sizeof(char *));
            if (p_raw_buffer == NULL) {
                QT_ABORT("can't allocate row pointers");
            }
            s_buff_size = 3 * s_height * s_width;
            p_buffer = tc_malloc(s_buff_size);
            if (p_buffer == NULL) {
                free(p_raw_buffer);
                QT_ABORT("can't allocate rgb buffer");
            }
            p_tmp = p_buffer;
            for (s_cont = 0; s_cont < s_height; s_cont++) {
                p_raw_buffer[s_cont] = p_tmp;
                p_tmp += s_width * 3;
            }
            s_qt_pos = quicktime_video_position(qt_handle, 0);
            quicktime_set_video_position(qt_handle, s_qt_pos+decode->frame_limit[0], 0);
            for (s_cont = 0; s_cont <s_video_size; s_cont++) {
                if (quicktime_decode_video(qt_handle, p_raw_buffer, 0) < 0) {
                    free(p_raw_buffer);
                    free(p_buffer);
                    QT_ABORT("error reading quicktime video frame");
                }
                QT_WRITE(decode->fd_out, p_buffer, s_buff_size);
            }
            free(p_raw_buffer);
        } else if(decode->format == TC_CODEC_YUV2) {
            if ((strcasecmp(p_v_codec,QUICKTIME_YUV4) != 0)
             && (strcasecmp(p_v_codec,QUICKTIME_YUV420) != 0)) {
                tc_snprintf(msgbuf, sizeof(msgbuf), "quicktime video codec"
                            " '%s' not suitable for YUV!", p_v_codec);
                QT_ABORT(msgbuf);
            }
            s_buff_size = (3 * s_height * s_width)/2;
            p_buffer=tc_malloc(s_buff_size);
            if (p_buffer == NULL) {
                QT_ABORT("can't allocate rgb buffer");
            }
            s_qt_pos = quicktime_video_position(qt_handle, 0);
            quicktime_set_video_position(qt_handle, s_qt_pos + decode->frame_limit[0], 0);
            for (s_cont = 0; s_cont < s_video_size; s_cont++) {
                if (quicktime_read_frame(qt_handle,p_buffer,0) < 0) {
                    free(p_buffer);
                    QT_ABORT("error reading quicktime video frame");
                }
                QT_WRITE(decode->fd_out, p_buffer, s_buff_size);
            }
        } else {
            tc_snprintf(msgbuf, sizeof(msgbuf), "unknown format mode"
                        " (0x%x)", (unsigned int)decode->format);
            QT_ABORT(msgbuf);
        }
        quicktime_close(qt_handle);
        free(p_buffer);
    }
    import_exit(0);
}

#else

void decode_mov(decode_t *decode)
{
    tc_log_error(__FILE__, "no support for Quicktime configured - exit.");
    import_exit(1);
}
#endif

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
