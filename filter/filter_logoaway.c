/*
 *  filter_logoaway.c
 *
 *  Copyright (C) Thomas Wehrspann - 2002/2003
 *
 *  This plugin is based on ideas of Krzysztof Wojdon's
 *  logoaway filter for VirtualDub
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

/* TODO:
 - reSTYLE
 - docs
 - retesting
 */
 

#define MOD_NAME    "filter_logoaway.so"
#define MOD_VERSION "v0.6.0 (2009-02-24)"
#define MOD_CAP     "remove an image from the video"
#define MOD_AUTHOR  "Thomas Wehrspann"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcvideo/tcvideo.h"
#include "libtcext/tc_magick.h"
#include "libtcmodule/tcmodule-plugin.h"


/* FIXME */
enum {
    MODE_NONE,
    MODE_SOLID,
    MODE_XY,
    MODE_SHAPE
};

static char *mode_name[] = {
    "NONE",
    "SOLID",
    "XY",
    "SHAPE"
};


static const char logoaway_help[] = ""
    "* Overview\n"
    "    This filter removes an image in a user specified area from the video.\n"
    "    You can choose from different methods.\n"
    "\n"
    "* Options\n"
    "       'range' Frame Range      (0-oo)                        [0-end]\n"
    "         'pos' Position         (0-width x 0-height)          [0x0]\n"
    "        'size' Size             (0-width x 0-height)          [10x10]\n"
    "        'mode' Filter Mode      (0=none,1=solid,2=xy,3=shape) [0]\n"
    "      'border' Visible Border\n"
    "        'dump' Dump filter area to file\n"
    "     'xweight' X-Y Weight       (0%%-100%%)                   [50]\n"
    "        'fill' Solid Fill Color (RRGGBB)                      [000000]\n"
    "        'file' Image with alpha/shape information             []\n"
    "\n";


typedef struct logoawayprivatedata_ LogoAwayPrivateData;
struct logoawayprivatedata_ {
    unsigned int    start, end;
    int             xpos, ypos;
    int             width, height;
    int             mode;
    int             border;
    int             xweight, yweight;
    int             rcolor, gcolor, bcolor;
    int             ycolor, ucolor, vcolor;
    char            file[PATH_MAX];
    int             instance;

    int             alpha;

    TCMagickContext logo_ctx;
    TCMagickContext dump_ctx;
    PixelPacket     *pixels;

    int             dump;
    uint8_t         *dump_buf;

    char            conf_str[TC_BUF_MIN];

    int (*process_frame)(LogoAwayPrivateData *pd,
                         uint8_t *buffer, int width, int height);
};


/*********************************************************
 * blend two pixel
 * this function blends two pixel with the given
 * weight
 * @param   srcPixel        source pixel value
 *          destPixel       source pixel value
 *          alpha           weight
 * @return  unsigned char   new pixel value
 *********************************************************/
static uint8_t alpha_blending(uint8_t srcPixel, uint8_t destPixel, int alpha)
{
  return (((alpha * (srcPixel - destPixel) ) >> 8 ) + destPixel);
}


static void dump_image_rgb(LogoAwayPrivateData *pd,
                           uint8_t *buffer, int width, int height)
{
    int row = 0, col = 0, buf_off = 0, pkt_off = 0;
    int ret = TC_OK;

    for (row = pd->ypos; row < pd->height; row++) {
        for (col = pd->xpos; col < pd->width; col++) {
            pkt_off = ((row-pd->ypos)*(pd->width-pd->xpos)+(col-pd->xpos)) * 3;
            buf_off = ((height-row)*width+col) * 3;
            /* R */
            pd->dump_buf[pkt_off +0] = buffer[buf_off +0];
            /* G */
            pd->dump_buf[pkt_off +1] = buffer[buf_off +1];
            /* B */
            pd->dump_buf[pkt_off +2] = buffer[buf_off +2];
        }
    }

    ret = tc_magick_RGBin(&pd->dump_ctx,
                          pd->width  - pd->xpos,
                          pd->height - pd->ypos,
                          pd->dump_buf);
    if (ret != TC_OK) {
        tc_log_error(MOD_NAME, "FIXME");
    } else {
        tc_snprintf(pd->dump_ctx.image_info->filename,
                    MaxTextExtent, "dump[%d].png", pd->instance); /* FIXME */

        WriteImage(pd->dump_ctx.image_info, pd->dump_ctx.image);
    }
}

/* FIXME: both the inner if(N&1)s can be factored out */
static void draw_border_rgb(LogoAwayPrivateData *pd,
                            uint8_t *buffer, int width, int height)
{
    int row = 0, col = 0, buf_off = 0;

    for (row = pd->ypos; row < pd->height; row++) {
        if ((row == pd->ypos) || (row==pd->height-1)) {
            for (col = pd->xpos*3; col < pd->width*3; col++)
                if (col & 1)
                    buffer[((height-row)*width*3+col)] = 255 & 0xff;
        }
        if (row & 1) {
            buf_off = ((height-row)*width+pd->xpos)*3;
            buffer[buf_off +0] = 255;
            buffer[buf_off +1] = 255;
            buffer[buf_off +2] = 255;

            buf_off = ((height-row)*width+pd->width)*3;
            buffer[buf_off +0] = 255;
            buffer[buf_off +1] = 255;
            buffer[buf_off +2] = 255;
        }
    }
}


/* FIXME: both the inner if(N&1)s can be factored out */
static void draw_border_yuv(LogoAwayPrivateData *pd,
                            uint8_t *buffer, int width, int height)
{
    int row = 0, col = 0;

    for (row = pd->ypos; row < pd->height; row++) {
        if ((row == pd->ypos) || (row == pd->height - 1)) {
            for (col = pd->xpos; col < pd->width; col++)
                if (col & 1)
                    buffer[row*width+col] = 255 & 0xff;
        }
        if (row & 1) {
            buffer[row*width+pd->xpos]  = 255 & 0xff;
            buffer[row*width+pd->width] = 255 & 0xff;
        }
    }
}

/*************************************************************************/

static int process_frame_null(LogoAwayPrivateData *pd,
                              uint8_t *buffer, int width, int height)
{
    return TC_OK;
}

/*************************************************************************/

static int process_frame_rgb_solid(LogoAwayPrivateData *pd,
                                   uint8_t *buffer, int width, int height)
{
    int row, col, buf_off, pkt_off;
    uint8_t px;

    if (pd->dump) {
        dump_image_rgb(pd, buffer, width, height);
    }

    for (row = pd->ypos; row < pd->height; row++) {
        for (col = pd->xpos; col < pd->width; col++) {
            buf_off = ((height-row)*width+col) * 3;
            pkt_off = (row-pd->ypos) * (pd->width-pd->xpos) + (col-pd->xpos);
            /* R */
            if (!pd->alpha) {
                buffer[buf_off +0] = pd->rcolor;
                buffer[buf_off +1] = pd->gcolor;
                buffer[buf_off +2] = pd->bcolor;
            } else {
                px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].red);
                buffer[buf_off +0] = alpha_blending(buffer[buf_off +0], pd->rcolor, px);
                /* G */
                px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].green);
                buffer[buf_off +1] = alpha_blending(buffer[buf_off +1], pd->gcolor, px);
                /* B */
                px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].blue);
                buffer[buf_off +2] = alpha_blending(buffer[buf_off +2], pd->bcolor, px);
            }
        }
    }
 
    if (pd->border) {
        draw_border_rgb(pd, buffer, width, height);
    }
    return TC_OK;
}


static int process_frame_rgb_xy(LogoAwayPrivateData *pd,
                                uint8_t *buffer, int width, int height)
{
    int row, col, xdistance, ydistance, distance_west, distance_north;
    unsigned char hcalc, vcalc;
    int buf_off, pkt_off, buf_off_xpos, buf_off_width, buf_off_ypos, buf_off_height;
    int alpha_hori, alpha_vert;
    uint8_t npx[3], px[3];

    if (pd->dump) {
        dump_image_rgb(pd, buffer, width, height);
    }

    xdistance = 256 / (pd->width - pd->xpos);
    ydistance = 256 / (pd->height - pd->ypos);
    for (row = pd->ypos; row < pd->height; row++) {
        distance_north = pd->height - row;
        alpha_vert = ydistance * distance_north;

        buf_off_xpos = ((height-row)*width+pd->xpos) * 3;
        buf_off_width = ((height-row)*width+pd->width) * 3;

        for (col = pd->xpos; col < pd->width; col++) {
            distance_west  = pd->width - col;

            alpha_hori = xdistance * distance_west;

            buf_off_ypos = ((height-pd->ypos)*width+col) * 3;
            buf_off_height = ((height-pd->height)*width+col) * 3;
            buf_off = ((height-row)*width+col) * 3;

            pkt_off = (row-pd->ypos) * (pd->width-pd->xpos) + (col-pd->xpos);

            /* R */
            hcalc  = alpha_blending(buffer[buf_off_xpos +0], buffer[buf_off_width  +0], alpha_hori);
            vcalc  = alpha_blending(buffer[buf_off_ypos +0], buffer[buf_off_height +0], alpha_vert);
            npx[0] = ((hcalc*pd->xweight + vcalc*pd->yweight)/100);
            /* G */
            hcalc  = alpha_blending(buffer[buf_off_xpos +1], buffer[buf_off_width  +1], alpha_hori);
            vcalc  = alpha_blending(buffer[buf_off_ypos +1], buffer[buf_off_height +1], alpha_vert);
            npx[1] = ((hcalc*pd->xweight + vcalc*pd->yweight)/100);
            /* B */
            hcalc  = alpha_blending(buffer[buf_off_xpos +2], buffer[buf_off_width  +2], alpha_hori);
            vcalc  = alpha_blending(buffer[buf_off_ypos +2], buffer[buf_off_height +2], alpha_vert);
            npx[2] = ((hcalc*pd->xweight + vcalc*pd->yweight)/100);
            if (!pd->alpha) {
                buffer[buf_off +0] = npx[0];
                buffer[buf_off +1] = npx[1];
                buffer[buf_off +2] = npx[2];
            } else {
                px[0] = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].red);
                px[1] = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].green);
                px[2] = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].blue);
                buffer[buf_off +0] = alpha_blending(buffer[buf_off +0], npx[0], px[0]);
                buffer[buf_off +1] = alpha_blending(buffer[buf_off +1], npx[1], px[1]);
                buffer[buf_off +2] = alpha_blending(buffer[buf_off +2], npx[2], px[2]);
            }
        }
    }

    if (pd->border) {
        draw_border_rgb(pd, buffer, width, height);
    }
    return TC_OK;
}


static int process_frame_rgb_shape(LogoAwayPrivateData *pd,
                                   uint8_t *buffer, int width, int height)
{
    int row, col, i = 0;
    int xdistance, ydistance, distance_west, distance_north;
    unsigned char hcalc, vcalc;
    int buf_off, pkt_off, buf_off_xpos, buf_off_width, buf_off_ypos, buf_off_height;
    int alpha_hori, alpha_vert;
    uint8_t tmpx, px[3], npx[3];

    if (pd->dump) {
        dump_image_rgb(pd, buffer, width, height);
    }

    xdistance = 256 / (pd->width - pd->xpos);
    ydistance = 256 / (pd->height - pd->ypos);
    for (row = pd->ypos; row<pd->height; row++) {
        distance_north = pd->height - row;

        alpha_vert = ydistance * distance_north;

        for (col = pd->xpos; col<pd->width; col++) {
            distance_west  = pd->width - col;

            alpha_hori = xdistance * distance_west;

            buf_off = ((height-row)*width+col) * 3;
            pkt_off = (row-pd->ypos) * (pd->width-pd->xpos) + (col-pd->xpos);

            buf_off_xpos = ((height-row)*width+pd->xpos) * 3;
            buf_off_width = ((height-row)*width+pd->width) * 3;
            buf_off_ypos = ((height-pd->ypos)*width+col) * 3;
            buf_off_height = ((height-pd->height)*width+col) * 3;

            tmpx = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off-i].red);
            i = 0;
            while ((tmpx != 255) && (col-i > pd->xpos))
                i++;
            buf_off_xpos   = ((height-row)*width + col-i) * 3;
            tmpx = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off+i].red);
            i = 0;
            while ((tmpx != 255) && (col + i < pd->width))
                i++;
            buf_off_width  = ((height-row)*width + col+i) * 3;

            tmpx = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off-i*(pd->width-pd->xpos)].red);
            i = 0;
            while ((tmpx != 255) && (row - i > pd->ypos))
                i++;
            buf_off_ypos   = (height*width*3)-((row-i)*width - col) * 3;
            tmpx = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off+i*(pd->width-pd->xpos)].red);
            i = 0;
            while ((tmpx != 255) && (row + i < pd->height))
                i++;
            buf_off_height = (height*width*3)-((row+i)*width - col) * 3;

            /* R */
            hcalc  = alpha_blending(buffer[buf_off_xpos +0], buffer[buf_off_width  +0], alpha_hori);
            vcalc  = alpha_blending(buffer[buf_off_ypos +0], buffer[buf_off_height +0], alpha_vert);
            npx[0] = (hcalc*pd->xweight + vcalc*pd->yweight)/100;
            px[0]  = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].red);
            /* G */
            hcalc = alpha_blending(buffer[buf_off_xpos +1], buffer[buf_off_width  +1], alpha_hori);
            vcalc = alpha_blending(buffer[buf_off_ypos +1], buffer[buf_off_height +1], alpha_vert);
            npx[1] = (hcalc*pd->xweight + vcalc*pd->yweight)/100;
            px[1] = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].green);
            /* B */
            hcalc = alpha_blending(buffer[buf_off_xpos +2], buffer[buf_off_width  +2], alpha_hori);
            vcalc = alpha_blending(buffer[buf_off_ypos +2], buffer[buf_off_height +2], alpha_vert);
            npx[2] = (hcalc*pd->xweight + vcalc*pd->yweight)/100;
            px[2] = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].blue);

            buffer[buf_off +0] = alpha_blending(buffer[buf_off +0], npx[0], px[0]);
            buffer[buf_off +0] = alpha_blending(buffer[buf_off +0], npx[1], px[1]);
            buffer[buf_off +0] = alpha_blending(buffer[buf_off +0], npx[2], px[2]);
        }
    }

    if (pd->border) {
        draw_border_rgb(pd, buffer, width, height);
    }
    return TC_OK;
}

static int process_frame_yuv_solid(LogoAwayPrivateData *pd,
                                   uint8_t *buffer, int width, int height)
{
    uint8_t px;
    int row, col, craddr, cbaddr, buf_off, pkt_off=0;

    craddr = (width * height);
    cbaddr = (width * height) * 5 / 4;

    /* Y */
    for (row = pd->ypos; row < pd->height; row++) {
        for (col = pd->xpos; col < pd->width; col++) {

            buf_off = row * width + col;
            pkt_off = (row - pd->ypos) * (pd->width - pd->xpos) + (col - pd->xpos);
            if (!pd->alpha) {
                buffer[buf_off] = pd->ycolor;
            } else {
                px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].red);
                buffer[buf_off] = alpha_blending(buffer[buf_off], pd->ycolor, px);
            }
        }
    }

    /* Cb, Cr */
    for(row = pd->ypos/2+1; row < pd->height/2; row++) {
        for(col = pd->xpos/2+1; col < pd->width/2; col++) {
            buf_off = row * width/2 + col;
            pkt_off = (row * 2 - pd->ypos) * (pd->width - pd->xpos) + (col * 2 - pd->xpos);

            if (!pd->alpha) {
                buffer[craddr + buf_off] = pd->ucolor;
                buffer[cbaddr + buf_off] = pd->vcolor;
            } else {
                /* sic */
                px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].red);
                buffer[craddr + buf_off] = alpha_blending(buffer[craddr + buf_off], pd->ucolor, px);
                buffer[cbaddr + buf_off] = alpha_blending(buffer[cbaddr + buf_off], pd->vcolor, px);
            }
        }
    }

    if (pd->border) {
        draw_border_yuv(pd, buffer, width, height);
    }
    return TC_OK;
}

static int process_frame_yuv_xy(LogoAwayPrivateData *pd,
                                uint8_t *buffer, int width, int height)
{
    int row, col, craddr, cbaddr;
    int xdistance, ydistance, distance_west, distance_north;
    unsigned char hcalc, vcalc;
    int buf_off, pkt_off=0, buf_off_xpos, buf_off_width, buf_off_ypos, buf_off_height;
    int alpha_hori, alpha_vert;
    uint8_t px;

    craddr = (width * height);
    cbaddr = (width * height) * 5 / 4;

    /* Y' */
      xdistance = 256 / (pd->width - pd->xpos);
      ydistance = 256 / (pd->height - pd->ypos);
      for(row=pd->ypos; row<pd->height; row++) {
        distance_north = pd->height - row;

        alpha_vert = ydistance * distance_north;

        buf_off_xpos = row*width+pd->xpos;
        buf_off_width = row*width+pd->width;

        for(col=pd->xpos; col<pd->width; col++) {
          uint8_t npx;
          distance_west  = pd->width - col;

          alpha_hori = xdistance * distance_west;

          buf_off = row*width+col;
          buf_off_ypos = pd->ypos*width+col;
          buf_off_height = pd->height*width+col;

          pkt_off = (row-pd->ypos) * (pd->width-pd->xpos) + (col-pd->xpos);

          hcalc = alpha_blending(buffer[buf_off_xpos], buffer[buf_off_width],  alpha_hori);
          vcalc = alpha_blending(buffer[buf_off_ypos], buffer[buf_off_height], alpha_vert);
          npx   = ((hcalc*pd->xweight + vcalc*pd->yweight)/100);
          if (!pd->alpha) {
            buffer[buf_off] = npx;
          } else {
            px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].red);
            buffer[buf_off] = alpha_blending(buffer[buf_off], npx, px);
          }
        }
      }

      /* Cb, Cr */
      xdistance = 512 / (pd->width - pd->xpos);
      ydistance = 512 / (pd->height - pd->ypos);
      for (row=pd->ypos/2+1; row<pd->height/2; row++) {
        distance_north = pd->height/2 - row;

        alpha_vert = ydistance * distance_north;

        buf_off_xpos = row*width/2+pd->xpos/2;
        buf_off_width = row*width/2+pd->width/2;

        for (col=pd->xpos/2+1; col<pd->width/2; col++) {
          uint8_t npx[2];
          distance_west  = pd->width/2 - col;

          alpha_hori = xdistance * distance_west;

          buf_off = row*width/2+col;
          buf_off_ypos = pd->ypos/2*width/2+col;
          buf_off_height = pd->height/2*width/2+col;

          pkt_off = (row*2-pd->ypos) * (pd->width-pd->xpos) + (col*2-pd->xpos);

          hcalc  = alpha_blending(buffer[craddr + buf_off_xpos], buffer[craddr + buf_off_width],  alpha_hori);
          vcalc  = alpha_blending(buffer[craddr + buf_off_ypos], buffer[craddr + buf_off_height], alpha_vert);
          npx[0] = ((hcalc*pd->xweight + vcalc*pd->yweight)/100);
          hcalc = alpha_blending( buffer[cbaddr + buf_off_xpos], buffer[cbaddr + buf_off_width],  alpha_hori );
          vcalc = alpha_blending( buffer[cbaddr + buf_off_ypos], buffer[cbaddr + buf_off_height], alpha_vert );
          npx[1] = ((hcalc*pd->xweight + vcalc*pd->yweight)/100);

          if (!pd->alpha) {
            buffer[craddr + buf_off] = npx[0];
            buffer[cbaddr + buf_off] = npx[1];
          } else {
            px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].red); /* sic */
            buffer[craddr + buf_off] = alpha_blending(buffer[craddr + buf_off], npx[0], px);
            buffer[craddr + buf_off] = alpha_blending(buffer[craddr + buf_off], npx[1], px);
          }
        }
      }

    if (pd->border) {
        draw_border_yuv(pd, buffer, width, height);
    }
    return TC_OK;
}

static int process_frame_yuv_shape(LogoAwayPrivateData *pd,
                                   uint8_t *buffer, int width, int height)
{
    int row, col, i;
    int craddr, cbaddr;
    int xdistance, ydistance, distance_west, distance_north;
    unsigned char hcalc, vcalc;
    int buf_off, pkt_off=0, buf_off_xpos, buf_off_width, buf_off_ypos, buf_off_height;
    int alpha_hori, alpha_vert;
    uint8_t px, npx[3];

    craddr = (width * height);
    cbaddr = (width * height) * 5 / 4;

      xdistance = 256 / (pd->width - pd->xpos);
      ydistance = 256 / (pd->height - pd->ypos);
      for(row=pd->ypos; row<pd->height; row++) {
        distance_north = pd->height - row;

        alpha_vert = ydistance * distance_north;

        for(col=pd->xpos; col<pd->width; col++) {
          distance_west  = pd->width - col;

          alpha_hori = xdistance * distance_west;

          buf_off = (row*width+col);
          pkt_off = (row-pd->ypos) * (pd->width-pd->xpos) + (col-pd->xpos);

          i=0;
          px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off-i].red);
          while( (px != 255) && (col-i>pd->xpos) ) i++;
          buf_off_xpos   = (row*width + col-i);
          i=0;
          px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off+i].red);
          while( (px != 255) && (col+i<pd->width) ) i++;
          buf_off_width  = (row*width + col+i);

          i=0;
          px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off-i*(pd->width-pd->xpos)].red);
          while( (px != 255) && (row-i>pd->ypos) ) i++;
          buf_off_ypos   = ((row-i)*width + col);
          i=0;
          px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off+i*(pd->width-pd->xpos)].red);
          while( (px != 255) && (row+i<pd->height) ) i++;
          buf_off_height = ((row+i)*width + col);

          hcalc  = alpha_blending( buffer[buf_off_xpos], buffer[buf_off_width],  alpha_hori );
          vcalc  = alpha_blending( buffer[buf_off_ypos], buffer[buf_off_height], alpha_vert );
          px     = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].red);
          npx[0] = ((hcalc*pd->xweight + vcalc*pd->yweight)/100); /* FIXME */
          buffer[buf_off] = alpha_blending(buffer[buf_off], npx[0], px);
        }
      }

      /* Cb, Cr */
      xdistance = 512 / (pd->width - pd->xpos);
      ydistance = 512 / (pd->height - pd->ypos);
      for (row=pd->ypos/2+1; row<pd->height/2; row++) {
        distance_north = pd->height/2 - row;

        alpha_vert = ydistance * distance_north;

        for (col=pd->xpos/2+1; col<pd->width/2; col++) {
          distance_west  = pd->width/2 - col;

          alpha_hori = xdistance * distance_west;

          i=0;
          px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off-i].red);
          while( (px != 255) && (col-i>pd->xpos) ) i++;
          buf_off_xpos   = (row*width/2 + col-i);
          i=0;
          px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off+i].red);
          while( (px != 255) && (col+i<pd->width) ) i++;
          buf_off_width  = (row*width/2 + col+i);

          i=0;
          px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off-i*(pd->width-pd->xpos)].red);
          while( (px != 255) && (row-i>pd->ypos) ) i++;
          buf_off_ypos   = ((row-i)*width/2 + col);
          i=0;
          px = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off+i*(pd->width-pd->xpos)].red);
          while( (px != 255) && (row+i<pd->height) ) i++;
          buf_off_height = ((row+i)*width/2 + col);

          buf_off = row*width/2+col;
          buf_off_ypos = pd->ypos/2*width/2+col;
          buf_off_height = pd->height/2*width/2+col;

          pkt_off = (row*2-pd->ypos) * (pd->width-pd->xpos) + (col*2-pd->xpos);

          px     = (uint8_t)ScaleQuantumToChar(pd->pixels[pkt_off].red);
          /* sic */
          hcalc  = alpha_blending(buffer[craddr + buf_off_xpos], buffer[craddr + buf_off_width],  alpha_hori);
          vcalc  = alpha_blending(buffer[craddr + buf_off_ypos], buffer[craddr + buf_off_height], alpha_vert);
          npx[1] = ((hcalc*pd->xweight + vcalc*pd->yweight)/100);
          hcalc  = alpha_blending(buffer[cbaddr + buf_off_xpos], buffer[cbaddr + buf_off_width],  alpha_hori);
          vcalc  = alpha_blending(buffer[cbaddr + buf_off_ypos], buffer[cbaddr + buf_off_height], alpha_vert);
          npx[2] = ((hcalc*pd->xweight + vcalc*pd->yweight)/100);

          buffer[craddr + buf_off] = alpha_blending(buffer[craddr + buf_off], npx[1], px);
          buffer[cbaddr + buf_off] = alpha_blending(buffer[cbaddr + buf_off], npx[2], px); 
        }
      }


    if (pd->border) {
        draw_border_yuv(pd, buffer, width, height);
    }
    return TC_OK;
}

/*************************************************************************/

static void free_dump_buf(LogoAwayPrivateData *pd)
{
    if (pd->dump) {
        tc_free(pd->dump_buf);
        pd->dump_buf = NULL;
    }   
}

static int logoaway_setup(LogoAwayPrivateData *pd, vob_t *vob)
{
    if (pd->dump) {
        pd->dump_buf = tc_malloc((pd->width-pd->xpos)*(pd->height-pd->ypos)*3);
        /* FIXME */
        if (pd->dump_buf == NULL){
            tc_log_error(MOD_NAME, "out of memory");
            return TC_ERROR; /* FIXME */
        }

        tc_magick_init(&pd->dump_ctx, TC_MAGICK_QUALITY_DEFAULT);
    }

    if (pd->alpha) {
        int ret = TC_OK;

        tc_magick_init(&pd->logo_ctx, TC_MAGICK_QUALITY_DEFAULT);

        ret =  tc_magick_filein(&pd->logo_ctx, pd->file);
        if (ret != TC_OK) {
            free_dump_buf(pd);
            return ret;
        }

        if ((pd->logo_ctx.image->columns != (pd->width-pd->xpos))
          || (pd->logo_ctx.image->rows != (pd->height-pd->ypos))) {
            tc_log_error(MOD_NAME, "\"%s\" has incorrect size", pd->file);
            free_dump_buf(pd);
            return TC_ERROR;
        }

        pd->pixels = GetImagePixels(pd->logo_ctx.image, 0, 0,
                                    pd->logo_ctx.image->columns,
                                    pd->logo_ctx.image->rows);
    }

    /* FIXME: this can be improved. What about a LUT? */
    switch (pd->mode) {
      case MODE_SOLID:
        pd->process_frame = (vob->im_v_codec == TC_CODEC_RGB24)
                                ?process_frame_rgb_solid
                                :process_frame_yuv_solid;
        break;
      case MODE_XY:
        pd->process_frame = (vob->im_v_codec == TC_CODEC_RGB24)
                                ?process_frame_rgb_xy
                                :process_frame_yuv_xy;
        break;
      case MODE_SHAPE:
        pd->process_frame = (vob->im_v_codec == TC_CODEC_RGB24)
                                ?process_frame_rgb_shape
                                :process_frame_yuv_shape;
        break;
      case MODE_NONE:
      default:
        pd->process_frame = process_frame_null; /* catchall */
        break;
    }
    return TC_OK;
}

static void logoaway_defaults(LogoAwayPrivateData *pd)
{
    pd->start    = 0;
    pd->end      = (unsigned int)-1;
    pd->xpos     = -1;
    pd->ypos     = -1;
    pd->width    = -1;
    pd->height   = -1;
    pd->mode     = 0;
    pd->border   = 0;
    pd->xweight  = 50;
    pd->yweight  = 50;
    pd->rcolor   = 0;
    pd->gcolor   = 0;
    pd->bcolor   = 0;
    pd->ycolor   = 16;
    pd->ucolor   = 128;
    pd->vcolor   = 128;
    pd->alpha    = 0;
    pd->dump     = 0;
}

static int logoaway_check_options(LogoAwayPrivateData *pd, vob_t *vob)
{
    if (vob->im_v_codec != TC_CODEC_RGB24
      && vob->im_v_codec != TC_CODEC_YUV420P) {
        tc_log_error(MOD_NAME, "unsupported colorspace");
        return TC_ERROR;
    }
    if ((pd->xpos > vob->im_v_width) || (pd->ypos > vob->im_v_height)
     || (pd->xpos < 0) || (pd->ypos < 0))  {
        tc_log_error(MOD_NAME, "invalid position");
        return TC_ERROR;
    }
    if ((pd->width > vob->im_v_width) || (pd->height > vob->im_v_height)
     || (pd->width-pd->xpos < 0) || (pd->height-pd->ypos < 0)) {
        tc_log_error(MOD_NAME, "invalid size");
        return TC_ERROR;
    }
    if ((pd->xweight > 100) || (pd->xweight < 0)) {
        tc_log_error(MOD_NAME, "invalid x weight");
        return TC_ERROR;
    }
    if ((pd->mode < 0) || (pd->mode > 3)) {
        tc_log_error(MOD_NAME, "invalid mode");
        return TC_ERROR;
    }
    if ((pd->mode == 3) && (pd->alpha == 0)) {
        tc_log_error(MOD_NAME, "alpha/shape file needed for SHAPE-mode");
        return TC_ERROR;
    }
    return TC_OK;
}

static void logoaway_show_options(LogoAwayPrivateData *pd)
{
    tc_log_info(MOD_NAME, " LogoAway Filter Settings:");
    tc_log_info(MOD_NAME, "            pos = %dx%d", pd->xpos, pd->ypos);
    tc_log_info(MOD_NAME, "           size = %dx%d", pd->width-pd->xpos, pd->height-pd->ypos);
    tc_log_info(MOD_NAME, "           mode = %d(%s)", pd->mode, mode_name[pd->mode]);
    tc_log_info(MOD_NAME, "         border = %d", pd->border);
    tc_log_info(MOD_NAME, "     x-y weight = %d:%d", pd->xweight, pd->yweight);
    tc_log_info(MOD_NAME, "     fill color = %2X%2X%2X", pd->rcolor, pd->gcolor, pd->bcolor);
    if (pd->alpha) {
        tc_log_info (MOD_NAME, "           file = %s", pd->file);
    }
    if (pd->dump) {
        tc_log_info (MOD_NAME, "           dump = %d", pd->dump);
    }
}



/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * logoaway_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_INIT(logoaway, LogoAwayPrivateData)

/*************************************************************************/

/**
 * logoaway_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

TC_MODULE_GENERIC_FINI(logoaway)


/*************************************************************************/

/**
 * logoaway_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int logoaway_configure(TCModuleInstance *self,
                              const char *options,
                              vob_t *vob,
                              TCModuleExtraData *xdata[])
{
    LogoAwayPrivateData *pd = NULL;
    int ret = TC_OK;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    logoaway_defaults(pd);

    if (options) {
        optstr_get(options,  "range",   "%d-%d",     &pd->start,  &pd->end);
        optstr_get(options,  "pos",     "%dx%d",     &pd->xpos,   &pd->ypos);
        optstr_get(options,  "size",    "%dx%d",     &pd->width,  &pd->height);
        pd->width += pd->xpos; pd->height += pd->ypos;
        optstr_get(options,  "mode",    "%d",        &pd->mode);
        if (optstr_lookup (options,  "border") != NULL) {
            pd->border = 1;
        }
        optstr_get(options,  "xweight", "%d",        &pd->xweight);
        pd->yweight = 100 - pd->xweight;
        optstr_get(options,  "fill",    "%2x%2x%2x", &pd->rcolor, &pd->gcolor, &pd->bcolor);
        pd->ycolor =  (0.257 * pd->rcolor) + (0.504 * pd->gcolor) + (0.098 * pd->bcolor) + 16;
        pd->ucolor =  (0.439 * pd->rcolor) - (0.368 * pd->gcolor) - (0.071 * pd->bcolor) + 128;
        pd->vcolor = -(0.148 * pd->rcolor) - (0.291 * pd->gcolor) + (0.439 * pd->bcolor) + 128;
        if (optstr_get(options, "file", "%[^:]", pd->file) >= 0) {
            pd->alpha = 1;
        }
        if (optstr_lookup(options,  "dump") != NULL) {
            pd->dump = 1;
        }
    }

    ret = logoaway_check_options(pd, vob);
    if (ret == TC_OK) {
        if (verbose) {
            logoaway_show_options(pd);
        }
        ret = logoaway_setup(pd, vob);
    }
    return ret;
}

/*************************************************************************/

/**
 * logoaway_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int logoaway_stop(TCModuleInstance *self)
{
    LogoAwayPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    tc_magick_fini(&pd->logo_ctx);
    tc_magick_fini(&pd->dump_ctx);

    free_dump_buf(pd);
    return TC_OK;
}

/*************************************************************************/

/**
 * logoaway_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int logoaway_inspect(TCModuleInstance *self,
                            const char *param, const char **value)
{
    LogoAwayPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    
    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = logoaway_help;
    }

    if (optstr_lookup(param, "pos")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%ix%i", pd->xpos, pd->ypos);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "size")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%ix%i", pd->width-pd->xpos, pd->height-pd->ypos);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "mode")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%i", pd->mode);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "border")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%i", pd->border);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "xweight")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%i:%i", pd->xweight, pd->yweight);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "fill")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%2X%2X%2X", pd->rcolor, pd->gcolor, pd->bcolor);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "dump")) {
        tc_snprintf(pd->conf_str, sizeof(pd->conf_str),
                    "%i", pd->dump);
        *value = pd->conf_str;
    }
    if (optstr_lookup(param, "alpha")) {
        *value = pd->file;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * logoaway_filter_video:  perform the logo removal for each frame of
 * this video stream. See tcmodule-data.h for function details.
 */

static int logoaway_filter_video(TCModuleInstance *self,
                                 vframe_list_t *frame)
{
    LogoAwayPrivateData *pd = NULL;
    int ret = TC_OK;

    TC_MODULE_SELF_CHECK(self, "filter");
    TC_MODULE_SELF_CHECK(frame, "filter");

    pd = self->userdata;

    if (frame->id >= pd->start && frame->id <= pd->end) {
        ret = pd->process_frame(pd, frame->video_buf,
                                frame->v_width, frame->v_height);
    }

    return ret;
}


/*************************************************************************/

static const TCCodecID logoaway_codecs_video_in[] = {
    TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_ERROR
};
static const TCCodecID logoaway_codecs_video_out[] = { 
    TC_CODEC_RGB24, TC_CODEC_YUV420P, TC_CODEC_ERROR
};
TC_MODULE_AUDIO_UNSUPPORTED(logoaway);
TC_MODULE_FILTER_FORMATS(logoaway);

TC_MODULE_INFO(logoaway);

static const TCModuleClass logoaway_class = {
    TC_MODULE_CLASS_HEAD(logoaway),

    .init         = logoaway_init,
    .fini         = logoaway_fini,
    .configure    = logoaway_configure,
    .stop         = logoaway_stop,
    .inspect      = logoaway_inspect,

    .filter_video = logoaway_filter_video,
};

TC_MODULE_ENTRY_POINT(logoaway)


/*************************************************************************/

static int logoaway_get_config(TCModuleInstance *self, char *options)
{
    LogoAwayPrivateData *pd = NULL;
    char buf[TC_BUF_MIN];

    TC_MODULE_SELF_CHECK(self, "get_config");

    pd = self->userdata;

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYOM", "1");

    tc_snprintf(buf, sizeof(buf), "%u-%u", pd->start, pd->end);
    optstr_param(options, "range", "Frame Range", "%d-%d", buf, "0", "oo", "0", "oo");

    tc_snprintf(buf, sizeof(buf), "%dx%d", pd->xpos, pd->ypos);
    optstr_param(options, "pos", "Position of logo", "%dx%d", buf, "0", "width", "0", "height");

    tc_snprintf(buf, sizeof(buf), "%dx%d", pd->width, pd->height);
    optstr_param(options, "size", "Size of logo", "%dx%d", buf, "0", "width", "0", "height");

    tc_snprintf(buf, sizeof(buf), "%d", pd->mode);
    optstr_param(options, "mode", "Filter Mode (0=none,1=solid,2=xy,3=shape)", "%d", buf, "0", "3");

    tc_snprintf(buf, sizeof(buf), "%d", pd->border);
    optstr_param(options, "border", "Visible Border", "", buf);

    tc_snprintf(buf, sizeof(buf), "%d", pd->dump);
    optstr_param(options, "dump", "Dump filterarea to file", "", buf);

    tc_snprintf(buf, sizeof(buf), "%d", pd->xweight);
    optstr_param(options, "xweight","X-Y Weight(0%-100%)", "%d", buf, "0", "100");

    tc_snprintf(buf, sizeof(buf), "%x%x%x", pd->rcolor, pd->gcolor, pd->bcolor);
    optstr_param(options, "fill", "Solid Fill Color(RGB)", "%2x%2x%2x", buf, "00", "FF", "00", "FF", "00", "FF");

    tc_snprintf(buf, sizeof(buf), "%s", pd->file);
    optstr_param(options, "file", "Image with alpha/shape information", "%s", buf);

    return TC_OK;
}

static int logoaway_process(TCModuleInstance *self, frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    if (frame->tag & TC_PRE_M_PROCESS && frame->tag & TC_VIDEO
      && !(frame->attributes & TC_FRAME_IS_SKIPPED)) {
        return logoaway_filter_video(self, (vframe_list_t*)frame);
    }
    return TC_OK;
}

/*************************************************************************/

/* Old-fashioned module interface. */

TC_FILTER_OLDINTERFACE_M(logoaway)

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

