/*
 *  encode_yuv4mpeg.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Converted to NMS by Andrew Church
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

/*************************************************************************/

#define MOD_NAME    "encode_yuv4mpeg.so"
#define MOD_VERSION "v0.2.0 (2009-07-13)"
#define MOD_CAP     "YUV4MPEG encoder (uncompressed YUV stream)"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_ENCODE|TC_MODULE_FEATURE_VIDEO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

/*----------------------------------*/

#include "transcode.h"
#include "libtc/libtc.h"
#include "libtc/tccodecs.h"
#include "libtcmodule/tcmodule-plugin.h"
#include "libtcutil/optstr.h"
#include "libtcvideo/tcvideo.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(HAVE_MJPEGTOOLS_INC)
# include "yuv4mpeg.h"
# include "mpegconsts.h"
#else
# include "mjpegtools/yuv4mpeg.h"
# include "mjpegtools/mpegconsts.h"
#endif

#ifndef DAR_4_3
# define DAR_4_3      {   4, 3   }
# define DAR_16_9     {  16, 9   }
# define DAR_221_100  { 221, 100 }
# define SAR_UNKNOWN  {   0, 0   }
#endif

static const y4m_ratio_t dar_4_3 = DAR_4_3;
static const y4m_ratio_t dar_16_9 = DAR_16_9;
static const y4m_ratio_t dar_221_100 = DAR_221_100;
static const y4m_ratio_t sar_UNKNOWN = SAR_UNKNOWN;

/*----------------------------------*/

static const char yuv4mpeg_help[] = ""
    "Overview:\n"
    "    This module outputs a raw YUV video stream in the YUV4MPEG format,\n"
    "    which can be used as input to other programs such as MPlayer.\n"
    "Options:\n"
    "    This module has no options.\n"
    ;

typedef struct Y4MPrivateData {
    int wrote_header;
    int size;
    TCVHandle tcvhandle;
    ImageFormat srcfmt;
    y4m_stream_info_t y4mstream;
} Y4MPrivateData;

/***************************************************************************/

static void asrcode2asrratio(int asr, y4m_ratio_t *r)
{
    switch (asr) {
      case 2: *r = dar_4_3; break;
      case 3: *r = dar_16_9; break;
      case 4: *r = dar_221_100; break;
      case 1: r->n = 1; r->d = 1; break;
      case 0: default: *r = sar_UNKNOWN; break;
    }
}

/*************************************************************************/

static ssize_t y4m_write_callback(void *dest, const void *src, size_t len)
{
    vframe_list_t *frame = (vframe_list_t *)dest;
    memcpy(frame->video_buf + frame->video_len, src, len);
    frame->video_len += len;
    /* Return type is ssize_t, but it seems to want zero for success... */
    return 0;
}

/*************************************************************************/
/*************************************************************************/

static int tc_yuv4mpeg_init(TCModuleInstance *self, uint32_t features)
{
    Y4MPrivateData *pd;
    vob_t *vob = tc_get_vob();

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    /* Check frame dimensions */
    if (vob->ex_v_width % 2 || vob->ex_v_height % 2) {
        tc_log_warn(MOD_NAME, "init: only even dimensions allowed (%dx%d)",
                              vob->ex_v_width, vob->ex_v_height);
        return TC_ERROR;
    }

    pd = tc_malloc(sizeof(*pd));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: can't allocate private data");
        return TC_ERROR;
    }
    pd->wrote_header = 0;

    if (vob->im_v_codec == TC_CODEC_YUV420P) {
        pd->srcfmt = IMG_YUV_DEFAULT;
    } else if (vob->im_v_codec == TC_CODEC_YUV422P) {
        pd->srcfmt = IMG_YUV422P;
    } else if (vob->im_v_codec == TC_CODEC_RGB24) {
        pd->srcfmt = IMG_RGB_DEFAULT;
    } else {
        tc_log_warn(MOD_NAME, "unsupported video format %d",
                    vob->im_v_codec);
        tc_free(pd);
        return TC_ERROR;
    }

    pd->tcvhandle = tcv_init();
    if (!pd->tcvhandle) {
        tc_log_warn(MOD_NAME, "image conversion init failed");
        tc_free(pd);
        return TC_ERROR;
    }

    self->userdata = pd;
    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return TC_OK;
}

/***************************************************************************/

static int tc_yuv4mpeg_configure(TCModuleInstance *self, const char *options,
                                 TCJob *vob, TCModuleExtraData *xdata[])
{
    Y4MPrivateData *pd;
    int asr;
    // char dar_tag[20];
    y4m_ratio_t framerate;
    y4m_ratio_t asr_rate;

    TC_MODULE_SELF_CHECK(self, "configure");
    TC_MODULE_SELF_CHECK(vob, "configure"); // FIXME: need a more general macro

    pd = self->userdata;

    //note: this is the real framerate of the raw stream
    framerate = (vob->ex_frc == 0)
                     ?mpeg_conform_framerate(vob->ex_fps)
                     :mpeg_framerate(vob->ex_frc);
    if (framerate.n == 0 && framerate.d == 0) {
        framerate.n = vob->ex_fps*1000;
        framerate.d = 1000;
    }

    asr = (vob->ex_asr<0) ?vob->im_asr :vob->ex_asr;
    asrcode2asrratio(asr, &asr_rate);

    y4m_init_stream_info(&pd->y4mstream);
    y4m_si_set_framerate(&pd->y4mstream, framerate);
    if (vob->encode_fields == TC_ENCODE_FIELDS_TOP_FIRST) {
        y4m_si_set_interlace(&pd->y4mstream, Y4M_ILACE_TOP_FIRST);
    } else if (vob->encode_fields == TC_ENCODE_FIELDS_BOTTOM_FIRST) {
        y4m_si_set_interlace(&pd->y4mstream, Y4M_ILACE_BOTTOM_FIRST);
    } else if (vob->encode_fields == TC_ENCODE_FIELDS_PROGRESSIVE) {
        y4m_si_set_interlace(&pd->y4mstream, Y4M_ILACE_NONE);
    }
    y4m_si_set_sampleaspect(&pd->y4mstream, y4m_guess_sar(vob->ex_v_width, vob->ex_v_height, asr_rate));
    /*
    tc_snprintf( dar_tag, 19, "XM2AR%03d", asr );
    y4m_xtag_add( y4m_si_xtags(&pd->y4mstream), dar_tag );
    */
    y4m_si_set_height(&pd->y4mstream, vob->ex_v_height);
    y4m_si_set_width(&pd->y4mstream, vob->ex_v_width);
    y4m_si_set_chroma(&pd->y4mstream, Y4M_CHROMA_420JPEG); // XXX

    pd->size = vob->ex_v_width * vob->ex_v_height * 3/2;

    return TC_OK;
}

/***************************************************************************/

static int tc_yuv4mpeg_inspect(TCModuleInstance *self,
                               const char *param, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");

    if (optstr_lookup(param, "help")) {
        *value = yuv4mpeg_help;
    }

    return TC_OK;
}

/***************************************************************************/

static int tc_yuv4mpeg_encode_video(TCModuleInstance *self,
                                    vframe_list_t *inframe,
                                    vframe_list_t *outframe)
{
    Y4MPrivateData *pd;
    vob_t *vob = tc_get_vob();
    y4m_frame_info_t info;
    y4m_cb_writer_t writer;

    TC_MODULE_SELF_CHECK(self, "encode_video");

    pd = self->userdata;
    writer.data = outframe;
    writer.write = y4m_write_callback;
    outframe->video_len = 0;

    if (!pd->wrote_header) {
        int ret = y4m_write_stream_header_cb(&writer, &pd->y4mstream);
        if (ret != Y4M_OK) {
            tc_log_error(MOD_NAME, "write stream header (err=%i)", ret);
            tc_log_perror(MOD_NAME, "error");
            return TC_ERROR;
        }
        pd->wrote_header = 1;
    }

    if (!inframe) {  // Nothing to flush
        return TC_OK;
    }

    if (!tcv_convert(pd->tcvhandle, inframe->video_buf, inframe->video_buf,
                     vob->ex_v_width, vob->ex_v_height,
                     pd->srcfmt, IMG_YUV420P)) {
        tc_log_warn(MOD_NAME, "image format conversion failed");
        return TC_ERROR;
    }

#ifdef USE_NEW_MJPEGTOOLS_CODE
    y4m_init_frame_info(&info);

    if (y4m_write_frame_header_cb(&writer, &pd->y4mstream, &info) != Y4M_OK) {
        tc_log_perror(MOD_NAME, "write frame header");
        return TC_ERROR;
    }
#else
    y4m_init_frame_info(&info);

    if (y4m_write_frame_header_cb(&writer, &info) != Y4M_OK) {
        tc_log_perror(MOD_NAME, "write frame header");
        return TC_ERROR;
    }
#endif

    /*
     * do not trust param->size
     * -- Looks like there is an outdated comment,
     *  a latent issue or both FR
     */
    y4m_write_callback(outframe, inframe->video_buf, pd->size);

    return TC_OK;
}

/***************************************************************************/

static int tc_yuv4mpeg_stop(TCModuleInstance *self)
{
    return TC_OK;
}

/***************************************************************************/

static int tc_yuv4mpeg_fini(TCModuleInstance *self)
{
    Y4MPrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "fini");

    tc_yuv4mpeg_stop(self);

    pd = self->userdata;

    tcv_free(pd->tcvhandle);
    tc_free(self->userdata);
    self->userdata = NULL;

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID tc_yuv4mpeg_codecs_video_in[] =
    { TC_CODEC_RGB24, TC_CODEC_YUV422P, TC_CODEC_YUV420P,
      TC_CODEC_ERROR };
static const TCCodecID tc_yuv4mpeg_codecs_video_out[] =
    { TC_CODEC_YUV4MPEG, TC_CODEC_ERROR };
static const TCCodecID tc_yuv4mpeg_codecs_audio_in[] = { TC_CODEC_ERROR };
static const TCCodecID tc_yuv4mpeg_codecs_audio_out[] = { TC_CODEC_ERROR };

TC_MODULE_CODEC_FORMATS(tc_yuv4mpeg);

TC_MODULE_INFO(tc_yuv4mpeg);

static const TCModuleClass yuv4mpeg_class = {
    TC_MODULE_CLASS_HEAD(tc_yuv4mpeg),

    .init         = tc_yuv4mpeg_init,
    .fini         = tc_yuv4mpeg_fini,
    .configure    = tc_yuv4mpeg_configure,
    .stop         = tc_yuv4mpeg_stop,
    .inspect      = tc_yuv4mpeg_inspect,

    .encode_video = tc_yuv4mpeg_encode_video
};

TC_MODULE_ENTRY_POINT(yuv4mpeg)

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
