/*
 *  Copyright (C) 2005 Francesco Romani <fromani@gmail.com>
 * 
 *  This Software is heavily based on tcvp's (http://tcvp.sf.net) 
 *  mpeg muxer/demuxer, which is
 *
 *  Copyright (C) 2001-2002 Michael Ahlberg, M<C3><A5>ns Rullg<C3><A5>rd
 *  Copyright (C) 2003-2004 Michael Ahlberg, M<C3><A5>ns Rullg<C3><A5>rd
 *  Copyright (C) 2005 Michael Ahlberg, M<C3><A5>ns Rullg<C3><A5>rd
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

const mpeg_stream_type_t mpeg_stream_types[] = {
    { MPEG_STREAM_VIDEO_MPEG1, MPEG_STREAM_ID_BASE_VIDEO,
      MPEG_STREAM_TYPE_VIDEO, "video/mpeg", mpeg_probe_mpvideo },
    { MPEG_STREAM_VIDEO_MPEG2, MPEG_STREAM_ID_BASE_VIDEO, 
      MPEG_STREAM_TYPE_VIDEO, "video/mpeg2", mpeg_probe_mpvideo },
    { MPEG_STREAM_AUDIO_MPEG1, MPEG_STREAM_ID_BASE_AUDIO, 
      MPEG_STREAM_TYPE_AUDIO, "audio/mpeg", mpeg_probe_mpaudio },
    { MPEG_STREAM_AUDIO_MPEG1, MPEG_STREAM_ID_BASE_AUDIO, 
      MPEG_STREAM_TYPE_AUDIO, "audio/mp2", mpeg_probe_mpaudio },
    { MPEG_STREAM_AUDIO_MPEG1, MPEG_STREAM_ID_BASE_AUDIO, 
      MPEG_STREAM_TYPE_AUDIO, "audio/mp3", mpeg_probe_mpaudio },
    { MPEG_STREAM_AUDIO_MPEG2, MPEG_STREAM_ID_BASE_AUDIO, 
      MPEG_STREAM_TYPE_AUDIO, "audio/mpeg", mpeg_probe_mpaudio },
    { MPEG_STREAM_AUDIO_AAC, MPEG_STREAM_ID_BASE_AUDIO, 
      MPEG_STREAM_TYPE_AUDIO, "audio/aac", mpeg_probe_null },
    { MPEG_STREAM_VIDEO_MPEG4, MPEG_STREAM_ID_BASE_VIDEO, 
      MPEG_STREAM_TYPE_VIDEO, "video/mpeg4", mpeg_probe_null },
    { MPEG_STREAM_VIDEO_H264, MPEG_STREAM_ID_BASE_VIDEO, 
      MPEG_STREAM_TYPE_VIDEO, "video/h264", mpeg_probe_null },
    { MPEG_STREAM_AUDIO_AC3, MPEG_STREAM_ID_BASE_PRIVATE, 
      MPEG_STREAM_TYPE_AUDIO, "audio/ac3", mpeg_probe_ac3 }
};

const mpeg_fraction_t mpeg_frame_rates[16] = {
    { 0,     0    },
    { 24000, 1001 },
    { 24,    1    },
    { 25,    1    },
    { 30000, 1001 },
    { 30,    1    },
    { 50,    1    },
    { 60000, 1001 },
    { 60,    1    },
    { 1,     1    },
    { 5,     1    },
    { 10,    1    },
    { 12,    1    },
    { 15,    1    }
};

/* 0, 0 -> invalid ASR */
const mpeg_fraction_t mpeg_aspect_ratios[16] = {
    { 0,   0   },
    { 1,   1   },
    { 4,   3   },
    { 16,  9   },
    { 221, 100 },
    /* compatibility with TC */
    { 0,   0   },
    { 0,   0   },
    { 0,   0   },
    { 4,   3   },
    { 0,   0   },
    { 0,   0   },
    { 4,   3   },
    /* it's a wild guess */
    { 4,   3   },
};

const mpeg_stream_type_t* mpeg_stream_type(char *codec)
{
    int i;

    for(i = 0; i < MPEG_STREAM_TYPES_NUM; i++) {
        if(!strcmp(codec, mpeg_stream_types[i].codec)) {
            return &mpeg_stream_types[i];
        }
    }
    
    return NULL;
}

int stream_type2codec(int st)
{
    int i;

    for(i = 0; i < MPEG_STREAM_TYPES_NUM; i++) {
        if(mpeg_stream_types[i].stream_id_content == st) {
            return i;
        }
    }

    return MPEG_ERR;
}

// FIXME: recomment and refactorize
mpeg_err_t mpeg_pes_parse_header(mpeg_pkt_t *pes, uint8_t *data, int dlen)
{
    int hdrlen = MPEG_PES_HDR_MIN_SIZE, pktlen = 0;
    uint8_t *pts = NULL, *dts = NULL;
    uint8_t c;

    assert(pes != NULL);
    assert(data != NULL);
    
    /*
     * Verify that we have enough data and the correct start code 
     * (0x00:0x00:0x01) 
     * If h != 0 we assume that caller has already done this verify
     */
    if(dlen < 4 || ((htob_32(unaligned32(data)) >> 8) != 1)) {
        return -MPEG_ERROR_BAD_FORMAT;
    }

    pes->stream_id = data[3];
    pktlen = htob_16(unaligned16(data + 4));
    if(dlen < pktlen) {
        return -MPEG_ERROR_INSUFF_MEM;
    }
    /*
     * we look for value of two MSB of byte 6 in PES data.
     * If those two bytes equals to '10' we have a lot more
     * of pes data to analyze
     */
    c = data[MPEG_PES_HDR_MIN_SIZE];
    if((c & 0xc0) == 0x80) {
        /* OK, let's boogie down */
        hdrlen = data[8] + 9;
        /* we have PTS flag set? */
        if(data[7] & 0x80){
            pts = data + 9;
        }
        /* we have DTS flag set? */
        if(data[7] & 0x40){
            dts = data + 14;
        }
    } else {
        /* shorter header */
        hdrlen = MPEG_PES_HDR_MIN_SIZE;
        /* remove stuffing? */
        while(c == 0xff) {
            c = data[++hdrlen];
        }
        if((c & 0xc0) == 0x40) {
            hdrlen += 2;
            c = data[hdrlen];
        }
        if((c & 0xe0) == 0x20) {
            pts = data + hdrlen;
            hdrlen += 4;
        }
        if((c & 0xf0) == 0x30) {
            hdrlen += 5;
        }
        hdrlen++;
    }

    pes->flags = 0;

    if(pts != NULL) {
        pes->flags |= MPEG_PKT_FLAG_PTS;
        pes->pts = (htob_16(unaligned16(pts+3)) & 0xfffe) >> 1;
        pes->pts |= (htob_16(unaligned16(pts+1)) & 0xfffe) << 14;
        pes->pts |= (uint64_t) (*pts & 0xe) << 29;
    }

    if(dts != NULL) {
        pes->flags |= MPEG_PKT_FLAG_DTS;
        pes->dts = (htob_16(unaligned16(dts+3)) & 0xfffe) >> 1;
        pes->dts |= (htob_16(unaligned16(dts+1)) & 0xfffe) << 14;
        pes->dts |= (uint64_t) (*dts & 0xe) << 29;
    }

    pes->hdrsize = (uint16_t)hdrlen;
    pes->hdr = data;
    pes->data = data + hdrlen;
    
    if(pktlen > 0) {
        pes->size = pktlen - (hdrlen - MPEG_PES_HDR_MIN_SIZE);
        /* 
         * It comprehends also padding. This is safe following the spec.
         * "[...](payload length is) the value indicated in PES_packet_length
         * minus the number of bytes between the last byte of the 
         * PES_packet_length field and the first PES_packet_data byte.
         */
    } else {
        pes->size  = 0; 
        /* this should be OK following spec, but can cause trouble */
    }
        
    if(IS_PRIVATE(pes->stream_id)) {
        pes->stream_id = *pes->data++;
        pes->data += 3;
        pes->size -= 4;
    }
    return MPEG_OK;
}

#define HDR_PACK_MIN_SIZE               10

static inline int mpeg_pes_read_pack_header(mpeg_t *MPEG, uint8_t *buf, int len)
{
    mpeg_file_t *MFILE = NULL;
    int size = 0, offset = 0, ret = 0;
    uint8_t byte = 0;

    assert(MPEG != NULL);
    assert(MPEG->MFILE != NULL);

    MFILE = MPEG->MFILE;

    if(len < HDR_PACK_MIN_SIZE) {
        MPEG->errcode = MPEG_ERROR_INSUFF_MEM;
        return MPEG_ERR;
    }
    
    ret = get_bits8(MFILE, &byte);
    if(ret != 0) {
        MPEG->errcode = MPEG_ERROR_READ;            
        return MPEG_ERR;
    }

#define SL          (buf[8])
    if((byte & 0xc0) == 0x40) {
        ret = MFILE->read(MFILE, buf+1, 1, 8);
        if(ret != 8) {
            MPEG->errcode = MPEG_ERROR_READ;
            return MPEG_ERR;
        }
        
        ret = get_bits8(MFILE, &SL);
        if(ret != 0) {
            MPEG->errcode = MPEG_ERROR_READ;
            return MPEG_ERR;
        }
    
        offset = 1 + 8 + 1; /* byte + buf + sl */
        size = (SL & 7);
    } else {
        size = 7;
    }
 
    buf[0] = byte;
    ret = MFILE->read(MFILE, buf + offset, 1, size);
    if(ret != size) {
       MPEG->errcode = MPEG_ERROR_READ;
       return MPEG_ERR;
    }
    
    return size + offset;
}

#define STARTCODE_LEN                       3

static inline mpeg_res_t mpeg_pes_find_startcode(mpeg_t *MPEG, int tries)
{
   /* zc = zero count */
   uint32_t scode, zc = 0;
   uint8_t byte;
   int ret = 0, loops = 0;

   assert(MPEG != NULL);
   assert(MPEG->MFILE != NULL);

   /* looping give us some error resilience */
   do {
       ret = get_bits8(MPEG->MFILE, &byte);
       if(ret != 0) {
           MPEG->errcode = MPEG_ERROR_READ;
           return MPEG_ERR;
       }
       scode = byte;
        
       if(scode == 0){
           zc++;
       } else if(zc >= 2 && scode == 1) {
           break;
       } else if(scode < 0) {
           MPEG->errcode = MPEG_ERROR_BAD_FORMAT;
           return MPEG_ERR;
       } else {
           zc = 0;
       }
       loops++;
    } while(!scode || loops < tries);
   
    if(tries == loops) {
        mpeg_log(MPEG_LOG_WARN, "MPEG: startcode not found in stream\n");            
    }
        
    if(zc < 2 || scode != 1) {
        MPEG->errcode = MPEG_ERROR_BAD_FORMAT;
        return MPEG_ERR;
    }
    
    /* 
     * 3 == startcode length, a clear search tooks reads only 3 bytes,
     * so it tooks exactly 3 loops
     */
    if(loops > STARTCODE_LEN) { 
        mpeg_log(MPEG_LOG_WARN, "MPEG: not-aligned startcode "
                                "(distance: %i)\n", loops - STARTCODE_LEN);
    }
    return MPEG_OK;
}


#define HDR_MIN_SIZE    (STARTCODE_LEN + 1 + 2)
#define HDR_BUF_SIZE    (HDR_MIN_SIZE + MPEG_PACK_HDR_SIZE + HDR_MIN_SIZE)

/**
 * startcode = 0x00 0x00 0x01
 * 
 * 3 bytes : startcode             \_                \ 
 * 1 byte  : stream_id             /  HDR_MIN_SIZE   |
 *                                                   |
 * if(stream_id == 0xba) // pack header              |__ HDR_BUF_SIZE
 *                                                   |   (max size, in facts)
 *     N bytes : mpeg pack header                    |
 *               (N = at most MPEG_PACK_HDR_SIZE)    |
 *                                                   |
 *     3 bytes : startcode (again)                   | 
 *     1 byte  : payload stream_id                   |
 *     2 bytes : payload PES length                  /
 *
 * 'packet begin' term here stands for 
 * startcode (3 bytes, 0x00:0x00:0x01) + stream_id code (1 byte, 0x??)
 */

// FIXME: move to inline function?
#define MPEG_HANDLE_PACKET_BEGIN(tries) \
    MPEG->errcode = mpeg_pes_find_startcode(MPEG, tries); \
    if(MPEG->errcode != MPEG_OK) { \
        return NULL; \
    } \
    hdrbuf[hdrlen++] = 0x00; \
    hdrbuf[hdrlen++] = 0x00; \
    hdrbuf[hdrlen++] = 0x01; \
    \
    ret = get_bits8(MPEG->MFILE, &stream_id); \
    if(ret != 0) { \
        MPEG->errcode = MPEG_ERROR_READ; \
        return NULL; \
    } \
    hdrbuf[hdrlen++] = stream_id;

// FIXME: reduce exit points, review buffer management
mpeg_pkt_t* mpeg_pes_read_packet(mpeg_t *MPEG, int deepscan)
{
    uint32_t peslen = 0;
    uint16_t dbyte = 0;
    uint8_t stream_id = 0; 
    /* we use it only to understand if we must deal with pack header */
    uint8_t hdrbuf[HDR_BUF_SIZE] = { 0x00, 0x00, 0x01 }; 
    /* buffer for header data (startcode, stream_id, peslen) */
    uint8_t packbuf[HDR_BUF_SIZE] = { 0x00, 0x00, 0x01 };
    /* buffer for pack header data plus _first_ header data (see above) */
    int ret = 0, hdrlen = 0, packlen = 0;
    int tries = ((deepscan == TRUE) ?MPEG_PKTS_MIN_PROBE 
                                    :MPEG_PKTS_MAX_PROBE);
    mpeg_pkt_t *pes = NULL;
    mpeg_file_t *MFILE = NULL;

    assert(MPEG != NULL);
    assert(MPEG->MFILE != NULL);

    MFILE = MPEG->MFILE;
    
    MPEG_HANDLE_PACKET_BEGIN(tries); /* updates hdrbuf and hdrlen */

    if(stream_id == MPEG_PROGRAM_END_CODE) {
        return NULL;
    }

    if(stream_id == MPEG_PACK_HEADER) {
        packbuf[3] = stream_id; // XXX: magic number
        packlen = mpeg_pes_read_pack_header(MPEG, packbuf + HDR_MIN_SIZE, 
                                              HDR_BUF_SIZE);
        packlen += hdrlen; /* startcode + stream_id already in packbuf */
        hdrlen = 0; /* reset to get ready to read another packet begin */
 
        MPEG_HANDLE_PACKET_BEGIN(1); /* updates hdrbuf and hdrlen */
        /* 
         * now this read the 'effective' portion of header of interest,
         * with 'real' stream_id byte. Old packet begin, with 
         * stream_id == MPEG_PACK_HEADER in header was previously prepended to
         * packbuf, so it holds the full pack header.
         */
    }
    
    ret = get_bits16(MPEG->MFILE, &dbyte);
    if(ret != 0) {
        MPEG->errcode = MPEG_ERROR_READ;
        return NULL;
    }
    hdrbuf[hdrlen++] = (dbyte >> 8) & 0xff;
    hdrbuf[hdrlen++] = dbyte & 0xff;
    /* this one too should be stored in hdrbuf */
    peslen = dbyte;

    pes = mpeg_pkt_new(hdrlen + packlen + peslen);
    if(pes == NULL) {
        return NULL;
    }
    
    memcpy(pes->hdr, packbuf, packlen); 
    /* first pack header data with first packet begin */
    pes->data += packlen;
    pes->hdrsize = packlen;
    memcpy(pes->data, hdrbuf, hdrlen);
    /* then 'real' packet begin data */
         
    /* 
     * Why we read <peslen> more bytes here? 
     * So far we have read  3 + 1 + 4 = MPEG_PES_HDR_MIN_SIZE bytes. 
     * And we must read the rest of the packet, so rest of the 
     * header + payload lenght. From spec:
     * "[...](payload length is) the value indicated in PES_packet_length
     * minus the number of bytes between the last byte of the 
     * PES_packet_length field and the first PES_packet_data byte.
     * So: 
     * pes->size (payload size) = peslen - (hdrlen - MPEG_PES_HDR_MIN_SIZE)
     * or:
     * peslen = pes->size + (hdrlen - MPEG_PES_HDR_MIN_SIZE)
     * Conclusion: reading more <pktlen> bytes from here correctly reads the
     * full remaining part of the packet.
     */
    ret = MFILE->read(MFILE, pes->data + hdrlen, 1, peslen);
    /* 
     * mpeg_pes_parse_header(), below, wants to parse a buffer
     * starting with full header data (startcode...)
     */
    if(ret < peslen) {
        mpeg_pkt_del(pes);
        MPEG->errcode = MPEG_ERROR_READ;
        return NULL;
    }

    if((IS_VIDEO(stream_id) || IS_AUDIO(stream_id)) ||
      IS_PRIVATE(stream_id)) {
        MPEG->errcode = mpeg_pes_parse_header(pes, pes->data, peslen);
        /* 
         * setup all mpeg_pkt_t fields of interest. 
         * Sets pes->stream_id too 
         */
           
        if(MPEG->errcode != MPEG_OK) {
            mpeg_pkt_del(pes);
            return NULL;
        }
    }
    return pes;
}

int mpeg_parse_descriptor(mpeg_stream_t *s, uint8_t *data, int dlen)
{
    int tag, len, n;

    if(dlen < 4) {
        return MPEG_ERROR_INSUFF_MEM;
    }
    tag = data[0];
    len = data[1];
    
    switch(tag){
    case MPEG_VIDEO_STREAM_DESCRIPTOR:
        if(s->stream_type != MPEG_STREAM_TYPE_VIDEO) {
            mpeg_log(MPEG_LOG_WARN, "MPEG: Video stream descriptor "
                        "for non-video stream\n");
            break;
        }
        s->video.frame_rate = mpeg_frame_rates[(data[2] >> 3) & 0xf];
#ifdef DEBUG
        if(data[2] & 0x4) {
            mpeg_log(MPEG_LOG_INFO, "MPEG: MPEG 1 only\n");
        }
        if(data[2] & 0x2) {
            mpeg_log(MPEG_LOG_INFO, "MPEG: constrained parameter\n");
        }
        if(!(data[2] & 0x4)) {
            mpeg_log(MPEG_LOG_INFO, "MPEG: esc %i profile %i, level %i\n",
                data[3] >> 7, (data[3] >> 4) & 0x7, data[3] & 0xf);
        }
#endif
        break;

   case MPEG_AUDIO_STREAM_DESCRIPTOR:
        break;

    case MPEG_TARGET_BACKGROUND_GRID_DESCRIPTOR:
        n = htob_32(unaligned32(data + 2));

        if(s->stream_type != MPEG_STREAM_TYPE_VIDEO) {
            mpeg_log(MPEG_LOG_WARN, "MPEG: Target background grid descriptor"
                        " for non-video stream\n");
            break;
        }

        s->video.width = (n >> 18) & 0x3fff;
        s->video.height = (n >> 4) & 0x3fff;

        n &= 0xf;
        if(n == 1) {
            s->video.aspect.num = s->video.width;
            s->video.aspect.den = s->video.height;
            mpeg_fraction_reduce(&s->video.aspect);
        } else if(mpeg_aspect_ratios[n].num) {
            s->video.aspect = mpeg_aspect_ratios[n];
        }
        break;
    
    case MPEG_ISO_639_LANGUAGE_DESCRIPTOR:
        break;
    }

    return len + 2;
}

mpeg_pkt_t* mpeg_pkt_new(size_t size)
{
    mpeg_pkt_t *pes = NULL;
    void *p = NULL;
    
    assert(size > 0);

    /* note this */
    p = mpeg_mallocz(sizeof(mpeg_pkt_t) + size);
    pes = p;
    p += sizeof(mpeg_pkt_t);
    
    /* default is to have only opaque payload */
    pes->hdr = p;
    pes->data = p;
    pes->hdrsize = 0;
    pes->size = size;

    return pes;
}

void mpeg_pkt_del(const mpeg_pkt_t *p)
{
    mpeg_pkt_t *pes = (mpeg_pkt_t*)p;
    assert(pes != NULL);

    mpeg_free(pes);
}

mpeg_err_t mpeg_get_last_error(mpeg_t *MPEG)
{
    assert(MPEG != NULL);
    return MPEG->errcode;
}

int mpeg_get_stream_number(mpeg_t *MPEG)
{
    assert(MPEG != NULL);
    return MPEG->n_streams;
}

const mpeg_stream_t* mpeg_get_stream_info(mpeg_t *MPEG, int stream_num)
{
    assert(MPEG != NULL);

    if(stream_num < 0 || stream_num > MPEG->n_streams) {
        MPEG->errcode = MPEG_ERROR_BAD_REF;         
        return NULL;
    }
    return &(MPEG->streams[MPEG->smap[stream_num]]);
}

const mpeg_pkt_t* mpeg_read_packet(mpeg_t *MPEG, int stream_id)
{
    assert(MPEG != NULL);

    return MPEG->read_packet(MPEG, stream_id);  
}

mpeg_res_t mpeg_probe(mpeg_t *MPEG)
{
    int64_t pos = 0;
    mpeg_res_t ret = MPEG_OK;

    assert(MPEG != NULL);
    assert(MPEG->MFILE != NULL);
   
    pos = MPEG->MFILE->tell(MPEG->MFILE);
    if(pos == -1) {
        MPEG->errcode = MPEG_ERROR_IO;
        return MPEG_ERR;
    }

    ret = MPEG->MFILE->seek(MPEG->MFILE, 0, SEEK_SET);
    if(ret == -1) {
        MPEG->errcode = MPEG_ERROR_SEEK;
        return MPEG_ERR;
    }
    
    ret = MPEG->probe(MPEG);
    if(ret == MPEG_ERROR_PROBE_FAILED) {
        MPEG->errcode = ret;
        return MPEG_ERR;
    }
    
    ret = MPEG->MFILE->seek(MPEG->MFILE, pos, SEEK_SET);
    if(ret == -1) {
        MPEG->errcode = MPEG_ERROR_SEEK;
        return MPEG_ERR;
    }
    return MPEG_OK;
}

mpeg_t* mpeg_open(int type, mpeg_file_t *MFILE, uint32_t flags, int *errcode)
{
    mpeg_t *MPEG = mpeg_mallocz(sizeof(mpeg_t));
    mpeg_err_t err = MPEG_ERROR_NONE;
    
    switch(type) {
    case MPEG_TYPE_ES:
        err = mpeg_es_open(MPEG, MFILE, flags);
        break;
    case MPEG_TYPE_PS:
        err = mpeg_ps_open(MPEG, MFILE, flags);
        break;
    case MPEG_TYPE_ANY:
        mpeg_log(MPEG_LOG_INFO, "MPEG: trying with PS format...\n");
        err = mpeg_ps_open(MPEG, MFILE, flags);
        if(err != MPEG_OK && (MPEG->errcode == MPEG_ERROR_BAD_FORMAT ||
           MPEG->errcode  == MPEG_ERROR_PROBE_FAILED)) {
                    mpeg_log(MPEG_LOG_INFO, "MPEG: trying with ES format...\n");
            err = mpeg_es_open(MPEG, MFILE, flags);
        }
        break;
    case MPEG_TYPE_NONE: /* fallthrough */
    default:
        mpeg_log(MPEG_LOG_ERR, "MPEG: bad type in mpeg_open()\n");
        err = MPEG_ERROR_UNK_FORMAT;
        break;
    }
    
    if(err != MPEG_ERROR_NONE) {
        mpeg_log(MPEG_LOG_ERR, 
                "MPEG: mpeg_open() internal failure\n");
        if(errcode != NULL) {
            *errcode = err;
        }

        mpeg_free(MPEG);
        MPEG = NULL;
    }
    return MPEG;
}

mpeg_res_t mpeg_close(mpeg_t *MPEG)
{
    mpeg_res_t ret = MPEG_OK;
    assert(MPEG != NULL);

    ret = MPEG->close(MPEG);
    
    mpeg_free(MPEG);
    return ret;
}


