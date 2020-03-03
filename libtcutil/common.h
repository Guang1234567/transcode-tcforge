/*
 *  libtc.h - include file for utilities library for transcode
 *
 *  Copyright (C) Thomas Oestreich - August 2003
 *  Copyright (C) Transcode Team - 2005-2010
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

#ifndef COMMON_H
#define COMMON_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* 
 * this header is ONLY FOR INTERNAL USAGE.
 * DO NOT INCLUDE IT DIRECTLY!
 */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TC_NULL_MATCH = -1, /* kind of boolean... */
    TC_FALSE      =  0,
    TC_TRUE       =  1
};

enum {
    TC_BUF_MAX  = 1024,
    TC_BUF_LINE =  256,
    TC_BUF_MIN  =  128
};

#define TC_MAX(a, b)		(((a) > (b)) ?(a) :(b))
#define TC_MIN(a, b)		(((a) < (b)) ?(a) :(b))
/* clamp x between a and b */
#define TC_CLAMP(x, a, b)	TC_MIN(TC_MAX((a), (x)), (b))

/* 
 * Made to be compatible with 
 *      TC_IMPORT_{OK,ERROR,UNKNOWN}
 *      TC_EXPORT_{OK,ERROR,UNKNOWN}
 * see src/transcode.h
 */
typedef enum {
    TC_ERROR     = -1,
    TC_OK        =  0,
    TC_INTERRUPT =  1,
    TC_UNKNOWN, /* MUST always be the last one */
} TCReturnCode;


 #ifdef __cplusplus
}
#endif

#endif  /* COMMON_H */
