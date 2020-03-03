/*
 * tccodecs.c -- codecs helper functions.
 * (C) 2005-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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


#include "libtc.h"
#include "tccodecs.h"

#include "tccore/tc_defaults.h"

#include <string.h>

/* internal usage only ***************************************************/

/*
 * this table is *always* accessed in RO mode, so there is no need
 * to protect it with threading locks
 */
static const TCCodecInfo tc_codecs_info[] = {
    /* pixel formats */
    { TC_CODEC_RGB24,      "rgb24",       "RGB",
                           "RGB/BGR",                0, TC_VIDEO },
    { TC_CODEC_YUV420P,    "yuv420p",     "I420",
                           "YUV420P",                0, TC_VIDEO },
    { TC_CODEC_YUV422P,    "yuv422p",     "UYVY",
                           "YUV422P",                0, TC_VIDEO },
    { TC_CODEC_YUY2,       "yuy2",        "YUY2",
                           "YUY2",                   0, TC_VIDEO },
    /* video codecs */
    // XXX: right fcc?
    { TC_CODEC_MPEG1VIDEO, "mpeg1video",  "mpg1",
                           "MPEG1 ES",               1, TC_VIDEO },
    { TC_CODEC_MPEG2VIDEO, "mpeg2video",  "mpg2",
                           "MPEG2 ES",               1, TC_VIDEO },
    { TC_CODEC_MPEG4VIDEO, "mpeg4video",  "mp4v", 
                           "MPEG4 ES",               1, TC_VIDEO },
    /* FIXME; set up `DIVX' fcc for backward compatibility? */ 
    { TC_CODEC_XVID,       "xvid",        "XVID",
                           "XviD",                   1, TC_VIDEO },
    { TC_CODEC_DIVX3,      "divx3",       "DIV3",
                           "DivX;-)",                1, TC_VIDEO },
    { TC_CODEC_DIVX4,      "divx4",       "DIVX",
                           "DivX 4.x",               1, TC_VIDEO },
    { TC_CODEC_DIVX5,      "divx5",       "DX50",
                           "DivX 5.x",               1, TC_VIDEO },
    { TC_CODEC_MJPEG,      "mjpeg",       "MJPG",
                           "MJPEG",                  0, TC_VIDEO },
    { TC_CODEC_LJPEG,      "ljpeg",       "LJPG",
                           "Lossless (motion) JPEG", 0, TC_VIDEO },
    { TC_CODEC_DV,         "dvvideo",     "DVSD",
                           "DigitalVideo",           0, TC_VIDEO },
    { TC_CODEC_LZO1,       "lzo1",        "LZO1",
                           "LZO v1",                 0, TC_VIDEO },
    { TC_CODEC_LZO2,       "lzo2",        "LZO2",
                           "LZO v2",                 0, TC_VIDEO },
    { TC_CODEC_MP42,       "msmpeg4v2",   "MP42",
                           "MS MPEG4 v2",            1, TC_VIDEO },
    { TC_CODEC_MP43,       "msmpeg4v3",   "MP43",
                           "MS MPEG4 v3",            1, TC_VIDEO },
    { TC_CODEC_RV10,       "realvideo10", "RV10",
                           "RealVideo (old)",        0, TC_VIDEO },
    { TC_CODEC_WMV1,       "wmv1",        "WMV1",
                           "WMV v1 (WMP7)",          1, TC_VIDEO },
    { TC_CODEC_WMV2,       "wmv2",        "WMV2",
                           "WMV v2 (WMP8)",          1, TC_VIDEO },
    { TC_CODEC_H264,       "h264",        "H264",
                           "h.264 (AVC)",            1, TC_VIDEO },
    { TC_CODEC_H263P,      "h263p",       "H263",
                           "h.263 plus",             1, TC_VIDEO },
    // XXX: right fcc?
    { TC_CODEC_H263I,      "h263",        "H263",
                           "h.263",                  0, TC_VIDEO },

    { TC_CODEC_HUFFYUV,    "huffyuv",     "HFYU",
                           "HuffYUV",                1, TC_VIDEO },
    { TC_CODEC_THEORA,     "theora",      "THER",
                           "xiph theora (VP3)",      1, TC_VIDEO },
    // XXX: right fcc?
    { TC_CODEC_FFV1,       "ffv1",        "FFV1",
                           "FFV1 (experimental)",    1, TC_VIDEO },
    { TC_CODEC_ASV1,       "asusvideo1",  "ASV1",
                           "ASUS codec v1",          0, TC_VIDEO },
    { TC_CODEC_ASV2,       "asusvideo2",  "ASV2",
                           "ASUS codec v2",          0, TC_VIDEO },
    { TC_CODEC_PV3,        "pv3",         "PV3",
                           "PV3",                    0, TC_VIDEO }, /* XXX */
    { TC_CODEC_NUV,        "nuv",         "NUV",
                           "RTjpeg",                 0, TC_VIDEO }, /* XXX */
    /* image formats (for ImageMagick) */
    { TC_CODEC_JPEG,       "jpeg",        NULL,
                           "JPEG image",             0, TC_VIDEO },
    { TC_CODEC_TIFF,       "tiff",        NULL,
                           "TIFF image",             0, TC_VIDEO },
    { TC_CODEC_PNG,        "png",         NULL,
                           "PNG image",              0, TC_VIDEO },
    { TC_CODEC_PPM,        "ppm",         NULL,
                           "PPM image",              0, TC_VIDEO },
    { TC_CODEC_PGM,        "pgm",         NULL,
                           "PGM image",              0, TC_VIDEO },
    { TC_CODEC_GIF,        "gif",         NULL,
                           "GIF89 image",            0, TC_VIDEO },

    /* FIXME: add more codec informations, on demand */

    /* audio codecs */
    { TC_CODEC_PCM,        "pcm",         NULL, 
                           "PCM",                    0, TC_AUDIO },
    { TC_CODEC_LPCM,       "lpcm",        NULL,
                           "LPCM",                   0, TC_AUDIO },
    { TC_CODEC_ULAW,       "u-Law",       NULL,
                           "ULAW",                   0, TC_AUDIO },
    { TC_CODEC_AC3,        "ac3",         NULL,
                           "AC3",                    0, TC_AUDIO },
    { TC_CODEC_MP3,        "mp3",         NULL,
                           "MPEG ES Layer 3",        0, TC_AUDIO },
    { TC_CODEC_MP2,        "mp2",         NULL,
                           "MPEG ES Layer 2",        0, TC_AUDIO },
    { TC_CODEC_AAC,        "aac",         NULL,
                           "AAC",                    0, TC_AUDIO },
    { TC_CODEC_VORBIS,     "vorbis",      "VRBS",
                           "xiph vorbis",             0, TC_AUDIO },
    { TC_CODEC_FLAC,       "flac",        "FLAC",
                           "xiph flac",              0, TC_AUDIO },
    { TC_CODEC_SPEEX,      "speex",       "SPEX",
                           "xiph speex",             0, TC_AUDIO },
    { TC_CODEC_VAG,        "vag",         NULL,
                           "PS-VAG",                 0, TC_AUDIO },
    /* FIXME: add more codec informations, on demand */

    /* miscelanous; XXX: drop from here */
    { TC_CODEC_MPEG,       "MPEG",        NULL,
                           "MPEG program stream",    0, TC_VIDEO|TC_AUDIO },
    { TC_CODEC_MPEG1,      "MPEG-1",      NULL,
                           "MPEG 1 program stream",  0, TC_VIDEO|TC_AUDIO },
    { TC_CODEC_MPEG2,      "MPEG-2",      NULL, 
                           "MPEG 2 program stream",  0, TC_VIDEO|TC_AUDIO },

    /* special codecs*/
    { TC_CODEC_ANY,        "everything",  NULL, 
                           NULL,                     0, 0 },
    { TC_CODEC_UNKNOWN,    "unknown",     NULL,
                           NULL,                     0, 0 },
    { TC_CODEC_ERROR,      "error",       NULL,
                           NULL,                     0, 0 }, // XXX
    /* this MUST be the last one */
};

/*
 * TCCodecMatcher:
 *      generic codec finder function family.
 *      tell if a TCCodecInfo descriptor match certains given criterias
 *      using a function-dependent method.
 *      See also 'find_tc_codec' function.
 *
 * Parameters:
 *      info: a pointer to a TCCodecInfo descriptor to be examinated
 *  userdata: a pointer to data with function-dependent meaning
 * Return Value:
 *      TC_TRUE if function succeed,
 *      TC_FALSE otherwise.
 */
typedef int (*TCCodecMatcher)(const TCCodecInfo *info, const void *userdata);

/*
 * id_matcher:
 *      match a TCCodecInfo descriptor on codec's id.
 *      'userdata' must be an *address* of an uint32_t containing a TC_CODEC_*
 *      to match.
 *
 * Parameters:
 *      as for TCCodecMatcher
 * Return Value:
 *      as for TCCodecMatcher
 */
static int id_matcher(const TCCodecInfo *info, const void *userdata)
{
    if (info == NULL || userdata == NULL) {
        return TC_FALSE;
    }

    return (*(int*)userdata == info->id) ?TC_TRUE :TC_FALSE;
}

/*
 * name_matcher:
 *      match a TCCodecInfo descriptor on codec's name (note: note != fourcc).
 *      'userdata' must be the C-string to match.
 *      Note: ignore case.
 *
 * Parameters:
 *      as for TCCodecMatcher
 * Return Value:
 *      as for TCCodecMatcher
 */
static int name_matcher(const TCCodecInfo *info, const void *userdata)
{
    if (info == NULL || userdata == NULL) {
        return TC_FALSE;
    }
    if(!info->name || (strcasecmp(info->name, userdata) != 0)) {
        return TC_FALSE;
    }
    return TC_TRUE;
}

/*
 * find_tc_codec:
 *      find a TCCodecInfo descriptor matching certains given criterias.
 *      It scans the whole TCCodecInfos table applying the given
 *      matcher with the given data to each element, halting when a match
 *      is found
 *
 * Parameters:
 *      matcher: a TCCodecMatcher to be applied to find the descriptor.
 *     userdata: matching data to be passed to matcher together with a table
 *               entry.
 *
 * Return Value:
 *      >= 0: index of an entry in TCCodecInfo in table if an entry match
 *            the finding criteria
 *      TC_NULL_MATCH if no entry matches the given criteria
 */
static int find_tc_codec(const TCCodecInfo *infos,
                         TCCodecMatcher matcher,
                         const void *userdata)
{
    int found = TC_FALSE, i = 0;

    if (infos == NULL) {
        return TC_NULL_MATCH;
    }

    for (i = 0; infos[i].id != TC_CODEC_ERROR; i++) {
        found = matcher(&infos[i], userdata);
        if (found) {
            break;
        }
    }
    if (!found) {
        i = TC_NULL_MATCH;
    }

    return i;
}

/* public API ************************************************************/

const char* tc_codec_to_comment(TCCodecID codec)
{
    int idx = find_tc_codec(tc_codecs_info, id_matcher, &codec);

    if (idx == TC_NULL_MATCH) { /* not found */
        return "unknown";
    }
    return tc_codecs_info[idx].comment; /* can be NULL */
}


const char* tc_codec_to_string(TCCodecID codec)
{
    int idx = find_tc_codec(tc_codecs_info, id_matcher, &codec);

    if (idx == TC_NULL_MATCH) { /* not found */
        return NULL;
    }
    return tc_codecs_info[idx].name; /* can be NULL */
}

TCCodecID tc_codec_from_string(const char *codec)
{
    int idx = find_tc_codec(tc_codecs_info, name_matcher, codec);

    if (idx == TC_NULL_MATCH) { /* not found */
        return TC_CODEC_ERROR;
    }
    return tc_codecs_info[idx].id;
}

const char* tc_codec_fourcc(TCCodecID codec)
{
    int idx = find_tc_codec(tc_codecs_info, id_matcher, &codec);

    if (idx == TC_NULL_MATCH) { /* not found */
        return NULL;
    }
    return tc_codecs_info[idx].fourcc; /* can be NULL */
}

int tc_codec_description(TCCodecID codec, char *buf, size_t bufsize)
{
    int idx = find_tc_codec(tc_codecs_info, id_matcher, &codec);
    int ret;

    if (idx == TC_NULL_MATCH) { /* not found */
        return -1;
    }

    ret = tc_snprintf(buf, bufsize, "%-12s: (fourcc=%s multipass=%-3s) %s",
                      tc_codecs_info[idx].name,
                      tc_codecs_info[idx].fourcc,
                      tc_codecs_info[idx].multipass ?"yes" :"no",
                      tc_codecs_info[idx].comment);
    return ret; // FIXME
}

int tc_codec_is_multipass(TCCodecID codec)
{
    int idx = find_tc_codec(tc_codecs_info, id_matcher, &codec);

    if (idx == TC_NULL_MATCH) { /* not found */
        return TC_FALSE;
    }
    return tc_codecs_info[idx].multipass;
}

/*************************************************************************/

int tc_codec_foreach(TCCodecVisitorFn visitor, void *userdata)
{
    const TCCodecInfo *info = tc_codecs_info;
    int i, ret = TC_TRUE;

    for (i = 0; ret == TC_TRUE && info[i].id != TC_CODEC_ERROR; i++) {
        ret = visitor(&(info[i]), userdata);
    }
    return i;
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
