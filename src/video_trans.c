/*
 * video_trans.c - video frame transformation routines
 * Written by Andrew Church <achurch@achurch.org>
 * Based on code written by Thomas Oestreich.
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "transcode.h"
#include "framebuffer.h"
#include "video_trans.h"
#include "libtcvideo/tcvideo.h"

/*************************************************************************/

/* Structure that holds video frame information for passing around to
 * processing routines.  Since this is used only locally, we don't add
 * the fields to vframe_list_t itself. */

typedef struct {
    vframe_list_t *ptr;
    int preadj_w, preadj_h;  // width and height used for secondary buffer
    int Bpp;                 // BYTES (not bits) per pixel
    int nplanes;             // number of planes
    uint8_t *planes[3];      // pointer to start of each plane
    uint8_t *tmpplanes[3];   // same, for secondary buffer
    int width_div[3];        // width divisors for each plane
    int height_div[3];       // height divisors for each plane
    uint8_t black_pixel[3];  // "black" value for each plane (e.g. 128 for U/V)
} video_trans_data_t;

/* Macro to perform a transformation on a frame.  `vtd' is a pointer to a
 * video_trans_data_t; the given function `func' will be called for each
 * plane `i' as:
 *     func(handle, vtd->planes[i], vtd->tmpplanes[i], vtd->ptr->v_width,
 *          vtd->ptr->v_height, vtd->Bpp, args)
 * where `args' are all arguments to this macro (if any) following `vtd'.
 * swap_buffers(vtd) is called after the processing is complete.
 */
#define PROCESS_FRAME(func,vtd,args...) do {                    \
    int i;                                                      \
    for (i = 0; i < (vtd)->nplanes; i++) {                      \
        func(handle, (vtd)->planes[i], (vtd)->tmpplanes[i],     \
             (vtd)->ptr->v_width / (vtd)->width_div[i],         \
             (vtd)->ptr->v_height / (vtd)->height_div[i],       \
             (vtd)->Bpp , ## args);                             \
    }                                                           \
    swap_buffers(vtd);                                          \
} while (0)

/* Handle for calling tcvideo functions. */
static TCVHandle handle = 0;

/*************************************************************************/
/*************************** Internal routines ***************************/
/*************************************************************************/

/**
 * set_vtd:  Initialize the given vtd structure from the given
 * vframe_list_t, and update ptr->video_size.
 *
 * Parameters:
 *     vtd: Pointer to video frame data to be initialized.
 *     ptr: Pointer to video frame buffer.
 * Return value:
 *     None.
 */

static void set_vtd(video_trans_data_t *vtd, vframe_list_t *ptr)
{
    int i;

    vtd->ptr = ptr;
    vtd->preadj_w = 0;
    vtd->preadj_h = 0;
    /* Set some defaults */
    vtd->Bpp = 1;
    vtd->nplanes = 1;
    vtd->planes[0] = ptr->video_buf;
    vtd->tmpplanes[0] = ptr->video_buf_Y[ptr->free];
    vtd->width_div[0] = 1;
    vtd->height_div[0] = 1;
    vtd->black_pixel[0] = 0;
    /* Now set parameters based on image format */
    if (ptr->v_codec == TC_CODEC_YUV420P) {
        vtd->nplanes = 3;
        vtd->Bpp = 1;
        vtd->width_div[1] = 2;
        vtd->width_div[2] = 2;
        vtd->height_div[1] = 2;
        vtd->height_div[2] = 2;
        vtd->black_pixel[1] = 128;
        vtd->black_pixel[2] = 128;
    } else if (vtd->ptr->v_codec == TC_CODEC_YUV422P) {
        vtd->nplanes = 3;
        vtd->Bpp = 1;
        vtd->width_div[1] = 2;
        vtd->width_div[2] = 2;
        vtd->height_div[1] = 1;
        vtd->height_div[2] = 1;
        vtd->black_pixel[1] = 128;
        vtd->black_pixel[2] = 128;
    } else if (vtd->ptr->v_codec == TC_CODEC_RGB24) {
        vtd->Bpp = 3;
    }
    ptr->video_size = 0;
    for (i = 0; i < vtd->nplanes; i++) {
        int planesize = (ptr->v_width/vtd->width_div[i])
                      * (ptr->v_height/vtd->height_div[i])
                      * vtd->Bpp;
        ptr->video_size += planesize;
        if (i < vtd->nplanes-1) {
            vtd->planes[i+1] = vtd->planes[i] + planesize;
            vtd->tmpplanes[i+1] = vtd->tmpplanes[i] + planesize;
        }
    }
}

/*************************************************************************/

/**
 * preadjust_frame_size:  Prepare for an operation that will change the
 * frame size, setting up the secondary buffer plane pointers with the new
 * size.  Calling swap_buffers() will store the new size in the
 * vframe_list_t structure.
 *
 * Parameters:
 *       vtd: Pointer to video frame data.
 *     new_w: New frame width.
 *     new_h: New frame height.
 * Return value:
 *     None.
 */

static void preadjust_frame_size(video_trans_data_t *vtd, int new_w, int new_h)
{
    int i;

    vtd->preadj_w = new_w;
    vtd->preadj_h = new_h;
    for (i = 0; i < vtd->nplanes-1; i++) {
        int planesize = (new_w/vtd->width_div[i]) * (new_h/vtd->height_div[i])
                      * vtd->Bpp;
        vtd->tmpplanes[i+1] = vtd->tmpplanes[i] + planesize;
    }
}

/*************************************************************************/

/**
 * swap_buffers:  Swap current video frame buffer with free buffer.  Also
 * updates frame size if preadjust_frame_size() has been called.
 *
 * Parameters:
 *     vtd: Pointer to video frame data.
 * Return value:
 *     None.
 */

static void swap_buffers(video_trans_data_t *vtd)
{
    vtd->ptr->video_buf = vtd->ptr->video_buf_Y[vtd->ptr->free];
    vtd->ptr->free = (vtd->ptr->free==0) ? 1 : 0;
    /* Install new width/height if preadjust_frame_size() was called */
    if (vtd->preadj_w && vtd->preadj_h) {
        vtd->ptr->v_width = vtd->preadj_w;
        vtd->ptr->v_height = vtd->preadj_h;
        vtd->preadj_w = 0;
        vtd->preadj_h = 0;
    }
    /* Set up plane pointers again */
    set_vtd(vtd, vtd->ptr);
}

/*************************************************************************/
/*************************************************************************/

/**
 * do_process_frame:  Perform video frame transformations based on global
 * transcoding settings (derived from command-line arguments).
 *
 * Parameters:
 *     vob: Global data pointer.
 *     ptr: Pointer to video frame buffer.
 * Return value:
 *     0 on success, -1 on failure.
 */

static int do_process_frame(vob_t *vob, vframe_list_t *ptr)
{
    video_trans_data_t vtd;  /* for passing to subroutines */


    /**** Sanity check and initialization ****/

    if (ptr->video_buf_Y[0] == ptr->video_buf_Y[1]) {
        tc_log_error(__FILE__, "video frame has no temporary buffer!");
        return -1;
    }
    if (ptr->video_buf == ptr->video_buf_Y[ptr->free]) {
        static int warned = 0;
        if (!warned) {
            tc_log_warn(__FILE__, "ptr->free points to wrong buffer"
                        " (BUG in transcode or modules)");
            warned = 1;
        }
        ptr->free = !ptr->free;
    }
    set_vtd(&vtd, ptr);

    /**** -j: clip frame (import) ****/

    if (im_clip) {
        preadjust_frame_size(&vtd,
                ptr->v_width - vob->im_clip_left - vob->im_clip_right,
                ptr->v_height - vob->im_clip_top - vob->im_clip_bottom);
        PROCESS_FRAME(tcv_clip, &vtd,
                      vob->im_clip_left   / vtd.width_div[i],
                      vob->im_clip_right  / vtd.width_div[i],
                      vob->im_clip_top    / vtd.height_div[i],
                      vob->im_clip_bottom / vtd.height_div[i],
                      vtd.black_pixel[i]);
    }

    /**** -I: deinterlace video frame ****/

    if (vob->deinterlace > 0
     || ((ptr->attributes & TC_FRAME_IS_INTERLACED) && ptr->deinter_flag > 0)
    ) {
        int mode = (vob->deinterlace>0 ? vob->deinterlace : ptr->deinter_flag);
        if (mode == 1) {
            /* Simple linear interpolation */
            /* Note that for YUV, we can just leave U and V alone, since
             * they already cover pairs of lines; thus instead of using
             * PROCESS_FRAME, we just call tcv_deinterlace() on the Y/RGB
             * plane, then copy the other two planes and swap_buffers(). */
            int i;
            tcv_deinterlace(handle, vtd.planes[0], vtd.tmpplanes[0],
                            ptr->v_width, ptr->v_height, vtd.Bpp,
                            TCV_DEINTERLACE_INTERPOLATE);
            for (i = 1; i < vtd.nplanes; i++) {
                ac_memcpy(vtd.tmpplanes[i], vtd.planes[i],
                          (ptr->v_width/vtd.width_div[i])
                          * (ptr->v_height/vtd.height_div[i]));
            }
            swap_buffers(&vtd);
        } else if (mode == 3 || mode == 4) {
            /* Drop every other line (and zoom back out in mode 3) */
            preadjust_frame_size(&vtd, ptr->v_width, ptr->v_height/2);
            /* Drop the top or the bottom field?  (Does it matter?) */
            PROCESS_FRAME(tcv_deinterlace, &vtd,
                          TCV_DEINTERLACE_DROP_FIELD_BOTTOM);
            if (mode == 3) {
                int w = ptr->v_width, h = ptr->v_height*2;
                preadjust_frame_size(&vtd, w, h);
                PROCESS_FRAME(tcv_zoom, &vtd, w / vtd.width_div[i],
                              h / vtd.height_div[i], vob->zoom_filter);
            }
        } else if (mode == 5) {
            /* Linear blend; as for -I 1, only Y is processed in YUV mode */
            int i;
            tcv_deinterlace(handle, vtd.planes[0], vtd.tmpplanes[0],
                            ptr->v_width, ptr->v_height, vtd.Bpp,
                            TCV_DEINTERLACE_LINEAR_BLEND);
            for (i = 1; i < vtd.nplanes; i++) {
                ac_memcpy(vtd.tmpplanes[i], vtd.planes[i],
                          (ptr->v_width/vtd.width_div[i])
                          * (ptr->v_height/vtd.height_div[i]));
            }
            swap_buffers(&vtd);
        }
        /* else mode 2 (handled by encoder) or unknown: do nothing */
        ptr->attributes &= ~TC_FRAME_IS_INTERLACED;
    }

    /**** -X: fast resize (up) ****/
    /**** -B: fast resize (down) ****/

    if (resize1 || resize2) {
        int width = ptr->v_width, height = ptr->v_height;
        int resize_w = vob->hori_resize2 - vob->hori_resize1;
        int resize_h = vob->vert_resize2 - vob->vert_resize1;
        if (resize_h) {
            preadjust_frame_size(&vtd, width, height+resize_h*8);
            PROCESS_FRAME(tcv_resize, &vtd, 0, resize_h, 8/vtd.width_div[i],
                          8/vtd.height_div[i]);
            height += resize_h * 8;
        }
        if (resize_w) {
            preadjust_frame_size(&vtd, width+resize_w*8, height);
            PROCESS_FRAME(tcv_resize, &vtd, resize_w, 0, 8/vtd.width_div[i],
                          8/vtd.height_div[i]);
        }
    }

    /**** -Z: zoom frame (slow resize) ****/

    if (vob->zoom_flag) {
        preadjust_frame_size(&vtd, vob->zoom_width, vob->zoom_height);
        if (vob->zoom_interlaced) {
            /* In YUV mode, only handle the first place as interlaced;
             * the U and V planes are shared between both fields */
            int i;
            tcv_zoom(handle, vtd.planes[0], vtd.tmpplanes[0],
                     ptr->v_width, ptr->v_height, vtd.Bpp,
                     vob->zoom_width, -vob->zoom_height, vob->zoom_filter);
            for (i = 1; i < vtd.nplanes; i++) {
                tcv_zoom(handle, vtd.planes[i], vtd.tmpplanes[i],
                         ptr->v_width / vtd.width_div[i],
                         ptr->v_height / vtd.height_div[i], vtd.Bpp,
                         vob->zoom_width / vtd.width_div[i],
                         vob->zoom_height / vtd.height_div[i],
                         vob->zoom_filter);
            }
            swap_buffers(&vtd);
        } else {
            PROCESS_FRAME(tcv_zoom, &vtd, vob->zoom_width / vtd.width_div[i],
                          vob->zoom_height / vtd.height_div[i],
                          vob->zoom_filter);
        }
    }

    /**** -Y: clip frame (export) ****/

    if (ex_clip) {
        preadjust_frame_size(&vtd,
                ptr->v_width - vob->ex_clip_left-vob->ex_clip_right,
                ptr->v_height - vob->ex_clip_top - vob->ex_clip_bottom);
        PROCESS_FRAME(tcv_clip, &vtd,
                      vob->ex_clip_left   / vtd.width_div[i],
                      vob->ex_clip_right  / vtd.width_div[i],
                      vob->ex_clip_top    / vtd.height_div[i],
                      vob->ex_clip_bottom / vtd.height_div[i],
                      vtd.black_pixel[i]);
    }

    /**** -r: rescale video frame ****/

    if (rescale) {
        preadjust_frame_size(&vtd, ptr->v_width / vob->reduce_w,
                             ptr->v_height / vob->reduce_h);
        PROCESS_FRAME(tcv_reduce, &vtd, vob->reduce_w, vob->reduce_h);
    }

    /**** -z: flip frame vertically ****/

    if (vob->flip) {
        PROCESS_FRAME(tcv_flip_v, &vtd);
    }

    /**** -l: flip flame horizontally (mirror) ****/

    if (vob->mirror) {
        PROCESS_FRAME(tcv_flip_h, &vtd);
    }

    /**** -k: red/blue swap ****/

    if (vob->rgbswap) {
        if (ptr->v_codec == TC_CODEC_RGB24) {
            int i;
            for (i = 0; i < ptr->v_width * ptr->v_height; i++) {
                uint8_t tmp = vtd.planes[0][i*3];
                vtd.planes[0][i*3] = vtd.planes[0][i*3+2];
                vtd.planes[0][i*3+2] = tmp;
            }
        } else {
            int UVsize = (ptr->v_width  / vtd.width_div[1])
                       * (ptr->v_height / vtd.height_div[1]) * vtd.Bpp;
            ac_memcpy(vtd.tmpplanes[1], vtd.planes[1], UVsize);  /* tmp<-U   */
            ac_memcpy(vtd.planes[1], vtd.planes[2], UVsize);     /*   U<-V   */
            ac_memcpy(vtd.planes[2], vtd.tmpplanes[1], UVsize);  /*   V<-tmp */
        }
    }

    /**** -K: grayscale ****/

    if (vob->decolor) {
        if (ptr->v_codec == TC_CODEC_RGB24) {
            /* Convert to 8-bit grayscale, then back to RGB24.  Just
             * averaging the values won't give us the right intensity. */
            tcv_convert(handle, vtd.planes[0], vtd.tmpplanes[0],
                        ptr->v_width, ptr->v_height, IMG_RGB24, IMG_GRAY8);
            tcv_convert(handle, vtd.tmpplanes[0], vtd.planes[0],
                        ptr->v_width, ptr->v_height, IMG_GRAY8, IMG_RGB24);
        } else {
            /* YUV is easy: just set U and V to 128 */
            int UVsize = (ptr->v_width  / vtd.width_div[1])
                       * (ptr->v_height / vtd.height_div[1]) * vtd.Bpp;
            memset(vtd.planes[1], 128, UVsize);
            memset(vtd.planes[2], 128, UVsize);
        }
    }

    /**** -G: gamma correction ****/

    if (vob->dgamma) {
        /* Only process the first plane (Y) for YUV; for RGB it's all in
         * one plane anyway */
        tcv_gamma_correct(handle, ptr->video_buf, ptr->video_buf,
                          ptr->v_width, ptr->v_height, vtd.Bpp, vob->gamma);
    }

    /**** -C: antialiasing ****/

    if (vob->antialias) {
        /* Only Y is antialiased; U and V remain the same */
        tcv_antialias(handle, vtd.planes[0], vtd.tmpplanes[0],
                      ptr->v_width, ptr->v_height, vtd.Bpp,
                      vob->aa_weight, vob->aa_bias);
        if (ptr->v_codec != TC_CODEC_RGB24) {
            int UVsize = (ptr->v_width  / vtd.width_div[1])
                       * (ptr->v_height / vtd.height_div[1]) * vtd.Bpp;
            ac_memcpy(vtd.tmpplanes[1], vtd.planes[1], UVsize);
            ac_memcpy(vtd.tmpplanes[2], vtd.planes[2], UVsize);
        }
        swap_buffers(&vtd);
    }

    /**** End of processing ****/

    return 0;
}

/*************************************************************************/
/*************************** Exported routines ***************************/
/*************************************************************************/

/**
 * process_vid_frame:  Main video frame processing routine.  The image is
 * passed in ptr->video_buf; this can be updated as needed, e.g. to point
 * to the secondary buffer after transformations.
 *
 * Parameters:
 *     vob: Global data pointer.
 *     ptr: Pointer to video frame buffer.
 * Return value:
 *     0 on success, -1 on failure.
 */

int process_vid_frame(vob_t *vob, vframe_list_t *ptr)
{
    /* Check parameter validity */
    if (!vob || !ptr)
        return -1;

    /* Check for pass-through mode or skipped frames */
    if (vob->pass_flag & TC_VIDEO)
        return 0;
    if (ptr->attributes & TC_FRAME_IS_SKIPPED)
        return 0;

    /* It's a valid frame, check the colorspace for validity and process it */
    if (vob->im_v_codec == TC_CODEC_RGB24
     || vob->im_v_codec == TC_CODEC_YUV420P
     || vob->im_v_codec == TC_CODEC_YUV422P
    ) {
        ptr->v_codec = vob->im_v_codec;
        return do_process_frame(vob, ptr);
    }

    /* Invalid colorspace, bail out */
    tc_error("Oops, invalid colorspace video frame data");
    return -1;
}

/*************************************************************************/

/**
 * preprocess_vid_frame:  Frame preprocessing routine.  Checks for frame
 * out of -c range and performs early clipping.
 *
 * Parameters:
 *     vob: Global data pointer.
 *     ptr: Pointer to video frame buffer.
 * Return value:
 *     0 on success, -1 on failure.
 */

int preprocess_vid_frame(vob_t *vob, vframe_list_t *ptr)
{
    /* Check parameter validity */
    if (!vob || !ptr)
        return -1;

    /* Allocate tcvideo handle if necessary */
    if (!handle) {
        handle = tcv_init();
        if (!handle) {
            tc_log_error(PACKAGE, "video_trans.c: tcv_init() failed!");
            return -1;
        }
    }

    /* Check for pass-through mode */
    if (vob->pass_flag & TC_VIDEO)
        return 0;

    /* Check frame colorspace */
    if (vob->im_v_codec != TC_CODEC_RGB24
     && vob->im_v_codec != TC_CODEC_YUV420P
     && vob->im_v_codec != TC_CODEC_YUV422P
    ) {
        tc_error("Oops, invalid colorspace video frame data");
        return -1;
    }

    /* Perform early clipping */
    if (pre_im_clip) {
        video_trans_data_t vtd;
        ptr->v_codec = vob->im_v_codec;
        set_vtd(&vtd, ptr);
        preadjust_frame_size(&vtd,
            ptr->v_width - vob->pre_im_clip_left - vob->pre_im_clip_right,
            ptr->v_height - vob->pre_im_clip_top - vob->pre_im_clip_bottom);
        PROCESS_FRAME(tcv_clip, &vtd,
                      vob->pre_im_clip_left   / vtd.width_div[i],
                      vob->pre_im_clip_right  / vtd.width_div[i],
                      vob->pre_im_clip_top    / vtd.height_div[i],
                      vob->pre_im_clip_bottom / vtd.height_div[i],
                      vtd.black_pixel[i]);
    }

    /* Finished with preprocessing */
    return 0;
}

/*************************************************************************/

/**
 * postprocess_vid_frame:  Frame postprocessing routine.  Performs final
 * clipping and sanity checks.
 *
 * Parameters:
 *     vob: Global data pointer.
 *     ptr: Pointer to video frame buffer.
 * Return value:
 *     0 on success, -1 on failure.
 */

/* Frame postprocessing routine.  Performs final clipping and sanity
 * checks.  Parameters are as for process_vid_frame().
 */

int postprocess_vid_frame(vob_t *vob, vframe_list_t *ptr)
{
    /* Check parameter validity */
    if (!vob || !ptr)
        return -1;

    /* Check for pass-through mode or skipped frames */
    if (vob->pass_flag & TC_VIDEO)
        return 0;
    if (ptr->attributes & TC_FRAME_IS_SKIPPED)
        return 0;

    /* Check frame colorspace */
    if (vob->im_v_codec != TC_CODEC_RGB24
     && vob->im_v_codec != TC_CODEC_YUV420P
     && vob->im_v_codec != TC_CODEC_YUV422P
    ) {
        tc_error("Oops, invalid colorspace video frame data");
        return -1;
    }

    /* Perform final clipping, if this isn't a cloned frame */
    if (post_ex_clip && !(ptr->attributes & TC_FRAME_WAS_CLONED)) {
        video_trans_data_t vtd;
        ptr->v_codec = vob->im_v_codec;
        set_vtd(&vtd, ptr);
        preadjust_frame_size(&vtd,
            ptr->v_width - vob->post_ex_clip_left - vob->post_ex_clip_right,
            ptr->v_height - vob->post_ex_clip_top - vob->post_ex_clip_bottom);
        PROCESS_FRAME(tcv_clip, &vtd,
                      vob->post_ex_clip_left   / vtd.width_div[i],
                      vob->post_ex_clip_right  / vtd.width_div[i],
                      vob->post_ex_clip_top    / vtd.height_div[i],
                      vob->post_ex_clip_bottom / vtd.height_div[i],
                      vtd.black_pixel[i]);
    }

    /* Sanity check: make sure the frame size is what we're expecting */
    if (ptr->v_width != vob->ex_v_width || ptr->v_height != vob->ex_v_height) {
        tc_log_msg(__FILE__, "width %d %d | height %d %d",
                   ptr->v_width, vob->ex_v_width,
                   ptr->v_height, vob->ex_v_height);
        tc_error("Oops, frame parameter mismatch detected");
    }

    /* Finished with postprocessing */
    return 0;
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
