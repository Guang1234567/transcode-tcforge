/********* FIXME: tcvhandle sanity checks ************/
/*
 * tcvideo.c - video processing library for transcode
 * Written by Andrew Church <achurch@achurch.org>
 * Based on code written by Thomas Oestreich.
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "tcvideo.h"
#include "zoom.h"

#define zoom zoom_  // temp to avoid name conflict
#include "tccore/tc_defaults.h"
#include "tccore/frame.h"
#include "tccore/job.h"
#include "libtc/libtc.h"
#include "aclib/ac.h"
#undef zoom
#include <math.h>

/*************************************************************************/

#ifndef PI
# ifdef M_PI
#  define PI M_PI
# else
#  define PI 3.14159265358979323846264338327950
# endif
#endif

/* Antialiasing threshold for determining whether two pixels are the same
 * color. */
#define AA_DIFFERENT 25

/* Data for generating a resized pixel. */
struct resize_table_elem {
    int source;
    uint32_t weight1, weight2;
};

/* Maximum number of ZoomInfo structures to cache. */
#define ZOOMINFO_CACHE_SIZE 10


/* Internal data structure to hold various state information.  The
 * TCVHandle returned by tcv_init() and passed by the caller to other
 * functions is a pointer to this structure. */

struct tcvhandle_ {
    /* Various lookup tables */
    struct resize_table_elem resize_table_x[TC_MAX_V_FRAME_WIDTH/8];
    struct resize_table_elem resize_table_y[TC_MAX_V_FRAME_HEIGHT/8];
    uint8_t gamma_table[256];
    uint32_t aa_table_c[256];
    uint32_t aa_table_x[256];
    uint32_t aa_table_y[256];
    uint32_t aa_table_d[256];
    /* Initialization values used in creating lookup tables (so we know
     * whether we have to re-create them */
    int saved_oldw, saved_neww, saved_oldh, saved_newh;
    double saved_gamma;
    double saved_weight, saved_bias;
    /* ZoomInfo cache */
    struct {
        int old_w, old_h, new_w, new_h, Bpp, ilace;
        TCVZoomFilter filter;
        ZoomInfo *zi;
    } zoominfo_cache[ZOOMINFO_CACHE_SIZE];
    /* Buffer and buffer size for tcv_convert() */
    uint8_t *convert_buffer;
    uint32_t convert_buffer_size;
};

/*************************************************************************/

/* Internal-use functions (defined at the bottom of the file). */

static void init_resize_tables(TCVHandle handle,
                               int oldw, int neww, int oldh, int newh);
static void init_one_resize_table(struct resize_table_elem *table,
                                  int oldsize, int newsize);
static void init_gamma_table(TCVHandle handle, double gamma);
static void init_aa_table(TCVHandle handle, double aa_weight, double aa_bias);

/*************************************************************************/
/*************************************************************************/

/* External interface functions. */

/*************************************************************************/

/**
 * tcv_init:  Create and return a handle for use in other tcvideo
 * functions.  The handle should be freed with tcv_free() when no longer
 * needed.  Note that if you need to operate on multiple image sizes or
 * use different gamma or antialiasing values, you will get improved
 * performance by using separate handles for each set of values.  (However,
 * tcv_zoom() can cache lookup tables for multiple sets of image sizes,
 * currently 10 sets.)
 *
 * Parameters: None.
 * Return value: A handle to be passed to other tcvideo functions, or 0 on
 *               error.
 * Preconditions: None.
 * Postconditions: None.
 */

TCVHandle tcv_init(void)
{
    TCVHandle handle;

    handle = tc_zalloc(sizeof(*handle));
    if (handle) {
        handle->saved_weight = handle->saved_bias = -1.0;
    }
    return handle;
}

/*************************************************************************/

/**
 * tcv_free:  Free resources allocated for the given handle.  Does nothing
 * if handle is zero.
 *
 * Parameters: handle: tcvideo handle.
 * Return value: None.
 * Preconditions: handle != 0: handle was returned by tcv_init()
 * Postconditions: None.
 */

void tcv_free(TCVHandle handle)
{
    if (handle) {
        int i;
        for (i = 0; i < ZOOMINFO_CACHE_SIZE; i++) {
            if (handle->zoominfo_cache[i].zi)
                zoom_free(handle->zoominfo_cache[i].zi);
        }
        free(handle->convert_buffer);
        free(handle);
    }
}

/*************************************************************************/

/**
 * tcv_clip:  Clip the given image by removing the specified number of
 * pixels from each edge.  If a clip value is negative, instead expands the
 * frame by inserting the given number of black pixels (the value to be
 * inserted is given by the `black_pixel' parameter).  Conceptually,
 * expansion is done before clipping, so that if, for example,
 *     width == 640
 *     clip_left == 642
 *     clip_right == -4
 * then the result is a two-pixel-wide black frame (this is not considered
 * an error).
 *
 * Parameters:      handle: tcvideo handle.
 *                     src: Source data plane.
 *                    dest: Destination data plane.
 *                   width: Width of frame.
 *                  height: Height of frame.
 *                     Bpp: Bytes (not bits!) per pixel.
 *               clip_left: Number of pixels to clip from left edge.
 *              clip_right: Number of pixels to clip from right edge.
 *                clip_top: Number of pixels to clip from top edge.
 *             clip_bottom: Number of pixels to clip from bottom edge.
 *             black_pixel: Value to be filled into expanded areas.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tcv_init()
 *                src != NULL: src[0]..src[width*height*Bpp-1] are readable
 *                dest != NULL:
 *                    destw = width - clip_left - clip_right;
 *                    desth = height - clip_top - clip_bottom;
 *                    dest[0]..dest[destw*desth*Bpp-1] are writable
 *                src != dest: src and dest do not overlap
 * Postconditions: (on success) dest[0]..dest[destw*desth*Bpp-1] are set
 */

int tcv_clip(TCVHandle handle,
             uint8_t *src, uint8_t *dest, int width, int height, int Bpp,
             int clip_left, int clip_right, int clip_top, int clip_bottom,
             uint8_t black_pixel)
{
    int new_w, copy_w, copy_h, y;


    if (!src || !dest || width <= 0 || height <= 0 || (Bpp != 1 && Bpp != 3)) {
        tc_log_error("libtcvideo", "tcv_clip: invalid frame parameters!");
        return 0;
    }
    if (clip_left + clip_right >= width || clip_top + clip_bottom >= height) {
        tc_log_error("libtcvideo", "tcv_clip: clipping parameters"
                     " (%d,%d,%d,%d) invalid for frame size %dx%d",
                     clip_top, clip_left, clip_bottom, clip_right,
                     width, height);
        return 0;
    }
    /* Normalize clipping values (e.g. clip_left > width, clip_right < 0) */
    if (clip_left > width) {
        clip_right += clip_left - width;
        clip_left = width;
    }
    if (clip_right > width) {
        clip_left += clip_right - width;
        clip_right = width;
    }
    if (clip_top > height) {
        clip_bottom += clip_top - height;
        clip_top = height;
    }
    if (clip_bottom > height) {
        clip_top += clip_bottom - height;
        clip_bottom = height;
    }

    new_w = width - clip_left - clip_right;
    copy_w = width - (clip_left<0 ? 0 : clip_left)
                   - (clip_right<0 ? 0 : clip_right);
    copy_h = height - (clip_top<0 ? 0 : clip_top)
                    - (clip_bottom<0 ? 0 : clip_bottom);

    if (clip_top < 0) {
        memset(dest, black_pixel, (-clip_top) * new_w * Bpp);
        dest += (-clip_top) * new_w * Bpp;
    } else {
        src += clip_top * width * Bpp;
    }
    if (clip_left > 0)
        src += clip_left * Bpp;
    for (y = 0; y < copy_h; y++) {
        if (clip_left < 0) {
            memset(dest, black_pixel, (-clip_left) * Bpp);
            dest += (-clip_left) * Bpp;
        }
        if (copy_w > 0)
            ac_memcpy(dest, src, copy_w * Bpp);
        dest += copy_w * Bpp;
        src += width * Bpp;
        if (clip_right < 0) {
            memset(dest, black_pixel, (-clip_right) * Bpp);
            dest += (-clip_right) * Bpp;
        }
    }
    if (clip_bottom < 0) {
        memset(dest, black_pixel, (-clip_bottom) * new_w * Bpp);
    }
    return 1;
}

/*************************************************************************/

/**
 * tcv_deinterlace:  Deinterlace the given image.
 *
 * Parameters: handle: tcvideo handle.
 *                src: Source data plane.
 *               dest: Destination data plane.
 *              width: Width of frame.
 *             height: Height of frame.
 *                Bpp: Bytes (not bits!) per pixel.
 *               mode: Deinterlacing mode (TCV_DEINTERLACE_* constant).
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tcv_init()
 *                src != NULL: src[0]..src[width*height*Bpp-1] are readable
 *                dest != NULL:
 *                    mode == TCV_DEINTERLACE_DROP_FIELD_{TOP,BOTTOM}:
 *                        dest[0]..dest[width*(height/2)*Bpp-1] are writable
 *                    mode != TCV_DEINTERLACE_DROP_FIELD_{TOP,BOTTOM}:
 *                        dest[0]..dest[width*height*Bpp-1] are writable
 *                src != dest: src and dest do not overlap
 * Postconditions: (on success)
 *                     mode == TCV_DEINTERLACE_DROP_FIELD_{TOP,BOTTOM}:
 *                         dest[0]..dest[width*(height/2)*Bpp-1] are set
 *                     mode != TCV_DEINTERLACE_DROP_FIELD_{TOP,BOTTOM}:
 *                         dest[0]..dest[width*height*Bpp-1] are set
 */

static int deint_drop_field(uint8_t *src, uint8_t *dest, int width,
                            int height, int Bpp, int drop_top);
static int deint_interpolate(uint8_t *src, uint8_t *dest, int width,
                             int height, int Bpp);
static int deint_linear_blend(uint8_t *src, uint8_t *dest, int width,
                              int height, int Bpp);

int tcv_deinterlace(TCVHandle handle,
                    uint8_t *src, uint8_t *dest, int width, int height,
                    int Bpp, TCVDeinterlaceMode mode)
{
    if (!src || !dest || width <= 0 || height <= 0 || (Bpp != 1 && Bpp != 3)) {
        tc_log_error("libtcvideo", "tcv_deinterlace: invalid frame parameters!");
        return 0;
    }
    switch (mode) {
      case TCV_DEINTERLACE_DROP_FIELD_TOP:
        return deint_drop_field(src, dest, width, height, Bpp, 1);
      case TCV_DEINTERLACE_DROP_FIELD_BOTTOM:
        return deint_drop_field(src, dest, width, height, Bpp, 0);
      case TCV_DEINTERLACE_INTERPOLATE:
        return deint_interpolate(src, dest, width, height, Bpp);
      case TCV_DEINTERLACE_LINEAR_BLEND:
        return deint_linear_blend(src, dest, width, height, Bpp);
      default:
        tc_log_error("libtcvideo", "tcv_deinterlace: invalid mode %d!", mode);
        return 0;
    }
}

/**
 * deint_drop_field, deint_interpolate, deint_linear_blend:  Helper
 * functions for tcv_deinterlace() that implement the individual
 * deinterlacing methods.
 *
 * Parameters: As for tcv_deinterlace(), less `handle'.
 * Return value: As for tcv_deinterlace().
 * Side effects: (for deint_linear_blend())
 *                   src[0..width*height-1] are destroyed.
 * Preconditions: As for tcv_deinterlace(), less `handle', plus:
 *                src != NULL
 *                dest != NULL
 *                width > 0
 *                height > 0
 *                Bpp == 1 || Bpp == 3
 *                (for deint_linear_blend())
 *                    src[0..width*height-1] are writable
 * Postconditions: As for tcv_deinterlace().
 */

static int deint_drop_field(uint8_t *src, uint8_t *dest, int width,
                            int height, int Bpp, int drop_top)
{
    int Bpl = width * Bpp;
    int y;

    if (drop_top)
        src += Bpl;
    for (y = 0; y < height/2; y++)
        ac_memcpy(dest + y*Bpl, src + (y*2)*Bpl, Bpl);
    return 1;
}


static int deint_interpolate(uint8_t *src, uint8_t *dest, int width,
                             int height, int Bpp)
{
    int Bpl = width * Bpp;
    int y;

    for (y = 0; y < height; y++) {
        if (y%2 == 0) {
            ac_memcpy(dest + y*Bpl, src + y*Bpl, Bpl);
        } else if (y == height-1) {
            /* if the last line is odd, copy from the previous line */
            ac_memcpy(dest + y*Bpl, src + (y-1)*Bpl, Bpl);
        } else {
            ac_average(src + (y-1)*Bpl, src + (y+1)*Bpl, dest + y*Bpl, Bpl);
        }
    }
    return 1;
}


static int deint_linear_blend(uint8_t *src, uint8_t *dest, int width,
                              int height, int Bpp)
{
    int Bpl = width * Bpp;
    int y;

    /* First interpolate odd lines into the target buffer */
    if (!deint_interpolate(src, dest, width, height, Bpp))
        return 0;

    /* Now interpolate even lines in the source buffer; we don't use it
     * after this so it's okay to destroy it */
    ac_memcpy(src, src+Bpl, Bpl);
    for (y = 2; y < height-1; y += 2)
        ac_average(src + (y-1)*Bpl, src + (y+1)*Bpl, src + y*Bpl, Bpl);
    if (y < height)
        ac_memcpy(src + y*Bpl, src + (y-1)*Bpl, Bpl);

    /* Finally average the two frames together */
    ac_average(src, dest, dest, height*Bpl);

    return 1;
}

/*************************************************************************/

/**
 * tcv_resize:  Resize the given image using a lookup table.  `scale_w' and
 * `scale_h' are the number of blocks the image is divided into (normally
 * 8; 4 for subsampled U/V).  `resize_w' and `resize_h' are given in units
 * of `scale_w' and `scale_h' respectively.  Only one of `resize_w' and
 * `resize_h' may be nonzero.
 * N.B. doesn't work well if shrinking by more than a factor of 2 (only
 *      averages 2 adjacent lines/pixels)
 *
 * Parameters:   handle: tcvideo handle.
 *                  src: Source data plane.
 *                 dest: Destination data plane.
 *                width: Width of frame.
 *               height: Height of frame.
 *                  Bpp: Bytes (not bits!) per pixel.
 *             resize_w: Amount to add to width, in units of `scale_w'.
 *             resize_h: Amount to add to width, in units of `scale_h'.
 *              scale_w: Size in pixels of a `resize_w' unit.
 *              scale_h: Size in pixels of a `resize_h' unit.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tcv_init()
 *                src != NULL: src[0]..src[width*height*Bpp-1] are readable
 *                dest != NULL:
 *                    destw = width + resize_w*scale_w;
 *                    desth = height + resize_h*scale_h;
 *                    dest[0]..dest[destw*desth*Bpp-1] are writable
 *                src != dest: src and dest do not overlap
 * Postconditions: (on success) dest[0]..dest[destw*desth*Bpp-1] are set
 */

static inline void rescale_pixel(const uint8_t *src1, const uint8_t *src2,
                                 uint8_t *dest, int bytes,
                                 uint32_t weight1, uint32_t weight2);

int tcv_resize(TCVHandle handle,
               uint8_t *src, uint8_t *dest, int width, int height, int Bpp,
               int resize_w, int resize_h, int scale_w, int scale_h)
{
    int new_w, new_h;


    if (!src || !dest || width <= 0 || height <= 0 || (Bpp != 1 && Bpp != 3)) {
        tc_log_error("libtcvideo", "tcv_resize: invalid frame parameters!");
        return 0;
    }
    if ((scale_w != 1 && scale_w != 2 && scale_w != 4 && scale_w != 8)
     || (scale_h != 1 && scale_h != 2 && scale_h != 4 && scale_h != 8)) {
        tc_log_error("libtcvideo", "tcv_resize: invalid scale parameters!");
        return 0;
    }
    if (width % scale_w != 0 || height % scale_h != 0) {
        tc_log_error("libtcvideo", "tcv_resize: scale parameters (%d,%d)"
                     "invalid for frame size %dx%d",
                     scale_w, scale_h, width, height);
        return 0;
    }

    new_w = width + resize_w*scale_w;
    new_h = height + resize_h*scale_h;
    if (new_w <= 0 || new_h <= 0) {
        tc_log_error("libtcvideo", "tcv_resize: resizing parameters"
                     " (%d,%d,%d,%d) invalid for frame size %dx%d",
                     resize_w, resize_h, scale_w, scale_h, width, height);
        return 0;
    }

    /* Resize vertically (fast, using accelerated routine) */
    if (resize_h) {
        int Bpl = width * Bpp;  /* bytes per line */
        int i, y;

        init_resize_tables(handle, 0, 0, height*8/scale_h, new_h*8/scale_h);
        for (i = 0; i < scale_h; i++) {
            uint8_t *sptr = src  + (i * (height/scale_h)) * Bpl;
            uint8_t *dptr = dest + (i * (new_h /scale_h)) * Bpl;
            for (y = 0; y < new_h / scale_h; y++) {
                ac_rescale(sptr + (handle->resize_table_y[y].source  ) * Bpl,
                           sptr + (handle->resize_table_y[y].source+1) * Bpl,
                           dptr + y*Bpl, Bpl,
                           handle->resize_table_y[y].weight1,
                           handle->resize_table_y[y].weight2);
            }
        }
    }

    /* Resize horizontally; calling the accelerated routine for each pixel
     * has far too much overhead, so we just perform the calculations
     * directly. */
    if (resize_w) {
        int i, x;

        init_resize_tables(handle, width*8/scale_w, new_w*8/scale_w, 0, 0);
        /* Treat the image as an array of blocks */
        for (i = 0; i < new_h * scale_w; i++) {
            /* This `if' is an optimization hint to the compiler, to
             * suggest that it generate a separate version of the loop
             * code for Bpp==1 without the unnecessary multiply ops. */
            if (Bpp == 1) {  /* optimization hint */
                uint8_t *sptr = src  + (i * (width/scale_w)) * Bpp;
                uint8_t *dptr = dest + (i * (new_w/scale_w)) * Bpp;
                for (x = 0; x < new_w / scale_w; x++) {
                    rescale_pixel(sptr + (handle->resize_table_x[x].source  ) * Bpp,
                                  sptr + (handle->resize_table_x[x].source+1) * Bpp,
                                  dptr + x*Bpp, Bpp,
                                  handle->resize_table_x[x].weight1,
                                  handle->resize_table_x[x].weight2);
                }
            } else {  /* exactly the same thing */
                uint8_t *sptr = src  + (i * (width/scale_w)) * Bpp;
                uint8_t *dptr = dest + (i * (new_w/scale_w)) * Bpp;
                for (x = 0; x < new_w / scale_w; x++) {
                    rescale_pixel(sptr + (handle->resize_table_x[x].source  ) * Bpp,
                                  sptr + (handle->resize_table_x[x].source+1) * Bpp,
                                  dptr + x*Bpp, Bpp,
                                  handle->resize_table_x[x].weight1,
                                  handle->resize_table_x[x].weight2);
                }
            }
        }
    }

    return 1;
}

static inline void rescale_pixel(const uint8_t *src1, const uint8_t *src2,
                                 uint8_t *dest, int bytes,
                                 uint32_t weight1, uint32_t weight2)
{
    int byte;
    for (byte = 0; byte < bytes; byte++) {
        /* Watch out for trying to access beyond the end of the frame on
         * the last pixel */
        if (weight1 < 0x10000)  /* this is the more likely case */
            dest[byte] = (src1[byte]*weight1 + src2[byte]*weight2 + 32768)
                         >> 16;
        else
            dest[byte] = src1[byte];
    }
}

/*************************************************************************/

/**
 * tcv_zoom:  Resize the given image to an arbitrary size, with filtering.
 *
 * Parameters: handle: tcvideo handle.
 *                src: Source data plane.
 *               dest: Destination data plane.
 *              width: Width of frame.
 *             height: Height of frame.
 *                Bpp: Bytes (not bits!) per pixel.
 *              new_w: New frame width.
 *              new_h: New frame height.  If negative, the frame is
 *                     processed in an interlaced mode, zooming each
 *                     field separately to a total height of -new_h.
 *                     Both `height' and `new_h' must be even.
 *             filter: Filter type (TCV_ZOOM_*).
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tcv_init()
 *                src != NULL: src[0]..src[width*height*Bpp-1] are readable
 *                dest != NULL: dest[0]..dest[new_w*new_h*Bpp-1] are writable
 *                src != dest: src and dest do not overlap
 * Postconditions: (on success) dest[0]..dest[new_w*new_h*Bpp-1] are set
 */

int tcv_zoom(TCVHandle handle,
             uint8_t *src, uint8_t *dest, int width, int height, int Bpp,
             int new_w, int new_h, TCVZoomFilter filter)
{
    ZoomInfo *zi;
    int free_zi = 0;  // Should the ZoomInfo be freed after use?
    int interlace_mode = 0;
    int i;

    if (!src || !dest || width <= 0 || height <= 0 || (Bpp != 1 && Bpp != 3)) {
        tc_log_error("libtcvideo", "tcv_zoom: invalid frame parameters!");
        return 0;
    }
    if (new_h < 0) {
        new_h = -new_h;
        interlace_mode = 1;
        if (height % 2 != 0 || new_h % 2 != 0) {
            tc_log_error("libtcvideo", "tcv_zoom: heights must be even in"
                         " interlace mode (old height %d, new height %d)",
                         height, new_h);
            return 0;
        }
    }
    if (new_w <= 0 || new_h <= 0) {
        tc_log_error("libtcvideo", "tcv_zoom: invalid target size %dx%d!",
                     new_w, new_h);
        return 0;
    }
    switch (filter) {
      case TCV_ZOOM_BOX:
      case TCV_ZOOM_TRIANGLE:
      case TCV_ZOOM_HERMITE:
      case TCV_ZOOM_BELL:
      case TCV_ZOOM_B_SPLINE:
      case TCV_ZOOM_MITCHELL:
      case TCV_ZOOM_LANCZOS3:
        break;
      default:
        tc_log_error("libtcvideo", "tcv_zoom: invalid filter %d!", filter);
        return 0;
    }

    for (i = 0, zi = NULL; i < ZOOMINFO_CACHE_SIZE && zi == NULL; i++) {
        if (handle->zoominfo_cache[i].zi     != NULL
         && handle->zoominfo_cache[i].old_w  == width
         && handle->zoominfo_cache[i].old_h  == height
         && handle->zoominfo_cache[i].new_w  == new_w
         && handle->zoominfo_cache[i].new_h  == new_h
         && handle->zoominfo_cache[i].Bpp    == Bpp
         && handle->zoominfo_cache[i].ilace  == interlace_mode
         && handle->zoominfo_cache[i].filter == filter
        ) {
            zi = handle->zoominfo_cache[i].zi;
        }
    }
    if (!zi) {
        int ilace_height = height;
        int ilace_new_h = new_h;
        int old_stride = width * Bpp;
        int new_stride = new_w * Bpp;
        if (interlace_mode) {
            ilace_height /= 2;
            ilace_new_h /= 2;
            old_stride *= 2;
            new_stride *= 2;
        }
        zi = zoom_init(width, ilace_height, new_w, ilace_new_h, Bpp,
                       old_stride, new_stride, filter);
        if (!zi) {
            tc_log_error("libtcvideo", "tcv_zoom: zoom_init() failed!");
            return 0;
        }
        free_zi = 1;
        for (i = 0; i < ZOOMINFO_CACHE_SIZE; i++) {
            if (!handle->zoominfo_cache[i].zi) {
                handle->zoominfo_cache[i].zi     = zi;
                handle->zoominfo_cache[i].old_w  = width;
                handle->zoominfo_cache[i].old_h  = height;
                handle->zoominfo_cache[i].new_w  = new_w;
                handle->zoominfo_cache[i].new_h  = new_h;
                handle->zoominfo_cache[i].Bpp    = Bpp;
                handle->zoominfo_cache[i].ilace  = interlace_mode;
                handle->zoominfo_cache[i].filter = filter;
                free_zi = 0;
                break;
            }
        }
    }
    zoom_process(zi, src, dest);
    if (interlace_mode)
        zoom_process(zi, src + width*Bpp, dest + new_w*Bpp);
    if (free_zi)
        zoom_free(zi);
    return 1;
}

/*************************************************************************/

/**
 * tcv_reduce:  Efficiently reduce the image size by a specified integral
 * amount, by removing intervening pixels.
 *
 * Parameters:   handle: tcvideo handle.
 *                  src: Source data plane.
 *                 dest: Destination data plane.
 *                width: Width of frame.
 *               height: Height of frame.
 *                  Bpp: Bytes (not bits!) per pixel.
 *                new_w: New frame width.
 *                new_h: New frame height.
 *             reduce_w: Ratio to reduce width by.
 *             reduce_h: Ratio to reduce height by.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tcv_init()
 *                src != NULL: src[0]..src[width*height*Bpp-1] are readable
 *                dest != NULL && reduce_w > 0 && reduce_h > 0:
 *                    destw = width / reduce_w;
 *                    desth = height / reduce_h;
 *                    dest[0]..dest[destw*desth*Bpp-1] are writable
 *                src != dest: src and dest do not overlap
 * Postconditions: (on success) dest[0]..dest[destw*desth*Bpp-1] are set
 */

int tcv_reduce(TCVHandle handle,
               uint8_t *src, uint8_t *dest, int width, int height, int Bpp,
               int reduce_w, int reduce_h)
{
    if (!src || !dest || width <= 0 || height <= 0 || (Bpp != 1 && Bpp != 3)) {
        tc_log_error("libtcvideo", "tcv_reduce: invalid frame parameters!");
        return 0;
    }
    if (reduce_w <= 0 || reduce_h <= 0) {
        tc_log_error("libtcvideo", "tcv_reduce: invalid reduction parameters"
                     " (%d,%d)!", reduce_w, reduce_h);
        return 0;
    }

    if (reduce_w != 1) {
        /* Standard case: width and (possibly) height are being reduced */
        int x, y, i;
        int xstep = Bpp * reduce_w;
        for (y = 0; y < height / reduce_h; y++) {
            for (x = 0; x < width / reduce_w; x++) {
                for (i = 0; i < Bpp; i++)
                    *dest++ = src[x*xstep+i];
            }
            src += width*Bpp * reduce_h;
        }
    } else if (reduce_h != 1) {
        /* Optimized case 1: only height is being reduced */
        int y;
        int Bpl = width * Bpp;
        for (y = 0; y < height / reduce_h; y++)
            ac_memcpy(dest + y*Bpl, src + y*(Bpl*reduce_h), Bpl);
    } else {
        /* Optimized case 2: no reduction, direct copy */
        ac_memcpy(dest, src, width*height*Bpp);
    }

    return 1;
}

/*************************************************************************/

/**
 * tcv_flip_v:  Flip the given image vertically.
 *
 * Parameters: handle: tcvideo handle.
 *                src: Source data plane.
 *               dest: Destination data plane.
 *              width: Width of frame.
 *             height: Height of frame.
 *                Bpp: Bytes (not bits!) per pixel.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tcv_init()
 *                src != NULL: src[0]..src[width*height*Bpp-1] are readable
 *                dest != NULL: dest[0]..dest[width*height*Bpp-1] are writable
 *                src != dest: src and dest do not overlap
 * Postconditions: (on success) dest[0]..dest[width*height*Bpp-1] are set
 */

int tcv_flip_v(TCVHandle handle,
               uint8_t *src, uint8_t *dest, int width, int height, int Bpp)
{
    int Bpl = width * Bpp;  /* bytes per line */
    int y;
    uint8_t buf[TC_MAX_V_FRAME_WIDTH * TC_MAX_V_BYTESPP];

    if (!src || !dest || width <= 0 || height <= 0 || (Bpp != 1 && Bpp != 3)) {
        tc_log_error("libtcvideo", "tcv_flip_v: invalid frame parameters!");
        return 0;
    }

    /* Note that GCC4 can optimize this perfectly; no need for extra
     * pointer variables */
    if (src != dest) {
        for (y = 0; y < height; y++) {
            ac_memcpy(dest + ((height-1)-y)*Bpl, src + y*Bpl, Bpl);
        }
    } else {
        for (y = 0; y < (height+1)/2; y++) {
            ac_memcpy(buf, src + y*Bpl, Bpl);
            ac_memcpy(dest + y*Bpl, src + ((height-1)-y)*Bpl, Bpl);
            ac_memcpy(dest + ((height-1)-y)*Bpl, buf, Bpl);
        }
    }

    return 1;
}

/*************************************************************************/

/**
 * tcv_flip_h:  Flip the given image horizontally.
 *
 * Parameters: handle: tcvideo handle.
 *                src: Source data plane.
 *               dest: Destination data plane.
 *              width: Width of frame.
 *             height: Height of frame.
 *                Bpp: Bytes (not bits!) per pixel.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tcv_init()
 *                src != NULL: src[0]..src[width*height*Bpp-1] are readable
 *                dest != NULL: dest[0]..dest[width*height*Bpp-1] are writable
 *                src != dest: src and dest do not overlap
 * Postconditions: (on success) dest[0]..dest[width*height*Bpp-1] are set
 */

int tcv_flip_h(TCVHandle handle,
               uint8_t *src, uint8_t *dest, int width, int height, int Bpp)
{
    int x, y, i;

    if (!src || !dest || width <= 0 || height <= 0 || (Bpp != 1 && Bpp != 3)) {
        tc_log_error("libtcvideo", "tcv_flip_h: invalid frame parameters!");
        return 0;
    }

    for (y = 0; y < height; y++) {
        uint8_t *srcline = src + y*width*Bpp;
        uint8_t *destline = dest + y*width*Bpp;
        if (src != dest) {
            for (x = 0; x < width; x++) {
                for (i = 0; i < Bpp; i++) {
                    destline[((width-1)-x)*Bpp+i] = srcline[x*Bpp+i];
                }
            }
        } else {
            for (x = 0; x < (width+1)/2; x++) {
                for (i = 0; i < Bpp; i++) {
                    uint8_t tmp = srcline[x*Bpp+i];
                    destline[x*Bpp+i] = srcline[((width-1)-x)*Bpp+i];
                    destline[((width-1)-x)*Bpp+i] = tmp;
                }
            }
        }
    }

    return 1;
}

/*************************************************************************/

/**
 * tcv_gamma_correct:  Perform gamma correction on the given image.
 *
 * Parameters: handle: tcvideo handle.
 *                src: Source data plane.
 *               dest: Destination data plane.
 *              width: Width of frame.
 *             height: Height of frame.
 *                Bpp: Bytes (not bits!) per pixel.
 *              gamma: Gamma value.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tcv_init()
 *                src != NULL: src[0]..src[width*height*Bpp-1] are readable
 *                dest != NULL: dest[0]..dest[width*height*Bpp-1] are writable
 *                src != dest: src and dest do not overlap
 * Postconditions: (on success) dest[0]..dest[width*height*Bpp-1] are set
 */

int tcv_gamma_correct(TCVHandle handle,
                      uint8_t *src, uint8_t *dest, int width, int height,
                      int Bpp, double gamma)
{
    int i;

    if (!src || !dest || width <= 0 || height <= 0 || (Bpp != 1 && Bpp != 3)) {
        tc_log_error("libtcvideo", "tcv_gamma: invalid frame parameters!");
        return 0;
    }
    if (gamma <= 0) {
        tc_log_error("libtcvideo", "tcv_gamma: invalid gamma (%.3f)!", gamma);
        return 0;
    }

    init_gamma_table(handle, gamma);
    for (i = 0; i < width*height*Bpp; i++)
        dest[i] = handle->gamma_table[src[i]];

    return 1;
}

/*************************************************************************/

/**
 * tcv_antialias:  Perform antialiasing on the given image.
 *
 * Parameters: handle: tcvideo handle.
 *                src: Source data plane.
 *               dest: Destination data plane.
 *              width: Width of frame.
 *             height: Height of frame.
 *                Bpp: Bytes (not bits!) per pixel.
 *             weight: `weight' antialiasing parameter.
 *               bias: `bias' antialiasing parameter.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tcv_init()
 *                src != NULL: src[0]..src[width*height*Bpp-1] are readable
 *                dest != NULL: dest[0]..dest[width*height*Bpp-1] are writable
 *                src != dest: src and dest do not overlap
 * Postconditions: (on success) dest[0]..dest[width*height*Bpp-1] are set
 */

static void antialias_line(TCVHandle handle,
                           uint8_t *src, uint8_t *dest, int width, int Bpp);

int tcv_antialias(TCVHandle handle,
                  uint8_t *src, uint8_t *dest, int width, int height,
                  int Bpp, double weight, double bias)
{
    int y;

    if (!src || !dest || width <= 0 || height <= 0 || (Bpp != 1 && Bpp != 3)) {
        tc_log_error("libtcvideo", "tcv_antialias: invalid frame parameters!");
        return 0;
    }
    if (weight < 0 || weight > 1 || bias < 0 || bias > 1) {
        tc_log_error("libtcvideo", "tcv_antialias: invalid antialiasing"
                     " parameters (weight=%.3f, bias=%.3f)", weight, bias);
        return 0;
    }

    init_aa_table(handle, weight, bias);
    ac_memcpy(dest, src, width*Bpp);
    for (y = 1; y < height-1; y++) {
        antialias_line(handle, src + y*width*Bpp, dest + y*width*Bpp, width,
                       Bpp);
    }
    ac_memcpy(dest + (height-1)*width*Bpp, src + (height-1)*width*Bpp,
              width*Bpp);

    return 1;
}


/* Helper functions: */

static inline int samecolor(uint8_t *pixel1, uint8_t *pixel2, int Bpp)
{
    int i;
    int maxdiff = abs(pixel2[0]-pixel1[0]);
    for (i = 1; i < Bpp; i++) {
        int diff = abs(pixel2[i]-pixel1[i]);
        if (diff > maxdiff)
            maxdiff = diff;
    }
    return maxdiff < AA_DIFFERENT;
}

#define C (src + x*Bpp)
#define U (C - width*Bpp)
#define D (C + width*Bpp)
#define L (C - Bpp)
#define R (C + Bpp)
#define UL (U - Bpp)
#define UR (U + Bpp)
#define DL (D - Bpp)
#define DR (D + Bpp)
#define SAME(pix1,pix2) samecolor((pix1),(pix2),Bpp)
#define DIFF(pix1,pix2) !samecolor((pix1),(pix2),Bpp)

static void antialias_line(TCVHandle handle,
                           uint8_t *src, uint8_t *dest, int width, int Bpp)
{
    int i, x;

    for (i = 0; i < Bpp; i++)
        dest[i] = src[i];
    for (x = 1; x < width-1; x++) {
        if ((SAME(L,U) && DIFF(L,D) && DIFF(L,R))
         || (SAME(L,D) && DIFF(L,U) && DIFF(L,R))
         || (SAME(R,U) && DIFF(R,D) && DIFF(R,L))
         || (SAME(R,D) && DIFF(R,U) && DIFF(R,L))
        ) {
            for (i = 0; i < Bpp; i++) {
                uint32_t tmp = handle->aa_table_d[UL[i]]
                             + handle->aa_table_y[U [i]]
                             + handle->aa_table_d[UR[i]]
                             + handle->aa_table_x[L [i]]
                             + handle->aa_table_c[C [i]]
                             + handle->aa_table_x[R [i]]
                             + handle->aa_table_d[DL[i]]
                             + handle->aa_table_y[D [i]]
                             + handle->aa_table_d[DR[i]]
                             + 32768;
                /*
                dest[x*Bpp+i] = (verbose & TC_DEBUG) ? 255 : tmp>>16;
                                ^^^^^^^^^^^^^^^^^^^^
                FIXME: I don't get this -- FR
                */
                dest[x*Bpp+i] = tmp>>16; // to make it compile (see above)
            }
        } else {
            for (i = 0; i < Bpp; i++)
                dest[x*Bpp+i] = src[x*Bpp+i];
        }
    }
    for (i = 0; i < Bpp; i++)
        dest[(width-1)*Bpp+i] = src[(width-1)*Bpp+i];
}

/*************************************************************************/

/**
 * tcv_convert:  Convert an image from one image format to another.  The
 * source and destination image pointers can be the same, causing the image
 * to be converted in place (in this case a temporary buffer will be
 * allocated as necessary to perform the conversion).
 *
 * Parameters:  handle: tcvideo handle.
 *                 src: Image to convert.
 *                dest: Buffer for converted image.
 *               width: Width of image.
 *              height: Height of image.
 *              srcfmt: Format of image.
 *             destfmt: Format to convert image to.
 * Return value: Nonzero on success, zero on error (invalid parameters).
 * Preconditions: handle != 0: handle was returned by tcv_init()
 * Postconditions: None.
 */

int tcv_convert(TCVHandle handle, uint8_t *src, uint8_t *dest, int width,
                int height, ImageFormat srcfmt, ImageFormat destfmt)
{
    uint8_t *realdest;  // either dest or the temporary buffer
    uint8_t *srcplanes[3], *destplanes[3];
    uint32_t size;

    if (!handle) {
        tc_log_error("libtcvideo", "tcv_convert(): No handle given!");
        return 0;
    }
    if (!src || !dest || width <= 0 || height <= 0 || !srcfmt || !destfmt) {
        tc_log_error("libtcvideo", "tcv_convert(): Invalid image parameters!");
        return 0;
    }

    switch (destfmt) {
        case IMG_YUV420P:
        case IMG_YV12   : size = width*height + (width/2)*(height/2)*2; break;
        case IMG_YUV411P: size = width*height + (width/4)*height*2; break;
        case IMG_YUV422P: size = width*height + (width/2)*height*2; break;
        case IMG_YUV444P: size = width*height*3; break;
        case IMG_YUY2   :
        case IMG_UYVY   :
        case IMG_YVYU   : size = (width*2)*height; break;
        case IMG_Y8     :
        case IMG_GRAY8  : size = width*height; break;
        case IMG_RGB24  :
        case IMG_BGR24  : size = (width*3)*height; break;
        case IMG_RGBA32 :
        case IMG_ABGR32 :
        case IMG_ARGB32 :
        case IMG_BGRA32 : size = (width*4)*height; break;
        default         : return 0;
    }

    if (srcfmt == destfmt) {
        /* Formats are the same, just copy (if needed) and return */
        if (src != dest)
            ac_memcpy(dest, src, size);
        return 1;
    }

    if (src == dest) {
        /* In-place conversion, so allocate a properly-sized buffer */
        if (!handle->convert_buffer || handle->convert_buffer_size < size) {
            free(handle->convert_buffer);
            handle->convert_buffer = tc_malloc(size);
            if (!handle->convert_buffer)
                return 0;
            handle->convert_buffer_size = size;
        }
        realdest = handle->convert_buffer;
    } else {
        realdest = dest;
    }

    YUV_INIT_PLANES(srcplanes, src, srcfmt, width, height);
    YUV_INIT_PLANES(destplanes, realdest, destfmt, width, height);
    if (!ac_imgconvert(srcplanes, srcfmt, destplanes, destfmt, width, height))
        return 0;

    if (src == dest)
        ac_memcpy(src, handle->convert_buffer, size);

    return 1;
}

/*************************************************************************/
/*************************************************************************/

/* Internal-use helper functions. */

/*************************************************************************/

/**
 * init_resize_tables:  Initialize the lookup tables used for resizing.  If
 * either of `oldw' and `neww' is nonpositive, the horizontal resizing
 * table will not be initialized; likewise for `oldh', `newh', and the
 * vertical resizing table.  Initialization will also not be performed if
 * the values given are the same as in the previous call (thus repeated
 * calls with the same values suffer only the penalty of entering and
 * exiting the procedure).  Note the order of parameters!
 *
 * Parameters: handle: tcvideo handle.
 *               oldw: Original image width.
 *               neww: New image width.
 *               oldh: Original image height.
 *               newh: New image height.
 * Return value: None.
 * Preconditions: handle != 0
 *                oldw % 8 == 0
 *                neww % 8 == 0
 *                oldh % 8 == 0
 *                newh % 8 == 0
 * Postconditions: If oldw > 0 && neww > 0:
 *                     resize_table_x[0..neww/8-1] are initialized
 *                 If oldh > 0 && newh > 0:
 *                     resize_table_y[0..newh/8-1] are initialized
 */

static void init_resize_tables(TCVHandle handle,
                               int oldw, int neww, int oldh, int newh)
{
    if (oldw > 0 && neww > 0
     && (oldw != handle->saved_oldw || neww != handle->saved_neww)
    ) {
        init_one_resize_table(handle->resize_table_x, oldw, neww);
        handle->saved_oldw = oldw;
        handle->saved_neww = neww;
    }
    if (oldh > 0 && newh > 0
     && (oldh != handle->saved_oldh || newh != handle->saved_newh)
    ) {
        init_one_resize_table(handle->resize_table_y, oldh, newh);
        handle->saved_oldh = oldh;
        handle->saved_newh = newh;
    }
}


/**
 * init_one_resize_table:  Helper function for init_resize_tables() to
 * initialize a single table.
 *
 * Parameters:   table: Table to initialize.
 *             oldsize: Size to resize from.
 *             newsize: Size to resize to.
 * Return value: None.
 * Preconditions: table != NULL
 *                oldsize > 0
 *                oldsize % 8 == 0
 *                newsize > 0
 *                newsize % 8 == 0
 * Postconditions: table[0..newsize/8-1] are initialized
 */

static void init_one_resize_table(struct resize_table_elem *table,
                                  int oldsize, int newsize)
{
    int i;

    /* Compute the number of source pixels per destination pixel */
    double width_ratio = (double)oldsize / (double)newsize;

    for (i = 0; i < newsize/8; i++) {
        double oldpos;

        /* Left/topmost source pixel to use */
        oldpos = (double)i * (double)oldsize / (double)newsize;
        table[i].source = (int)oldpos;

        /* Is the new pixel contained entirely within the old? */
        if (oldpos+width_ratio < table[i].source+1) {
            /* Yes, weight ratio is 1.0:0.0 */
            table[i].weight1 = 65536;
            table[i].weight2 = 0;
        } else {
            /* No, compute appropriate weight ratio */
            double temp = ((table[i].source+1) - oldpos) / width_ratio * PI/2;
            table[i].weight1 = (uint32_t)(sin(temp)*sin(temp) * 65536 + 0.5);
            table[i].weight2 = 65536 - table[i].weight1;
        }
    }
}

/*************************************************************************/

/**
 * init_gamma_table:  Initialize the gamma correction lookup table.
 * Initialization will not be performed for repeated calls with the same
 * value.
 *
 * Parameters: handle: tcvideo handle.
 *              gamma: Gamma value.
 * Return value: None.
 * Preconditions: handle != 0
 *                gamma > 0
 * Postconditions: gamma_table[0..255] are initialized
 */

static void init_gamma_table(TCVHandle handle, double gamma)
{
    if (gamma != handle->saved_gamma) {
        int i;
        for (i = 0; i < 256; i++)
            handle->gamma_table[i] = (uint8_t) (pow((i/255.0),gamma) * 255);
        handle->saved_gamma = gamma;
    }
}

/*************************************************************************/

/**
 * init_handle->aa_table:  Initialize the antialiasing lookup tables.
 * Initialization will not be performed for repeated calls with the same
 * values.
 *
 * Parameters:    handle: tcvideo handle.
 *             aa_weight: Antialiasing weight value.
 *               aa_bias: Antialiasing bias value.
 * Return value: None.
 * Preconditions: handle != 0
 *                0 <= aa_weight && aa_weight <= 1
 *                0 <= aa_bias && aa_bias <= 1
 * Postconditions: gamma_table[0..255] are initialized
 */

static void init_aa_table(TCVHandle handle, double aa_weight, double aa_bias)
{
    if (aa_weight != handle->saved_weight || aa_bias != handle->saved_bias) {
        int i;
        for (i = 0; i < 256; ++i) {
            handle->aa_table_c[i] = i*aa_weight * 65536;
            handle->aa_table_x[i] = i*aa_bias*(1-aa_weight)/4 * 65536;
            handle->aa_table_y[i] = i*(1-aa_bias)*(1-aa_weight)/4 * 65536;
            handle->aa_table_d[i] =
                (handle->aa_table_x[i]+handle->aa_table_y[i]+1)/2;
        }
        handle->saved_weight = aa_weight;
        handle->saved_bias = aa_bias;
    }
}

/*************************************************************************/
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
