/*
 * filter_doublefps.c -- double frame rate by deinterlacing fields into frames
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#define MOD_NAME        "filter_doublefps.so"
#define MOD_VERSION     "v1.1.1 (2009-02-07)"
#define MOD_CAP         "double frame rate by deinterlacing fields into frames"
#define MOD_AUTHOR      "Andrew Church"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO|TC_MODULE_FEATURE_AUDIO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE|TC_MODULE_FLAG_BUFFERING

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"
#include "libtcvideo/tcvideo.h"
#include "aclib/ac.h"

/*************************************************************************/

typedef struct {
    int topfirst;           // Top field first?
    int fullheight;         // Full-height mode
    int have_first_frame;   // Nonzero if we've seen a frame
    TCVHandle tcvhandle;    // For tcv_zoom() when shifting
    int deinter_handle;     // For high-quality mode
    int saved_audio_len;    // Number of bytes of audio saved for second field
    uint8_t saved_audio[SIZE_PCM_FRAME];
    uint8_t saved_frame[TC_MAX_V_FRAME_WIDTH*TC_MAX_V_FRAME_HEIGHT*3];
    int saved_width, saved_height;  // For full-height operation
} DfpsPrivateData;

/*************************************************************************/
/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * doublefps_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int doublefps_init(TCModuleInstance *self, uint32_t features)
{
    DfpsPrivateData *pd;
    vob_t *vob = tc_get_vob();

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    self->userdata = pd = tc_malloc(sizeof(DfpsPrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }
    pd->topfirst = -1;
    pd->fullheight = 0;
    pd->have_first_frame = pd->saved_width = pd->saved_height = 0;

    /* FIXME: we need a proper way for filters to tell the core that
     * they're changing the export parameters */
    if (!(vob->export_attributes
          & (TC_EXPORT_ATTRIBUTE_FPS | TC_EXPORT_ATTRIBUTE_FRC))
    ) {
        vob->ex_fps *= 2;
        switch (vob->ex_frc) {
            case  3: vob->ex_frc =  6; break;
            case  4: vob->ex_frc =  7; break;
            case  5: vob->ex_frc =  8; break;
            case 10: vob->ex_frc = 11; break;
            case 12: vob->ex_frc =  2; break;
            case 13: vob->ex_frc =  5; break;
            default: vob->ex_frc =  0; break;
        }
    }
    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * doublefps_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int doublefps_fini(TCModuleInstance *self)
{
    DfpsPrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "fini");

    pd = self->userdata;

    if (pd->tcvhandle) {
        tcv_free(pd->tcvhandle);
        pd->tcvhandle = 0;
    }

    tc_free(self->userdata);
    self->userdata = NULL;
    return TC_OK;
}

/*************************************************************************/

/**
 * doublefps_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int doublefps_configure(TCModuleInstance *self,
                               const char *options,
                               TCJob *vob,
                               TCModuleExtraData *xdata[])
{
    DfpsPrivateData *pd;
    int new_topfirst = -1;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    if (options) {
        if (optstr_get(options, "shiftEven", "%d", &pd->topfirst) == 1) {
            tc_log_warn(MOD_NAME, "The \"shiftEven\" option name is obsolete;"
                        " please use \"topfirst\" instead.");
        }
        optstr_get(options, "topfirst", "%d", &new_topfirst);
        optstr_get(options, "fullheight", "%d", &pd->fullheight);
    }
    if (new_topfirst == -1) {
        if (pd->topfirst == -1)
            pd->topfirst = (vob->im_v_height == 480 ? 0 : 1);
    } else {
        pd->topfirst = new_topfirst;
    }

    if (!pd->fullheight) {
        if (vob->encode_fields == TC_ENCODE_FIELDS_TOP_FIRST
         || vob->encode_fields == TC_ENCODE_FIELDS_BOTTOM_FIRST
        ) {
            pd->topfirst = (vob->encode_fields == TC_ENCODE_FIELDS_TOP_FIRST);
            if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_FIELDS) {
                tc_log_warn(MOD_NAME, "Use \"-J doublefps=topfirst=%d\","
                            " not \"--encode_fields %c\"", pd->topfirst,
                            pd->topfirst ? 't' : 'b');
            }
        }
        vob->encode_fields = TC_ENCODE_FIELDS_PROGRESSIVE;
        vob->export_attributes |= TC_EXPORT_ATTRIBUTE_FIELDS;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * doublefps_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int doublefps_stop(TCModuleInstance *self)
{
    DfpsPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;
    pd->have_first_frame = pd->saved_width = pd->saved_height = 0;
    return TC_OK;
}

/*************************************************************************/

/**
 * doublefps_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int doublefps_inspect(TCModuleInstance *self,
                             const char *param, const char **value)
{
    DfpsPrivateData *pd = NULL;
    static char buf[TC_BUF_MAX];

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = 
"Overview:\n"
"\n"
"    Doubles the frame rate of interlaced video by separating each field\n"
"    into a separate frame.  The fields can either be left as is (giving a\n"
"    progessive video with half the height of the original) or re-interlaced\n"
"    into their original height (at the doubled frame rate) for the\n"
"    application of a separate deinterlacing filter.\n"
"\n"
"    Note that due to transcode limitations, it is currently necessary to\n"
"    use the -Z option to specify the output frame size when using\n"
"    half-height mode (this does not slow the program down if no actual\n"
"    zooming is done).\n"
"\n"
"    When using this filter in half-height mode, make sure you specify\n"
"    \"--encode_fields p\" on the transcode command line, and do not use the\n"
"    \"-I\" option.\n"
"\n"
"Options available:\n"
"\n"
"    topfirst=0|1     Selects whether the top field is the first displayed.\n"
"                     Defaults to 0 (bottom-first) for 480-line video, 1\n"
"                     (top-first) otherwise.\n"
"\n"
"    fullheight=0|1   Selects whether or not to retain full height when\n"
"                     doubling the frame rate.  If this is set to 1, the\n"
"                     resulting video will have the same frame size as the\n"
"                     original at double the frame rate, and the frames will\n"
"                     consist of fields 0 and 1, 1 and 2, 2 and 3, and so\n"
"                     forth.  This can be used to let a separate filter\n"
"                     perform deinterlacing on the double-rate frames; note\n"
"                     that the filter must be able to deal with the top and\n"
"                     bottom fields switching with each frame.\n"
"                     Note that this option cannot be changed after startup.\n"
;
    }
    if (optstr_lookup(param, "topfirst")) {
        tc_snprintf(buf, sizeof(buf), "%d", pd->topfirst);
        *value = buf;
    }
    if (optstr_lookup(param, "fullheight")) {
        tc_snprintf(buf, sizeof(buf), "%d", pd->fullheight);
        *value = buf;
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * doublefps_filter_video:  Perform the FPS-doubling operation on the video
 * stream.  See tcmodule-data.h for function details.
 */

static int doublefps_filter_video(TCModuleInstance *self, vframe_list_t *frame)
{
    DfpsPrivateData *pd;
    int w, h, hUV;

    TC_MODULE_SELF_CHECK(self, "filter_video");
    TC_MODULE_SELF_CHECK(frame, "filter_video");

    pd = self->userdata;
    /* If we have a saved frame size, restore it and clear the saved values */
    if (pd->saved_width && pd->saved_height) {
        frame->v_width = pd->saved_width;
        frame->v_height = pd->saved_height;
        pd->saved_width = pd->saved_height = 0;
    }
    w = frame->v_width;
    h = frame->v_height;
    hUV = (frame->v_codec == TC_CODEC_YUV422P) ? h : h/2;

    switch ((pd->fullheight ? 2 : 0)
          + (frame->attributes & TC_FRAME_WAS_CLONED ? 1 : 0)
    ) {

      case 0: {  // Half height, first field
        uint8_t *oldbuf[3], *newbuf[3], *savebuf[3];
        TCVDeinterlaceMode dropfirst, dropsecond;

        if (pd->topfirst) {
            dropfirst  = TCV_DEINTERLACE_DROP_FIELD_BOTTOM;
            dropsecond = TCV_DEINTERLACE_DROP_FIELD_TOP;
        } else {
            dropfirst  = TCV_DEINTERLACE_DROP_FIELD_TOP;
            dropsecond = TCV_DEINTERLACE_DROP_FIELD_BOTTOM;
        }

        oldbuf[0]  = frame->video_buf;
        oldbuf[1]  = oldbuf[0] + w * h;
        oldbuf[2]  = oldbuf[1] + (w/2) * hUV;
        newbuf[0]  = frame->video_buf_Y[frame->free];
        newbuf[1]  = newbuf[0] + w * (h/2);
        newbuf[2]  = newbuf[1] + (w/2) * (hUV/2);
        savebuf[0] = pd->saved_frame;
        savebuf[1] = savebuf[0] + w * (h/2);
        savebuf[2] = savebuf[1] + (w/2) * (hUV/2);

        /* Deinterlace the fields into separate frames, and save the second
         * frame for the next time we're called. */
        if (!tcv_deinterlace(pd->tcvhandle, oldbuf[0], newbuf[0], w, h, 1,
                             dropfirst)
         || !tcv_deinterlace(pd->tcvhandle, oldbuf[1], newbuf[1], w/2, hUV, 1,
                             dropfirst)
         || !tcv_deinterlace(pd->tcvhandle, oldbuf[2], newbuf[2], w/2, hUV, 1,
                             dropfirst)
         || !tcv_deinterlace(pd->tcvhandle, oldbuf[0], savebuf[0], w, h, 1,
                             dropsecond)
         || !tcv_deinterlace(pd->tcvhandle, oldbuf[1], savebuf[1], w/2, hUV, 1,
                             dropsecond)
         || !tcv_deinterlace(pd->tcvhandle, oldbuf[2], savebuf[2], w/2, hUV, 1,
                             dropsecond)
        ) {
            tc_log_warn(MOD_NAME, "tcv_deinterlace() failed!");
            return TC_ERROR;
        }

        frame->attributes |= TC_FRAME_IS_CLONED;
        frame->attributes &= ~TC_FRAME_IS_INTERLACED;
        frame->v_height /= 2;
        frame->video_buf = newbuf[0];
        frame->free = (frame->free==0) ? 1 : 0;
        break;

      }  // case 0: half height, first field

      case 1:  // Half height, second field

        /* The new frame is already saved in our private data structure,
         * so copy it over and return. */
        ac_memcpy(frame->video_buf, pd->saved_frame, w*h + (w/2)*hUV*2);
        frame->attributes &= ~TC_FRAME_IS_INTERLACED;
        break;

      case 2: {  // Full height, first field
        uint8_t *top[3], *bottom[3], *out[3];
        uint8_t *oldframe = frame->video_buf;  // for saving

        /* Merge this frame's first field and the previous frame's second
         * field (unless this is the first frame, in which case we do
         * nothing). */
        if (pd->have_first_frame) {
            int plane;
            if (pd->topfirst) {
                top[0]    = frame->video_buf;
                bottom[0] = pd->saved_frame;
            } else {
                top[0]    = pd->saved_frame;
                bottom[0] = frame->video_buf;
            }
            top[1]    = top[0] + w * h;
            top[2]    = top[1] + (w/2) * hUV;
            bottom[1] = bottom[0] + w * h;
            bottom[2] = bottom[1] + (w/2) * hUV;
            out[0]    = frame->video_buf_Y[frame->free];
            out[1]    = out[0] + w * h;
            out[2]    = out[1] + (w/2) * hUV;
            /* Only interlace the Y plane in YUV420 mode */
            for (plane = 0; plane < (h==hUV ? 3 : 1); plane++) {
                int W = plane==0 ? w : w/2;
                int y;
                for (y = 0; y < h; y += 2) {
                    ac_memcpy(out[plane]+(y  )*W, top   [plane]+(y  )*W, W);
                    ac_memcpy(out[plane]+(y+1)*W, bottom[plane]+(y+1)*W, W);
                }
            }
            /* Copy U and V planes in YUV420 mode */
            if (h != hUV)
                ac_memcpy(out[1], frame->video_buf + w*h, (w/2)*hUV*2);
            /* Update frame data */
            frame->video_buf = out[0];
            frame->free = (frame->free==0) ? 1 : 0;
        }
        frame->attributes |= TC_FRAME_IS_CLONED;

        /* Now save this frame in the temporary buffer. */
        ac_memcpy(pd->saved_frame, oldframe, w*h + (w/2)*hUV*2);
        pd->saved_width = w;
        pd->saved_height = h;
        break;

      }  // case 2: full height, first field

      case 3:  // Full height, second field

        /* Restore the original frame (we copy the entire frame because
         * somebody else might have changed the working copy). */
        ac_memcpy(frame->video_buf, pd->saved_frame, w*h + (w/2)*hUV*2);
        break;

    }  // switch (height mode + field number)

    pd->have_first_frame = 1;
    return TC_OK;
}

/*************************************************************************/

/**
 * doublefps_filter_audio:  Perform the FPS-doubling operation on the audio
 * stream.  See tcmodule-data.h for function details.
 */

static int doublefps_filter_audio(TCModuleInstance *self, aframe_list_t *frame)
{
    DfpsPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "filter_audio");
    TC_MODULE_SELF_CHECK(frame, "filter_audio");

    pd = self->userdata;

    if (!(frame->attributes & TC_FRAME_WAS_CLONED)) {
        /* First field */
        int bps = frame->a_chan * frame->a_bits / 8;
        int nsamples = frame->audio_size / bps;
        int nsamples_first = (nsamples+1) / 2;  // put odd sample in 1st frame
        int nsamples_second = nsamples - nsamples_first;

        frame->attributes |= TC_FRAME_IS_CLONED;
        frame->audio_size = nsamples_first * bps;
        pd->saved_audio_len = nsamples_second * bps;
        if (pd->saved_audio_len > 0) {
            ac_memcpy(pd->saved_audio, frame->audio_buf + frame->audio_size,
                      pd->saved_audio_len);
        }
    } else {
        /* Second frame */
        frame->audio_size = pd->saved_audio_len;
        if (pd->saved_audio_len > 0)
            ac_memcpy(frame->audio_buf, pd->saved_audio, pd->saved_audio_len);
    }

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID doublefps_codecs_video_in[] =
    { TC_CODEC_YUV420P, TC_CODEC_YUV422P, TC_CODEC_ERROR };
static const TCCodecID doublefps_codecs_video_out[] =
    { TC_CODEC_YUV420P, TC_CODEC_YUV422P, TC_CODEC_ERROR };
static const TCCodecID doublefps_codecs_audio_in[] =
    { TC_CODEC_PCM, TC_CODEC_ERROR };
static const TCCodecID doublefps_codecs_audio_out[] =
    { TC_CODEC_PCM, TC_CODEC_ERROR };
TC_MODULE_FILTER_FORMATS(doublefps);

TC_MODULE_INFO(doublefps);

static const TCModuleClass doublefps_class = {
    TC_MODULE_CLASS_HEAD(doublefps),

    .init         = doublefps_init,
    .fini         = doublefps_fini,
    .configure    = doublefps_configure,
    .stop         = doublefps_stop,
    .inspect      = doublefps_inspect,

    .filter_video = doublefps_filter_video,
    /* We have to handle the audio too! */
    .filter_audio = doublefps_filter_audio,
};

TC_MODULE_ENTRY_POINT(doublefps)

/*************************************************************************/

static int doublefps_get_config(TCModuleInstance *self, char *options)
{
    DfpsPrivateData *pd = NULL;
    char buf[TC_BUF_MIN];

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = self->userdata;

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       MOD_AUTHOR, "VAEY4", "1");
    tc_snprintf(buf, sizeof(buf), "%i", pd->topfirst);
    optstr_param(options, "topfirst",
                 "select if top first is first displayed or not",
                 "%d", buf, "0", "1");
    tc_snprintf(buf, sizeof(buf), "%i", pd->fullheight);
    optstr_param(options, "fullheight",
                 "select if full height must be retained when doubling fps",
                 "%d", buf, "0", "1");

    return TC_OK;
}

static int doublefps_process(TCModuleInstance *self, frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    if (frame->tag & TC_PRE_M_PROCESS && frame->tag & TC_VIDEO) {
        return doublefps_filter_video(self, (vframe_list_t *)frame);
    }
    if (frame->tag & TC_PRE_M_PROCESS && frame->tag & TC_AUDIO) {
        return doublefps_filter_audio(self, (aframe_list_t *)frame);
    }
    return TC_OK;
}

/*************************************************************************/

TC_FILTER_OLDINTERFACE(doublefps)

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
