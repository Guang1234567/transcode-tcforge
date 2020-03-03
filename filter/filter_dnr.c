/*
 *  filter_dnr.c
 *
 *  Copyright (C) Gerhard Monzel - November 2001
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

#define MOD_NAME    "filter_dnr.so"
#define MOD_VERSION "v0.3.1 (2009-02-07)"
#define MOD_CAP     "dynamic noise reduction"
#define MOD_AUTHOR  "Gerhard Monzel"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE|TC_MODULE_FLAG_BUFFERING

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

#include <math.h>


/*************************************************************************/

/* Constants and data structure */

static const char dnr_help[] = ""
    "Overview:\n"
    "    this filter performs dynamic noise reduction on video frames.\n"
    "Options:\n"
    "    lt      Threshold to blend luma/red          (1,128) [10]\n"
    "    ll      Threshold to lock  luma/red          (1,128) [4]\n"
    "    ct      Threshold to blend chroma/green+blue (1,128) [16]\n"
    "    cl      Threshold to lock  chroma/green+blue (1,128) [8]\n"
    "    sc      Percentage of picture difference\n"
    "            (scene change)                       (1,90)  [30]\n"
    "    help    print this help message\n";


enum {
    DEFAULT_LT = 10,
    DEFAULT_LL = 4,
    DEFAULT_CT = 16,
    DEFAULT_CL = 8,
    DEFAULT_SC = 30,
};


typedef struct dnrprivatedata_ {
    int is_first_frame;
    int pPartial;
    int pThreshold;
    int pThreshold2;
    int pPixellock;
    int pPixellock2;
    int pScene;

    int isYUV;
    uint8_t *lastframe;
    uint8_t *origframe;
    int gu_ofs, bv_ofs;

    unsigned char lookup[256][256];
    unsigned char *lockhistory;

    uint8_t *src_data;
    uint8_t *undo_data;
    long src_h, src_w;
    int img_size;
    int hist_size;
    int pitch;
    int line_size_c;
    int line_size_l;
    int undo;

    char conf_str[TC_BUF_MIN];
} DNRPrivateData;

/*************************************************************************/

/**
 * dnr_run:  Perform the core noise reduction routine.
 */

static int dnr_run(DNRPrivateData *pd, uint8_t *data)
{
    uint8_t *RY1, *RY2, *RY3, *GU1, *GU2, *GU3, *BV1, *BV2, *BV3;
    int rl, rc, w, h, update_needed, totpixels;
    int threshRY, threshGU = 0, threshBV = 0;
    int ry1, ry2, gu1 = 0, gu2 = 0, bv1 = 0, bv2 = 0;
    long totlocks = 0;
    uint8_t *lockhistory = pd->lockhistory;

    //-- get data into account --
    pd->src_data = data;

    //-- if we are dealing with the first --
    //-- frame, just make a copy.         --
    if (pd->is_first_frame) {
        ac_memcpy(pd->lastframe, pd->src_data, pd->img_size);
        pd->undo_data = pd->lastframe;
        pd->is_first_frame = 0;

         return TC_OK;
    }

    //-- make sure to preserve the existing frame --
    //-- in case this is a scene change           --
    ac_memcpy(pd->origframe, pd->src_data, pd->img_size);

    if (pd->isYUV) {
        RY1 = pd->src_data;
        GU1 = RY1 + pd->gu_ofs; 
        BV1 = RY1 + pd->bv_ofs;    
    
        RY2 = pd->lastframe;
        GU2 = RY2 + pd->gu_ofs; 
        BV2 = RY2 + pd->bv_ofs;   

        RY3 = pd->src_data;     
        GU3 = RY3 + pd->gu_ofs; 
        BV3 = RY3 + pd->bv_ofs;   
    } else {
        BV1 = pd->src_data;
        GU1 = BV1 + pd->gu_ofs; 
        RY1 = BV1 + pd->bv_ofs;    
    
        BV2 = pd->lastframe;
        GU2 = BV2 + pd->gu_ofs; 
        RY2 = BV2 + pd->bv_ofs;   

        BV3 = pd->src_data;     
        GU3 = BV3 + pd->gu_ofs; 
        RY3 = BV3 + pd->bv_ofs;
    }
 
    h = pd->src_h;
    do {
        w = pd->src_w;
        rl = rc = 0;
        do {
            update_needed = 1;

            //-- on every row get (luma) actual/locked pixels --
            //-- -> calculate thresold (biased diff.)         --
            //--------------------------------------------------
            ry1 = RY1[rl];
            ry2 = RY2[rl];
            threshRY = pd->lookup[ry1][ry2];

            //-- in YUV-Mode on every even row  (RGB every --
            //-- row) get (chroma) actual/locked pixels    --
            //-- -> calculate thresold (biased diff.)      --
            //-----------------------------------------------
            if (!pd->isYUV || !(rl & 0x01)) {
                gu1 = GU1[rc];
                bv1 = BV1[rc];
                gu2 = GU2[rc];
                bv2 = BV2[rc];
                threshGU = pd->lookup[gu1][gu2];
                threshBV = pd->lookup[bv1][bv2];
            }

            //-- PARTIAL --
            //-------------
            if (pd->pPartial) {
                // we're doing a full pixel lock since we're --
                // under all thresholds in a couple of time  --
                //---------------------------------------------
                if ((threshRY < pd->pPixellock)
                    && (threshGU < pd->pPixellock2)
                    && (threshBV < pd->pPixellock2)) {

                    //-- if we've locked more than 30 times at --
                    //-- this point, let's refresh the pixel.  --
                    if (*lockhistory > 30) {
                        *lockhistory = 0;

                        ry1 = (ry1 + ry2) / 2;
                        gu1 = (gu1 + gu2) / 2;
                        bv1 = (bv1 + bv2) / 2;
                    }
                    else {
                        *lockhistory = *lockhistory + 1;

                        //-- take locked pixels --
                        ry1 = ry2;
                        gu1 = gu2;
                        bv1 = bv2;
                    }
                } else if ((threshRY < pd->pPixellock)
                         && (threshGU < pd->pThreshold2)
                         && (threshBV < pd->pThreshold2)) {
                    //-- If the luma is within pixellock, and the chroma is within   --
                    //-- blend, lets blend the chroma and lock the luma.
                    //-----------------------------------------------------------------
                    *lockhistory = 0;

                    ry1 = ry2;
                    gu1 = (gu1 + gu2) / 2;
                    bv1 = (bv1 + bv2) / 2;
                } else if ((threshRY < pd->pThreshold)
                         && (threshGU < pd->pThreshold2)
                         && (threshBV < pd->pThreshold2)) {
                    //-- We are above pixellock in luma and chroma, but     --
                    //-- below the blend thresholds in both, so let's blend --
                    //--------------------------------------------------------
                    *lockhistory = 0;

                    ry1 = (ry1 + ry2) / 2;
                    gu1 = (gu1 + gu2) / 2;
                    bv1 = (bv1 + bv2) / 2;
                } else {
                    //-- if we are above all thresholds, --
                    //-- just leave the output untouched --
                    //-------------------------------------
                    *lockhistory = 0;
                    update_needed = 0;
                    totlocks++;
                }
            }
            //-- nonPARTIAL --
            //----------------
            else {
                //-- beneath pixellock so lets keep   --
                //-- the existing pixel (most likely) --
                //--------------------------------------
                if ((threshRY < pd->pPixellock)
                    && (threshGU < pd->pPixellock2)
                    && (threshBV < pd->pPixellock2)) {
                    // if we've locked more than 30 times at this point,
                    // let's refresh the pixel
                    if (*lockhistory > 30) {
                        *lockhistory = 0;

                        ry1 = (ry1 + ry2) / 2;
                        gu1 = (gu1 + gu2) / 2;
                        bv1 = (bv1 + bv2) / 2;
                    } else {
                        *lockhistory = *lockhistory + 1;

                        ry1 = ry2;
                        gu1 = gu2;
                        bv1 = bv2;
                    }
                } else if ((threshRY < pd->pThreshold) &&
                         (threshGU < pd->pThreshold2) &&
                         (threshBV < pd->pThreshold2)) {
                    //-- we are above pixellock, but below the --
                    //-- blend threshold so we want to blend   --
                    //-------------------------------------------
                    *lockhistory = 0;

                    ry1 = (ry1 + ry2) / 2;
                    gu1 = (gu1 + gu2) / 2;
                    bv1 = (bv1 + bv2) / 2;
                } else {
                    //-- it's beyond the thresholds, just leave it alone --
                    //-----------------------------------------------------
                    *lockhistory = 0;
                    update_needed = 0;
                    totlocks++;
                }
            }

            //-- set destination --
            //---------------------
            if (update_needed) {
                RY3[rl] = ry1;
                GU3[rc] = gu1;
                BV3[rc] = bv1;
            }

            //-- refresh locked pixels --
            //---------------------------
            if (*lockhistory == 0) {
                RY2[rl] = ry1;
                GU2[rc] = gu1;
                BV2[rc] = bv1;
            }

            lockhistory++;

            rl += pd->pitch;
            rc = (pd->isYUV) ? (rl >> 1) : rl;

        } while (--w);

        //-- next line ... --
        RY1 += pd->line_size_l;
        RY2 += pd->line_size_l;
        RY3 += pd->line_size_l;

        //-- ... in YUV-Mode for chromas, only on even luma-lines --
        if (!pd->isYUV || !(h & 0x01)) {
            GU1 += pd->line_size_c;
            BV1 += pd->line_size_c;

            GU2 += pd->line_size_c;
            BV2 += pd->line_size_c;

            GU3 += pd->line_size_c;
            BV3 += pd->line_size_c;
        }

    } while (--h);

    totpixels = pd->src_h * pd->src_w;
    totpixels *= pd->pScene;
    totpixels /= 100;

    // If more than the specified percent of pixels have exceeded all thresholds
    // then we restore the saved frame.  (this doesn't happen very often
    // hopefully)  We also set the pixellock history to 0 for all frames

    if (totlocks > totpixels) {
        uint8_t *ptmp = pd->lastframe;

        pd->lastframe = pd->origframe;
        pd->undo_data = pd->lastframe;
        pd->origframe = ptmp;
        pd->undo = 1;

        memset(pd->lockhistory, 0, pd->hist_size);
    } else {
        pd->undo_data = pd->src_data;
        pd->undo = 0;
    }

     return TC_OK;
}

/*************************************************************************/

/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * dnr_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int dnr_stop(TCModuleInstance *self)
{
    DNRPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    if (pd->lastframe) {
        tc_free(pd->lastframe);
        pd->lastframe = NULL;
    }
    if (pd->origframe) {
        tc_free(pd->origframe);
        pd->origframe = NULL;
    }
    if (pd->lockhistory) {
        tc_free(pd->lockhistory);
        pd->lockhistory = NULL;
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * dnr_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int dnr_configure(TCModuleInstance *self,
                         const char *options,
                         TCJob *vob,
                         TCModuleExtraData *xdata[])
{
    DNRPrivateData *pd = NULL;
    double low1, low2;
    double high1, high2;
    int a, b, dif1, dif2;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    //-- PARAMETERS --
    pd->pThreshold = DEFAULT_LT;  // threshold to blend luma/red (default 10)
    pd->pPixellock = DEFAULT_LL;  // threshold to lock luma/red (default 4)
    pd->pThreshold2 = DEFAULT_CT; // threshold to blend croma/green+blue (default 16)
    pd->pPixellock2 = DEFAULT_CL; // threshold to lock croma/green+blue (default 8)
    pd->pScene = DEFAULT_SC;  // percentage of picture difference
    // to interpret as a new scene (default 30%)
    pd->pPartial = 0;         // operating mode [0,1] (default 0)
    //----------------

    if (options) {
        optstr_get(options, "lt", "%d", &pd->pThreshold);
        optstr_get(options, "ll", "%d", &pd->pPixellock);
        optstr_get(options, "ct", "%d", &pd->pThreshold2);
        optstr_get(options, "cl", "%d", &pd->pPixellock2);
        optstr_get(options, "sc", "%d", &pd->pScene);

        if (pd->pThreshold > 128 || pd->pThreshold < 1)
            pd->pThreshold = DEFAULT_LT;
        if (pd->pPixellock > 128 || pd->pPixellock < 1)
            pd->pPixellock = DEFAULT_LL;
        if (pd->pThreshold2 > 128 || pd->pThreshold2 < 1)
            pd->pThreshold2 = DEFAULT_CT;
        if (pd->pPixellock2 > 128 || pd->pPixellock2 < 1)
            pd->pPixellock2 = DEFAULT_CL;
        if (pd->pScene > 90 || pd->pScene < 1)
            pd->pScene = DEFAULT_SC;
    }

    pd->isYUV = (vob->im_v_codec == TC_CODEC_YUV420P);
    pd->src_h = vob->ex_v_height;
    pd->src_w = vob->ex_v_width;
    pd->is_first_frame = 1;
    pd->lastframe = tc_zalloc(pd->src_h * pd->src_w * 3);
    pd->origframe = tc_zalloc(pd->src_h * pd->src_w * 3);
    pd->lockhistory = tc_zalloc(pd->src_h * pd->src_w);
    pd->hist_size = pd->src_h * pd->src_w;

    if (pd->isYUV) {
        pd->gu_ofs = pd->hist_size;
        pd->bv_ofs = pd->gu_ofs + (pd->src_h / 2) * (pd->src_w / 2);
        pd->img_size = pd->bv_ofs + (pd->src_h / 2) * (pd->src_w / 2);
        pd->pitch = 1;

        pd->line_size_c = (pd->src_w >> 1);
        pd->line_size_l = pd->src_w;
    } else {
        pd->img_size = pd->hist_size * 3;
        pd->gu_ofs = 1;
        pd->bv_ofs = 2;
        pd->pitch = 3;

        pd->line_size_c = pd->src_w * 3;
        pd->line_size_l = pd->src_w * 3;
    }

    if (!pd->lastframe || !pd->origframe || !pd->lockhistory) {
        dnr_stop(self);
        return TC_ERROR;
    }

    // setup a biased thresholding difference matrix
    // this is an expensive operation we only want to to once
    for (a = 0; a < 256; a++) {
        for (b = 0; b < 256; b++) {
            // instead of scaling linearly
            // we scale according to the following formulas
            // val1 = 256 * (x / 256) ^ .9
            // and
            // val2 = 256 * (x / 256) ^ (1/.9)
            // and we choose the maximum distance between two points
            // based on these two scales
            low1 = a;
            low2 = b;
            low1 = low1 / 256;
            low1 = 256 * pow(low1, .9);
            low2 = low2 / 256;
            low2 = 256 * pow(low2, .9);

            // the low scale should make all values larger
            // and the high scale should make all values smaller
            high1 = a;
            high2 = b;
            high1 = high1 / 256;
            high2 = high2 / 256;
            high1 = 256 * pow(high1, 1.0 / .9);
            high2 = 256 * pow(high2, 1.0 / .9);
            dif1 = abs((int) (low1 - low2));
//            if (dif1 < 0)
//                dif1 *= -1;
            dif2 = abs((int) (high1 - high2));
//            if (dif2 < 0)
//                dif2 *= -1;
            pd->lookup[a][b] = TC_MAX(dif1, dif2);
        }
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * dnr_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(dnr, DNRPrivateData)

/*************************************************************************/

/**
 * dnr_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(dnr)

/*************************************************************************/

/**
 * dnr_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

#define INSPECT_PARAM(PARM, NAME, TYPE) do { \
    if (optstr_lookup(param, NAME)) { \
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str), \
                    "%s=" TYPE, NAME, pd->PARM); \
        *value = pd->conf_str; \
    } \
} while (0)


static int dnr_inspect(TCModuleInstance *self,
                       const char *param, const char **value)
{
    DNRPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    
    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = dnr_help;
    }

    INSPECT_PARAM(pThreshold,  "lt", "%i");
    INSPECT_PARAM(pPixellock,  "ll", "%i");
    INSPECT_PARAM(pThreshold2, "ct", "%i");
    INSPECT_PARAM(pPixellock2, "cl", "%i");
    INSPECT_PARAM(pScene,      "sc", "%i");

    return TC_OK;
}

#undef INSPECT_PARAM

/*************************************************************************/

/**
 * dnr_filter_video:  perform the dynamic noise reduction for each frame of
 * this video stream. See tcmodule-data.h for function details.
 */

static int dnr_filter_video(TCModuleInstance *self,
                            vframe_list_t *frame)
{
    DNRPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "filter");
    TC_MODULE_SELF_CHECK(frame, "filter");

    pd = self->userdata;

    dnr_run(pd, frame->video_buf);

    if (pd->undo) {
        ac_memcpy(frame->video_buf, pd->undo_data,
                  pd->img_size);
    }
    return TC_OK;
}



/*************************************************************************/

static const TCCodecID dnr_codecs_video_in[] = { 
    TC_CODEC_YUV420P, TC_CODEC_RGB24, TC_CODEC_ERROR
};
static const TCCodecID dnr_codecs_video_out[] = { 
    TC_CODEC_YUV420P, TC_CODEC_RGB24, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(dnr);
TC_MODULE_FILTER_FORMATS(dnr);

TC_MODULE_INFO(dnr);

static const TCModuleClass dnr_class = {
    TC_MODULE_CLASS_HEAD(dnr),

    .init         = dnr_init,
    .fini         = dnr_fini,
    .configure    = dnr_configure,
    .stop         = dnr_stop,
    .inspect      = dnr_inspect,

    .filter_video = dnr_filter_video,
};

TC_MODULE_ENTRY_POINT(dnr)

/*************************************************************************/

static int dnr_get_config(TCModuleInstance *self, char *options)
{
    DNRPrivateData *pd = NULL;
    char buf[TC_BUF_MIN];

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = self->userdata;

    /* use optstr_param to do introspection */
    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       "Gerhard Monzel", "VYRO", "1");

    tc_snprintf(buf, sizeof(buf), "%d", pd->pThreshold);
    optstr_param(options, "lt", "Threshold to blend luma/red", "%d",
                 buf, "1", "128");

    tc_snprintf(buf, sizeof(buf), "%d", pd->pPixellock);
    optstr_param(options, "ll", "Threshold to lock luma/red", "%d", buf,
                 "1", "128");

    tc_snprintf(buf, sizeof(buf), "%d", pd->pThreshold2);
    optstr_param(options, "ct", "Threshold to blend croma/green+blue",
                 "%d", buf, "1", "128");

    tc_snprintf(buf, sizeof(buf), "%d", pd->pPixellock2);
    optstr_param(options, "cl", "Threshold to lock croma/green+blue",
                 "%d", buf, "1", "128");

    tc_snprintf(buf, sizeof(buf), "%d", pd->pScene);
    optstr_param(options, "sc",
                 "Percentage of picture difference (scene change)",
                 "%d", buf, "1", "90");
   
    return TC_OK;
}

static int dnr_process(TCModuleInstance *self, frame_list_t *frame)
{
    DNRPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "process");

    pd = self->userdata;

    if ((frame->tag & TC_POST_M_PROCESS) && (frame->tag & TC_VIDEO)
      && !(frame->attributes & TC_FRAME_IS_SKIPPED)) {
        return dnr_filter_video(self, (vframe_list_t*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE(dnr)

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
