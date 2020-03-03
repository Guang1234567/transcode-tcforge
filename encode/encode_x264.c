/*
 * encode_x264.c - encodes video using the x264 library
 * Written by Christian Bodenstedt, with NMS adaptation and other changes
 * by Andrew Church
 * 
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

/*
 * Many parts of this file are taken from FFMPEGs "libavcodec/x264.c",
 * which is licensed under LGPL. Other sources of information were
 * "export_ffmpeg.c", X264s "x264.c" and MPlayers "libmpcodecs/ve_x264.c"
 * (all licensed GPL afaik).
 */


#include "src/transcode.h"
#include "aclib/ac.h"
#include "libtc/libtc.h"
#include "libtc/ratiocodes.h"
#include "libtcutil/cfgfile.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

#include <x264.h>
#if X264_BUILD < 89
# error x264 version 89 or later is required
#endif


#define MOD_NAME    "encode_x264.so"
#define MOD_VERSION "v0.4.0 (2010-03-29)"
#define MOD_CAP     "x264 encoder"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_ENCODE|TC_MODULE_FEATURE_VIDEO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE


/* Module configuration file */
#define X264_CONFIG_FILE        "x264.cfg"
#define X264_HEADER_LEN_MAX     1024
/* just try something "big enough" */

/* Private data for this module */
typedef struct {
    int framenum;
    int interval;
    int width;
    int height;
    int flush_flag;
    x264_param_t x264params;
    x264_t *enc;
    int twopass_bug_workaround;  // Work around x264 logfile generation bug?
    char twopass_log_path[4096]; // Logfile path (for 2-pass bug workaround)

    /* extradata (header) support */
    uint8_t hdr_buf[X264_HEADER_LEN_MAX];
    size_t hdr_len;
} X264PrivateData;

/* Static structure to provide pointers for configuration entries */
static struct confdata_struct {
    x264_param_t x264params;
    /* Local parameters */
    int twopass_bug_workaround;
} confdata;

/*************************************************************************/

/* This array describes all option-names, pointers to where their
 * values are stored and the allowed ranges. It's needed to parse the
 * x264.cfg file using libtc. */

/* Use e.g. OPTION("overscan", vui.i_overscan) for x264params.vui.i_overscan */
#define OPTION(field,name,type,flag,low,high) \
    {name, &confdata.x264params.field, (type), (flag), (low), (high)},

/* Option to turn a flag on or off; the off version will have "no" prepended */
#define OPT_FLAG(field,name) \
    OPTION(field,      name, TCCONF_TYPE_FLAG, 0, 0, 1) \
    OPTION(field, "no" name, TCCONF_TYPE_FLAG, 0, 1, 0)
/* Integer option with range */
#define OPT_RANGE(field,name,low,high) \
    OPTION(field, name, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, (low), (high))
/* Floating-point option */
#define OPT_FLOAT(field,name) \
    OPTION(field, name, TCCONF_TYPE_FLOAT, 0, 0, 0)
/* Floating-point option with range */
#define OPT_RANGF(field,name,low,high) \
    OPTION(field, name, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, (low), (high))
/* String option */
#define OPT_STR(field,name) \
    OPTION(field, name, TCCONF_TYPE_STRING, 0, 0, 0)
/* Dummy entry that doesn't generate an option (placeholder) */
#define OPT_NONE(field) /*nothing*/

static TCConfigEntry conf[] ={

    /* CPU flags */

    /* CPU acceleration flags (we leave the x264 default alone) */
    OPT_NONE (cpu)
    /* Number of parallel encoding threads to use */
    OPT_RANGE(i_threads,                  "threads",        0,     4)
    /* Whether to use slice-based threading */
    OPT_FLAG (b_sliced_threads,           "sliced_threads")
    /* Whether to avoid non-deterministic optimizations when threaded */
    OPT_FLAG (b_deterministic,            "deterministic")
    /* Threaded lookahead buffer */
    OPT_NONE (i_sync_lookahead)

    /* Video Properties */

    OPT_NONE (i_width)
    OPT_NONE (i_height)
    OPT_NONE (i_csp)  /* CSP of encoded bitstream, only i420 supported */
    /* H.264 level (1.0 ... 5.1) */
    OPT_RANGE(i_level_idc,                "level_idc",     10,    51)
    OPT_NONE (i_frame_total) /* number of frames to encode if known, else 0 */

    /* Add NAL HRD parameters to the bitstream */
    /* OPT_FLAG is intentional here; we don't currently support CBR encoding
     * (FIXME) */
    OPT_FLAG (i_nal_hrd,                  "nal_hrd")

    /* they will be reduced to be 0 < x <= 65535 and prime */
    OPT_NONE (vui.i_sar_height)
    OPT_NONE (vui.i_sar_width)

    /* 0=undef, 1=show, 2=crop */
    OPT_RANGE(vui.i_overscan,             "overscan",       0,     2)

    /* 0=component 1=PAL 2=NTSC 3=SECAM 4=Mac 5=undef */
    OPT_RANGE(vui.i_vidformat,            "vidformat",      0,     5)
    OPT_FLAG (vui.b_fullrange,            "fullrange")
    /* 1=bt709 2=undef 4=bt470m 5=bt470bg 6=smpte170m 7=smpte240m 8=film */
    OPT_RANGE(vui.i_colorprim,            "colorprim",      0,     8)
    /* 1..7 as above, 8=linear, 9=log100, 10=log316 */
    OPT_RANGE(vui.i_transfer,             "transfer",       0,    10)
    /* 0=GBR 1=bt709 2=undef 4=fcc 5=bt470bg 6=smpte170m 7=smpte240m 8=YCgCo */
    OPT_RANGE(vui.i_colmatrix,            "colmatrix",      0,     8)
    /* ??? */
    OPT_RANGE(vui.i_chroma_loc,           "chroma_loc",     0,     5)

    OPT_NONE (i_fps_num)
    OPT_NONE (i_fps_den)

    /* Bitstream parameters */

    /* Maximum number of reference frames */
    OPT_RANGE(i_frame_reference,          "frameref",       1,    16)
    /* Force an IDR keyframe at this interval */
    OPT_RANGE(i_keyint_max,               "keyint",         1,999999)
    OPT_RANGE(i_keyint_max,               "keyint_max",     1,999999)
    /* Scenecuts closer together than this are coded as I, not IDR. */
    OPT_RANGE(i_keyint_min,               "keyint_min",     1,999999)
    /* How aggressively to insert extra I frames */
    OPT_RANGE(i_scenecut_threshold,       "scenecut",      -1,   100)
    /* Whether to use periodic intra refresh instead of IDR frames */
    OPT_FLAG (b_intra_refresh,            "intra_refresh")

    /* How many B-frames between 2 reference pictures */
    OPT_RANGE(i_bframe,                   "bframes",        0,    16)
    /* Use adaptive B-frame encoding */
    OPT_RANGE(i_bframe_adaptive,          "b_adapt",        0,     2)
    /* How often B-frames are used */
    OPT_RANGE(i_bframe_bias,              "b_bias",       -90,   100)
    /* Keep some B-frames as references */
    OPT_RANGE(i_bframe_pyramid,           "b_pyramid",      0,     2)

    /* Use deblocking filter */
    OPT_FLAG (b_deblocking_filter,        "deblock")
    /* [-6, 6] -6 light filter, 6 strong */
    OPT_RANGE(i_deblocking_filter_alphac0,"deblockalpha",  -6,     6)
    /* [-6, 6]  idem */
    OPT_RANGE(i_deblocking_filter_beta,   "deblockbeta",   -6,     6)

    /* Use context-adaptive binary arithmetic coding */
    OPT_FLAG (b_cabac,                    "cabac")
    /* Initial data for CABAC? */
    OPT_RANGE(i_cabac_init_idc,           "cabac_init_idc", 0,     2)

    /* Enable interlaced encoding (--encode_fields) */
    OPT_NONE (b_interlaced)

    OPT_NONE (constrained_intra)

    /* Quantization matrix selection: 0=flat 1=JVT 2=custom */
    OPT_RANGE(i_cqm_preset,               "cqm",            0,     2)
    /* Custom quantization matrix filename */
    OPT_STR  (psz_cqm_file,               "cqm_file")
    /* Quantization matrix arrays set up by library */

    /* Logging */

    OPT_NONE (pf_log)
    OPT_NONE (p_log_private)
    OPT_NONE (i_log_level)
    OPT_NONE (b_visualize)

    /* Encoder analyser parameters */

    /* Partition selection (we always enable everything) */
    OPT_NONE (analyse.intra)
    OPT_NONE (analyse.inter)
    /* Allow integer 8x8 DCT transforms */
    OPT_FLAG (analyse.b_transform_8x8,    "8x8dct")
    /* Weighting for P-frames */
    OPT_RANGE(analyse.i_weighted_pred,    "weight_p",       0,     2)
    /* Implicit weighting for B-frames */
    OPT_FLAG (analyse.b_weighted_bipred,  "weight_b")
    /* Spatial vs temporal MV prediction, 0=none 1=spatial 2=temporal 3=auto */
    OPT_RANGE(analyse.i_direct_mv_pred,   "direct_pred",    0,     3)
    /* QP difference between chroma and luma */
    OPT_RANGE(analyse.i_chroma_qp_offset, "chroma_qp_offset",-12, 12)

    /* Motion estimation algorithm to use (X264_ME_*) 0=dia 1=hex 2=umh 3=esa*/
    OPT_RANGE(analyse.i_me_method,        "me",             0,     3)
    /* Integer pixel motion estimation search range (from predicted MV) */
    OPT_RANGE(analyse.i_me_range,         "me_range",       4,    64)
    /* Maximum length of a MV (in pixels), 32-2048 or -1=auto */
    OPT_RANGE(analyse.i_mv_range,         "mv_range",      -1,  2048)
    /* Maximum length of a MV (in pixels), 32-2048 or -1=auto */
    OPT_RANGE(analyse.i_mv_range_thread,  "mv_range_thread",-1, 2048)
    /* Subpixel motion estimation quality: 1=fast, 11=best */
    OPT_RANGE(analyse.i_subpel_refine,    "subq",           1,    11)
    /* Chroma ME for subpel and mode decision in P-frames */
    OPT_FLAG (analyse.b_chroma_me,        "chroma_me")
    /* Allow each MB partition in P-frames to have its own reference number */
    OPT_FLAG (analyse.b_mixed_references, "mixed_refs")
    /* Trellis RD quantization */
    OPT_RANGE(analyse.i_trellis,          "trellis",        0,     2)
    /* Early SKIP detection on P-frames */
    OPT_FLAG (analyse.b_fast_pskip,       "fast_pskip")
    /* Transform coefficient thresholding on P-frames */
    OPT_FLAG (analyse.b_dct_decimate,     "dct_decimate")
    /* Noise reduction */
    OPT_RANGE(analyse.i_noise_reduction,  "nr",             0, 65536)
    /* Psychovisual optimization parameters */
    OPT_FLOAT(analyse.f_psy_rd,           "psy_rd")
    OPT_FLOAT(analyse.f_psy_trellis,      "psy_trellis")
    /* Psychovisual optimization enable/disable */
    OPT_FLAG (analyse.b_psy,              "psy")
    /* Luma dead zone size */
    OPT_RANGE(analyse.i_luma_deadzone[0], "luma_deadzone_inter", 0, 99)
    OPT_RANGE(analyse.i_luma_deadzone[1], "luma_deadzone_intra", 0, 99)
    /* Compute and print PSNR stats */
    OPT_FLAG (analyse.b_psnr,             "psnr")
    /* Compute and print SSIM stats */
    OPT_FLAG (analyse.b_ssim,             "ssim")

    /* Rate control parameters */

    /* X264_RC_* (set automatically) */
    OPT_NONE (rc.i_rc_method)

    /* QP value for constant-quality encoding (to be a transcode option,
     * eventually--FIXME) */
    OPT_NONE (rc.i_qp_constant)
    /* Minimum allowed QP value */
    OPT_RANGE(rc.i_qp_min,                "qp_min",         0,    51)
    /* Maximum allowed QP value */
    OPT_RANGE(rc.i_qp_max,                "qp_max",         0,    51)
    /* Maximum QP difference between frames */
    OPT_RANGE(rc.i_qp_step,               "qp_step",        0,    50)

    /* Bitrate (transcode -w) */
    OPT_NONE (rc.i_bitrate)
    /* Nominal QP for 1-pass VBR */
    OPT_RANGF(rc.f_rf_constant,           "crf",            0,    51)
    /* Allowed variance from average bitrate */
    OPT_FLOAT(rc.f_rate_tolerance,        "ratetol")
    /* Maximum local bitrate (kbit/s) */
    OPT_RANGE(rc.i_vbv_max_bitrate,       "vbv_maxrate",    0,240000)
    /* Size of VBV buffer for CBR encoding */
    OPT_RANGE(rc.i_vbv_buffer_size,       "vbv_bufsize",    0,240000)
    /* Initial occupancy of VBV buffer */
    OPT_RANGF(rc.f_vbv_buffer_init,       "vbv_init",     0.0,   1.0)
    /* QP ratio between I and P frames */
    OPT_FLOAT(rc.f_ip_factor,             "ip_ratio")
    /* QP ratio between P and B frames */
    OPT_FLOAT(rc.f_pb_factor,             "pb_ratio")

    /* Psychovisual adaptive QP mode */
    OPT_RANGE(rc.i_aq_mode,               "aq_mode",        0,     3)
    /* Adaptive QP strength */
    OPT_FLOAT(rc.f_aq_strength,           "aq_strength")
    /* Macroblock-tree rate control */
    OPT_FLAG (rc.b_mb_tree,               "mbtree")
    /* Number of lookahead frames to buffer for rate control */
    OPT_RANGE(rc.i_lookahead,             "lookahead",      0,   999)

    /* 2-pass logfile parameters (set automatically) */
    OPT_NONE (rc.b_stat_write)
    OPT_NONE (rc.psz_stat_out)
    OPT_NONE (rc.b_stat_reg)
    OPT_NONE (rc.psz_stat_in)

    /* QP curve compression: 0.0 = constant bitrate, 1.0 = constant quality */
    OPT_RANGF(rc.f_qcompress,             "qcomp",        0.0,   1.0)
    /* QP blurring after compression */
    OPT_FLOAT(rc.f_qblur,                 "qblur")
    /* Complexity blurring before QP compression */
    OPT_FLOAT(rc.f_complexity_blur,       "cplx_blur")
    /* Rate control override zones (not supported by transcode) */
    OPT_NONE (rc.zones)
    OPT_NONE (rc.i_zones)
    /* Alternate method of specifying zones */
    OPT_STR  (rc.psz_zones,               "zones")

    /* Rate control parameters */

    OPT_FLAG (b_aud,                      "aud")
    OPT_NONE (b_repeat_headers)
    OPT_NONE (i_sps_id)
    OPT_NONE (b_vfr_input)
    OPT_NONE (i_timebase_num)
    OPT_NONE (i_timebase_den)
    OPT_NONE (b_dts_compress)

    /* First field (1=top, 0=bottom) (--encode_fields) */
    OPT_NONE (b_tff)

    /* Pulldown flag (not currently used) */
    OPT_NONE (b_pic_struct)

    /* Slicing parameters */

    OPT_RANGE(i_slice_max_size,           "slice_max_size", 0,999999)
    OPT_RANGE(i_slice_max_mbs,            "slice_max_mbs",  0,999999)
    OPT_RANGE(i_slice_count,              "slices",         0,   999)

    /* Module configuration options (which do not affect encoding) */

    {"2pass_bug_workaround", &confdata.twopass_bug_workaround,
                                                 TCCONF_TYPE_FLAG, 0, 0, 1},
    {"no2pass_bug_workaround", &confdata.twopass_bug_workaround,
                                                 TCCONF_TYPE_FLAG, 0, 1, 0},

    {NULL}
};

/*************************************************************************/
/*************************************************************************/

/**
 * x264_log:  Logging routine for x264 library.
 *
 * Parameters:
 *     userdata: Unused.
 *        level: x264 log level (X264_LOG_*).
 *       format: Log message format string.
 *         args: Log message format arguments.
 * Return value:
 *     None.
 */

static void x264_log(void *userdata, int level, const char *format,
                     va_list args)
{
    TCLogType logtype;
    char buf[TC_BUF_MAX];

    if (!format)
        return;
    switch (level) {
      case X264_LOG_ERROR:
        logtype = TC_LOG_ERR;
        break;
      case X264_LOG_WARNING:
        logtype = TC_LOG_WARN;
        break;
      case X264_LOG_INFO:
        if (!(verbose >= TC_INFO))
            return;
        logtype = TC_LOG_INFO;
        break;
      case X264_LOG_DEBUG:
        if (!(verbose >= TC_DEBUG))
            return;
        logtype = TC_LOG_MSG;
        break;
      default:
        return;
    }
    tc_vsnprintf(buf, sizeof(buf), format, args);
    buf[strcspn(buf,"\r\n")] = 0;  /* delete trailing newline */
    /* bypass log filtering silently */
    tc_log(logtype, MOD_NAME, "%s", buf);
}

/*************************************************************************/

/**
 * x264params_set_multipass:  Does all settings related to multipass.
 *
 * Parameters:
 *              pass: 0 = single pass
 *                    1 = 1st pass
 *                    2 = 2nd pass (final pass of multipass encoding)
 *                    3 = Nth pass (intermediate passes of multipass encoding)
 *     statsfilename: where to read and write multipass stat data.
 * Return value:
 *     Always 0.
 * Preconditions:
 *     params != NULL
 *     pass == 0 || statsfilename != NULL
 */

static int x264params_set_multipass(x264_param_t *params,
                                    int pass, const char *statsfilename)
{
    /* Drop the const and hope that x264 treats it as const anyway */
    params->rc.psz_stat_in  = (char *)statsfilename;
    params->rc.psz_stat_out = (char *)statsfilename;

    switch (pass) {
      default:
        params->rc.b_stat_write = 0;
        params->rc.b_stat_read = 0;
        break;
      case 1:
        params->rc.b_stat_write = 1;
        params->rc.b_stat_read = 0;
        break;
      case 2:
        params->rc.b_stat_write = 0;
        params->rc.b_stat_read = 1;
        break;
      case 3:
        params->rc.b_stat_write = 1;
        params->rc.b_stat_read = 1;
        break;
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * x264params_check:  Checks or corrects some strange combinations of
 * settings done in x264params.
 *
 * Parameters:
 *     params: x264_param_t structure to check
 * Return value:
 *     0 on success, nonzero otherwise.
 */

static int x264params_check(x264_param_t *params)
{
    /* don't know if these checks are really needed, but they won't hurt */
    if (params->rc.i_qp_min > params->rc.i_qp_constant) {
        params->rc.i_qp_min = params->rc.i_qp_constant;
    }
    if (params->rc.i_qp_max < params->rc.i_qp_constant) {
        params->rc.i_qp_max = params->rc.i_qp_constant;
    }

    if (params->rc.i_rc_method == X264_RC_ABR) {
        if ((params->rc.i_vbv_max_bitrate > 0)
            != (params->rc.i_vbv_buffer_size > 0)
        ) {
            tc_log_error(MOD_NAME,
                         "VBV requires both vbv_maxrate and vbv_bufsize.");
            return TC_ERROR;
        }
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * x264params_set_by_vob:  Handle transcode CLI and tc-autodetection
 * dependent entries in x264_param_t.
 *
 * This method copies various values from transcodes vob_t structure to
 * x264 $params. That means all settings that can be done through
 * transcodes CLI or autodetection are applied to x264s $params here
 * (and I hope nowhere else).
 *
 * Parameters:
 *     params: x264_param_t structure to apply changes to
 *        vob: transcodes vob_t structure to copy values from
 * Return value:
 *     0 on success, nonzero otherwise.
 * Preconditions:
 *     params != NULL
 *     vob != NULL
 */

static int x264params_set_by_vob(x264_param_t *params, const vob_t *vob)
{
    int tc_accel = tc_get_session()->acceleration; /* XXX ugly */
    /* Set video/bitstream parameters */

    params->i_width = vob->ex_v_width;
    params->i_height = vob->ex_v_height;
    params->b_interlaced = (vob->encode_fields==TC_ENCODE_FIELDS_TOP_FIRST
                         || vob->encode_fields==TC_ENCODE_FIELDS_BOTTOM_FIRST);
#ifdef HAVE_X264_NAL_HRD
    params->b_tff        = (vob->encode_fields==TC_ENCODE_FIELDS_TOP_FIRST);
#endif

    if (params->rc.f_rf_constant != 0) {
        params->rc.i_rc_method = X264_RC_CRF;
    } else {
        params->rc.i_rc_method = X264_RC_ABR;
        params->rc.i_bitrate = vob->divxbitrate; /* what a name */
    }

    params->b_vfr_input = 0;
    if (vob->im_frc == 0
     || TC_NULL_MATCH == tc_frc_code_to_ratio(vob->im_frc,
                                              &params->i_timebase_den,
                                              &params->i_timebase_num)
    ) {
        if (vob->fps > 29.9 && vob->fps < 30) {
            params->i_timebase_den = 30000;
            params->i_timebase_num = 1001;
        } else if (vob->fps > 23.9 && vob->fps < 24) {
            params->i_timebase_den = 24000;
            params->i_timebase_num = 1001;
        } else if (vob->fps > 59.9 && vob->fps < 60) {
            params->i_timebase_den = 60000;
            params->i_timebase_num = 1001;
        } else {
            params->i_timebase_den = vob->fps * 1000;
            params->i_timebase_num = 1000;
        }
    }

    if (vob->ex_frc == 0
     || TC_NULL_MATCH == tc_frc_code_to_ratio(vob->ex_frc,
                                              &params->i_fps_num,
                                              &params->i_fps_den)
    ) {
        if (vob->ex_fps > 29.9 && vob->ex_fps < 30) {
            params->i_fps_num = 30000;
            params->i_fps_den = 1001;
        } else if (vob->ex_fps > 23.9 && vob->ex_fps < 24) {
            params->i_fps_num = 24000;
            params->i_fps_den = 1001;
        } else if (vob->ex_fps > 59.9 && vob->ex_fps < 60) {
            params->i_fps_num = 60000;
            params->i_fps_den = 1001;
        } else {
            params->i_fps_num = vob->ex_fps * 1000;
            params->i_fps_den = 1000;
        }
    }

    if (0 != tc_find_best_aspect_ratio(vob,
                                       &params->vui.i_sar_width,
                                       &params->vui.i_sar_height,
                                       MOD_NAME)
    ) {
        tc_log_error(MOD_NAME, "unable to find sane value for SAR");
        return TC_ERROR;
    }

    /* Set logging function and acceleration flags */
    params->pf_log = x264_log;
    params->p_log_private = NULL;
    params->cpu &= ~(X264_CPU_MMX
                   | X264_CPU_MMXEXT
                   | X264_CPU_SSE
                   | X264_CPU_SSE2
                   | X264_CPU_SSE3
                   | X264_CPU_SSSE3
                   | X264_CPU_SSE4
                   | X264_CPU_SSE42
                   | X264_CPU_LZCNT);
    if (tc_accel & AC_MMX)      params->cpu |= X264_CPU_MMX;
    if (tc_accel & AC_MMXEXT)   params->cpu |= X264_CPU_MMXEXT;
    if (tc_accel & AC_SSE)      params->cpu |= X264_CPU_SSE;
    if (tc_accel & AC_SSE2)     params->cpu |= X264_CPU_SSE2;
    if (tc_accel & AC_SSE3)     params->cpu |= X264_CPU_SSE3;
    if (tc_accel & AC_SSSE3)    params->cpu |= X264_CPU_SSSE3;
    if (tc_accel & AC_SSE41)    params->cpu |= X264_CPU_SSE4;
    if (tc_accel & AC_SSE42)    params->cpu |= X264_CPU_SSE42;
    if (tc_accel & AC_SSE4A)    params->cpu |= X264_CPU_LZCNT;

    return TC_OK;
}

/*************************************************************************/

/**
 * do_2pass_bug_workaround:  Work around a bug present in at least x264
 * versions 65 through 67 which causes invalid frame numbers to be written
 * to the 2-pass logfile.
 *
 * Parameters:
 *     path: Logfile pathname.
 * Return value:
 *     0 on success, nonzero otherwise.
 * Preconditions:
 *     path != NULL
 */

static int do_2pass_bug_workaround(const char *path)
{
    FILE *fp;
    char *buffer;
    long filesize, nread, offset;
    long nframes;

    fp = fopen(path, "r+");
    if (!fp) {
        tc_log_warn(MOD_NAME, "Failed to open 2-pass logfile '%s': %s",
                    path, strerror(errno));
        goto error_return;
    }

    /* x264 treats the logfile as a single, semicolon-separated buffer
     * rather than a series of lines, so do the same here. */

    /* Read in the logfile data */
    if (fseek(fp, 0, SEEK_END) != 0) {
        tc_log_warn(MOD_NAME, "Seek to end of 2-pass logfile failed: %s",
                    strerror(errno));
        goto error_close_file;
    }
    filesize = ftell(fp);
    if (filesize < 0) {
        tc_log_warn(MOD_NAME, "Get size of 2-pass logfile failed: %s",
                    strerror(errno));
        goto error_close_file;
    }
    buffer = malloc(filesize);
    if (!buffer) {
        tc_log_warn(MOD_NAME, "No memory for 2-pass logfile buffer"
                    " (%ld bytes)", filesize);
        goto error_close_file;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        tc_log_warn(MOD_NAME, "Seek to beginning of 2-pass logfile failed: %s",
                    strerror(errno));
        goto error_free_buffer;
    }
    nread = fread(buffer, 1, filesize, fp);
    if (nread != filesize) {
        tc_log_warn(MOD_NAME, "Short read on 2-pass logfile (expected %ld"
                    " bytes, got %ld)", filesize, nread);
        goto error_free_buffer;
    }

    /* Count the number of frames */
    nframes = 0;
    offset = 0;
    if (strncmp(buffer, "#options:", 9) == 0) {  // just like x264
        offset = strcspn(buffer, "\n") + 1;
    }
    for (; offset < filesize; offset++) {
        if (buffer[offset] == ';') {
            nframes++;
        }
    }

    /* Go through the frame list and check for out-of-range frame numbers */
    offset = 0;
    if (strncmp(buffer, "#options:", 9) == 0) {
        offset = strcspn(buffer, "\n") + 1;
    }
    while (offset < filesize) {
        long framenum;
        char *s;
        if (strncmp(&buffer[offset], "in:", 3) != 0) {
            tc_log_warn(MOD_NAME, "Can't parse 2-pass logfile at offset %ld,"
                        " giving up.", offset);
            offset = filesize;  // Don't truncate the file
            break;
        }
        framenum = strtol(&buffer[offset+3], &s, 10);
        if ((s && *s != ' ') || framenum < 0) {
            tc_log_warn(MOD_NAME, "Can't parse 2-pass logfile at offset %ld,"
                        " giving up.", offset+3);
            offset = filesize;  // Don't truncate the file
            break;
        }
        if (framenum >= nframes) {
            tc_log_warn(MOD_NAME, "Truncating corrupt x264 logfile:");
            tc_log_warn(MOD_NAME, "    in(%ld) >= nframes(%ld) at offset %ld",
                        framenum, nframes, offset);
            tc_log_warn(MOD_NAME, "Please report this bug to the x264"
                        " developers.");
            break;  // Truncate the file here
        }
        offset += strcspn(&buffer[offset], ";");
        offset += strspn(&buffer[offset], ";\n");
    }

    /* Truncate the file if the bug was detected */
    if (offset < filesize) {
        if (ftruncate(fileno(fp), offset) != 0) {
            tc_log_warn(MOD_NAME, "Failed to truncate 2-pass logfile: %s",
                        strerror(errno));
            goto error_free_buffer;
        }
    }

    /* Successful return */
    free(buffer);
    fclose(fp);
    return 0;

    /* Error handling */
  error_free_buffer:
    free(buffer);
  error_close_file:
    fclose(fp);
  error_return:
    return -1;
}

/*************************************************************************/
/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * x264_init:  Initialize this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int x264_init(TCModuleInstance *self, uint32_t features)
{
    X264PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    pd = tc_malloc(sizeof(X264PrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }
    pd->framenum = 0;
    pd->enc = NULL;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    self->userdata = pd;

    return TC_OK;
}

/*************************************************************************/

/**
 * x264_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int x264_fini(TCModuleInstance *self)
{
    X264PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "fini");

    pd = self->userdata;

    if (pd->enc) {
        x264_encoder_close(pd->enc);
        pd->enc = NULL;
    }

    tc_free(self->userdata);
    self->userdata = NULL;
    return TC_OK;
}

/*************************************************************************/

enum {
    H264_NAL_TYPE_SEI       = 0x6,
    H264_NAL_TYPE_SEQ_PARAM = 0x7,
    H264_NAL_TYPE_PIC_PARAM = 0x8
};

#define RETURN_ERROR_IF_BAD_LEN(LEN, MSG) do { \
    if ((LEN) <= 0) { \
        tc_log_error(MOD_NAME, (MSG)); \
        return TC_ERROR; \
    } \
} while (0)

#define HDR_BUF_ADD_XPS(PD, XPS, LEN) do { \
    (PD)->hdr_buf[(PD)->hdr_len    ] = ((LEN) >> 8); \
    (PD)->hdr_buf[(PD)->hdr_len + 1] = ((LEN)     ) & 0xff; \
    (PD)->hdr_len += 2; \
    ac_memcpy((PD)->hdr_buf + (PD)->hdr_len, (XPS), (LEN)); \
    (PD)->hdr_len += (LEN); \
} while (0)

static int tc_x264_setup_extradata(X264PrivateData *pd)
{
    x264_nal_t *nal = NULL;
    int i = 0, ret = 0, nal_count = 0;
    uint8_t pps[X264_HEADER_LEN_MAX] = { 0 };
    uint8_t sps[X264_HEADER_LEN_MAX] = { 0 };
    uint8_t sei[X264_HEADER_LEN_MAX] = { 0 };
    int pps_len = 0, sps_len = 0, sei_len = 0;

    memset(&(pd->hdr_buf), 0, X264_HEADER_LEN_MAX);
    pd->hdr_len = 0;

    ret = x264_encoder_headers(pd->enc, &nal, &nal_count);
    if (ret < 0) {
        tc_log_error(MOD_NAME, "error encoding the headers");
        return TC_ERROR;
    }
    tc_debug(TC_DEBUG_PRIVATE, "header nal count=%i", nal_count);

    for (i = 0; i < nal_count; i++) {
        switch (nal[i].i_type) {
          case H264_NAL_TYPE_SEQ_PARAM:
            sps_len = nal[i].i_payload;
            memcpy(sps, nal[i].p_payload, sps_len);
            break;
          case H264_NAL_TYPE_PIC_PARAM:
            pps_len = nal[i].i_payload;
            memcpy(pps, nal[i].p_payload, pps_len);
            break;
          case H264_NAL_TYPE_SEI:
            sei_len = nal[i].i_payload;
            memcpy(sei, nal[i].p_payload, sei_len);
            break;
          default:
            tc_log_warn(MOD_NAME, "unexpected type 0x%X nal #%i",
                        nal[i].i_type, i);
        }
    }

    RETURN_ERROR_IF_BAD_LEN(sps_len, "missing SPS");
    RETURN_ERROR_IF_BAD_LEN(pps_len, "missing PPS");

    tc_debug(TC_DEBUG_PRIVATE, "SPS length=%i", sps_len);
    tc_debug(TC_DEBUG_PRIVATE, "PPS length=%i", pps_len);

    /* filling, at last */
    pd->hdr_buf[0] = 1;         // Version
    pd->hdr_buf[1] = sps[1];    // AVCProfileIndication
    pd->hdr_buf[2] = sps[2];    // profile_compatibility
    pd->hdr_buf[3] = sps[3];    // AVCLevelIndication
    pd->hdr_buf[4] = 0xFC + 3;  // lengthSizeMinusOne 
    pd->hdr_buf[5] = 0xE0 + 1;  // nonReferenceDegredationPriorityLow        
    pd->hdr_len = 6;
    HDR_BUF_ADD_XPS(pd, sps, sps_len);
    pd->hdr_buf[pd->hdr_len] = 1;   // numOfPictureParameterSets
    pd->hdr_len++;
    HDR_BUF_ADD_XPS(pd, pps, pps_len);

    tc_debug(TC_DEBUG_PRIVATE, "header length=%i", (int)pd->hdr_len);

    return TC_OK;
}

static int tc_x264_free_extradata(X264PrivateData *pd)
{
    /* do nothing (yet) */
    return TC_OK;
}

static int tc_x264_export_extradata(X264PrivateData *pd,
                                    TCModuleExtraData *xdata[])
{
    if (xdata && xdata[0]) {
        xdata[0]->stream_id  = 0; /* ignored by export core */
        xdata[0]->codec      = TC_CODEC_H264;
        xdata[0]->extra.data = pd->hdr_buf;
        xdata[0]->extra.size = pd->hdr_len;
    }
    return TC_OK;
}

/**
 * x264_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int x264_configure(TCModuleInstance *self,
                         const char *options,
                         TCJob *vob,
                         TCModuleExtraData *xdata[])
{
    const char *dirs[] = { ".", NULL };
    X264PrivateData *pd = NULL;
    char *s = NULL;
    int ret;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    pd->flush_flag = vob->encoder_flush;

    /* Initialize parameter block */
    memset(&confdata, 0, sizeof(confdata));
    x264_param_default(&confdata.x264params);
    confdata.x264params.rc.f_rf_constant = 0;  // Default to VBR

    /* Parameters not (yet) settable via options: */
    confdata.x264params.analyse.intra = ~0;
    confdata.x264params.analyse.inter = ~0;

    /* Read settings from configuration file */
    tc_config_read_file(dirs, X264_CONFIG_FILE, NULL, conf, MOD_NAME);

    /* Parse options given in -y option string (format:
     * "name1=value1:name2=value2:...") */
    for (s = (vob->ex_v_string ? strtok(vob->ex_v_string,":") : NULL);
         s != NULL;
         s = strtok(NULL,":")
    ) {
        if (!tc_config_read_line(s, conf, MOD_NAME)) {
            tc_log_error(MOD_NAME, "Error parsing module options");
            return TC_ERROR;
        }
    }

    /* Save multipass logfile name if 2-pass bug workaround was requested */
    if (confdata.twopass_bug_workaround
     && (vob->divxmultipass == 1 || vob->divxmultipass == 3)
    ) {
        const size_t strsize = strlen(vob->divxlogfile) + 1;
        if (strsize > sizeof(pd->twopass_log_path)) {
            tc_log_error(MOD_NAME, "2-pass logfile path too long.\n"
                         "    Use a shorter pathname or disable the"
                         " 2pass_bug_workaround option.");
            return TC_ERROR;
        }
        ac_memcpy(pd->twopass_log_path, vob->divxlogfile, strsize);
        pd->twopass_bug_workaround = 1;
    } else {
        pd->twopass_bug_workaround = 0;
    }

    /* Apply extra settings to $x264params */
    if (0 != x264params_set_multipass(&confdata.x264params, vob->divxmultipass,
                                      vob->divxlogfile)
    ) {
        tc_log_error(MOD_NAME, "Failed to apply multipass settings.");
        return TC_ERROR;
    }

    /* Copy parameter block to module private data */
    ac_memcpy(&pd->x264params, &confdata.x264params, sizeof(pd->x264params));

    /* Apply transcode CLI and autodetected values from $vob to
     * $x264params. This is done as the last step to make transcode CLI
     * override any settings done before. */
    if (0 != x264params_set_by_vob(&pd->x264params, vob)) {
        tc_log_error(MOD_NAME, "Failed to evaluate vob_t values.");
        return TC_ERROR;
    }

    /* Test if the set parameters fit together. */
    if (0 != x264params_check(&pd->x264params)) {
        return TC_ERROR;
    }

    /* Now we've set all parameters gathered from transcode and the config
     * file to $x264params. Let's give some status report and finally open
     * the encoder. */
    if (verbose >= TC_DEBUG) {
        tc_config_print(conf, MOD_NAME);
    }

    pd->enc = x264_encoder_open(&pd->x264params);
    if (!pd->enc) {
        tc_log_error(MOD_NAME, "x264_encoder_open() returned NULL - sorry.");
        return TC_ERROR;
    }
    ret = tc_x264_setup_extradata(pd);
    if (ret != TC_OK) {
        return ret;
    }

    return tc_x264_export_extradata(pd, xdata);
}

/*************************************************************************/

/**
 * x264_stop:  Reset this instance of the module.  See tcmodule-data.h for
 * function details.
 */

static int x264_stop(TCModuleInstance *self)
{
    X264PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    tc_x264_free_extradata(pd); /* mostly a placeholder */

    if (pd->enc) {
        x264_encoder_close(pd->enc);
        pd->enc = NULL;
    }

    if (pd->twopass_bug_workaround) {
        do_2pass_bug_workaround(pd->twopass_log_path);
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * x264_inspect:  Return the value of an option in this instance of the
 * module.  See tcmodule-data.h for function details.
 */

static int x264_inspect(TCModuleInstance *self,
                        const char *param, const char **value)
{
    X264PrivateData *pd = NULL;
    static char buf[TC_BUF_MAX];

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        tc_snprintf(buf, sizeof(buf),
"Overview:\n"
"    Encodes video in h.264 format using the x264 library.\n"
"Options available:\n"
"    All options in x264.cfg can be specified on the command line\n"
"    using the format: -y x264=name1=value1:name2=value2:...\n");
        *value = buf;
    }
    /* FIXME: go through the option list to find a match to param */

    return TC_OK;
}

/*************************************************************************/

/**
 * x264_encode_video:  Encode a video frame.  See tcmodule-data.h for
 * function details.
 */

static int x264_encode_video(TCModuleInstance *self,
                             TCFrameVideo *inframe, TCFrameVideo *outframe)
{
    X264PrivateData *pd;
    x264_nal_t *nal;
    x264_picture_t pic, pic_out;
    int nnal, i, ret;

    TC_MODULE_SELF_CHECK(self, "encode_video");

    pd = self->userdata;

    pd->framenum++;

    memset(&pic,     0, sizeof(pic));
    memset(&pic_out, 0, sizeof(pic_out));

    /* inframe will always be non-NULL on an interface call, but will be
     * NULL when called by x264_flush_video() to encode buffered frames. */
    if (inframe) {
        pic.img.i_csp       = X264_CSP_I420;
        pic.img.i_plane     = 3;

        pic.img.plane[0]    = inframe->video_buf;
        pic.img.i_stride[0] = inframe->v_width;

        pic.img.plane[1]    = pic.img.plane[0]
                              + inframe->v_width*inframe->v_height;
        pic.img.i_stride[1] = inframe->v_width / 2;

        pic.img.plane[2]    = pic.img.plane[1]
                              + (inframe->v_width/2)*(inframe->v_height/2);
        pic.img.i_stride[2] = inframe->v_width / 2;

        pic.i_type = X264_TYPE_AUTO;
        pic.i_qpplus1 = 0;
        /* FIXME: Is this pts-handling ok? I don't have a clue how
         * PTS/DTS handling works. Does it matter, when no muxing is
         * done? */
        pic.i_pts = (int64_t) pd->framenum * pd->x264params.i_fps_den;
    }

    ret = x264_encoder_encode(pd->enc, &nal, &nnal,
                              inframe ? &pic : NULL, &pic_out);
    if (ret < 0) {
        return TC_ERROR;
    }

    outframe->video_len = 0;
    for (i = 0; i < nnal; i++) {
        int size = outframe->video_size - outframe->video_len;
        if (size <= 0) {
            tc_log_error(MOD_NAME, "output buffer overflow");
            return TC_ERROR;
        }
        ac_memcpy(outframe->video_buf + outframe->video_len,
                  nal[i].p_payload, nal[i].i_payload); 
        outframe->video_len += nal[i].i_payload;
    }

    /* FIXME: ok, that sucks. How to reformat it ina better way? -- fromani */
    if ((pic_out.i_type == X264_TYPE_IDR)
     || (pic_out.i_type == X264_TYPE_I
      &&  pd->x264params.i_frame_reference == 1
      && !pd->x264params.i_bframe)) {
        outframe->attributes |= TC_FRAME_IS_KEYFRAME;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * x264_flush_video:  Flush a video frame from x264's internal buffer.
 * See tcmodule-data.h for function details.
 */

static int x264_flush_video(TCModuleInstance *self,
                            TCFrameVideo *outframe, int *frame_returned)
{
    X264PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "encode_video");

    pd = self->userdata;

    *frame_returned = 0;

    if (!pd->flush_flag) {
        /* Flushing disabled by the user, which is not a good idea with x264
         * since it can buffer several dozen frames before encoding anything.
         * Add a warning just to make sure the user knows what they're doing. */
        tc_log_warn(MOD_NAME, "Using -O (--encoder_noflush) with x264 can"
                    " cause frames to be lost from the output file!");
        return TC_OK;
    }

    if (x264_encoder_delayed_frames(pd->enc) == 0) {
        /* No buffered frames left to encode */
        return TC_OK;
    }

    if (x264_encode_video(self, NULL, outframe) == TC_ERROR) {
        return TC_ERROR;
    }

    *frame_returned = 1;
    return TC_OK;
}

/*************************************************************************/

static const TCCodecID x264_codecs_video_in[] = { 
    TC_CODEC_YUV420P, TC_CODEC_ERROR
};
static const TCCodecID x264_codecs_video_out[] = { 
    TC_CODEC_H264, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(x264);
TC_MODULE_CODEC_FORMATS(x264);

TC_MODULE_INFO(x264);

static const TCModuleClass x264_class = {
    TC_MODULE_CLASS_HEAD(x264),

    .init         = x264_init,
    .fini         = x264_fini,
    .configure    = x264_configure,
    .stop         = x264_stop,
    .inspect      = x264_inspect,

    .encode_video = x264_encode_video,
    .flush_video  = x264_flush_video,
};

TC_MODULE_ENTRY_POINT(x264)

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

