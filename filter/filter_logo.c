/*
 *  filter_logo.c
 *
 *  Copyright (C) Tilmann Bitterberg - April 2002
 *  filter updates, enhancements and cleanup:
 *  Copyright (C) Sebastian Kun <seb at sarolta dot com> - March 2006
 *  NMS support:
 *  Copyright (C) Francesco Romani <fromani at gmail dot com> - 2009-2010
 *
 *  This file is part of transcode, a video stream rendering tool
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

/* TODO:
    - docs
    - REtesting
    - sequences of jpgs maybe would be nice.
   BIG FIXMEs:
   - allocation helpers (don't reinvent a square wheel over and over again
   - check for any resource leak
*/

#define MOD_NAME    "filter_logo.so"
#define MOD_VERSION "v0.11.0 (2009-03-01)"
#define MOD_CAP     "render image in videostream"
#define MOD_AUTHOR  "Tilmann Bitterberg"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE


/* Add workaround for deprecated ScaleCharToQuantum() function */
#undef ScaleCharToQuantum
#if MAGICKCORE_QUANTUM_DEPTH == 8
# define ScaleCharToQuantum(x) ((x))
#elif MAGICKCORE_QUANTUM_DEPTH == 16
# define ScaleCharToQuantum(x) ((x)*257)
#elif MAGICKCORE_QUANTUM_DEPTH == 32
# define ScaleCharToQuantum(x) ((x)*16843009)
#elif MAGICKCORE_QUANTUM_DEPTH == 64
# define ScaleCharToQuantum(x) ((x)*71777214294589695ULL)
#endif

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcvideo/tcvideo.h"
#include "libtcext/tc_magick.h"
#include "libtcmodule/tcmodule-plugin.h"


#define DEFAULT_LOGO_FILE   "logo.png"
#define MAX_UINT8_VAL       ((uint8_t)(-1))

// basic parameter

enum POS { NONE, TOP_LEFT, TOP_RIGHT, BOT_LEFT, BOT_RIGHT, CENTER };

typedef struct logoprivatedata_ LogoPrivateData;
typedef struct workitem_ WorkItem;

typedef int (*RenderLogoFn)(LogoPrivateData *pd,
                            const WorkItem *W, TCFrameVideo *frame);

struct workitem_ {
    PixelPacket *pixels;
    int         do_fade;
    float       fade_coeff;
};

struct logoprivatedata_ {
    /* public */
    int          posx;           /* X offset in video               */
    int          posy;           /* Y offset in video               */
    enum POS     pos;            /* predifined position             */
    int          flip;           /* bool if to flip image           */
    int          ignoredelay;    /* allow the user to ignore delays */
    int          rgbswap;        /* bool if swap colors             */
    int          grayout;        /* only render lume values         */
    int          hqconv;         /* do high quality rgb->yuv conv.  */
    unsigned int start, end;     /* ranges                          */
    unsigned int fadein;         /* No. of frames to fade in        */
    unsigned int fadeout;        /* No. of frames to fade out       */

    /* private */
    uint8_t     **yuv;           /* buffer for RGB->YUV conversion  */
    unsigned int nr_of_images;   /* animated: number of images      */
    unsigned int cur_seq;        /* animated: current image         */
    int          cur_delay;      /* animated: current delay         */
    int          rgb_offset;

    /* These used to be static (per-module), but are now per-instance. */
    vob_t       *vob;            /* video info from transcode       */
    TCMagickContext magick;
    Image       *images;

    /* Coefficients used for transparency calculations. Pre-generating these
     * in a lookup table provides a small speed boost.
     */
    float img_coeff_lookup[MAX_UINT8_VAL + 1];
    float vid_coeff_lookup[MAX_UINT8_VAL + 1];

    RenderLogoFn  render;
};

static const char logo_help[] = ""
"* Overview\n"
"    This filter renders an user specified image into the video.\n"
"    Any image format GraphicsMagick can read is accepted.\n"
"    Transparent images are also supported.\n"
"    Image origin is at the very top left.\n"
"\n"
"* Options\n"
"        'file' Image filename (required) [logo.png]\n"
"         'pos' Position (0-width x 0-height) [0x0]\n"
"      'posdef' Position (0=None, 1=TopL, 2=TopR, 3=BotL, 4=BotR, 5=Center) [0]\n"
"       'range' Restrict rendering to framerange (0-oo) [0-end]\n"
"        'fade' Fade image in/out (# of frames) (0-oo) [0-0]\n"
"        'flip' Mirror image (0=off, 1=on) [0]\n"
"     'rgbswap' Swap colors [0]\n"
"     'grayout' YUV only: don't write Cb and Cr, makes a nice effect [0]\n"
"      'hqconv' YUV only: do high quality rgb->yuv img conversion [0]\n"
" 'ignoredelay' Ignore delay specified in animations [0]\n";


/*************************************************************************/
/* Helpers                                                               */
/*************************************************************************/


/**
 * flogo_yuvbuf_free: Frees a set of YUV frame buffers allocated with
 *                    flogo_yuvbuf_alloc().
 * Parameters:     yuv: a pointer to a set of YUV frames
 *                 num: the number of frames to free
 * Return value:   N/A
 * Preconditions:  yuv was allocated with flogo_yuvbuf_alloc
 *                 num > 0
 * Postconditions: N/A
 */
static void flogo_yuvbuf_free(uint8_t **yuv, int num)
{
    int i;

    if (yuv) {
        for (i = 0; i < num; i++) {
            if (yuv[i] != NULL)
                tc_free(yuv[i]);
        }
        tc_free(yuv);
    }

    return;
}


/**
 * flogo_yuvbuf_alloc: Allocates a set of zeroed YUV frame buffers.
 *
 * Parameters:     size: the size of each frame
 *                 num:  the number of frames to allocate.
 * Return value:   An array of pointers to zeroed YUV buffers.
 * Preconditions:  size > 0
 *                 num > 0
 * Postconditions: The returned pointer should be freed with
 *                 flogo_yuvbuf_free.
 */
static uint8_t **flogo_yuvbuf_alloc(size_t size, int num)
{
    uint8_t **yuv = NULL;
    int i;

    yuv = tc_malloc(sizeof(uint8_t *) * num);
    if (yuv != NULL) {
        for (i = 0; i < num; i++) {
            yuv[i] = tc_zalloc(sizeof(uint8_t) * size);
            if (yuv[i] == NULL) {
                // free what's already been allocated
                flogo_yuvbuf_free(yuv, i-1);
                return NULL;
            }
        }
    }

    return yuv;
}


/**
 * flogo_convert_image: Converts a single GraphicsMagick RGB image into a format
 *                      usable by transcode.
 *
 * Parameters:     tcvhandle:  Opaque libtcvideo handle
 *                 src:        An GraphicsMagick handle (the source image)
 *                 dst:        A pointer to the output buffer
 *                 ifmt:       The output format (see aclib/imgconvert.h)
 *                 do_rgbswap: zero for no swap, nonzero to swap red and blue
 *                             pixel positions
 * Return value:   1 on success, 0 on failure
 * Preconditions:  tcvhandle != null, was returned by a call to tcv_init()
 *                 src is a valid GraphicsMagick RGB image handle
 *                 dst buffer is large enough to hold the result of the
 *                   requested conversion
 * Postconditions: dst get overwritten with the result of the conversion
 */
static int flogo_convert_image(TCVHandle    tcvhandle,
                               Image       *src,
                               uint8_t     *dst,
                               ImageFormat  ifmt,
                               int          do_rgbswap)
{
    PixelPacket *pixels;
    uint8_t *dst_ptr = dst;

    int row, col;
    int height = src->rows;
    int width  = src->columns;
    int ret;

    unsigned long r_off = 0, g_off = 1, b_off = 2;

    if (do_rgbswap) {
        r_off = 2;
        b_off = 0;
    }

    pixels = GetImagePixels(src, 0, 0, width, height);

    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            /* FIXME */
            *(dst_ptr + r_off) = (uint8_t)ScaleQuantumToChar(pixels->red);
            *(dst_ptr + g_off) = (uint8_t)ScaleQuantumToChar(pixels->green);
            *(dst_ptr + b_off) = (uint8_t)ScaleQuantumToChar(pixels->blue);

            dst_ptr += 3;
            pixels++;
        }
    }

    ret = tcv_convert(tcvhandle, dst, dst, width, height, IMG_RGB24, ifmt);
    if (ret == 0) {
        tc_log_error(MOD_NAME, "RGB->YUV conversion failed");
        return 0;
    }

    return 1;
}

static void flogo_defaults(LogoPrivateData *pd, vob_t *vob)
{
    memset(pd, 0, sizeof(*pd));

    pd->end     = (unsigned int)-1;
    pd->rgbswap = vob->rgbswap;
    pd->flip    = vob->flip;
    pd->vob     = vob;
}

static int flogo_parse_options(LogoPrivateData *pd,
                               const char *options, char *logo_file)
{
    /* default */
    strlcpy(logo_file, DEFAULT_LOGO_FILE, PATH_MAX);

    optstr_get(options, "file",     "%[^:]", logo_file);
    optstr_get(options, "posdef",   "%d",    (int *)&pd->pos);
    optstr_get(options, "pos",      "%dx%d", &pd->posx,  &pd->posy);
    optstr_get(options, "range",    "%u-%u", &pd->start, &pd->end);
    optstr_get(options, "fade",     "%u-%u", &pd->fadein, &pd->fadeout);

    if (optstr_lookup(options, "ignoredelay") != NULL)
        pd->ignoredelay = !pd->ignoredelay;
    if (optstr_lookup(options, "flip") != NULL)
        pd->flip    = !pd->flip;
    if (optstr_lookup(options, "rgbswap") != NULL)
        pd->rgbswap = !pd->rgbswap;
    if (optstr_lookup(options, "grayout") != NULL)
        pd->grayout = !pd->grayout;
    if (optstr_lookup(options, "hqconv") != NULL)
        pd->hqconv  = !pd->hqconv;

    if (verbose) {
        tc_log_info(MOD_NAME, " Logo renderer Settings:");
        tc_log_info(MOD_NAME, "         file = %s",    logo_file);
        tc_log_info(MOD_NAME, "       posdef = %d",    pd->pos);
        tc_log_info(MOD_NAME, "          pos = %dx%d", pd->posx,
                                                       pd->posy);
        tc_log_info(MOD_NAME, "        range = %u-%u", pd->start,
                                                       pd->end);
        tc_log_info(MOD_NAME, "         fade = %u-%u", pd->fadein,
                                                       pd->fadeout);
        tc_log_info(MOD_NAME, "         flip = %d",    pd->flip);
        tc_log_info(MOD_NAME, "  ignoredelay = %d",    pd->ignoredelay);
        tc_log_info(MOD_NAME, "      rgbswap = %d",    pd->rgbswap);
        tc_log_info(MOD_NAME, "      grayout = %d",    pd->grayout);
        tc_log_info(MOD_NAME, "       hqconv = %d",    pd->hqconv);
    }
    return TC_OK;
}

static int flogo_compute_position(LogoPrivateData *pd)
{
    switch (pd->pos) {
      case NONE: /* 0 */
        break;
      case TOP_LEFT:
        pd->posx = 0;
        pd->posy = pd->rgb_offset;
        break;
      case TOP_RIGHT:
        pd->posx = pd->vob->ex_v_width  - pd->magick.image->columns;
        break;
      case BOT_LEFT:
        pd->posy = pd->vob->ex_v_height - pd->magick.image->rows 
                 - pd->rgb_offset;
        break;
      case BOT_RIGHT:
        pd->posx = pd->vob->ex_v_width  - pd->magick.image->columns;
        pd->posy = pd->vob->ex_v_height - pd->magick.image->rows
                 - pd->rgb_offset;
        break;
      case CENTER:
        pd->posx = (pd->vob->ex_v_width - pd->magick.image->columns)/2;
        pd->posy = (pd->vob->ex_v_height- pd->magick.image->rows)/2;
        /* align to not cause color disruption */
        if (pd->posx & 1)
            pd->posx++;
        if (pd->posy & 1)
            pd->posy++;
        break;
    }

    if (pd->posy < 0 || pd->posx < 0
     || (pd->posx + pd->magick.image->columns) > pd->vob->ex_v_width
     || (pd->posy + pd->magick.image->rows)    > pd->vob->ex_v_height) {
        tc_log_error(MOD_NAME, "invalid position");
        return TC_ERROR;
    }
    return TC_OK;
}

static int flogo_calc_coeff(LogoPrivateData *pd)
{
    /* Set up image/video coefficient lookup tables */
    int i;
    float maxrgbval = (float)MaxRGB; // from GraphicsMagick

    for (i = 0; i <= MAX_UINT8_VAL; i++) {
        float x = (float)ScaleCharToQuantum(i);
        /* Alternatively:
         *  img_coeff = (maxrgbval - x) / maxrgbval;
         *  vid_coeff = x / maxrgbval;
         */
        pd->img_coeff_lookup[i] = 1.0 - (x / maxrgbval);
        pd->vid_coeff_lookup[i] = 1.0 - pd->img_coeff_lookup[i];
    }
    return TC_OK;
}

static void set_fade(WorkItem *W, int id, const LogoPrivateData *pd)
{
    if (id - pd->start < pd->fadein) {
        // fading-in
        W->fade_coeff = (float)(pd->start - id + pd->fadein) / (float)(pd->fadein);
        W->do_fade    = 1;
    } else if (pd->end - id < pd->fadeout) {
        // fading-out
        W->fade_coeff = (float)(id - pd->end + pd->fadeout) / (float)(pd->fadeout);
        W->do_fade    = 1;
    } else {
        /* enforce defaults */
        W->fade_coeff = 0.0;
        W->do_fade    = 0;
    }
}

static void set_delay(LogoPrivateData *pd)
{
    pd->cur_delay--;
    if (pd->cur_delay < 0 || pd->ignoredelay) {
        int seq;

        pd->cur_seq = (pd->cur_seq + 1) % pd->nr_of_images;

        pd->images = pd->magick.image;
        for (seq = 0; seq < pd->cur_seq; seq++)
            pd->images = pd->images->next;

        pd->cur_delay = pd->images->delay * pd->vob->fps/100;
    }
}

static int load_images(LogoPrivateData *pd)
{
    Image         *timg;
    Image         *nimg;

    pd->images = GetFirstImageInList(pd->magick.image);
    nimg = NewImageList();

    while (pd->images != (Image *)NULL) {
        if (pd->flip) {
            timg = FlipImage(pd->images, 
                             &pd->magick.exception_info);
            if (timg == NULL) {
                CatchException(&pd->magick.exception_info);
                return TC_ERROR;
            }
            AppendImageToList(&nimg, timg);
        }

        pd->images = GetNextImageInList(pd->images);
        pd->nr_of_images++;
    }

    // check for memleaks;
    //DestroyImageList(image);
    if (pd->flip) {
        /* DANGEROUS!!! */
        pd->magick.image = nimg;
    }

    /* for running through image sequence */
    /* DANGEROUS!!! */
    pd->images = pd->magick.image;

    return TC_OK;
}

static int sanity_check(LogoPrivateData *pd,
                        vob_t *vob, const char *logo_file)
{
    if (pd->magick.image->columns > vob->ex_v_width
     || pd->magick.image->rows    > vob->ex_v_height) {
        tc_log_error(MOD_NAME, "\"%s\" is too large", logo_file);
        return TC_ERROR;
    }

    if (vob->im_v_codec == TC_CODEC_YUV420P) {
        if ((pd->magick.image->columns & 1)
         || (pd->magick.image->rows & 1)) {
            tc_log_error(MOD_NAME, "\"%s\" has odd sizes", logo_file);
            return TC_ERROR;
        }
    }

    return TC_OK;
}

static int setup_logo_rgb(LogoPrivateData *pd, vob_t *vob)
{
    /* for RGB format is origin bottom left */
    /* for RGB, rgbswap is done in the frame routine */
    pd->rgb_offset = vob->ex_v_height - pd->magick.image->rows;
    pd->posy       = pd->rgb_offset   - pd->posy;

    return TC_OK;
}

/* convert Magick RGB image format to YUV */
/* todo: convert the magick image if it's not rgb! (e.g. cmyk) */
static int setup_logo_yuv(LogoPrivateData *pd)
{
    TCVHandle   tcvhandle;      /* handle for RGB->YUV conversion  */
    Image       *image;
    uint8_t     *yuv_hqbuf = NULL;
    /* Round up for odd-size images */
    unsigned long width  = pd->magick.image->columns;
    unsigned long height = pd->magick.image->rows;
    int i;

    tcvhandle = tcv_init();
    if (tcvhandle == NULL) {
        tc_log_error(MOD_NAME, "image conversion init failed");
        return TC_ERROR;
    }
   
    /* Allocate buffers for the YUV420P frames. pd->nr_of_images
     * will be 1 unless this is an animated GIF or MNG.
     * This buffer needs to be large enough to store a temporary
     * 24-bit RGB image (extracted from the GraphicsMagick handle).
     */
    pd->yuv = flogo_yuvbuf_alloc(width*height * 3, pd->nr_of_images);
    if (pd->yuv == NULL) {
        tc_log_error(MOD_NAME, "(%d) out of memory\n", __LINE__);
        return TC_ERROR;
    }

    if (pd->hqconv) {
        /* One temporary buffer, to hold full Y, U, and V planes. */
        yuv_hqbuf = tc_malloc(width*height * 3);
        if (yuv_hqbuf == NULL) {
            tc_log_error(MOD_NAME, "(%d) out of memory\n", __LINE__);
            return TC_ERROR;
        }
    }

    image = GetFirstImageInList(pd->magick.image);

    for (i = 0; i < pd->nr_of_images; i++) {
        if (!pd->hqconv) {
            flogo_convert_image(tcvhandle, image, pd->yuv[i],
                                IMG_YUV420P, pd->rgbswap);
        } else {
            flogo_convert_image(tcvhandle, image, yuv_hqbuf,
                                IMG_YUV444P, pd->rgbswap);
            // Copy over Y data from the 444 image
            ac_memcpy(pd->yuv[i], yuv_hqbuf, width * height);

            // Resize U plane by 1/2 in each dimension, into the
            // pd YUV buffer
            tcv_zoom(tcvhandle,
                     yuv_hqbuf + (width * height),
                     pd->yuv[i] + (width * height),
                     width, height, 1,
                     width / 2, height / 2,
                     TCV_ZOOM_LANCZOS3);
            // Do the same with the V plane
            tcv_zoom(tcvhandle,
                     yuv_hqbuf + 2*width*height,
                     pd->yuv[i] + width*height + (width/2)*(height/2),
                     width, height, 1,
                     width / 2, height / 2,
                     TCV_ZOOM_LANCZOS3);
        }
        image = GetNextImageInList(image);
    }

    tcv_free(tcvhandle);
    if (pd->hqconv)
        tc_free(yuv_hqbuf);

    return TC_OK;
}


static int render_logo_rgb(LogoPrivateData *pd,
                             const WorkItem *W, TCFrameVideo *frame)
{
    int row, col;
    unsigned long r_off = 0, g_off = 1, b_off = 2;
    float img_coeff, vid_coeff;
    /* Note: GraphicsMagick defines opacity = 0 as fully visible, and
     * opacity = MaxRGB as fully transparent.
     */
    Quantum opacity;
    uint8_t *video_buf = NULL;
    PixelPacket *pixels = GetImagePixels(pd->images, 0, 0,
                                         pd->images->columns,
                                         pd->images->rows);

    if (pd->rgbswap) {
        r_off = 2;
        b_off = 0;
    }

    for (row = 0; row < pd->magick.image->rows; row++) {
        video_buf = frame->video_buf + 3 * ((row + pd->posy) * pd->vob->ex_v_width + pd->posx);

        for (col = 0; col < pd->magick.image->columns; col++) {
            opacity = pixels->opacity;

            if (W->do_fade)
                opacity += (Quantum)((MaxRGB - opacity) * W->fade_coeff);

            if (opacity == 0) {
                *(video_buf + r_off) = ScaleQuantumToChar(pixels->red);
                *(video_buf + g_off) = ScaleQuantumToChar(pixels->green);
                *(video_buf + b_off) = ScaleQuantumToChar(pixels->blue);
            } else if (opacity < MaxRGB) {
                uint8_t opacity_byte = ScaleQuantumToChar(opacity);
                img_coeff = pd->img_coeff_lookup[opacity_byte];
                vid_coeff = pd->vid_coeff_lookup[opacity_byte];

                *(video_buf + r_off) = (uint8_t)((*(video_buf + r_off)) * vid_coeff)
                                        + (uint8_t)(ScaleQuantumToChar(pixels->red)   * img_coeff);
                *(video_buf + g_off) = (uint8_t)((*(video_buf + g_off)) * vid_coeff)
                                        + (uint8_t)(ScaleQuantumToChar(pixels->green) * img_coeff);
                *(video_buf + b_off) = (uint8_t)((*(video_buf + b_off)) * vid_coeff)
                                        + (uint8_t)(ScaleQuantumToChar(pixels->blue)  * img_coeff);
            }

            video_buf += 3;
            pixels++;
        }
    }
    return TC_OK;
}

static int render_logo_yuv(LogoPrivateData *pd,
                           const WorkItem *W, TCFrameVideo *frame)
{
    unsigned long vid_size = pd->vob->ex_v_width * pd->vob->ex_v_height;
    unsigned long img_size = pd->images->columns * pd->images->rows;
    uint8_t *img_pixel_Y, *img_pixel_U, *img_pixel_V;
    uint8_t *vid_pixel_Y, *vid_pixel_U, *vid_pixel_V;
    float img_coeff, vid_coeff;
    /* Note: GraphicsMagick defines opacity = 0 as fully visible, and
     * opacity = MaxRGB as fully transparent.
     */
    Quantum opacity;
    int row, col;
    PixelPacket *pixels = GetImagePixels(pd->images, 0, 0,
                                         pd->images->columns,
                                         pd->images->rows);


    /* FIXME: yet another independent reimplementation of
     * the YUV planes pointer assignement.
     */
    img_pixel_Y = pd->yuv[pd->cur_seq];
    img_pixel_U = img_pixel_Y + img_size;
    img_pixel_V = img_pixel_U + img_size/4;

    for (row = 0; row < pd->images->rows; row++) {
        vid_pixel_Y = frame->video_buf + (row + pd->posy)*pd->vob->ex_v_width + pd->posx;
        vid_pixel_U = frame->video_buf + vid_size + (row/2 + pd->posy/2)*(pd->vob->ex_v_width/2) + pd->posx/2;
        vid_pixel_V = vid_pixel_U + vid_size/4;
        for (col = 0; col < pd->images->columns; col++) {
            int do_UV_pixels = (pd->grayout == 0 && !(row % 2) && !(col % 2)) ? 1 : 0;
            opacity = pixels->opacity;

            if (W->do_fade)
                opacity += (Quantum)((MaxRGB - opacity) * W->fade_coeff);

            if (opacity == 0) {
                *vid_pixel_Y = *img_pixel_Y;
                if (do_UV_pixels) {
                    *vid_pixel_U = *img_pixel_U;
                    *vid_pixel_V = *img_pixel_V;
                }
            } else if (opacity < MaxRGB) {
                unsigned char opacity_byte = ScaleQuantumToChar(opacity);
                img_coeff = pd->img_coeff_lookup[opacity_byte];
                vid_coeff = pd->vid_coeff_lookup[opacity_byte];

                *vid_pixel_Y = (uint8_t)(*vid_pixel_Y * vid_coeff)
                             + (uint8_t)(*img_pixel_Y * img_coeff);

                if (do_UV_pixels) {
                    *vid_pixel_U = (uint8_t)(*vid_pixel_U * vid_coeff)
                                 + (uint8_t)(*img_pixel_U * img_coeff);
                    *vid_pixel_V = (uint8_t)(*vid_pixel_V * vid_coeff)
                                 + (uint8_t)(*img_pixel_V * img_coeff);
                }
            }

            vid_pixel_Y++;
            img_pixel_Y++;
            if (do_UV_pixels) {
                vid_pixel_U++;
                img_pixel_U++;
                vid_pixel_V++;
                img_pixel_V++;
            }
            pixels++;
        }
    }
    return TC_OK;
}


/*************************************************************************/
/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * logo_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(logo, LogoPrivateData)

/*************************************************************************/

/**
 * logo_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(logo)

/*************************************************************************/

#define RETURN_IF_NOT_OK(RET) do { \
    if ((RET) != TC_OK) { \
        return (RET); \
    } \
} while (0)

/**
 * logo_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */
static int logo_configure(TCModuleInstance *self,
                          const char *options,
                          vob_t *vob,
                          TCModuleExtraData *xdata[])
{
    char file[PATH_MAX + 1] = { '\0' }; /* input filename */
    LogoPrivateData *pd = NULL;
    int ret = TC_OK;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    flogo_defaults(pd, vob);

    ret = flogo_parse_options(pd, options, file);
    RETURN_IF_NOT_OK(ret);

    ret = tc_magick_init(&pd->magick, TC_MAGICK_QUALITY_DEFAULT);
    RETURN_IF_NOT_OK(ret);

    ret = tc_magick_filein(&pd->magick, file);
    RETURN_IF_NOT_OK(ret);

    ret = sanity_check(pd, vob, file);
    RETURN_IF_NOT_OK(ret);

    ret = load_images(pd);
    RETURN_IF_NOT_OK(ret);

    /* initial delay. real delay = 1/100 sec * delay */
    /* FIXME */
    pd->cur_delay = pd->magick.image->delay*vob->fps/100;

    if (verbose >= TC_DEBUG)
        tc_log_info(MOD_NAME, "Nr: %d Delay: %d ImageDelay %lu|",
                    pd->nr_of_images, pd->cur_delay, pd->magick.image->delay);

    if (vob->im_v_codec == TC_CODEC_YUV420P) {
        ret = setup_logo_yuv(pd);
        RETURN_IF_NOT_OK(ret);

        pd->render = render_logo_yuv;
    } else {
        ret = setup_logo_rgb(pd, vob);
        RETURN_IF_NOT_OK(ret);

        pd->render = render_logo_rgb;
    }

    ret = flogo_compute_position(pd);
    RETURN_IF_NOT_OK(ret);

    return flogo_calc_coeff(pd);
}


/*************************************************************************/

/**
 * logo_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int logo_stop(TCModuleInstance *self)
{
    LogoPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    tc_magick_fini(&pd->magick);

    if (pd->yuv) {
        flogo_yuvbuf_free(pd->yuv, pd->nr_of_images);
        pd->yuv = NULL;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * logo_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int logo_inspect(TCModuleInstance *self,
                        const char *param, const char **value)
{
    LogoPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self,  "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    
    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = logo_help;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * logo_filter_video:  perform the logo rendering for each frame of
 * this video stream. See tcmodule-data.h for function details.
 */

static int logo_filter_video(TCModuleInstance *self,
                             TCFrameVideo *frame)
{
    LogoPrivateData *pd = NULL;
    WorkItem W = { NULL, 0, 0.0 };

    TC_MODULE_SELF_CHECK(self,  "filter");
    TC_MODULE_SELF_CHECK(frame, "filter");

    pd = self->userdata;

    if (frame->id < pd->start || frame->id > pd->end) {
        /* out of the interval, so skip processing */
        return TC_OK;
    }

    set_fade(&W, frame->id, pd);
    set_delay(pd);

    W.pixels = GetImagePixels(pd->images, 0, 0,
                              pd->images->columns,
                              pd->images->rows);

    return pd->render(pd, &W, frame);
}

/*************************************************************************/

static const TCCodecID logo_codecs_video_in[] = { 
    TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_ERROR
};
static const TCCodecID logo_codecs_video_out[] = {
    TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(logo);
TC_MODULE_FILTER_FORMATS(logo);

TC_MODULE_INFO(logo);

static const TCModuleClass logo_class = {
    TC_MODULE_CLASS_HEAD(logo),

    .init         = logo_init,
    .fini         = logo_fini,
    .configure    = logo_configure,
    .stop         = logo_stop,
    .inspect      = logo_inspect,

    .filter_video = logo_filter_video,
};

TC_MODULE_ENTRY_POINT(logo)

/*************************************************************************/

static int logo_get_config(TCModuleInstance *self, char *options)
{
    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYO", "1");
    // buf, name, comment, format, val, from, to
    optstr_param(options, "file",   "Image filename",    "%s",    "logo.png");
    optstr_param(options, "posdef", "Position (0=None, 1=TopL, 2=TopR, 3=BotL, 4=BotR, 5=Center)",  "%d", "0", "0", "5");
    optstr_param(options, "pos",    "Position (0-width x 0-height)",  "%dx%d", "0x0", "0", "width", "0", "height");
    optstr_param(options, "range",  "Restrict rendering to framerange",  "%u-%u", "0-0", "0", "oo", "0", "oo");
    optstr_param(options, "fade",   "Fade image in/out (# of frames)",  "%u-%u", "0-0", "0", "oo", "0", "oo");
    // bools
    optstr_param(options, "ignoredelay", "Ignore delay specified in animations", "", "0");
    optstr_param(options, "rgbswap", "Swap red/blue colors", "", "0");
    optstr_param(options, "grayout", "YUV only: don't write Cb and Cr, makes a nice effect", "",  "0");
    optstr_param(options, "hqconv",  "YUV only: do high quality rgb->yuv img conversion", "",  "0");
    optstr_param(options, "flip",    "Mirror image",  "", "0");

    return TC_OK;
}

static int logo_process(TCModuleInstance *self, TCFrame *frame)
{
    if ((frame->tag & TC_POST_M_PROCESS)
      && (frame->tag & TC_VIDEO)
      && !(frame->attributes & TC_FRAME_IS_SKIPPED)) {
        return logo_filter_video(self, (TCFrameVideo*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE_M(logo)

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

