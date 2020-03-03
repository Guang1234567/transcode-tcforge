/*****************************************************************************
 *  - XviD Transcode Export Module -
 *
 *  Copyright (C) 2001-2003 - Thomas Oestreich
 *
 *  Author : Edouard Gomez <ed.gomez@free.fr>
 *
 *  Port to transcode 1.1.0+ Module System:
 *  (C) 2005-2010 Francesco Romani <fromani at gmail dot com>
 *
 *  This file is part of transcode, a video stream processing tool
 *
 *  transcode is free software ; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation ; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY ; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program ; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *****************************************************************************/

/*****************************************************************************
 * Includes
 ****************************************************************************/

#include "src/transcode.h"
#include "libtcvideo/tcvideo.h"
#include "libtcutil/cfgfile.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"
#include "libtc/tccodecs.h"

#include <string.h>
#include <math.h>

#include <xvid.h>

/*
 * notes:
 * - tc_xvid_ prefix is used to avoid any possible name clash with XviD code
 * - export_xvid4.c was a really nice and well-written module, this module
 *   is largely based on such module and share a large portion of code with
 *   it. So hopefully it should work nicely :)
 * - handling of dynamic loading of further shared objects (XviD codec, here)
 *   isn't optimal, such details should be handled by libtc.
 *   libdldarwin too should be merged with libtc, or maybe we should switch
 *   to libtool wizardry.
 */
/*****************************************************************************
 * Transcode module binding functions and strings
 ****************************************************************************/

#define MOD_NAME    "encode_xvid.so"
#define MOD_VERSION "v0.0.7 (2009-02-07)"
#define MOD_CAP     "XviD 1.1.x encoder"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_ENCODE|TC_MODULE_FEATURE_VIDEO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE


#define XVID_CONFIG_FILE "xvid.cfg"

static const char xvid_help[] = ""
    "Overview:\n"
    "    this module encodes raw RGB/YUV video frames in MPEG4, using XviD.\n"
    "    XviD is a high quality/performance ISO MPEG4 codec.\n"
    "Options:\n"
    "    help    produce module overview and options explanations\n";

/*****************************************************************************
 * XviD symbols grouped in a nice struct.
 ****************************************************************************/

typedef int (*xvid_function_t)(void *handle, int opt,
                               void *param1, void *param2);

/*****************************************************************************
 * Transcode module private data
 ****************************************************************************/

typedef struct {
    /* Instance related global vars */
    void *instance;
    xvid_gbl_init_t   xvid_gbl_init;
    xvid_enc_create_t xvid_enc_create;
    xvid_enc_frame_t  xvid_enc_frame;

    /* This data must survive local block scope, so here it is */
    xvid_enc_plugin_t    plugins[7];
    xvid_enc_zone_t      zones[2];
    xvid_plugin_single_t onepass;
    xvid_plugin_2pass1_t pass1;
    xvid_plugin_2pass2_t pass2;

    /* Options from the config file */
    xvid_enc_create_t    cfg_create;
    xvid_enc_frame_t     cfg_frame;
    xvid_plugin_single_t cfg_onepass;
    xvid_plugin_2pass2_t cfg_pass2;
    char *cfg_intra_matrix_file;
    char *cfg_inter_matrix_file;
    char *cfg_quant_method;
    int cfg_packed;
    int cfg_closed_gop;
    int cfg_interlaced;
    int cfg_quarterpel;
    int cfg_gmc;
    int cfg_trellis;
    int cfg_cartoon;
    int cfg_hqacpred;
    int cfg_chromame;
    int cfg_vhq;
    int cfg_bvhq;
    int cfg_motion;
    int cfg_stats;
    int cfg_greyscale;
    int cfg_turbo;
    int cfg_full1pass;
    int cfg_lumimask;

    /* MPEG4 stream buffer */
    int   stream_size;
    uint8_t *stream;

    /* Stats accumulators */
    int frames;
    int64_t sse_y;
    int64_t sse_u;
    int64_t sse_v;

    /* Image format conversion handle */
    TCVHandle tcvhandle;

    int flush_flag;
    int need_flush;
} XviDPrivateData;

static const char *errorstring(int err);
static void reset_module(XviDPrivateData *mod);
static void cleanup_module(XviDPrivateData *mod);
static void read_config_file(XviDPrivateData *mod);
static void dispatch_settings(XviDPrivateData *mod);
static void set_create_struct(XviDPrivateData *mod, const vob_t *vob);
static void set_frame_struct(XviDPrivateData *mod, vob_t *vob,
                             const TCFrameVideo *inframe,
                             TCFrameVideo *outframe);

/***************************************************************************/

static int tc_xvid_configure(TCModuleInstance *self,
                             const char *options,
                             TCJob *vob,
                             TCModuleExtraData *xdata[])
{
    int ret;
    XviDPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");
    TC_MODULE_SELF_CHECK(vob, "configure"); /* uhu, hackish? */

    pd = self->userdata;

    pd->flush_flag = vob->encoder_flush;
    pd->need_flush = TC_FALSE;

    /* Load the config file settings */
    read_config_file(pd);

    /* Dispatch settings to xvid structures that hold the config ready to
     * be copied to encoder structures */
    dispatch_settings(pd);

    /* Init the xvidcore lib */
    memset(&pd->xvid_gbl_init, 0, sizeof(xvid_gbl_init_t));
    pd->xvid_gbl_init.version = XVID_VERSION;

    ret = xvid_global(NULL, XVID_GBL_INIT, &pd->xvid_gbl_init, NULL);
    if (ret < 0) {
        tc_log_error(MOD_NAME, "configure: library initialization failed");
        return TC_ERROR;
    }

    /* Combine both the config settings with the transcode direct options
     * into the final xvid_enc_create_t struct */
    set_create_struct(pd, vob);
    ret = xvid_encore(NULL, XVID_ENC_CREATE, &pd->xvid_enc_create, NULL);

    if (ret < 0) {
        tc_log_error(MOD_NAME, "configure: encoder initialization failed"
                               " (XviD returned %i)", ret);
        return TC_ERROR;
    }

    /* Attach returned instance */
    pd->instance = pd->xvid_enc_create.handle;

    return TC_OK;
}


static int tc_xvid_init(TCModuleInstance *self, uint32_t features)
{
    XviDPrivateData *pd = NULL;
    vob_t *vob = tc_get_vob();

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    /* Check frame dimensions */
    if (vob->ex_v_width % 2 || vob->ex_v_height % 2) {
        tc_log_warn(MOD_NAME, "init: only even dimensions allowed (%dx%d)",
                              vob->ex_v_width, vob->ex_v_height);
        return TC_ERROR;
    }

    pd = tc_malloc(sizeof(XviDPrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: can't allocate XviD private data");
        return TC_ERROR;
    }

    /* Buffer allocation
     * We allocate width*height*bpp/8 to "receive" the compressed stream
     * I don't think the codec will ever return more than that. It's and
     * encoder, so if it fails delivering smaller frames than original
     * ones, something really odd occurs somewhere and i prefer the
     * application crash */
    if (vob->im_v_codec != TC_CODEC_YUV420P) {
        pd->tcvhandle = tcv_init();
        if (!pd->tcvhandle) {
            tc_log_warn(MOD_NAME, "init: tcv_init failed");
            goto init_failed;
        }
    }

    reset_module(pd);
    self->userdata = pd;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return TC_OK;

init_failed:
    tc_free(pd);
    self->userdata = NULL; /* paranoia */
    return TC_ERROR;
}

static int tc_xvid_inspect(TCModuleInstance *self,
                           const char *param, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");

    if (optstr_lookup(param, "help")) {
        *value = xvid_help;
    }

    return TC_OK;
}

static int tc_xvid_flush(TCModuleInstance *self, TCFrameVideo *outframe,
                         int *frame_returned)
{
    int bytes;
    xvid_enc_stats_t xvid_enc_stats;
    vob_t *vob = tc_get_vob();
    XviDPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "flush");

    pd = self->userdata;

    *frame_returned = 0;
    if (pd->flush_flag && pd->need_flush) {
        pd->need_flush = TC_FALSE;

        /* Init the stat structure */
        memset(&xvid_enc_stats, 0, sizeof(xvid_enc_stats_t));
        xvid_enc_stats.version = XVID_VERSION;

        /* Combine both the config settings with the transcode direct options
         * into the final xvid_enc_frame_t struct */
        set_frame_struct(pd, vob, NULL, outframe);

        bytes = xvid_encore(pd->instance, XVID_ENC_ENCODE,
                            &pd->xvid_enc_frame, &xvid_enc_stats);

        outframe->video_len = bytes;
        if (bytes > 0) {
            *frame_returned = 0;
            /* Extract stats info */
            if (xvid_enc_stats.type > 0 && pd->cfg_stats) {
                pd->frames++;
                pd->sse_y += xvid_enc_stats.sse_y;
                pd->sse_u += xvid_enc_stats.sse_u;
                pd->sse_v += xvid_enc_stats.sse_v;
            }

            if (pd->xvid_enc_frame.out_flags & XVID_KEYFRAME) {
                outframe->attributes |= TC_FRAME_IS_KEYFRAME;
            }
        }
    }

    return TC_OK;
}


static int tc_xvid_encode_video(TCModuleInstance *self,
                                TCFrameVideo *inframe, TCFrameVideo *outframe)
{
    int bytes;
    xvid_enc_stats_t xvid_enc_stats;
    vob_t *vob = tc_get_vob();
    XviDPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "encode_video");

    pd = self->userdata;

    /* Init the stat structure */
    memset(&xvid_enc_stats, 0, sizeof(xvid_enc_stats_t));
    xvid_enc_stats.version = XVID_VERSION;

    if(vob->im_v_codec == TC_CODEC_YUV422P) {
        /* Convert to UYVY */
        tcv_convert(pd->tcvhandle, inframe->video_buf, inframe->video_buf,
                    vob->ex_v_width, vob->ex_v_height, IMG_YUV422P, IMG_UYVY);
    } else if (vob->im_v_codec == TC_CODEC_RGB24) {
        /* Convert to BGR (why isn't RGB supported??) */
        tcv_convert(pd->tcvhandle, inframe->video_buf, inframe->video_buf,
                    vob->ex_v_width, vob->ex_v_height, IMG_RGB24, IMG_BGR24);
    }
    /* Combine both the config settings with the transcode direct options
     * into the final xvid_enc_frame_t struct */
    set_frame_struct(pd, vob, inframe, outframe);

    bytes = xvid_encore(pd->instance, XVID_ENC_ENCODE,
                        &pd->xvid_enc_frame, &xvid_enc_stats);

    /* Error handling */
    if (bytes < 0) {
        tc_log_error(MOD_NAME, "encode_video: xvidcore returned"
                               " an error: \"%s\"",
                               errorstring(bytes));
        return TC_ERROR;
    }
    outframe->video_len = bytes;

    /* There may now be data that needs flushing */
    pd->need_flush = TC_TRUE;

    /* Extract stats info */
    if (xvid_enc_stats.type > 0 && pd->cfg_stats) {
        pd->frames++;
        pd->sse_y += xvid_enc_stats.sse_y;
        pd->sse_u += xvid_enc_stats.sse_u;
        pd->sse_v += xvid_enc_stats.sse_v;
    }

    /* XviD Core rame buffering handling
    * We must make sure audio A/V is still good and does not run away */
    if (bytes == 0) {
        outframe->attributes |= TC_FRAME_IS_DELAYED;
        return TC_OK;
    }

    if (pd->xvid_enc_frame.out_flags & XVID_KEYFRAME) {
        outframe->attributes |= TC_FRAME_IS_KEYFRAME;
    }

    return TC_OK;
}

#define SSE2PSNR(sse, width, height) \
((!(sse)) ? (99.0f) : (48.131f - 10*(float)log10((float)(sse)/((float)((width)*(height))))))

static int tc_xvid_stop(TCModuleInstance *self)
{
    int ret;
    XviDPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    /* ToDo: Can we flush the last frames here ? */

    if (pd->instance != NULL) {
        /* Destroy the encoder instance */
        ret = xvid_encore(pd->instance, XVID_ENC_DESTROY, NULL, NULL);
        if (ret < 0) {
            tc_log_warn(MOD_NAME, "stop: encoder instance releasing failed");
            return TC_ERROR;
        }

        /* Print stats before resting the complete module structure */
        if (pd->cfg_stats) {
            if(pd->frames > 0) {
                pd->sse_y /= pd->frames;
                pd->sse_u /= pd->frames;
                pd->sse_v /= pd->frames;
            } else {
                pd->sse_y = 0;
                pd->sse_u = 0;
                pd->sse_v = 0;
            }

            tc_log_info(MOD_NAME,
                        "psnr y = %.2f dB, "
                        "psnr u = %.2f dB, "
                        "psnr v = %.2f dB",
                        SSE2PSNR(pd->sse_y,
                             pd->xvid_enc_create.width,
                             pd->xvid_enc_create.height),
                        SSE2PSNR(pd->sse_u,
                             pd->xvid_enc_create.width/2,
                             pd->xvid_enc_create.height/2),
                        SSE2PSNR(pd->sse_v,
                             pd->xvid_enc_create.width/2,
                             pd->xvid_enc_create.height/2));
        }
        pd->instance = NULL;
    }

    pd->need_flush = TC_FALSE;
    return TC_OK;
}
#undef SSE2PSNR

static int tc_xvid_fini(TCModuleInstance *self)
{
    XviDPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "fini");

    tc_xvid_stop(self);

    pd = self->userdata;

    /* Free all dynamic memory allocated in the module structure */
    cleanup_module(pd);

    /* This is the last function according to the transcode API
     * this should be safe to reset the module structure */
    reset_module(pd);

    tc_free(self->userdata);
    self->userdata = NULL;

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID tc_xvid_codecs_video_in[] = {
    TC_CODEC_RGB24, TC_CODEC_YUV422P, TC_CODEC_YUV420P,
    TC_CODEC_ERROR
};

static const TCCodecID tc_xvid_codecs_video_out[] = {
    TC_CODEC_MPEG4VIDEO,
    TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(tc_xvid);
TC_MODULE_CODEC_FORMATS(tc_xvid);

TC_MODULE_INFO(tc_xvid);

static const TCModuleClass xvid_class = {
    TC_MODULE_CLASS_HEAD(tc_xvid),

    .init         = tc_xvid_init,
    .fini         = tc_xvid_fini,
    .configure    = tc_xvid_configure,
    .stop         = tc_xvid_stop,
    .inspect      = tc_xvid_inspect,

    .encode_video = tc_xvid_encode_video,
    .flush_video  = tc_xvid_flush,
};

TC_MODULE_ENTRY_POINT(xvid)


/*****************************************************************************
 * Transcode module helper functions
 ****************************************************************************/

static void reset_module(XviDPrivateData *mod)
{
    /* Default options */
    mod->cfg_packed = 0;
    mod->cfg_closed_gop = 1;
    mod->cfg_interlaced = 0;
    mod->cfg_quarterpel = 0;
    mod->cfg_gmc = 0;
    mod->cfg_trellis = 0;
    mod->cfg_cartoon = 0;
    mod->cfg_hqacpred = 1;
    mod->cfg_chromame = 1;
    mod->cfg_vhq = 1;
    mod->cfg_bvhq = 0;
    mod->cfg_motion = 6;
    mod->cfg_turbo = 0;
    mod->cfg_full1pass = 0;
    mod->cfg_stats = 0;
    mod->cfg_greyscale = 0;
    mod->cfg_quant_method = "h263";
    mod->cfg_create.max_bframes = 1;
    mod->cfg_create.bquant_ratio = 150;
    mod->cfg_create.bquant_offset = 100;
    mod->cfg_lumimask = 0;
}

static void cleanup_module(XviDPrivateData *mod)
{

    /* Free tcvideo handle */
    if (mod->tcvhandle != NULL) {
        tcv_free(mod->tcvhandle);
        mod->tcvhandle = NULL;
    }

    /* Release stream buffer memory */
    if(mod->stream != NULL) {
        free(mod->stream);
        mod->stream = NULL;
    }

    /* Release the matrix file name string */
    if(mod->cfg_inter_matrix_file != NULL) {
        tc_free(mod->cfg_inter_matrix_file);
        mod->cfg_inter_matrix_file = NULL;
    }

    /* Release the matrix definition */
    if(mod->cfg_frame.quant_inter_matrix != NULL) {
        tc_free(mod->cfg_frame.quant_inter_matrix);
        mod->cfg_frame.quant_inter_matrix = NULL;
    }

    /* Release the matrix file name string */
    if(mod->cfg_intra_matrix_file != NULL) {
        tc_free(mod->cfg_intra_matrix_file);
        mod->cfg_intra_matrix_file = NULL;
    }

    /* Release the matrix definition */
    if(mod->cfg_frame.quant_intra_matrix != NULL) {
        tc_free(mod->cfg_frame.quant_intra_matrix);
        mod->cfg_frame.quant_intra_matrix = NULL;
    }
}

/*****************************************************************************
 * Configuration functions
 *
 * They fill the .cfg_xxx members of the module structure.
 *  - read_config_file reads the values from the config file and sets .cfg_xxx
 *    members of the module structure.
 *  - dispatch_settings uses the values retrieved by read_config_file and
 *    turns them into XviD settings in the cfg_xxx xvid structure available
 *    in the module structure.
 *  - set_create_struct sets a xvid_enc_create structure according to the
 *    settings generated by the two previous functions calls.
 *  - set_frame_struct same as above for a xvid_enc_frame_t struct.
 ****************************************************************************/

#define INTRA_MATRIX    0
#define INTER_MATRIX    1

static void load_matrix(XviDPrivateData *mod, int type)
{
    xvid_enc_frame_t *frame = &mod->cfg_frame;
    const char *filename = (type == INTER_MATRIX)
                                    ?mod->cfg_inter_matrix_file
                                    :mod->cfg_intra_matrix_file;
    uint8_t *matrix = NULL;

    if (!filename) {
        return;
    }

    matrix = tc_malloc(TC_MATRIX_SIZE);
    if (matrix != NULL) {
        int ret =  tc_read_matrix(filename, matrix, NULL);

        if (ret == 0) {
            tc_log_info(MOD_NAME, "Loaded %s matrix (switching to "
                                  "mpeg quantization type)",
                                  (type == INTER_MATRIX) ?"Inter" :"Intra");
                //print_matrix(matrix, NULL);
                //free(mod->cfg_quant_method);
                mod->cfg_quant_method = "mpeg";
        } else {
            tc_free(matrix);
            matrix = NULL;
        }
    }

    if (type == INTER_MATRIX) {
        frame->quant_inter_matrix = matrix;
    } else {
        frame->quant_intra_matrix = matrix;
    }
}

static void read_config_file(XviDPrivateData *mod)
{
    xvid_plugin_single_t *onepass = &mod->cfg_onepass;
    xvid_plugin_2pass2_t *pass2   = &mod->cfg_pass2;
    xvid_enc_create_t    *create  = &mod->cfg_create;
    xvid_enc_frame_t     *frame   = &mod->cfg_frame;

    TCConfigEntry xvid_config[] = {
            /* Section [features] */
//            {"features", "Feature settings", TCCONF_TYPE_SECTION, 0, 0, 0},
            {"quant_type", &mod->cfg_quant_method, TCCONF_TYPE_STRING, 0, 0, 0},
            {"motion", &mod->cfg_motion, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 6},
            {"chromame", &mod->cfg_chromame, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"vhq", &mod->cfg_vhq, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 4},
            {"bvhq", &mod->cfg_bvhq, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"max_bframes", &create->max_bframes, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 20},
            {"bquant_ratio", &create->bquant_ratio, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 200},
            {"bquant_offset", &create->bquant_offset, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 200},
            {"bframe_threshold", &frame->bframe_threshold, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -255, 255},
            {"quarterpel", &mod->cfg_quarterpel, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"gmc", &mod->cfg_gmc, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"trellis", &mod->cfg_trellis, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"packed", &mod->cfg_packed, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"closed_gop", &mod->cfg_closed_gop, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"interlaced", &mod->cfg_interlaced, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"cartoon", &mod->cfg_cartoon, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"hqacpred", &mod->cfg_hqacpred, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"frame_drop_ratio", &create->frame_drop_ratio, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"stats", &mod->cfg_stats, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"greyscale", &mod->cfg_greyscale, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"turbo", &mod->cfg_turbo, TCCONF_TYPE_FLAG, 0, 0, 1},
#if XVID_API >= XVID_MAKE_API(4,1)
            {"threads", &create->num_threads, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 8},
#endif            
            {"full1pass", &mod->cfg_full1pass, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"luminance_masking", &mod->cfg_lumimask, TCCONF_TYPE_FLAG, 0, 0, 1},

            /* section [quantizer] */
//            {"quantizer", "Quantizer settings", TCCONF_TYPE_SECTION, 0, 0, 0},
            {"min_iquant", &create->min_quant[0], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"max_iquant", &create->max_quant[0], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"min_pquant", &create->min_quant[1], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"max_pquant", &create->max_quant[1], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"min_bquant", &create->min_quant[2], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"max_bquant", &create->max_quant[2], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"quant_intra_matrix", &mod->cfg_intra_matrix_file, TCCONF_TYPE_STRING, 0, 0, 100},
            {"quant_inter_matrix", &mod->cfg_inter_matrix_file, TCCONF_TYPE_STRING, 0, 0, 100},

            /* section [cbr] */
//            {"cbr", "CBR settings", TCCONF_TYPE_SECTION, 0, 0, 0},
            {"reaction_delay_factor", &onepass->reaction_delay_factor, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"averaging_period", &onepass->averaging_period, TCCONF_TYPE_INT, TCCONF_FLAG_MIN, 0, 0},
            {"buffer", &onepass->buffer, TCCONF_TYPE_INT, TCCONF_FLAG_MIN, 0, 0},

            /* section [vbr] */
//            {"vbr", "VBR settings", TCCONF_TYPE_SECTION, 0, 0, 0},
            {"keyframe_boost", &pass2->keyframe_boost, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"curve_compression_high", &pass2->curve_compression_high, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"curve_compression_low", &pass2->curve_compression_low, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"overflow_control_strength", &pass2->overflow_control_strength, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"max_overflow_improvement", &pass2->max_overflow_improvement, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"max_overflow_degradation", &pass2->max_overflow_degradation, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"kfreduction", &pass2->kfreduction, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"kfthreshold", &pass2->kfthreshold, TCCONF_TYPE_INT, TCCONF_FLAG_MIN, 0, 0},
            {"container_frame_overhead", &pass2->container_frame_overhead, TCCONF_TYPE_INT, TCCONF_FLAG_MIN, 0, 0},

            /* End of the config file */
            {NULL, 0, 0, 0, 0, 0}
    };
    const char *dirs[] = { ".", NULL };

    /* Read the values */
    tc_config_read_file(dirs, XVID_CONFIG_FILE, NULL, xvid_config, MOD_NAME);

    /* Print the values */
    if (verbose & TC_DEBUG) {
        tc_config_print(xvid_config, MOD_NAME);
    }

    return;
}

static void dispatch_settings(XviDPrivateData *mod)
{

    xvid_enc_create_t *create = &mod->cfg_create;
    xvid_enc_frame_t  *frame  = &mod->cfg_frame;

    const int motion_presets[7] =
        {
            0,
            0,
            0,
            0,
            XVID_ME_HALFPELREFINE16,
            XVID_ME_HALFPELREFINE16 | XVID_ME_ADVANCEDDIAMOND16,
            XVID_ME_HALFPELREFINE16 | XVID_ME_EXTSEARCH16 |
            XVID_ME_HALFPELREFINE8  | XVID_ME_USESQUARES16
        };


    /* Dispatch all settings having an impact on the "create" structure */
    create->global = 0;

    if (mod->cfg_packed) {
        create->global |= XVID_GLOBAL_PACKED;
    }
    if (mod->cfg_closed_gop) {
        create->global |= XVID_GLOBAL_CLOSED_GOP;
    }
    if (mod->cfg_stats) {
        create->global |= XVID_GLOBAL_EXTRASTATS_ENABLE;
    }
    /* Dispatch all settings having an impact on the "frame" structure */
    frame->vol_flags = 0;
    frame->vop_flags = 0;
    frame->motion    = 0;

    frame->vop_flags |= XVID_VOP_HALFPEL;
    frame->motion    |= motion_presets[mod->cfg_motion];

    if (mod->cfg_stats) {
        frame->vol_flags |= XVID_VOL_EXTRASTATS;
    }
    if (mod->cfg_greyscale) {
        frame->vop_flags |= XVID_VOP_GREYSCALE;
    }
    if (mod->cfg_cartoon) {
        frame->vop_flags |= XVID_VOP_CARTOON;
        frame->motion |= XVID_ME_DETECT_STATIC_MOTION;
    }

    load_matrix(mod, INTRA_MATRIX);
    load_matrix(mod, INTER_MATRIX);

    if (!strcasecmp(mod->cfg_quant_method, "mpeg")) {
        frame->vol_flags |= XVID_VOL_MPEGQUANT;
    }
    if (mod->cfg_quarterpel) {
        frame->vol_flags |= XVID_VOL_QUARTERPEL;
        frame->motion    |= XVID_ME_QUARTERPELREFINE16;
        frame->motion    |= XVID_ME_QUARTERPELREFINE8;
    }
    if (mod->cfg_gmc) {
        frame->vol_flags |= XVID_VOL_GMC;
        frame->motion    |= XVID_ME_GME_REFINE;
    }
    if (mod->cfg_interlaced) {
        frame->vol_flags |= XVID_VOL_INTERLACING;
    }
    if (mod->cfg_trellis) {
        frame->vop_flags |= XVID_VOP_TRELLISQUANT;
    }
    if (mod->cfg_hqacpred) {
        frame->vop_flags |= XVID_VOP_HQACPRED;
    }
    if (mod->cfg_motion > 4) {
        frame->vop_flags |= XVID_VOP_INTER4V;
    }
    if (mod->cfg_chromame) {
        frame->motion |= XVID_ME_CHROMA_PVOP;
        frame->motion |= XVID_ME_CHROMA_BVOP;
    }
    if (mod->cfg_vhq >= 1) {
        frame->vop_flags |= XVID_VOP_MODEDECISION_RD;
    }
    if (mod->cfg_vhq >= 2) {
        frame->motion |= XVID_ME_HALFPELREFINE16_RD;
        frame->motion |= XVID_ME_QUARTERPELREFINE16_RD;
    }
    if (mod->cfg_vhq >= 3) {
        frame->motion |= XVID_ME_HALFPELREFINE8_RD;
        frame->motion |= XVID_ME_QUARTERPELREFINE8_RD;
        frame->motion |= XVID_ME_CHECKPREDICTION_RD;
    }
    if (mod->cfg_vhq >= 4) {
        frame->motion |= XVID_ME_EXTSEARCH_RD;
    }
    if (mod->cfg_turbo) {
        frame->motion |= XVID_ME_FASTREFINE16;
        frame->motion |= XVID_ME_FASTREFINE8;
        frame->motion |= XVID_ME_SKIP_DELTASEARCH;
        frame->motion |= XVID_ME_FAST_MODEINTERPOLATE;
        frame->motion |= XVID_ME_BFRAME_EARLYSTOP;
    }
    if (mod->cfg_bvhq) {
#if XVID_API >= XVID_MAKE_API(4,1)
        frame->vop_flags |= XVID_VOP_RD_BVOP;
#endif
    }

    /* motion level == 0 means no motion search which is equivalent to
     * intra coding only */
    if (mod->cfg_motion == 0) {
        frame->type = XVID_TYPE_IVOP;
    } else {
        frame->type = XVID_TYPE_AUTO;
    }

    return;
}

static void set_create_struct(XviDPrivateData *mod, const vob_t *vob)
{
    xvid_enc_create_t *x    = &mod->xvid_enc_create;
    xvid_enc_create_t *xcfg = &mod->cfg_create;

    memset(x, 0, sizeof(xvid_enc_create_t));
    x->version = XVID_VERSION;

    /* Global encoder options */
    x->global = xcfg->global;

    /* Width and Height */
    x->width  = vob->ex_v_width;
    x->height = vob->ex_v_height;

    /* Max keyframe interval */
    x->max_key_interval = vob->divxkeyframes;

    /* FPS : we take care of non integer values */
    if ((vob->ex_fps - (int)vob->ex_fps) == 0) {
        x->fincr = 1;
        x->fbase = (int)vob->ex_fps;
    } else {
        x->fincr = 1001;
        x->fbase = (int)(1001 * vob->ex_fps);
    }

    /* BFrames settings */
    x->max_bframes   = xcfg->max_bframes;
    x->bquant_ratio  = xcfg->bquant_ratio;
    x->bquant_offset = xcfg->bquant_offset;

    /* Frame dropping factor */
    x->frame_drop_ratio = xcfg->frame_drop_ratio;

    /* Quantizers */
    x->min_quant[0] = xcfg->min_quant[0];
    x->min_quant[1] = xcfg->min_quant[1];
    x->min_quant[2] = xcfg->min_quant[2];
    x->max_quant[0] = xcfg->max_quant[0];
    x->max_quant[1] = xcfg->max_quant[1];
    x->max_quant[2] = xcfg->max_quant[2];

    /* Encodings zones
     * ToDo?: Allow zones definitions */
    memset(mod->zones, 0, sizeof(mod->zones));
    x->zones     = mod->zones;

    if (1 == vob->divxmultipass && mod->cfg_full1pass)
    {
        x->zones[0].frame = 0;
        x->zones[0].mode = XVID_ZONE_QUANT;
        x->zones[0].increment = 200;
        x->zones[0].base = 100;
        x->num_zones = 1;
    } else {
        x->num_zones = 0;
    }

    /* Plugins */
    memset(mod->plugins, 0, sizeof(mod->plugins));
    x->plugins     = mod->plugins;
    x->num_plugins = 0;

    /* Initialize rate controller plugin */

    /* This is the first pass of a Two pass process */
    if (vob->divxmultipass == 1) {
        xvid_plugin_2pass1_t *pass1 = &mod->pass1;

        memset(pass1, 0, sizeof(xvid_plugin_2pass1_t));
        pass1->version  = XVID_VERSION;
        pass1->filename = (char*)vob->divxlogfile; /* XXX: ugh */

        x->plugins[x->num_plugins].func  = xvid_plugin_2pass1;
        x->plugins[x->num_plugins].param = pass1;
        x->num_plugins++;
    }

    /* This is the second pass of a Two pass process */
    if (vob->divxmultipass == 2) {
        xvid_plugin_2pass2_t *pass2 = &mod->pass2;
        xvid_plugin_2pass2_t *pass2cfg = &mod->cfg_pass2;

        memset(pass2, 0, sizeof(xvid_plugin_2pass2_t));
        pass2->version  = XVID_VERSION;
        pass2->filename = (char*)vob->divxlogfile; /* XXX: ugh */

        /* Apply config file settings if any, or all 0s which lets XviD
         * apply its defaults */
        pass2->keyframe_boost = pass2cfg->keyframe_boost;
        pass2->curve_compression_high = pass2cfg->curve_compression_high;
        pass2->curve_compression_low = pass2cfg->curve_compression_low;
        pass2->overflow_control_strength = pass2cfg->overflow_control_strength;
        pass2->max_overflow_improvement = pass2cfg->max_overflow_improvement;
        pass2->max_overflow_degradation = pass2cfg->max_overflow_degradation;
        pass2->kfreduction = pass2cfg->kfreduction;
        pass2->kfthreshold = pass2cfg->kfthreshold;
        pass2->container_frame_overhead = pass2cfg->container_frame_overhead;

        /* Positive bitrate values are bitrates as usual but if the
         * value is negative it is considered as being a total size
         * to reach (in kilobytes) */
        if (vob->divxbitrate > 0) {
            pass2->bitrate  = vob->divxbitrate*1000;
        } else {
            pass2->bitrate  = vob->divxbitrate;
        }
        x->plugins[x->num_plugins].func  = xvid_plugin_2pass2;
        x->plugins[x->num_plugins].param = pass2;
        x->num_plugins++;
    }

    /* This is a single pass encoding: either a CBR pass or a constant
     * quantizer pass */
    if (vob->divxmultipass == 0  || vob->divxmultipass == 3) {
        xvid_plugin_single_t *onepass = &mod->onepass;
        xvid_plugin_single_t *cfgonepass = &mod->cfg_onepass;

        memset(onepass, 0, sizeof(xvid_plugin_single_t));
        onepass->version = XVID_VERSION;
        onepass->bitrate = vob->divxbitrate*1000;

        /* Apply config file settings if any, or all 0s which lets XviD
         * apply its defaults */
        onepass->reaction_delay_factor = cfgonepass->reaction_delay_factor;
        onepass->averaging_period = cfgonepass->averaging_period;
        onepass->buffer = cfgonepass->buffer;

        /* Quantizer mode uses the same plugin, we have only to define
         * a constant quantizer zone beginning at frame 0 */
        if (vob->divxmultipass == 3) {
            x->zones[x->num_zones].mode      = XVID_ZONE_QUANT;
            x->zones[x->num_zones].frame     = 1;
            x->zones[x->num_zones].increment = vob->divxbitrate;
            x->zones[x->num_zones].base      = 1;
            x->num_zones++;
        }


        x->plugins[x->num_plugins].func  = xvid_plugin_single;
        x->plugins[x->num_plugins].param = onepass;
        x->num_plugins++;
    }

    if (mod->cfg_lumimask) {
#if XVID_API >= XVID_MAKE_API(4,1)
        x->plugins[x->num_plugins].func  = xvid_plugin_lumimasking;
        x->plugins[x->num_plugins].param = NULL;
        x->num_plugins++;
#endif
    }

    return;
}

static void set_frame_struct(XviDPrivateData *mod, vob_t *vob,
                             const TCFrameVideo *inframe, TCFrameVideo *outframe)
{
    xvid_enc_frame_t *x    = &mod->xvid_enc_frame;
    xvid_enc_frame_t *xcfg = &mod->cfg_frame;

    memset(x, 0, sizeof(xvid_enc_frame_t));
    x->version = XVID_VERSION;

    /* Bind output buffer */
    x->bitstream = outframe->video_buf;

    if (!inframe) { /* flush request */
        x->length          = -1;
        x->input.csp       = XVID_CSP_NULL;
        x->input.plane[0]  = NULL;
//        x->input.plane[1]  = NULL;
//        x->input.plane[2]  = NULL;
        x->input.stride[0] = 0;
//        x->input.stride[1] = 0;
//        x->input.stride[2] = 0;
    } else {
        x->length    = outframe->video_size;

        /* Bind source frame */
        x->input.plane[0] = inframe->video_buf;
        if (vob->im_v_codec == TC_CODEC_RGB24) {
            x->input.csp       = XVID_CSP_BGR;
            x->input.stride[0] = vob->ex_v_width*3;
        } else if (vob->im_v_codec == TC_CODEC_YUV422P) {
            x->input.csp       = XVID_CSP_UYVY;
            x->input.stride[0] = vob->ex_v_width*2;
        } else {
            x->input.csp       = XVID_CSP_I420;
            x->input.stride[0] = vob->ex_v_width;
        }
    }
    /* Set up core's VOL level features */
    x->vol_flags = xcfg->vol_flags;

    /* Set up core's VOP level features */
    x->vop_flags = xcfg->vop_flags;

    /* Frame type -- let core decide for us */
    x->type = xcfg->type;

    /* Force the right quantizer -- It is internally managed by RC
     * plugins */
    x->quant = 0;

    /* Set up motion estimation flags */
    x->motion = xcfg->motion;

    /* We don't use special matrices */
    x->quant_intra_matrix = xcfg->quant_intra_matrix;
    x->quant_inter_matrix = xcfg->quant_inter_matrix;

    /* pixel aspect ratio
     * transcode.c uses 0 for EXT instead of 15 */
    if (vob->ex_par == 0) {
        x->par = XVID_PAR_EXT;
        x->par_width = vob->ex_par_width;
        x->par_height = vob->ex_par_height;
    } else {
        x->par = vob->ex_par;
        /* par_{width,height} already set to zero above */
    }
    return;
}

/*****************************************************************************
 * Returns an error string corresponding to the XviD err code
 ****************************************************************************/

static const char *errorstring(int err)
{
    const char *error;

    switch(err) {
      case XVID_ERR_FAIL:
        error = "General fault";
        break;
      case XVID_ERR_MEMORY:
        error =  "Memory allocation error";
        break;
      case XVID_ERR_FORMAT:
        error =  "File format error";
        break;
      case XVID_ERR_VERSION:
        error =  "Structure version not supported";
        break;
      case XVID_ERR_END:
        error =  "End of stream reached";
        break;
      default:
        error = "Unknown";
    }

    return error;
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
