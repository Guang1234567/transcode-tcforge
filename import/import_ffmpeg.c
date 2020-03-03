/*
 *  import_ffmpeg.c
 *
 *  Copyright (C) Moritz Bunkus - October 2002
 *  libavformat support and misc updates:
 *  Copyright (C) Francesco Romani - November 2007
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

#define MOD_NAME    "import_ffmpeg.so"
#define MOD_VERSION "v0.2.2 (2007-11-04)"
#define MOD_CODEC   "(video) libavformat/libavcodec"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtc/tcframes.h"
#include "libtcutil/optstr.h"
#include "libtcext/tc_avcodec.h"
#include "libtcvideo/tcvideo.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_YUV|TC_CAP_RGB;

#define MOD_PRE ffmpeg
#include "import_def.h"

/*
 * libavcodec is not thread-safe. We must protect concurrent access to it.
 * this is visible (without the mutex of course) with
 * transcode .. -x ffmpeg -y ffmpeg -F mpeg4
 */

typedef struct tcffdata_ TCFFData;
struct tcffdata_ {
    AVFormatContext *dmx_context;
    AVCodecContext  *dec_context;
    AVCodec         *dec_codec;
    int              streamid;
};


typedef void (*AdaptImageFn)(uint8_t *frame,
                             AVCodecContext *lavc_dec_context,
                             AVFrame *picture);

/*************************************************************************/

static AdaptImageFn     img_adaptor = NULL;
static TCVHandle        tcvhandle = 0;
static AVFrame         *picture;
static int              src_fmt;
static int              dst_fmt;
static size_t           frame_size = 0;
static uint8_t         *frame = NULL;

static TCFFData vff_data = {
    .dmx_context = NULL,
    .dec_context = NULL,
    .dec_codec   = NULL,
    .streamid    = -1,
};

/*************************************************************************/

static inline void enable_levels_filter(void)
{
    int h = 0;

    tc_log_info(MOD_NAME, "input is mjpeg, reducing range from YUVJ420P to YUV420P");
    h = tc_filter_add("levels", "output=16-240:pre=1");
    if (!h) {
        tc_log_warn(MOD_NAME, "cannot load levels filter");
    }
}

#define ALLOC_MEM_AREA(PTR, SIZE) do { \
    if ((PTR) == NULL) { \
        (PTR) = tc_bufalloc((SIZE)); \
    } \
    if ((PTR) == NULL) { \
        tc_log_perror(MOD_NAME, "out of memory"); \
        return TC_IMPORT_ERROR; \
    } \
} while (0)

/*************************************************************************/
/*
 * Image adaptors helper routines
 */

/*
 * yeah following adaptors can be further factored out.
 * Can't do this now, unfortunately. -- fromani
 */

static void adapt_image_yuv420p(uint8_t *frame,
                                AVCodecContext *lavc_dec_context,
                                AVFrame *picture)
{
    uint8_t *src_planes[3];
    YUV_INIT_PLANES(src_planes, frame, src_fmt,
                    lavc_dec_context->width, lavc_dec_context->height);

    // Remove "dead space" at right edge of planes, if any
    if (picture->linesize[0] != lavc_dec_context->width) {
        int y;
        for (y = 0; y < lavc_dec_context->height; y++) {
            ac_memcpy(src_planes[0] + y*lavc_dec_context->width,
                      picture->data[0] + y*picture->linesize[0],
                      lavc_dec_context->width);
        }
        for (y = 0; y < lavc_dec_context->height / 2; y++) {
            ac_memcpy(src_planes[1] + y*(lavc_dec_context->width/2),
                      picture->data[1] + y*picture->linesize[1],
                	  lavc_dec_context->width/2);
            ac_memcpy(src_planes[2] + y*(lavc_dec_context->width/2),
			          picture->data[2] + y*picture->linesize[2],
                      lavc_dec_context->width/2);
        }
    } else {
        ac_memcpy(src_planes[0], picture->data[0],
                  lavc_dec_context->width * lavc_dec_context->height);
        ac_memcpy(src_planes[1], picture->data[1],
                  (lavc_dec_context->width/2)*(lavc_dec_context->height/2));
        ac_memcpy(src_planes[2], picture->data[2],
		          (lavc_dec_context->width/2)*(lavc_dec_context->height/2));
    }
} 


static void adapt_image_yuv411p(uint8_t *frame,
                                AVCodecContext *lavc_dec_context,
                                AVFrame *picture)
{
    uint8_t *src_planes[3];
    YUV_INIT_PLANES(src_planes, frame, src_fmt,
                    lavc_dec_context->width, lavc_dec_context->height);

    if (picture->linesize[0] != lavc_dec_context->width) {
        int y;
        for (y = 0; y < lavc_dec_context->height; y++) {
            ac_memcpy(src_planes[0] + y*lavc_dec_context->width,
			          picture->data[0] + y*picture->linesize[0],
                      lavc_dec_context->width);
            ac_memcpy(src_planes[1] + y*(lavc_dec_context->width/4),
	    	          picture->data[1] + y*picture->linesize[1],
                      lavc_dec_context->width/4);
            ac_memcpy(src_planes[2] + y*(lavc_dec_context->width/4),
			          picture->data[2] + y*picture->linesize[2],
                      lavc_dec_context->width/4);
        }
    } else {
        ac_memcpy(src_planes[0], picture->data[0],
                  lavc_dec_context->width * lavc_dec_context->height);
        ac_memcpy(src_planes[1], picture->data[1],
		          (lavc_dec_context->width/4) * lavc_dec_context->height);
        ac_memcpy(src_planes[2], picture->data[2],
		          (lavc_dec_context->width/4) * lavc_dec_context->height);
    }
}


static void adapt_image_yuv422p(uint8_t *frame,
                                AVCodecContext *lavc_dec_context,
                                AVFrame *picture)
{
    uint8_t *src_planes[3];
    YUV_INIT_PLANES(src_planes, frame, src_fmt,
                    lavc_dec_context->width, lavc_dec_context->height);

    if (picture->linesize[0] != lavc_dec_context->width) {
        int y;
        for (y = 0; y < lavc_dec_context->height; y++) {
            ac_memcpy(src_planes[0] + y*lavc_dec_context->width,
			          picture->data[0] + y*picture->linesize[0],
                      lavc_dec_context->width);
            ac_memcpy(src_planes[1] + y*(lavc_dec_context->width/2),
	    	          picture->data[1] + y*picture->linesize[1],
                      lavc_dec_context->width/2);
            ac_memcpy(src_planes[2] + y*(lavc_dec_context->width/2),
			          picture->data[2] + y*picture->linesize[2],
                      lavc_dec_context->width/2);
        }
    } else {
        ac_memcpy(src_planes[0], picture->data[0],
                  lavc_dec_context->width * lavc_dec_context->height);
        ac_memcpy(src_planes[1], picture->data[1],
		         (lavc_dec_context->width/2) * lavc_dec_context->height);
        ac_memcpy(src_planes[2], picture->data[2],
		          (lavc_dec_context->width/2) * lavc_dec_context->height);
    }
}


static void adapt_image_yuv444p(uint8_t *frame,
                                AVCodecContext *lavc_dec_context,
                                AVFrame *picture)
{
    uint8_t *src_planes[3];
    YUV_INIT_PLANES(src_planes, frame, src_fmt,
                    lavc_dec_context->width, lavc_dec_context->height);

    if (picture->linesize[0] != lavc_dec_context->width) {
	    int y;
        for (y = 0; y < lavc_dec_context->height; y++) {
            ac_memcpy(picture->data[0] + y*lavc_dec_context->width,
		              picture->data[0] + y*picture->linesize[0],
                      lavc_dec_context->width);
            ac_memcpy(picture->data[1] + y*lavc_dec_context->width,
	    	          picture->data[1] + y*picture->linesize[1],
                      lavc_dec_context->width);
            ac_memcpy(picture->data[2] + y*lavc_dec_context->width,
			          picture->data[2] + y*picture->linesize[2],
                      lavc_dec_context->width);
        }
    } else {
        ac_memcpy(src_planes[0], picture->data[0],
		          lavc_dec_context->width * lavc_dec_context->height);
        ac_memcpy(src_planes[1], picture->data[1],
		          lavc_dec_context->width * lavc_dec_context->height);
        ac_memcpy(src_planes[2], picture->data[2],
	              lavc_dec_context->width * lavc_dec_context->height);
    }
}



MOD_open 
{
    if (param->flag == TC_VIDEO) {
        int workarounds = FF_BUG_AUTODETECT, trunc = TC_FALSE;
        int ret, i = 0;

        if (vob->im_v_string && optstr_lookup(vob->im_v_string, "nopad")) {
            if (verbose >= TC_INFO) 
                fprintf(stderr, "[%s] forcing no-pad mode\n", MOD_NAME);
            workarounds = FF_BUG_NO_PADDING;
        }
        if (vob->im_v_string && optstr_lookup(vob->im_v_string, "trunc")) {
            if (verbose >= TC_INFO) 
                fprintf(stderr, "[%s] allowing truncated streams\n", MOD_NAME);
            trunc = TC_TRUE;
        }

        /* special case in here, better to not use TC_INIT_LIBAVCODEC */
        TC_LOCK_LIBAVCODEC;
        av_register_all();
        avcodec_init();
        avcodec_register_all();

        ret = av_open_input_file(&(vff_data.dmx_context), vob->video_in_file,
                                 NULL, 0, NULL);
        TC_UNLOCK_LIBAVCODEC;

        if (ret != 0) {
            tc_log_error(MOD_NAME, "unable to open '%s'"
                                   " (libavformat failure)",
                         vob->video_in_file);
            return TC_IMPORT_ERROR;
        }

        ret = av_find_stream_info(vff_data.dmx_context);
        if (ret < 0) {
            tc_log_error(MOD_NAME, "unable to fetch informations from '%s'"
                                   " (libavformat failure)",
                         vob->video_in_file);
            return TC_IMPORT_ERROR;
        }

        if (verbose >= TC_DEBUG) {
            dump_format(vff_data.dmx_context, 0, vob->video_in_file, 0);
        }

        for (i = 0; i < vff_data.dmx_context->nb_streams; i++) {
            if (vff_data.dmx_context->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO) {
                vff_data.streamid = i;
                break;
            }
        }
        if (vff_data.streamid != -1) {
            if (verbose >= TC_DEBUG) {
                tc_log_info(MOD_NAME, "using stream #%i for video",
                            vff_data.streamid);
            }
        } else {
            tc_log_error(MOD_NAME, "video stream not found in '%s'",
                         vob->video_in_file);
            return TC_IMPORT_ERROR;
        }
 
        vff_data.dec_context = vff_data.dmx_context->streams[vff_data.streamid]->codec;

        if (vff_data.dec_context->width != vob->im_v_width
         || vff_data.dec_context->height != vob->im_v_height) {
            tc_log_error(MOD_NAME, "frame dimension mismatch:"
                                   " probing=%ix%i, opening=%ix%i",
                         vob->im_v_width, vob->im_v_height,
                         vff_data.dec_context->width, vff_data.dec_context->height);
            return TC_IMPORT_ERROR;
        }

        vff_data.dec_codec = avcodec_find_decoder(vff_data.dec_context->codec_id);
        if (vff_data.dec_codec == NULL) {
            tc_log_warn(MOD_NAME, "No codec found for the ID '0x%X'.",
                        vff_data.dec_context->codec_id);
            return TC_IMPORT_ERROR;
        }
        
        if (trunc && vff_data.dec_codec->capabilities & CODEC_CAP_TRUNCATED) {
            vff_data.dec_context->flags |= CODEC_FLAG_TRUNCATED;
        }
        if (vob->decolor) {
            vff_data.dec_context->flags |= CODEC_FLAG_GRAY;
        }
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
        vff_data.dec_context->error_resilience  = FF_ER_COMPLIANT;
#else
        vff_data.dec_context->error_recognition = FF_ER_COMPLIANT;
#endif
        vff_data.dec_context->error_concealment = FF_EC_GUESS_MVS|FF_EC_DEBLOCK;
        vff_data.dec_context->workaround_bugs = workarounds;

        TC_LOCK_LIBAVCODEC;
        ret = avcodec_open(vff_data.dec_context, vff_data.dec_codec);
        TC_UNLOCK_LIBAVCODEC;
        if (ret < 0) {
            tc_log_error(MOD_NAME, "Could not initialize the '%s' codec.",
                         vff_data.dec_codec->name);
            return TC_IMPORT_ERROR;
        }

        frame_size = tc_video_frame_size(vob->im_v_width, vob->im_v_height,
                                         vob->im_v_codec);
        ALLOC_MEM_AREA(frame, frame_size);

        picture = avcodec_alloc_frame();
        if (!picture) {
            tc_log_error(MOD_NAME, "cannot allocate lavc frame");
            return TC_IMPORT_ERROR;
        }

        /* translate src_fmt */
        dst_fmt = (vob->im_v_codec == TC_CODEC_YUV420P) ?IMG_YUV_DEFAULT :IMG_RGB_DEFAULT;
        switch (vff_data.dec_context->pix_fmt) {
          case PIX_FMT_YUVJ420P:
          case PIX_FMT_YUV420P:
            src_fmt = IMG_YUV420P;
            img_adaptor = adapt_image_yuv420p;
            break;

          case PIX_FMT_YUV411P:
            src_fmt = IMG_YUV411P;
            img_adaptor = adapt_image_yuv411p;
            break;

          case PIX_FMT_YUVJ422P:
          case PIX_FMT_YUV422P:
            src_fmt = IMG_YUV422P;
            img_adaptor = adapt_image_yuv422p;
            break;

          case PIX_FMT_YUVJ444P:
          case PIX_FMT_YUV444P:
            src_fmt = IMG_YUV444P;
            img_adaptor = adapt_image_yuv444p;
            break;

          default:
        	tc_log_error(MOD_NAME, "Unsupported decoded frame format: %d",
		                 vff_data.dec_context->pix_fmt);
            return TC_IMPORT_ERROR;
        }

        tcvhandle = tcv_init();
        if (!tcvhandle) {
    	    tc_log_error(MOD_NAME, "Image conversion init failed");
            return TC_EXPORT_ERROR;
        }

        param->fd = NULL;
        return TC_IMPORT_OK;
    }

    return TC_IMPORT_ERROR;
}

#undef ALLOC_MEM_AREA



MOD_decode 
{
    if (param->flag == TC_VIDEO) {
        int got_picture = 0, ret = 0, len = 0;
        AVPacket packet;

        do {
            ret = av_read_frame(vff_data.dmx_context, &packet);

            if (ret < 0) {
                tc_log_info(MOD_NAME,
                            "reading frame failed (return value=%i)", ret);
                return TC_IMPORT_ERROR;
            }
            if (packet.stream_index == vff_data.streamid) {
#ifdef TC_LAVC_DEBUG
                tc_log_info(MOD_NAME, "read bytes=%i", packet.size);
#endif
                TC_LOCK_LIBAVCODEC;
#if LIBAVCODEC_VERSION_MAJOR >= 53 \
 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 25)
                len = avcodec_decode_video2(vff_data.dec_context, picture,
                                            &got_picture, &packet);
#else
                len = avcodec_decode_video(vff_data.dec_context, picture,
                                           &got_picture, packet.data,
                                           packet.size);
#endif
                TC_UNLOCK_LIBAVCODEC;
#ifdef TC_LAVC_DEBUG
                tc_log_info(MOD_NAME, "decode_video: size=%i, len=%i, got_picture=%i",
                                      packet.size, len, got_picture);
#endif
            }
            if (!got_picture) {
                av_free_packet(&packet);
            }
        } while (!got_picture);

        img_adaptor(frame, vff_data.dec_context, picture);
        tcv_convert(tcvhandle, frame, param->buffer,
                    vff_data.dec_context->width,
                    vff_data.dec_context->height,
                    src_fmt, dst_fmt);
        /* Can't free this until we're done with the data (the picture will
         * be freed if it's a raw stream) */
        av_free_packet(&packet);

        param->size = frame_size;
#ifdef TC_LAVC_DEBUG
        tc_log_warn(MOD_NAME, "GOT PICTURE!: size=%lu", frame_size);
#endif
        return TC_IMPORT_OK;
    }
    return TC_IMPORT_ERROR;
}



MOD_close 
{
    if (param->flag == TC_VIDEO) {
        if (frame != NULL) {
            tc_buffree(frame);
            frame = NULL;
        }
        if (picture != NULL) {
            av_free(picture);
            picture = NULL;
        }

        if (vff_data.dec_context != NULL) {
            avcodec_flush_buffers(vff_data.dec_context);

            avcodec_close(vff_data.dec_context);
            vff_data.dec_context = NULL;
        }

        if (vff_data.dmx_context != NULL) {
            av_close_input_file(vff_data.dmx_context);
            vff_data.dmx_context = NULL;
        }

        tcv_free(tcvhandle);
        tcvhandle = 0;

        return TC_IMPORT_OK;
    }

    return TC_IMPORT_ERROR;
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
