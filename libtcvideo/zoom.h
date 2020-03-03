/*
 * zoom.h - include file for zoom function (internal to libtcvideo)
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef LIBTCVIDEO_ZOOM_H
#define LIBTCVIDEO_ZOOM_H

/*************************************************************************/

/* Internal data used by zoom_process(). (opaque to caller) */
typedef struct zoominfo ZoomInfo;

/* Create a ZoomInfo structure for the given parameters. */
ZoomInfo *zoom_init(int old_w, int old_h, int new_w, int new_h, int Bpp,
                    int old_stride, int new_stride, TCVZoomFilter filter);

/* The resizing function itself. */
void zoom_process(const ZoomInfo *zi, const uint8_t *src, uint8_t *dest);

/* Free a ZoomInfo structure. */
void zoom_free(ZoomInfo *zi);

/*************************************************************************/

#endif  /* LIBTCVIDEO_ZOOM_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
