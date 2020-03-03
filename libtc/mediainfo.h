/*
 *  mediaingo.h - transcode media information support.
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

#ifndef MEDIAINFO_H
#define MEDIAINFO_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "tccodecs.h"
#include "tcformats.h"

/**************************************************************************/

/* codec helpers **********************************************************/

/*
 * tc_codec_to_comment:
 *     return a short constant descriptive string given the codec identifier.
 *
 * Parameters:
 *     codec: TC_CODEC_* value to represent.
 * Return value:
 *     a constant string describing the given codec (there is no need to
 *     free() it).
 * Postconditions:
 *     Always return something sensible, even if unknown codec id was given.
 */
const char* tc_codec_to_comment(TCCodecID codec);

/*
 * tc_codec_to_string:
 *     return the codec name as a lowercase constant string,
 *     given the codec identifier.
 *
 * Parameters:
 *     codec: the TC_CODEC_* value to represent.
 * Return value:
 *     a constant string representing the given codec (there is no need to
 *     free() it).
 *     NULL if codec is (yet) unknown.
 */
const char* tc_codec_to_string(TCCodecID codec);

/*
 * tc_codec_from_string:
 *     extract codec identifier from its string representation
 *
 * Parameters:
 *     codec: string representation of codec, lowercase (name).
 * Return value:
 *     the correspinding TC_CODEC_* of given string representation,
 *     or TC_CODEC_ERROR if string is unknown or wrong.
 */
TCCodecID tc_codec_from_string(const char *codec);

/*
 * tc_codec_fourcc:
 *     extract the FOURCC code for a given codec, if exists.
 *
 * Parameters:
 *     codec: TC_CODEC_* value to get the FOURCC for.
 * Return value:
 *     a constant string representing the FOURCC for a given codec (there
 *     is no need to free() it NULL of codec's FOURCC is (yet) unknown or
 *     given codec has _not_ FOURCC (es: audio codec identifiers).
 */
const char* tc_codec_fourcc(TCCodecID codec);

/*
 * tc_codec_description:
 *     describe a codec, given its ID.
 *
 * Parameters:
 *     codec: TC_CODEC_* value to get the description for.
 *     buf: buffer provided to caller. Description will be stored here.
 *     bufsize: size of the given buffer.
 * Return value:
 *     -1 if requested codec isn't known.
 *     0  truncation error (given buffer too small).
 *     >0 no errors.
 */
int tc_codec_description(TCCodecID codec, char *buf, size_t bufsize);

/* 
 * tc_codec_is_multipass:
 *     tell if a given codec is multipass capable or not.
 *
 * Parameters:
 *     codec: TC_CODEC_* value to inquiry.
 * Return value:
 *     TC_TRUE: given codec is multipass capable.
 *     TC_FALSE: given codec is NOT multipass capable OR is not known.
 */
int tc_codec_is_multipass(TCCodecID codec);

/* format helpers **********************************************************/

/*
 * tc_format_from_string:
 *     extract format identifier from its string representation.
 *
 * Parameters:
 *     format: string representation of codec, lowercase (name).
 * Return value:
 *     the correspinding TC_FORMAT_* of given string representation,
 *     or TC_FORMAT_ERROR if string is unknown or wrong.
 */
TCFormatID tc_format_from_string(const char *codec);

/*
 * tc_format_to_string:
 *     return the format name as a lowercase constant string,
 *     given the format identifier.
 *
 * Parameters:
 *     codec: the TC_FORMAT_* value to represent.
 * Return value:
 *     a constant string representing the given codec (there is no need to
 *     free() it).
 *     NULL if codec is (yet) unknown.
 */
const char* tc_format_to_string(TCFormatID codec);

/*
 * tc_format_to_comment:
 *     return a short constant descriptive string given the format identifier.
 *
 * Parameters:
 *     codec: TC_FORMAT_* value to represent.
 * Return value:
 *     a constant string dscribing the given format (there is no need to
 *     free() it).
 * Postconditions:
 *     Always return something sensible, even if unknown codec id was given.
 */
const char* tc_format_to_comment(TCFormatID codec);

/*
 * tc_format_description:
 *     describe a format, given its ID.
 *
 * Parameters:
 *     format: TC_FORMAT_* value to get the description for.
 *     buf: buffer provided to caller. Description will be stored here.
 *     bufsize: size of the given buffer.
 * Return value:
 *     -1 if requested format isn't known.
 *     0  truncation error (given buffer too small).
 *     >0 no errors.
 */
int tc_format_description(TCFormatID format, char *buf, size_t bufsize);

/*
 * tc_magic_to_format:
 *     translate a magic value into a TCFormatID.
 *     To be used in contexts on which a magic value is used to mean
 *     a format (see comment in import/magic.h)
 *
 * Parameters:
 *     magic: magic value to convert into a TCFormatID
 * Return value:
 *     the corresponding TCFormatID or TC_FORMAT_ERROR if the magic
 *     value isn't known.
 */
int tc_magic_to_format(int magic);

/**************************************************************************/

typedef struct tccodecinfo_ TCCodecInfo;
struct tccodecinfo_ {
    TCCodecID   id;         /* a TC_CODEC_* value */
    const char  *name;      /* usually != fourcc */
    const char  *fourcc;    /* real-world fourcc */
    const char  *comment;
    int         multipass;  /* multipass capable */
    int         flags;      /* audio/video/subex... */
};

typedef struct tcformatinfo_ TCFormatInfo;
struct tcformatinfo_ {
    TCFormatID  id;            /* a TC_FORMAT_* value */
    const char  *name;    
    const char  *comment;
    int         flags;         /* audio/video/subex... */
};

typedef int (*TCCodecVisitorFn)(const TCCodecInfo *info, void *userdata);
typedef int (*TCFormatVisitorFn)(const TCFormatInfo *info, void *userdata);

int tc_codec_foreach(TCCodecVisitorFn visitor, void *userdata);
int tc_format_foreach(TCFormatVisitorFn visitor, void *userdata);


/**************************************************************************/



#ifdef __cplusplus
}
#endif

#endif  /* MEDIAINFO_H */
