/*
 * tcvideo.h - include file for video processing library for transcode
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef LIBTCVIDEO_TCVIDEO_H
#define LIBTCVIDEO_TCVIDEO_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>

#include "aclib/imgconvert.h"

/*************************************************************************/

/* Handle for calling tcvideo functions, allocated by tcv_init() and passed
 * to all other functions to hold internal state information.  Opaque to
 * the caller. */
typedef struct tcvhandle_ *TCVHandle;

/* Modes for tcv_deinterlace(): */
typedef enum {
    TCV_DEINTERLACE_DROP_FIELD_TOP,
    TCV_DEINTERLACE_DROP_FIELD_BOTTOM,
    TCV_DEINTERLACE_INTERPOLATE,
    TCV_DEINTERLACE_LINEAR_BLEND,
} TCVDeinterlaceMode;

/* Filter IDs for tcv_zoom(): */
typedef enum {
    TCV_ZOOM_DEFAULT = 0, /* alias for an existing following id */
    TCV_ZOOM_HERMITE = 1,
    TCV_ZOOM_BOX,
    TCV_ZOOM_TRIANGLE,
    TCV_ZOOM_BELL,
    TCV_ZOOM_B_SPLINE,
    TCV_ZOOM_LANCZOS3,
    TCV_ZOOM_MITCHELL,
    TCV_ZOOM_CUBIC_KEYS4,
    TCV_ZOOM_SINC8,
    TCV_ZOOM_NULL, /* this one MUST be the last one */
} TCVZoomFilter;

/*************************************************************************/

TCVHandle tcv_init(void);

void tcv_free(TCVHandle handle);

int tcv_clip(TCVHandle handle,
             uint8_t *src, uint8_t *dest, int width, int height, int Bpp,
             int clip_left, int clip_right, int clip_top, int clip_bottom,
             uint8_t black_pixel);

int tcv_deinterlace(TCVHandle handle,
                    uint8_t *src, uint8_t *dest, int width, int height,
                    int Bpp, TCVDeinterlaceMode mode);

int tcv_resize(TCVHandle handle,
               uint8_t *src, uint8_t *dest, int width, int height, int Bpp,
               int resize_w, int resize_h, int scale_w, int scale_h);

int tcv_zoom(TCVHandle handle,
             uint8_t *src, uint8_t *dest, int width, int height, int Bpp,
             int new_w, int new_h, TCVZoomFilter filter);

int tcv_reduce(TCVHandle handle,
               uint8_t *src, uint8_t *dest, int width, int height, int Bpp,
               int reduce_w, int reduce_h);

int tcv_flip_v(TCVHandle handle,
               uint8_t *src, uint8_t *dest, int width, int height, int Bpp);

int tcv_flip_h(TCVHandle handle,
               uint8_t *src, uint8_t *dest, int width, int height, int Bpp);

int tcv_gamma_correct(TCVHandle handle,
                      uint8_t *src, uint8_t *dest, int width, int height,
                      int Bpp, double gamma);

int tcv_antialias(TCVHandle handle,
                  uint8_t *src, uint8_t *dest, int width, int height,
                  int Bpp, double weight, double bias);

int tcv_convert(TCVHandle handle, uint8_t *src, uint8_t *dest, int width,
                int height, ImageFormat srcfmt, ImageFormat destfmt);

const char *tcv_zoom_filter_to_string(TCVZoomFilter filter);

TCVZoomFilter tcv_zoom_filter_from_string(const char *name);

/*************************************************************************/

#endif  /* LIBTCVIDEO_TCVIDEO_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
