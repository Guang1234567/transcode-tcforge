/*
 * encode_lzo.c -- encode video frames individually using LZO.
 * (C) 2005-2010 Francesco Romani <fromani at gmail dot com>
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

#include "src/transcode.h"
#include "aclib/imgconvert.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

#include "libtcext/tc_lzo.h"

#include <stdio.h>
#include <stdlib.h>

#define MOD_NAME    "encode_lzo.so"
#define MOD_VERSION "v0.0.3 (2009-02-07)"
#define MOD_CAP     "LZO lossless video encoder"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_ENCODE|TC_MODULE_FEATURE_VIDEO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE


/* tc_lzo_ prefix was used to avoid any possible name clash with liblzo? */

static const char tc_lzo_help[] = ""
    "Overview:\n"
    "    this module encodes raw RGB/YUV video frames in LZO, using liblzo V2.\n"
    "Options:\n"
    "    help    produce module overview and options explanations\n";

typedef struct {
    lzo_byte work_mem[LZO1X_1_MEM_COMPRESS];
    /* needed by encoder to work properly */

    int codec;
    int flush_flag;
} LZOPrivateData;

static int tc_lzo_configure(TCModuleInstance *self,
                            const char *options,
                            TCJob *vob,
                            TCModuleExtraData *xdata[])
{
    LZOPrivateData *pd = NULL;
    int ret;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;
    pd->codec = vob->im_v_codec;
    pd->flush_flag = vob->encoder_flush;

    ret = lzo_init();
    if (ret != LZO_E_OK) {
        tc_log_error(MOD_NAME, "configure: failed to initialize"
                               " LZO encoder");
        return TC_ERROR;
    }

    return TC_OK;
}

static int tc_lzo_stop(TCModuleInstance *self)
{
    LZOPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    return TC_OK;
}

static int tc_lzo_init(TCModuleInstance *self, uint32_t features)
{
    LZOPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    pd = tc_malloc(sizeof(LZOPrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: can't allocate private data");
        return TC_ERROR;
    }
    /* sane defaults */
    pd->codec = TC_CODEC_YUV420P;

    self->userdata = pd;
    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return TC_OK;
}

static int tc_lzo_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    tc_lzo_stop(self);

    tc_free(self->userdata);
    self->userdata = NULL;
    return TC_OK;
}

static int tc_lzo_inspect(TCModuleInstance *self,
                          const char *param, const char **value)
{
    LZOPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");

    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = tc_lzo_help;
    }

    return TC_OK;
}

/* ------------------------------------------------------------
 *
 * encode and export
 *
 * ------------------------------------------------------------*/

/* assert(len(data) >= TC_LZ_HDR_SIZE) */
static void tc_lzo_put_header(tc_lzo_header_t *hdr, void *data)
{
    /* always CPU byte order */
    uint32_t *ptr = data;

    *(ptr)     = hdr->magic;
    *(ptr + 1) = hdr->size;
    *(ptr + 2) = hdr->flags;
    *(ptr + 3) = (uint32_t)(hdr->method << 24 | hdr->level << 16 | hdr->pad);
}

/* maybe translation should go away */
static int tc_lzo_format_translate(int tc_codec)
{
    int ret;
    switch (tc_codec) {
      case TC_CODEC_YUV420P:
        ret = TC_LZO_FORMAT_YUV420P;
        break;
      case TC_CODEC_YUY2:
        ret = TC_LZO_FORMAT_YUY2;
        break;
      case TC_CODEC_RGB24:
        ret = TC_LZO_FORMAT_RGB24;
        break;
      default:
        /* shouldn't happen */
        ret = 0;
        break;
    }
    return ret;
}

static int tc_lzo_encode_video(TCModuleInstance *self,
                               TCFrameVideo *inframe, TCFrameVideo *outframe)
{
    LZOPrivateData *pd = NULL;
    lzo_uint out_len = 0;
    tc_lzo_header_t hdr;
    int ret;

    TC_MODULE_SELF_CHECK(self, "encode_video");

    pd = self->userdata;

    /* invariants */
    hdr.magic  = TC_CODEC_LZO2;
    hdr.method = 1;
    hdr.level  = 1;
    hdr.pad    = 0;
    hdr.flags  = 0; /* sane default */

    ret = lzo1x_1_compress(inframe->video_buf, inframe->video_size,
                           outframe->video_buf + TC_LZO_HDR_SIZE,
                           &out_len, pd->work_mem);
    if (ret != LZO_E_OK) {
        /* this should NEVER happen */
        tc_log_warn(MOD_NAME, "encode_video: LZO compression failed"
                              " (errcode=%i)", ret);
        return TC_ERROR;
    }

    /* check for an incompressible block */
    if (out_len >= inframe->video_size)  {
        hdr.flags |= TC_LZO_NOT_COMPRESSIBLE;
        out_len = inframe->video_size;
    }
    hdr.size = out_len;

    hdr.flags |= tc_lzo_format_translate(pd->codec);
    /* always put header */
    tc_lzo_put_header(&hdr, outframe->video_buf);

    if (hdr.flags & TC_LZO_NOT_COMPRESSIBLE) {
        /* inframe data not compressible: outframe will hold a copy */
        if (verbose >= TC_DEBUG) {
            tc_log_info(MOD_NAME, "encode_video: block contains"
                                  " incompressible data");
        }
        ac_memcpy(outframe->video_buf + TC_LZO_HDR_SIZE,
                  inframe->video_buf, out_len);
    } else {
        /* outframe data already in place */
        if (verbose >= TC_DEBUG) {
            tc_log_info(MOD_NAME, "encode_video: compressed %lu bytes"
                                  " into %lu bytes",
                                  (unsigned long)inframe->video_size,
                                  (unsigned long)out_len);
        }
    }

    /* only keyframes */
    outframe->video_len = out_len + TC_LZO_HDR_SIZE;
    outframe->attributes |= TC_FRAME_IS_KEYFRAME;

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID tc_lzo_codecs_video_in[] = {
    TC_CODEC_YUY2, TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_ERROR
};
static const TCCodecID tc_lzo_codecs_video_out[] = { 
    TC_CODEC_LZO2, TC_CODEC_ERROR 
};
TC_MODULE_AUDIO_UNSUPPORTED(tc_lzo);
TC_MODULE_CODEC_FORMATS(tc_lzo);

TC_MODULE_INFO(tc_lzo);

static const TCModuleClass tc_lzo_class = {
    TC_MODULE_CLASS_HEAD(tc_lzo),

    .init         = tc_lzo_init,
    .fini         = tc_lzo_fini,
    .configure    = tc_lzo_configure,
    .stop         = tc_lzo_stop,
    .inspect      = tc_lzo_inspect,

    .encode_video = tc_lzo_encode_video,
};

TC_MODULE_ENTRY_POINT(tc_lzo)

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
