/*
 * tc_lzo.h - LZO extra (meta) data used by transcode
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef TC_LZO_H
#define TC_LZO_H

#include <lzo/lzo1x.h>
#include <lzo/lzoutil.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <sys/types.h>

/* flags */
enum {
    TC_LZO_FORMAT_YV12      = 1,  /* obsolete */
    TC_LZO_FORMAT_RGB24     = 2,
    TC_LZO_FORMAT_YUY2      = 4,
    TC_LZO_NOT_COMPRESSIBLE = 8,
    TC_LZO_FORMAT_YUV420P   = 16,
};

#define TC_LZO_HDR_SIZE		16
/* 
 * bytes; sum of sizes of tc_lzo_header_t members;
 * _can_ be different from sizeof(tc_lzo_header_t)
 * because structure can be padded (even if it's unlikely
 * since it's already 32-bit and 64-bit aligned).
 * I don't like __attribute__(packed).
 */

typedef struct tc_lzo_header_t tc_lzo_header_t;
struct tc_lzo_header_t {
    uint32_t magic;
    uint32_t size;
    uint32_t flags;
    uint8_t method; /* compression method */
    uint8_t level;  /* compression level */
    uint16_t pad;
};
typedef struct tc_lzo_header_t TCLzoHeader;


#endif /* TC_LZO_H */
