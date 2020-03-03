/*
 *  tc_magick.h -- transcode GraphicsMagick utilities.
 *  (C) 2009-2010 Francesco Romani <fromani at gmail dot com>
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

#ifndef TC_MAGICK_H
#define TC_MAGICK_H

#include "libtc/tcframes.h"

#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <magick/api.h>

/*
 * Summary:
 * This code only wraps the commonly used functions and the routine task
 * needed by code using GraphicsMagick. Most functions are intentinally
 * not wrapped, since isn't worth to wrap functions used just in a single
 * place. For that reasin, the TCMagickContext structure below is NOT
 * opaque.
 */

typedef struct tcmagickcontext_ TCMagickContext;
struct tcmagickcontext_ {
    ExceptionInfo exception_info;
    Image         *image;
    ImageInfo     *image_info;
    PixelPacket   *pixel_packet;
};

#define TC_MAGICK_QUALITY_DEFAULT		(-1)

#define TC_MAGICK_GET_WIDTH(MCP)	((MCP)->image->columns)
#define TC_MAGICK_GET_HEIGHT(MCP) 	((MCP)->image->rows)

/*
 * tc_magick_init (thread safe):
 *     intializes the transcode GraphicsMagick module.
 *     This is translated into an underlying GraphicsMagick initialization
 *     just once, the first time this function is called.
 *     Always intializes the given local GraphicsMagick context.
 *
 * Parameters:
 *         ctx: pointer to a GraphicsMagick context to initialize.
 *     quality: quality level to apply (0-100 range). Meaningful only
 *              for subsequent tc_magick_frameout function calls.
 * Return Value:
 *        TC_OK: on success.
 *     TC_ERROR: otherwise. The error reason will be tc_log()'d out.
 */
int tc_magick_init(TCMagickContext *ctx, int quality);
/*
 * tc_magick_fini (thread safe):
 *     finalizes the transcode GraphicsMagick module.
 *     This is translated into an underlying GraphicsMagick finalization
 *     just once, the last time this function is called.
 *     Always finalizes the given local GraphicsMagick context.
 *
 * Parameters:
 *         ctx: pointer to a GraphicsMagick context to initialize.
 * Return Value:
 *        TC_OK: on success.
 *     TC_ERROR: otherwise. The error reason will be tc_log()'d out.
 */
int tc_magick_fini(TCMagickContext *ctx);

/*
 * tc_magick_filein:
 *    load and decode into a raw frame a given file containing any image
 *    format recognized by GraphicsMagick.
 *
 * Parameters:
 *         ctx: pointer to a GraphicsMagick context to use.
 *    filename: path of the image to load.
 * Return Value:
 *        TC_OK: on success.
 *     TC_ERROR: otherwise. The error reason will be tc_log()'d out.
 * Preconditions:
 *     `ctx' already succesfully initialized.
 */
int tc_magick_filein(TCMagickContext *ctx, const char *filename);

/*
 * tc_magick_RGBin:
 *    load an already decoded image as raw frame.
 *
 * Parameters:
 *         ctx: pointer to a GraphicsMagick context to use.
 *       width: width of the image to load.
 *      height: height of the image to load.
 *        data: pointer to the raw image to load.
 * Return Value:
 *        TC_OK: on success.
 *     TC_ERROR: otherwise. The error reason will be tc_log()'d out.
 * Preconditions:
 *     `ctx' already succesfully initialized.
 */
int tc_magick_RGBin(TCMagickContext *ctx,
                    int width, int height, const uint8_t *data);
/*
 * tc_magick_RGBout:
 *    decode and emit an image as a raw frame data.
 *
 * Parameters:
 *         ctx: pointer to a GraphicsMagick context to use.
 *       width: width of the image to emit.
 *      height: height of the image to emit.
 *        data: pointer to a memory area to be filled with the raw image.
 * Return Value:
 *        TC_OK: on success.
 *     TC_ERROR: otherwise. The error reason will be tc_log()'d out.
 * Preconditions:
 *     `ctx' already succesfully initialized.
 */
int tc_magick_RGBout(TCMagickContext *ctx, 
                     int width, int height, uint8_t *data);

/*
 * tc_magick_RGBout:
 *    encode and emit an image as frame data.
 *
 * Parameters:
 *         ctx: pointer to a GraphicsMagick context to use.
 *      format: a string representing any image format recognized by
 *              GraphicsMagick (passed in verbatim).
 *       frame: pointer to a TCFrameVideo to be filled with encoded image.
 * Return Value:
 *        TC_OK: on success.
 *     TC_ERROR: otherwise. The error reason will be tc_log()'d out.
 * Preconditions:
 *     `ctx' already succesfully initialized.
 */
int tc_magick_frameout(TCMagickContext *ctx, const char *format,
                       TCFrameVideo *frame);


#endif /* TC_MAGICK_H */

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

