/*
 *  import_avi.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
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

#define MOD_NAME    "import_avi.so"
#define MOD_VERSION "v0.5.0 (2008-01-15)"
#define MOD_CODEC   "(video) * | (audio) *"

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_PCM | TC_CAP_RGB | TC_CAP_AUD |
                             TC_CAP_VID | TC_CAP_YUV | TC_CAP_YUV422;

#define MOD_PRE avi
#include "import_def.h"

#include "libtc/tccodecs.h"
#include "libtcutil/xio.h"
#include "libtcvideo/tcvideo.h"


static avi_t *avifile_aud = NULL;
static avi_t *avifile_vid = NULL;

static int audio_codec;
static int aframe_count = 0, vframe_count = 0;
static int width = 0, height = 0;

static TCVHandle tcvhandle = NULL;
static ImageFormat srcfmt = IMG_NONE, dstfmt = IMG_NONE;
static int destsize = 0;

static const struct {
    const char *name;  // fourcc
    ImageFormat format;
    int bpp;
} formats[] = {
    { "I420", IMG_YUV420P, 12 },
    { "YV12", IMG_YV12,    12 },
    { "YUY2", IMG_YUY2,    16 },
    { "UYVY", IMG_UYVY,    16 },
    { "YVYU", IMG_YVYU,    16 },
    { "Y800", IMG_Y8,       8 },
    { "RGB",  IMG_RGB24,   24 },
    { NULL,   IMG_NONE,     0 }
};

static ImageFormat tc_csp_translate(TCCodecID id)
{
    switch (id) {
      case TC_CODEC_RGB24:
        return IMG_RGB24;
      case TC_CODEC_YUV420P:
        return IMG_YUV420P;
      case TC_CODEC_YUV422P:
        return IMG_YUV422P;
      default: /* cannot happen */
        return IMG_NONE;
    }
    return IMG_NONE; /*cannot happen */
}

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

#define PCM_FORMAT_TAG  0x00000001

MOD_open
{
    double fps=0;
    char *codec=NULL;
    long rate=0, bitrate=0;
    int format=0, chan=0, bits=0;
    struct stat fbuf;
    char import_cmd_buf[TC_BUF_MAX];
    long sret;

    param->fd = NULL;

    if (param->flag == TC_AUDIO) {
        // Is the input file actually a directory - if so use
        // tccat to dump out the audio. N.B. This isn't going
        // to work if a particular track is needed
        /* directory content should really be handled by upper levels... -- FR */
        if ((xio_stat(vob->audio_in_file, &fbuf)) == 0 && S_ISDIR(fbuf.st_mode)) {
            sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
                                "%s -a -i \"%s\" -d %d",
                                TCCAT_EXE, vob->video_in_file, vob->verbose);
            if (sret < 0)
                return TC_ERROR;
            if (verbose_flag)
                tc_log_info(MOD_NAME, "%s", import_cmd_buf);
            param->fd = popen(import_cmd_buf, "r");
            if (param->fd == NULL) {
                return TC_ERROR;
            }
            return TC_OK;
        }

        // Otherwise proceed to open the file directly and decode here
        if (avifile_aud == NULL) {
            if (vob->nav_seek_file) {
                avifile_aud = AVI_open_input_indexfile(vob->audio_in_file,
                                                       0, vob->nav_seek_file);
            } else {
                avifile_aud = AVI_open_input_file(vob->audio_in_file, 1);
            }
            if (avifile_aud == NULL) {
                AVI_print_error("avi open error");
                return TC_ERROR;
            }
        }

        // set selected for multi-audio AVI-files
        AVI_set_audio_track(avifile_aud, vob->a_track);

        rate   =  AVI_audio_rate(avifile_aud);
        chan   =  AVI_audio_channels(avifile_aud);

        if (!chan) {
            tc_log_warn(MOD_NAME, "error: no audio track found");
            return TC_ERROR;
        }

        bits   =  AVI_audio_bits(avifile_aud);
        bits   =  (!bits) ?16 :bits;

        format =  AVI_audio_format(avifile_aud);
        bitrate=  AVI_audio_mp3rate(avifile_aud);

        if (verbose_flag)
            tc_log_info(MOD_NAME, "format=0x%x, rate=%ld Hz, bits=%d, "
                        "channels=%d, bitrate=%ld",
                        format, rate, bits, chan, bitrate);

        if (vob->im_a_codec == TC_CODEC_PCM && format != PCM_FORMAT_TAG) {
            tc_log_info(MOD_NAME, "error: invalid AVI audio format '0x%x'"
                        " for PCM processing", format);
            return TC_ERROR;
        }
        // go to a specific byte for seeking
        AVI_set_audio_position(avifile_aud,
                               vob->vob_offset * vob->im_a_size);

        audio_codec = vob->im_a_codec;
        return TC_OK;
    }

    if (param->flag == TC_VIDEO) {
    	int i = 0;

        if(avifile_vid==NULL) {
            if (vob->nav_seek_file) {
                avifile_vid = AVI_open_input_indexfile(vob->video_in_file,
                                                       0, vob->nav_seek_file);
            } else {
                avifile_vid = AVI_open_input_file(vob->video_in_file, 1);
            }
            if (avifile_vid == NULL) {
                AVI_print_error("avi open error");
                return TC_ERROR;
            }
        }

        if (vob->vob_offset > 0)
            AVI_set_video_position(avifile_vid, vob->vob_offset);

        // read all video parameter from input file
        width  =  AVI_video_width(avifile_vid);
        height =  AVI_video_height(avifile_vid);
        fps    =  AVI_frame_rate(avifile_vid);
        codec  =  AVI_video_compressor(avifile_vid);

        tc_log_info(MOD_NAME, "codec=%s, fps=%6.3f, width=%d, height=%d",
                    codec, fps, width, height);

        if (AVI_max_video_chunk(avifile_vid) > SIZE_RGB_FRAME) {
            tc_log_error(MOD_NAME, "invalid AVI video frame chunk size detected");
            return TC_ERROR;
        }

	    for (i = 0; formats[i].name != NULL; i++) {
	        if (strcasecmp(formats[i].name, codec) == 0) {
    	        srcfmt = formats[i].format;
    	        dstfmt = tc_csp_translate(vob->im_v_codec);
        	    destsize = vob->im_v_width * vob->im_v_height * formats[i].bpp / 8;
    		    break;
            }
	    }
    	if ((srcfmt && dstfmt) && (srcfmt != dstfmt)) {
            tcvhandle = tcv_init();
            if (!tcvhandle) {
	            tc_log_error(MOD_NAME, "tcv_convert_init failed");
                return TC_ERROR;
            }
            tc_log_info(MOD_NAME, "raw source, "
                                  "converting colorspace: %s -> %s",
                        formats[i].name,
                        tc_codec_to_string(vob->im_v_codec));
        }
        return TC_OK;
    }
    return TC_ERROR;
}


/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

#define RETURN_IF_READ_ERROR(RET, MSG) do { \
    if ((RET) < 0) {                        \
        if (verbose & TC_DEBUG)             \
            AVI_print_error((MSG));         \
        return TC_ERROR;                    \
    }                                       \
} while (0)

MOD_decode
{
    int key;
    long bytes_read = 0;

    if (param->flag == TC_VIDEO) {
        int i, mod = width % 4;
        
        // If we are using tccat, then do nothing here
        if (param->fd != NULL) {
            return TC_OK;
        }

        param->size = AVI_read_frame(avifile_vid, param->buffer, &key);

        // Fixup: For uncompressed AVIs, it must be aligned at
        // a 4-byte boundary
        if (mod && vob->im_v_codec == TC_CODEC_RGB24) {
            for (i = 0; i < height; i++) {
                memmove(param->buffer+(i*width*3),
                        param->buffer+(i*width*3) + (mod)*i,
                        width*3);
            }   
        }

        if (verbose & TC_STATS && key)
            tc_log_info(MOD_NAME, "keyframe %d", vframe_count);

        if (param->size < 0) {
            if (verbose & TC_DEBUG)
                AVI_print_error("AVI read video frame");
            return TC_ERROR;
        }

    	if ((srcfmt && dstfmt) && (srcfmt != dstfmt)) {
            int ret = tcv_convert(tcvhandle,
                                  param->buffer, param->buffer,
		                          width, height,
                                  srcfmt, dstfmt);
            if (!ret) {
                tc_log_error(MOD_NAME, "image conversion failed");
                return TC_ERROR;
            }
            if (destsize)
                param->size = destsize;
        }

        if (key)
            param->attributes |= TC_FRAME_IS_KEYFRAME;

        vframe_count++;

        return TC_IMPORT_OK;
    }

    if (param->flag == TC_AUDIO) {
        if (audio_codec == TC_CODEC_RAW) {
            int r = 0;

            bytes_read = AVI_audio_size(avifile_aud, aframe_count);
            RETURN_IF_READ_ERROR(bytes_read, "AVI audio size frame");

            r = AVI_read_audio(avifile_aud, param->buffer, bytes_read);
            RETURN_IF_READ_ERROR(bytes_read, "AVI audio read frame");

            aframe_count++; // XXX ?? -- FR
        } else {
            bytes_read = AVI_read_audio(avifile_aud, param->buffer, param->size);
            RETURN_IF_READ_ERROR(bytes_read, "AVI audio read frame");
        }
        param->size = bytes_read;
        return TC_OK;
    }
    return TC_ERROR;
}

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

#define CLOSE_AVIFILE(AF) do { \
   if ((AF) != NULL) {         \
        AVI_close((AF));       \
        (AF) = NULL;           \
   }                           \
} while (0)

MOD_close
{
    if (param->fd != NULL)
        pclose(param->fd);

    if (param->flag == TC_AUDIO) {
        CLOSE_AVIFILE(avifile_aud);
        return TC_OK;
    }

    if (param->flag == TC_VIDEO) {
        CLOSE_AVIFILE(avifile_vid);
        return TC_OK;
    }

    if (tcvhandle) {
        tcv_free(tcvhandle);
        tcvhandle = NULL;
    }
    return TC_ERROR;
}

