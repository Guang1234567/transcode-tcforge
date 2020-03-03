/*
 *  encode_im.c -- encodes video frames using ImageMagick.
 *  (C) 2007-2010 Francesco Romani <fromani at gmail dot com>
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
#include "src/framebuffer.h"
#include "libtcutil/optstr.h"

#include "libtcmodule/tcmodule-plugin.h"

#include "libtcext/tc_magick.h"


#define MOD_NAME    "encode_im.so"
#define MOD_VERSION "v0.2.0 (2009-03-01)"
#define MOD_CAP     "ImageMagick video frames encoder"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_ENCODE|TC_MODULE_FEATURE_VIDEO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

#define FMT_NAME_LEN    16
#define DEFAULT_QUALITY 75
#define DEFAULT_FORMAT  "png"

static const char tc_im_help[] = ""
    "Overview:\n"
    "    This module encodes video frames independently in various\n"
    "    image formats using ImageMagick libraries.\n"
    "Options:\n"
    "    format  name of the format to use for encoding images\n"
    "    quality select output quality (higher is better)\n"
    "    help    produce module overview and options explanations\n";

typedef struct tcimprivatedata_ TCIMPrivateData;
struct tcimprivatedata_ {
    TCMagickContext magick;

    unsigned long   quality;
    int             width;
    int             height;
    char            opt_buf[TC_BUF_MIN];
    char            img_fmt[FMT_NAME_LEN];
};



static const TCCodecID tc_im_codecs_video_out[] = {
    TC_CODEC_JPEG, TC_CODEC_TIFF, TC_CODEC_PNG,
    TC_CODEC_PPM,  TC_CODEC_PGM,  TC_CODEC_GIF,
    TC_CODEC_ERROR
};


/*************************************************************************/

static int is_supported(TCCodecID codec)
{
    int i, found = TC_FALSE;
    for (i = 0; !found && tc_im_codecs_video_out[i] != TC_CODEC_ERROR; i++) {
        if (codec == tc_im_codecs_video_out[i]) {
            found = TC_TRUE;
        }
    }
    return found;
}

static int tc_im_configure(TCModuleInstance *self,
                          const char *options,
                          vob_t *vob,
                          TCModuleExtraData *xdata[])
{
    TCCodecID id = TC_CODEC_ERROR;
    TCIMPrivateData *pd = NULL;
    int ret = 0;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    pd->quality = DEFAULT_QUALITY;
    pd->width   = vob->ex_v_width;
    pd->height  = vob->ex_v_height;

    pd->img_fmt[0] = '\0';

    ret = optstr_get(options, "format", "%15s", pd->img_fmt);
    if (ret != 1) {
        /* missing option, let's use the default */
        strlcpy(pd->img_fmt, DEFAULT_FORMAT, sizeof(pd->img_fmt));
    } else {
        /* the user gave us something */
        id = tc_codec_from_string(pd->img_fmt);
        if (id == TC_CODEC_ERROR) {
            tc_log_error(MOD_NAME, "unknown format: `%s'", pd->img_fmt);
            return TC_ERROR;
        }
        if (!is_supported(id)) {
            tc_log_error(MOD_NAME, "unsupported format: `%s'", pd->img_fmt);
            return TC_ERROR;
        }
    }
    

    ret = optstr_get(options, "quality", "%lu", &pd->quality);
    if (ret != 1) {
        pd->quality = DEFAULT_QUALITY;
    }

    if (verbose >= TC_INFO) {
        tc_log_info(MOD_NAME, "encoding %s with quality %lu",
                    pd->img_fmt, pd->quality);
    }

    ret = tc_magick_init(&pd->magick, pd->quality);
    if (ret != TC_OK) {
        tc_log_error(MOD_NAME, "cannot create Magick context");
        return ret;
    }
    return TC_OK;
}

static int tc_im_inspect(TCModuleInstance *self,
                         const char *param, const char **value)
{
    TCIMPrivateData *pd = NULL;
    
    TC_MODULE_SELF_CHECK(self, "inspect");

    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = tc_im_help;
    }
    if (optstr_lookup(param, "format")) {
        *value = pd->img_fmt;
    }
    if (optstr_lookup(param, "quality")) {
        tc_snprintf(pd->opt_buf, sizeof(pd->opt_buf), "%lu", pd->quality);
        *value = pd->opt_buf;
    }
    return TC_OK;
}

static int tc_im_stop(TCModuleInstance *self)
{
    TCIMPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    return tc_magick_fini(&pd->magick);
}

TC_MODULE_GENERIC_INIT(tc_im, TCIMPrivateData);

TC_MODULE_GENERIC_FINI(tc_im);

static int tc_im_encode_video(TCModuleInstance *self,
                              TCFrameVideo *inframe, TCFrameVideo *outframe)
{
    TCIMPrivateData *pd = NULL;
    int ret = TC_OK;

    TC_MODULE_SELF_CHECK(self, "encode_video");

    pd = self->userdata;

    ret = tc_magick_RGBin(&pd->magick, pd->width, pd->height,
                          inframe->video_buf);
    if (ret != TC_OK) {
        return ret;
    }

    /* doing like that won't hurt if `encode' fails */
    outframe->attributes |= TC_FRAME_IS_KEYFRAME;
    return tc_magick_frameout(&pd->magick, pd->img_fmt, outframe);
}


/*************************************************************************/

static const TCCodecID tc_im_codecs_video_in[] = { 
    TC_CODEC_RGB24, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(tc_im);
TC_MODULE_CODEC_FORMATS(tc_im);

TC_MODULE_INFO(tc_im);

static const TCModuleClass tc_im_class = {
    TC_MODULE_CLASS_HEAD(tc_im),

    .init         = tc_im_init,
    .fini         = tc_im_fini,
    .configure    = tc_im_configure,
    .stop         = tc_im_stop,
    .inspect      = tc_im_inspect,

    .encode_video = tc_im_encode_video,
};

TC_MODULE_ENTRY_POINT(tc_im);

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

