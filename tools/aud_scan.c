/*
 *  aud_scan.c
 *
 *  Scans the audio track
 *
 *  Copyright (C) Tilmann Bitterberg - June 2003
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "aud_scan.h"

// MP3

// from mencoder
//----------------------- mp3 audio frame header parser -----------------------

static int tabsel_123[2][3][16] = {
   { {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0},
     {0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384,0},
     {0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320,0} },

   { {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0} }
};
static long freqs[9] = { 44100, 48000, 32000, 22050, 24000, 16000 , 11025 , 12000 , 8000 };

/*
 * return frame size or -1 (bad frame)
 */
int tc_get_mp3_header(uint8_t* hbuf, int* chans, int* srate, int *bitrate){
    int stereo, ssize, crc, lsf, mpeg25, framesize;
    int padding, bitrate_index, sampling_frequency;
    uint32_t newhead =
      hbuf[0] << 24 |
      hbuf[1] << 16 |
      hbuf[2] <<  8 |
      hbuf[3];

    // head_check:
    if ( (newhead & 0xffe00000) != 0xffe00000
      || (newhead & 0x0000fc00) == 0x0000fc00) {
        // fprintf( stderr, "[%s] head_check failed\n", __FILE__);
        return -1;
    }

    if ((4 - ((newhead >> 17) & 3)) != 3) {
        return -1;
    }

    if (newhead & ((long)1 << 20)) {
        lsf = (newhead & ((long)1 << 19)) ?0x0 :0x1;
        mpeg25 = 0;
    } else {
        lsf = 1;
        mpeg25 = 1;
    }

    if (mpeg25) {
        sampling_frequency = 6 + ((newhead>>10)&0x3);
    } else {
        sampling_frequency = ((newhead>>10)&0x3) + (lsf*3);
    }

    if (sampling_frequency > 8) {
        return -1;  // valid: 0..8
    }

    crc = ((newhead >> 16) & 0x1) ^ 0x1;
    bitrate_index = ((newhead >> 12) & 0xf);
    padding = ((newhead >> 9) & 0x1);

    stereo = ((((newhead >> 6) & 0x3)) == 3) ?1 :2;

    if (!bitrate_index) {
        return -1;
    }

    if (lsf) {
        ssize = (stereo == 1) ?9 :17;
    } else {
        ssize = (stereo == 1) ?17 :32;
    }
    if(crc) {
        ssize += 2;
    }

    framesize = tabsel_123[lsf][2][bitrate_index] * 144000;
    if (bitrate) {
        *bitrate = tabsel_123[lsf][2][bitrate_index];
    }

    if (!framesize){
        return -1;  // valid: 1..14
    }

    framesize /= freqs[sampling_frequency]<<lsf;
    framesize += padding;

    if (srate) {
        *srate = freqs[sampling_frequency];
    }
    if (chans) {
        *chans = stereo;
    }
    return framesize;
}

static const uint8_t nfchans[] = {
    2, 1, 2, 3, 3, 4, 4, 5, 1, 1, 2
};

struct frmsize_s {
    uint16_t bit_rate;
    uint16_t frm_size[3];
} frmsize_t;

static const struct frmsize_s frmsizecod_tbl[] = {
    { 32  ,{64   ,69   ,96   } },
    { 32  ,{64   ,70   ,96   } },
    { 40  ,{80   ,87   ,120  } },
    { 40  ,{80   ,88   ,120  } },
    { 48  ,{96   ,104  ,144  } },
    { 48  ,{96   ,105  ,144  } },
    { 56  ,{112  ,121  ,168  } },
    { 56  ,{112  ,122  ,168  } },
    { 64  ,{128  ,139  ,192  } },
    { 64  ,{128  ,140  ,192  } },
    { 80  ,{160  ,174  ,240  } },
    { 80  ,{160  ,175  ,240  } },
    { 96  ,{192  ,208  ,288  } },
    { 96  ,{192  ,209  ,288  } },
    { 112 ,{224  ,243  ,336  } },
    { 112 ,{224  ,244  ,336  } },
    { 128 ,{256  ,278  ,384  } },
    { 128 ,{256  ,279  ,384  } },
    { 160 ,{320  ,348  ,480  } },
    { 160 ,{320  ,349  ,480  } },
    { 192 ,{384  ,417  ,576  } },
    { 192 ,{384  ,418  ,576  } },
    { 224 ,{448  ,487  ,672  } },
    { 224 ,{448  ,488  ,672  } },
    { 256 ,{512  ,557  ,768  } },
    { 256 ,{512  ,558  ,768  } },
    { 320 ,{640  ,696  ,960  } },
    { 320 ,{640  ,697  ,960  } },
    { 384 ,{768  ,835  ,1152 } },
    { 384 ,{768  ,836  ,1152 } },
    { 448 ,{896  ,975  ,1344 } },
    { 448 ,{896  ,976  ,1344 } },
    { 512 ,{1024 ,1114 ,1536 } },
    { 512 ,{1024 ,1115 ,1536 } },
    { 576 ,{1152 ,1253 ,1728 } },
    { 576 ,{1152 ,1254 ,1728 } },
    { 640 ,{1280 ,1393 ,1920 } },
    { 640 ,{1280 ,1394 ,1920 } }
};

#define fscd_tbl_entries (sizeof(frmsizecod_tbl)/sizeof(frmsize_t))

static uint32_t get_ac3_header(uint8_t *buf)
{
  int i = 0;
  uint32_t tmp = 0;

  tmp = (tmp << 8) + (buf[i++] & 0xff);
  tmp = (tmp << 8) + (buf[i++] & 0xff);
  tmp = (tmp << 8) + (buf[i++] & 0xff);

  return tmp;
}

static int get_ac3_framesize(uint8_t *buf)
{
    int fscod, frmsizecod;
    uint32_t tmp = get_ac3_header(buf);

    if (tmp < 0) {
        return -1;
    }

    fscod      = (tmp >> 6) & 0x3;
    frmsizecod = tmp & 0x3f;

    if (frmsizecod >= fscd_tbl_entries || fscod > 2) {
        return -1;
    }
    return frmsizecod_tbl[frmsizecod].frm_size[fscod];
}

// FIXME: recheck me up, 1.0.x is reference
// We try to find the number of chans in the ac3 header (BSI)
static int get_ac3_nfchans(uint8_t *buf)
{
    /* skip syncinfo (size = 5bytes) */
    /* skip to acmod */
    int acmod = buf[6]>>5;
    /* LFE flags is on, we have one more channel */
    int lfe = ((buf[2] & 0x40) == 0x40);

    if (acmod < 0 || acmod > 11)
        return -1;
    return (nfchans[acmod] + lfe);
}


static int get_ac3_bitrate(uint8_t *buf)
{
    int frmsizecod = 0;
    uint32_t tmp = get_ac3_header(buf);

    frmsizecod = tmp & 0x3f;

    if (frmsizecod >= fscd_tbl_entries) {
        return -1;
    }
    return frmsizecod_tbl[frmsizecod].bit_rate;
}


static int get_ac3_samplerate(uint8_t *buf)
{
    int fscod, sampling_rate;
    uint32_t tmp = get_ac3_header(buf);

    // Get the sampling rate
    fscod  = (tmp >> 6) & 0x3;

    if (fscod == 3) {
        return -1;  //invalid sampling rate code
    } else if (fscod == 2) {
        sampling_rate = 32000;
    } else if (fscod == 1) {
        sampling_rate = 44100;
    } else {
        sampling_rate = 48000;
    }
    return sampling_rate;
}


int tc_get_ac3_header(uint8_t *_buf, int len, int *chans, int *srate, int *bitrate )
{
    int j = 0, i = 0;
    uint8_t *buffer = _buf;
    uint16_t sync_word = 0;

    // need to find syncframe:

    for (i = 0; i < len-4; i++) {
        sync_word = (sync_word << 8) + (uint8_t) buffer[i];
        if (sync_word == 0x0b77) {
            break;
        }
    }

    if (sync_word != 0x0b77) {
        return -1;
    }

    if (srate) {
        *srate = get_ac3_samplerate(&buffer[i+1]);
    }
    if (bitrate) {
        *bitrate = get_ac3_bitrate(&buffer[i+1]);
    }
    if (chans) {
        *chans = get_ac3_nfchans(&buffer[i+1]);
    }

    if (j < 0 || bitrate < 0) {
        return -1;
    }
    return 2 * get_ac3_framesize(&buffer[i+1]);
}


int tc_get_audio_header(uint8_t *buf, int buflen, int format, int *chans, int *srate, int *bitrate )
{
    switch (format) {
      case 0x55: // MP3
        return tc_get_mp3_header(buf, chans, srate, bitrate);
        break;
      case 0x2000: // AC3
        return tc_get_ac3_header(buf, buflen, chans, srate, bitrate);
        break;
      default:
        return -1;
    }
    return -1;
}

int tc_probe_audio_header(uint8_t *buf, int buflen)
{
    if (tc_get_mp3_header(buf, NULL, NULL, NULL) > 0) {
        return 0x55;
    }
    if (tc_get_ac3_header(buf, buflen, NULL, NULL, NULL) > 0) {
        return 0x2000;
    }
    return -1;
}

int tc_format_ms_supported(int format) 
{
    return (format == 0x55   /* MP3 */
         || format == 0x2000 /* AC3 */
         || format == 0x1);  /* PCM */
}

void tc_format_mute(uint8_t *buf, int buflen, int format) 
{
    switch (format) {
      case 0x1:
        memset(buf + 0, 0, buflen - 0);
        break;
      case 0x55:
        memset(buf + 4, 0, buflen - 4);
        break;
      case 0x2000:
        // check me!
        memset(buf + 5, 0, buflen - 5);
        break;
      default: /* we're already fine */
        break;
    }
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
