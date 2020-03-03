/*
 *  Copyright (C) 2005 Francesco Romani <fromani@gmail.com>
 * 
 *  This Software is heavily based on tcvp's (http://tcvp.sf.net) 
 *  mpeg muxer/demuxer, which is
 *
 *  Copyright (C) 2001-2002 Michael Ahlberg, Mans Rullgard
 *  Copyright (C) 2003-2004 Michael Ahlberg, Mans Rullgard
 *  Copyright (C) 2005 Michael Ahlberg, Mans Rullgard
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without
 *  restriction, including without limitation the rights to use, copy,
 *  modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "mpeglib.h"
#include "mpeglib_private.h"

/** 
 * What is supported?
 *
 * probing of MPEG1/2 video (probe_mpvideo)
 * probing of MPEG audio (Layer II, *NOT* layer III, yet) (probe_mpaudio)
 * probing of AC3 audio (probe_ac3)
 */

#define MPEG_DEFAULT_AUD_SAMPLE_SIZE			16 /* bits */

/* MYNIMAL SIZE, for our purpose, not typical or standard size */
#define MPEG_VID_HDR_SIZE				11
#define MPEG_AUD_HDR_SIZE				4
#define MPEG_AC3_HDR_SIZE				7

#define MPEG_HORIZ_MASK					0x00fff000
#define MPEG_VERT_MASK					0x00000fff
#define MPEG_HORIZ_SHIFT				12
#define MPEG_VERT_SHIFT					0

#define MPEG_FRC_MASK					0x0f
#define MPEG_ASR_MASK					0xf0
#define MPEG_FRC_SHIFT					0
#define MPEG_ASR_SHIFT					4

#define MPEG_AC3_CRC_BYTES				2

mpeg_err_t mpeg_probe_mpvideo(mpeg_stream_t *s, uint8_t *data, int dlen)
{
    int ret = MPEG_OK;
    uint8_t scode[] = { 0x00, 0x00, 0x01, MPEG_SEQUENCE_HEADER };
    
    assert(s != NULL);
    assert(data != NULL);
    
    /* 
     * PLEASE NOTE: this code actually is paranoid, does not scan
     * the given data. If data begins with a sequence header, well,
     * we parse it. Otherwise we assume that this data doesn't holds
     * any header. This should be changed in future releases
     */
    if(dlen >= MPEG_VID_HDR_SIZE) {
	/* we have a sequence header start code? */
        if(memcmp(scode, data, sizeof(scode)) == 0) {
            /* ok, parsing header */
	    uint32_t dims = 0, br = 0;  /* accumulate hsize and vsize */
	    uint8_t frc = 0, asr = 0;
	    
	    data += sizeof(scode); /* point to header data, skip code */
	    dims |= (data[0] << 16 | data[1] << 8 | data[2]);

	    /* FIXME: probable mplayer diversion due to 15 */
            s->video.width |= (((dims & MPEG_HORIZ_MASK) >> MPEG_HORIZ_SHIFT) + 15) & ~15;
	    s->video.height |= (((dims & MPEG_VERT_MASK) >> MPEG_VERT_SHIFT) + 15) & ~15;
	    
	    asr |= (data[3] & MPEG_ASR_MASK) >> MPEG_ASR_SHIFT;
	    frc |= (data[3] & MPEG_FRC_MASK) >> MPEG_FRC_SHIFT;

	    s->video.frame_rate = mpeg_frame_rates[frc];
	    s->video.aspect = mpeg_aspect_ratios[asr];

	    /* only two MSB of last bitrate but */
	    br |= (data[4] << 10) | (data[5] << 2) | (data[6] >> 6);
	    /* bitrate in header is experessed as 400bps units */
	    s->video.bit_rate = (br * 400 / 1000); 
	} else {
            mpeg_log(MPEG_LOG_ERR, "PROBE: Can't find a sequence header, "
			           "probe aborted...\n");
	    ret = -MPEG_ERROR_PROBE_FAILED;
	}
    } else {
        mpeg_log(MPEG_LOG_ERR, "PROBE: not enough data to parse sequence header\n");
	ret = -MPEG_ERROR_PROBE_FAILED;
    }
    return ret;
}

/* FIXME: recheck me up against standard */
/* out of function because it's too big */
static const int mpa_bitrates[][3] = {
    { 0,   0,   0   }, /* free format (VBR?) */
    { 32,  32,  32  },
    { 64,  48,  40  },
    { 92,  56,  48  },
    { 128, 64,  56  },
    { 160, 80,  64  },
    { 192, 96,  80  },
    { 224, 112, 96  },
    { 256, 128, 112 },
    { 288, 160, 128 },
    { 320, 192, 160 },
    { 352, 224, 192 },
    { 384, 256, 224 },
    { 416, 320, 256 },
    { 448, 384, 320 }
};
	
mpeg_err_t mpeg_probe_mpaudio(mpeg_stream_t *s, uint8_t *data, int dlen)
{
    int ret = MPEG_OK;
    uint8_t scode[] = { 0xff, 0xf0 };
    int layer_id[] = { 0, 3, 2, 1 };
    int frequencies[] = { 44100, 48000, 32000, 0 }; /* Hz */
#ifdef DEBUG    
    const char *modes[] = { "stereo", "joint stereo", 
	                    "double channel", "single channel" };
#endif    
        
    assert(s != NULL);
    assert(data != NULL);

    if(dlen >= MPEG_AUD_HDR_SIZE) {
        /* Only the first 12 MSB should match (they should be all '1' */	    
        if((data[0] == scode[0]) && ((data[1] & scode[1]) == 0xf0)) {
	    uint8_t layer = 0, br_idx = 0, freq_idx = 0, md_idx = 0;
	    uint8_t flags = data[1] & 0x0f; /* last 4 LSB */

	    /* 
	     * flags outline (4 bits)
	     * please note that flags it's local terminology :)
	     * 
	     * ID         : 1 bit
	     * Layer      : 2 bits
	     * Protection : 1 bit
	     * 
	     * MSB
	     * [.ID.BIT.|_LAYER_|ID_BIT_|-PROTE-]
	     *                               LSB
	     */

	    layer = layer_id[ (flags & 0x06) >> 1 ];
	    br_idx |= ((data[2] & 0xf0) >> 4) + 1;
	    // XXX: +1 is needed?
	    freq_idx = (data[2] & 0x0c) >> 2;
	    md_idx = (data[3] & 0xc0) >> 6;
#ifdef DEBUG	    
	    if(!(flags & 0x08)) {
                mpeg_log(MPEG_LOG_WARN, "PROBE: ID bit in MPEG audio "
			                "header is not set\n");
	    }
	    if(flags & 0x01) {
                mpeg_log(MPEG_LOG_INFO, "PROBE: Audio has "
				        "protection bit set\n");
	    }
            mpeg_log(MPEG_LOG_INFO, "PROBE: Audio is layer %i\n", layer);	   	    
	    mpeg_log(MPEG_LOG_INFO, "PROBE: Audio is %s\n", modes[ md_idx ]);
#endif	    
	    s->common.bit_rate = mpa_bitrates[br_idx][layer];
	    s->audio.sample_rate = frequencies[ freq_idx ];
	    s->audio.channels = (md_idx == 3) ?1 :2; // XXX correct?
	    s->audio.sample_size = MPEG_DEFAULT_AUD_SAMPLE_SIZE;
	    // XXX: correct?
	} else {
            mpeg_log(MPEG_LOG_ERR, "PROBE: Can't find an audio syncword, "
			           "probe aborted...\n");
	    ret = -MPEG_ERROR_PROBE_FAILED;
	}
    } else {
        mpeg_log(MPEG_LOG_ERR, "PROBE: not enough data to parse audio header\n");
	ret = -MPEG_ERROR_PROBE_FAILED;
    }
    return ret;
}

/* FIXME: explain duplicates */
static const uint16_t ac3_bitrates[48] = {
	32,  32,  
	40,  40,
	48,  48,  
	56,  56,
	64,  64,  
	80,  80,
	96,  96,  
	112, 122,
	128, 128, 
	160, 160,
	192, 192, 
	224, 244,
	256, 256, 
	320, 320,
	384, 384, 
	448, 448,
	512, 512, 
	576, 576,
	640, 640
};

static const uint8_t ac3_nchannels[8] = {
	2, /* 1+1, ch1, ch2 */
	1, /* 1/0 C */
	2, /* 2/0, L+R */
	3, /* 3/0, L+C+R */
	3, /* 2+1, L+R+S */
	4, /* 3+1, L+R+C+S */
	4, /* 2+2, L+R+SL+SR */
	5  /* 3+2, L+C+R+SL+SR */
};

/* FIXME: it is useful?
static const int ac3_word_syncframe_ratios[][3] = {
	{ 64  , 69  , 96   },
	{ 64  , 70  , 96   },
	{ 80  , 87  , 120  },
	{ 80  , 88  , 120  },
	{ 96  , 104 , 144  },
	{ 96  , 105 , 144  },
	{ 112 , 121 , 168  },
	{ 112 , 122 , 168  },
	{ 128 , 139 , 192  },
	{ 128 , 140 , 192  },
	{ 160 , 174 , 240  },
	{ 160 , 175 , 240  },
	{ 192 , 208 , 288  },
	{ 192 , 209 , 288  },
	{ 224 , 243 , 336  },
	{ 224 , 244 , 336  },
	{ 256 , 278 , 384  },
	{ 256 , 279 , 384  },
	{ 320 , 348 , 480  },
	{ 320 , 349 , 480  },
	{ 384 , 417 , 576  },
	{ 384 , 418 , 576  },
	{ 448 , 487 , 672  },
	{ 448 , 488 , 672  },
	{ 512 , 557 , 768  },
	{ 512 , 558 , 768  },
	{ 640 , 696 , 960  },
	{ 640 , 697 , 960  },
	{ 768 , 835 , 1152 },
	{ 768 , 836 , 1152 },
	{ 896 , 975 , 1344 },
	{ 896 , 976 , 1344 },
	{ 1024, 1114, 1536 },
	{ 1024, 1115, 1536 },
	{ 1152, 1253, 1728 },
	{ 1152, 1254, 1728 },
	{ 1280, 1393, 1920 },
	{ 1280, 1394, 1920 }
};
*/

mpeg_err_t mpeg_probe_ac3(mpeg_stream_t *s, uint8_t *data, int dlen)
{
    int ret = MPEG_OK;
    uint8_t scode[] = { 0x0b, 0x77 }; /* AC3 syncword, 16 bits */

    assert(s != NULL);
    assert(data != NULL);
    
    if(dlen >= MPEG_VID_HDR_SIZE) {
	/* we have a sequence header start code? */
        if(memcmp(scode, data, sizeof(scode)) == 0) {
	    int frequencies[] = { 48000, 44100, 32000, 0 };
	    uint8_t freq_idx = 0, br_idx = 0, chans_idx = 0;

	    /* parse syncinfo section */
            data += sizeof(scode); /* point to syncinfo data, skip syncword */
	    data += MPEG_AC3_CRC_BYTES; /* skip CRC data */

	    freq_idx |= (data[0] & 0xc0) >> 6; /* two MSB */
	    br_idx |= data[0] & 0x3f; /* last six LSB */
	    /* parse bsi section */
	    data += 1; 
	    /* skip first two fields (8 bits) and go to acmod field */
            chans_idx = data[0] & 0x07;
	    
	    s->audio.sample_rate = frequencies[ freq_idx ];
	    s->audio.bit_rate = ac3_bitrates[ br_idx ];
	    s->audio.channels = ac3_nchannels[ chans_idx ];
	    s->audio.sample_size = MPEG_DEFAULT_AUD_SAMPLE_SIZE;
	    // XXX: correct?
	} else {
            mpeg_log(MPEG_LOG_ERR, "PROBE: Can't find an AC3 syncword, "
			           "probe aborted...\n");
	    ret = -MPEG_ERROR_PROBE_FAILED;
	}
    } else {
        mpeg_log(MPEG_LOG_ERR, "PROBE: not enough data to parse AC3 header\n");
	ret = -MPEG_ERROR_PROBE_FAILED;
    }
    return ret;
}

mpeg_err_t mpeg_probe_null(mpeg_stream_t *s, uint8_t *data, int dlen)
{
    return -MPEG_ERROR_PROBE_FAILED;
}

static inline void mpeg_print_video_stream_info(const mpeg_video_stream_t *mpv, FILE *f)
{
	fprintf(f, "video stream (codec = '%s') id = 0x%x\n"
		   "\twidth = %i; height = %i; asr = %i/%i;\n"
		   "\tfps = %i/%i; bitrate = %i kbps\n", 
		   mpv->codec, mpv->stream_id, 
                   mpv->width, mpv->height, mpv->aspect.num, mpv->aspect.den,
                   mpv->frame_rate.num, mpv->frame_rate.den, mpv->bit_rate);
}

static inline void mpeg_print_audio_stream_info(const mpeg_audio_stream_t *mpa, FILE *f)
{
	fprintf(f, "audio stream (codec = '%s') id = 0x%x\n"
		   "\tsample rate = %i Hz; bitrate = %i kbps\n" 
		   "\tchannels = %i; bit for sample = %i\n", 
		   mpa->codec, mpa->stream_id,
		   mpa->sample_rate, mpa->bit_rate,
		   mpa->channels, mpa->sample_size);
}

/* FIXME: expand me */
void  mpeg_print_stream_info(const mpeg_stream_t *mps, FILE *f)
{
	assert(mps != NULL);
	assert(f != NULL);

	switch(mps->stream_type) {
	case MPEG_STREAM_TYPE_VIDEO:
		mpeg_print_video_stream_info(&(mps->video), f);
		break;
	case MPEG_STREAM_TYPE_AUDIO:
		mpeg_print_audio_stream_info(&(mps->audio), f);
		break;
	default:
		fprintf(f, "(%s) unknwon type for this stream (%i)\n", 
				__FILE__, mps->stream_type);
		break;
	}
	fflush(f);
}


