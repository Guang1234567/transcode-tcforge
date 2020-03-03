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

#ifndef _MPEG_PRIVATE_H
#define _MPEG_PRIVATE_H

#define _ISOC99_SOURCE 1
#define _GNU_SOURCE 1
#define _REENTRANT 1
#define _LARGEFILE_SOURCE 1
#define _LARGEFILE64_SOURCE 1
#define _FILE_OFFSET_BITS 64 

#include <assert.h>

/*
 * Yeah, almost arbitrary. Should be fixed in future.
 */
#define MPEG_COUNTER_VIDEO                              1
#define MPEG_COUNTER_AUDIO                              2
#define MPEG_COUNTER_AC3                                9

#define MPEG_PKTS_START_MAX                             16U
#define MPEG_PKTS_MIN_PROBE                             4U
#define MPEG_PKTS_MAX_PROBE                             256U
#define MPEG_STREAMS_NUM_BASE                           4U

#define DVD_PESID                                       0xfc

#define MPEG_PACK_HEADER                                0xba
#define MPEG_SYSTEM_HEADER                              0xbb
#define MPEG_SEQUENCE_HEADER                            0xb3

#define MPEG_PROGRAM_END_CODE                           0xb9
#define MPEG_PROGRAM_STREAM_MAP                         0xbc
#define MPEG_PRIVATE_STREAM_1                           0xbd
#define MPEG_PRIVATE_STREAM_2                           0xbf

#define MPEG_PADDING_STREAM                             0xbe

#define MPEG_ECM_STREAM                                 0xf0
#define MPEG_EMM_STREAM                                 0xf1
#define MPEG_DSMCC_STREAM                               0xf2
#define MPEG_ISO_13522_STREAM                           0xf3
#define MPEG_H222_A_STREAM                              0xf4
#define MPEG_H222_B_STREAM                              0xf5
#define MPEG_H222_C_STREAM                              0xf6
#define MPEG_H222_D_STREAM                              0xf7
#define MPEG_H222_E_STREAM                              0xf8
#define MPEG_ANCILLARY_STREAM                           0xf9
#define MPEG_ISO_14496_SL_STREAM                        0xfa
#define MPEG_ISO_14496_FLEXMUX_STREAM                   0xfb
#define MPEG_PROGRAM_STREAM_DIRECTORY                   0xff

#define MPEG_VIDEO_STREAM_DESCRIPTOR                     2
#define MPEG_AUDIO_STREAM_DESCRIPTOR                     3
#define MPEG_HIERARCHY_DESCRIPTOR                        4
#define MPEG_REGISTRATION_DESCRIPTOR                     5
#define MPEG_DATA_STREAM_ALIGNMENT_DESCRIPTOR            6
#define MPEG_TARGET_BACKGROUND_GRID_DESCRIPTOR           7
#define MPEG_VIDEO_WINDOW_DESCRIPTOR                     8
#define MPEG_CA_DESCRIPTOR                               9
#define MPEG_ISO_639_LANGUAGE_DESCRIPTOR                10
#define MPEG_SYSTEM_CLOCK_DESCRIPTOR                    11
#define MPEG_MULTIPLEX_BUFFER_UTILIZATION_DESCRIPTOR    12
#define MPEG_COPYRIGHT_DESCRIPTOR                       13
#define MPEG_MAXIMUM_BITRATE_DESCRIPTOR                 14
#define MPEG_PRIVATE_DATA_INDICATOR_DESCRIPTOR          15
#define MPEG_SMOOTHING_BUFFER_DESCRIPTOR                16
#define MPEG_STD_DESCRIPTOR                             17
#define MPEG_IBP_DESCRIPTOR                             18

#define MPEG_PKT_TYPE_DATA                              0
#define MPEG_PKT_TYPE_FLUSH                             1
#define MPEG_PKT_TYPE_STILL                             2

#define MPEG_PKT_FLAG_PTS                               0x1
#define MPEG_PKT_FLAG_DTS                               0x2
#define MPEG_PKT_FLAG_KEY                               0x4

#define MPEG_STREAM_FLAG_INTERLACED                     0x1

#define MPEG_PES_HDR_MIN_SIZE                           6

#define MPEG_STREAM_TYPES_NUM                           10
/* WARNING: keep in sync with structure size in mpeg-core.c */

#define IS_MPVIDEO(id) ((id & 0xf0) == 0xe0)
#define IS_MPAUDIO(id) ((id & 0xe0) == 0xc0)
#define IS_AC3(id) ((id & 0xf8) == 0x80)
#define IS_DTS(id) ((id & 0xf8) == 0x88)
#define IS_LPCM(id) ((id & 0xf8) == 0xa0)
#define IS_SPU(id) ((id & 0xe0) == 0x20)

#define IS_VIDEO(id) (IS_MPVIDEO(id))
#define IS_AUDIO(id) \
	((IS_MPAUDIO(id) || (IS_AC3(id) || IS_DTS(id))) || IS_LPCM(id))
#define IS_PRIVATE(id) \
	((id == MPEG_PRIVATE_STREAM_1) || (id == MPEG_PRIVATE_STREAM_2))

#ifndef HAVE_BYTESWAP
#define bswap_8(x)  (x)

#define bswap_16(x) (((x & 0xff00) >> 8) | ((x & 0x00ff) << 8))

#define bswap_32(x) (((x & 0xff000000) >> 24) | \
             ((x & 0x00ff0000) >> 8) | \
             ((x & 0x0000ff00) << 8) | \
             ((x & 0x000000ff) << 24))

#define bswap_64(x) (((x & 0xff00000000000000ULL) >> 56) | \
             ((x & 0x00ff000000000000ULL) >> 40) | \
             ((x & 0x0000ff0000000000ULL) >> 24) | \
             ((x & 0x000000ff00000000ULL) >> 8)  | \
             ((x & 0x00000000ff000000ULL) << 8)  | \
             ((x & 0x0000000000ff0000ULL) << 24) | \
             ((x & 0x000000000000ff00ULL) << 40) | \
             ((x & 0x00000000000000ffULL) << 56))
#endif

#if (!defined MPEG_BIG_ENDIAN && !defined MPEG_LITTLE_ENDIAN)
#error "you must define either MPEG_LITTLE_ENDIAN or MPEG_BIG_ENDIAN"
#endif

#if (defined MPEG_BIG_ENDIAN && defined MPEG_LITTLE_ENDIAN)
#error "you CAN'T define BOTH MPEG_LITTLE_ENDIAN and MPEG_BIG_ENDIAN"
#endif

#if defined MPEG_BIG_ENDIAN
#define htol_8(x)  (x)
#define htol_16(x) (bswap_16(x))
#define htol_32(x) (bswap_32(x))
#define htol_64(x) (bswap_64(x))

#define htob_8(x)  (x)
#define htob_16(x) (x)
#define htob_32(x) (x)
#define htob_64(x) (x)

#elif defined MPEG_LITTLE_ENDIAN

#define htol_8(x)  (x)
#define htol_16(x) (x)
#define htol_32(x) (x)
#define htol_64(x) (x)

#define htob_8(x)  (x)
#define htob_16(x) (bswap_16(x))
#define htob_32(x) (bswap_32(x))
#define htob_64(x) (bswap_64(x))

#endif

void mpeg_fraction_reduce(mpeg_fraction_t *f);

uint32_t mpeg_crc32(const uint8_t *data, int len);

#define mpeg_malloc(size) \
    _mpeg_real_malloc(__FILE__, __LINE__, size)
#define mpeg_mallocz(size) \
    _mpeg_real_mallocv(__FILE__, __LINE__, size, 0x00)
#define mpeg_mallocf(size, val) \
    _mpeg_real_mallocv(__FILE__, __LINE__, size, val)

void *_mpeg_real_malloc(const char *where, int at, size_t size);
void *_mpeg_real_mallocv(const char *where, int at, size_t size, uint8_t val);
void mpeg_free(void *ptr);

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

#define getuint(s) \
static inline int \
get_bits##s(mpeg_file_t *f, uint##s##_t *v) { \
    assert(f != NULL); \
    if(1 == f->read(f, v, sizeof(uint##s##_t), 1)) { \
        *v = htob_##s(*v); \
        return 0; \
    } \
    mpeg_log(MPEG_LOG_ERR, "Can't read %i bits " \
        "from file (%sEOF)\n", \
            sizeof(uint##s##_t) * 8, \
            f->eof_reached(f) ?"" :"not "); \
    return -1; \
}
                   
getuint(8)
getuint(16)
getuint(32)
getuint(64)

#ifdef __GNUC__
#define _unaligned(s) \
struct _unaligned##s { \
    uint##s##_t i; \
} __attribute__((packed)); \
 \
static inline uint##s##_t \
unaligned##s(const void *v) { \
    return ((const struct _unaligned##s *) v)->i; \
} \
 \
static inline void \
st_unaligned##s(uint##s##_t v, void *d) { \
    ((struct _unaligned##s *) d)->i = v; \
}
#else
#include <string.h>             
#define _unaligned(s) \
static inline uint##s##_t \
unaligned##s(const void *v) { \
    uint##s##_t i; \
    memcpy(&i, v, sizeof(i)); \
    return i; \
} \
 \
static inline void \
st_unaligned##s(uint##s##_t v, void *d) { \
    memcpy(d, &v, sizeof(v)); \
}
#endif

_unaligned(16)
_unaligned(32)
_unaligned(64)

typedef mpeg_err_t (*mpeg_probe_fn_t)(mpeg_stream_t *s, uint8_t *data, int dlen);
    
typedef struct mpeg_stream_type mpeg_stream_type_t;
struct mpeg_stream_type {
    int stream_id_content;
    int stream_id_base;
    int stream_type;
    char *codec;
    mpeg_probe_fn_t probe;
};

/* mpeg-core.c */
extern const mpeg_stream_type_t mpeg_stream_types[];
extern const mpeg_fraction_t mpeg_frame_rates[];
extern const mpeg_fraction_t mpeg_aspect_ratios[];

const mpeg_stream_type_t* mpeg_stream_type(char *codec);
int stream_type2codec(int st);

mpeg_err_t mpeg_pes_parse_header(mpeg_pkt_t *pes, uint8_t *data, int dlen);

int mpeg_parse_descriptor(mpeg_stream_t *s, uint8_t *data, int dlen);

mpeg_pkt_t *mpeg_pes_read_packet(mpeg_t *MPEG, int deepscan);

/* mpeg-probe.c */
mpeg_err_t mpeg_probe_mpvideo(mpeg_stream_t *s, uint8_t *data, int dlen);
mpeg_err_t mpeg_probe_mpaudio(mpeg_stream_t *s, uint8_t *data, int dlen);
mpeg_err_t mpeg_probe_ac3(mpeg_stream_t *s, uint8_t *data, int dlen);
mpeg_err_t mpeg_probe_null(mpeg_stream_t *s, uint8_t *data, int dlen);

/* mpeg-es.c */
mpeg_res_t mpeg_es_open(mpeg_t *MPEG, mpeg_file_t *MFILE, uint32_t flags);
/* mpeg-ps.c */
mpeg_res_t mpeg_ps_open(mpeg_t *MPEG, mpeg_file_t *MFILE, uint32_t flags);

#endif /* _MPEG_PRIVATE_H */
