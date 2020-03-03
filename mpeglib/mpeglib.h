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

#ifndef _MPEGLIB_H
#define _MPEGLIB_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>

typedef enum mpeg_bool_e mpeg_bool_t;
enum mpeg_bool_e {
    FALSE = 0,
    TRUE = 1
};

/* logging levels */
typedef enum mpeg_log_level_e mpeg_log_level_t;
enum mpeg_log_level_e {
    MPEG_LOG_ERR = 0,  /* related operation is much likely failed */
    MPEG_LOG_WARN = 1, /* unexpected behaviour */
    MPEG_LOG_INFO      /* an informative message */
};

/*
 * Error reporting:
 * When a function has an MPEG context in his parameters
 * and fails for some reason, shall update MPEG->errcode
 * and return MPEG_ERR (or NULL).
 * Direct logging is discouraged, but accepted in 
 * exceptional or critical situations.
 * Otherwise, if no context avalaible, a function shall
 * return the exact error code:
 * return -MPEG_ERROR_SOMETHING
 */
typedef enum mpeg_res_e mpeg_res_t;
enum mpeg_res_e {
    MPEG_ERR = -1,
    MPEG_OK = 0
};

/* 
 * aliasing with above two macros is desired, but
 * should be changed in future releases
 */
typedef enum mpeg_err_e mpeg_err_t;
enum mpeg_err_e {
    MPEG_ERROR_NONE = 0,
    MPEG_ERROR_GENERIC = 1,
    /* memory-related errors */
    MPEG_ERROR_NO_MEM = 2,
    MPEG_ERROR_BAD_REF,
    MPEG_ERROR_INSUFF_MEM,
    /* I/O-related errors */
    MPEG_ERROR_IO = 64,
    MPEG_ERROR_READ,
    MPEG_ERROR_WRITE,
    MPEG_ERROR_SEEK,
    /* MPEG-related errors */
    MPEG_ERROR_UNK_FORMAT = 128,
    MPEG_ERROR_BAD_FORMAT,
    MPEG_ERROR_PROBE_AGAIN, /* TEMPORARY failure */
    MPEG_ERROR_PROBE_FAILED
};
                               
/* opening flags */
#define MPEG_FLAG_PROBE                             (1U<<1) 
                                             /* probe file at opening */
#define MPEG_FLAG_TCORDER                           (1U<<2)
                           /* 
                            * provide an order of stream which is compatible 
                            * with transcode (and mplayer?)
                            */

#define MPEG_DEFAULT_FLAGS                  (MPEG_FLAG_PROBE)

typedef struct mpeg_file mpeg_file_t;
struct mpeg_file {
    int streamed; /* flag. We can seek or not? */
    void *priv;   /* private data for each mpeg_file_t implementation */

    /* fread wrapper */
    size_t (*read)(mpeg_file_t *MFILE, void *ptr, 
            size_t size, size_t num);
    /* fwrite wrapper */
    size_t (*write)(mpeg_file_t *MFILE, const void *ptr, 
            size_t size, size_t num);
    /* fseek wrapper */
    int (*seek)(mpeg_file_t *MFILE, uint64_t offset, int whence);
    /* ftell wrapper */
    int64_t (*tell)(mpeg_file_t *MFILE);

    /* 
     * get size ibn bytes of this file. 
     * Should return -1 if size can't be calculated 
     */
    int64_t (*get_size)(mpeg_file_t *MFILE);

    /*
     * returns bool flag
     */
    int (*eof_reached)(mpeg_file_t *MFILE);
};

typedef struct mpeg_fraction mpeg_fraction_t;
struct mpeg_fraction {
    int num;
    int den;
};

#define MPEG_VOB_PKT_SIZE                           2048
/* default guessed size */
#define MPEG_PACK_HDR_SIZE                          24
/* Maximum size. Yeah, this is a wild guess too */

#define MPEG_STREAM_VIDEO_MPEG1                     0x01 
                                                    /* ISO 11172 video */
#define MPEG_STREAM_VIDEO_MPEG2                     0x02
       /* ITU H.262 or  ISO 13818-2 video or 11172-2 constrained video */
#define MPEG_STREAM_AUDIO_MPEG1                     0x03
                                                    /* ISO 11172 audio */
#define MPEG_STREAM_AUDIO_MPEG2                     0x04
                                                  /* ISO 13818-3 audio */
#define MPEG_STREAM_AUDIO_AC3                       0x80
                                                /* user private stream */
#define MPEG_STREAM_VIDEO_MPEG4                     0x10
#define MPEG_STREAM_VIDEO_H264                      0x1a
#define MPEG_STREAM_AUDIO_AAC                       0x0f

#define MPEG_STREAM_ID_BASE_VIDEO                   0xe0
#define MPEG_STREAM_ID_BASE_AUDIO                   0xc0
#define MPEG_STREAM_ID_BASE_EXTRA                   0x80
#define MPEG_STREAM_ID_BASE_AC3         MPEG_STREAM_ID_BASE_EXTRA
#define MPEG_STREAM_ID_BASE_LPCM                    0xa0
                                                  /* correct ? */
#define MPEG_STREAM_ID_BASE_PRIVATE                 0xbd 
                        // XXX: correct should swap private/extra?

/* commodities */
#define MPEG_STREAM_ANY                             0xff 
                                                    // right? better 0x00? 
#define MPEG_STREAM_VIDEO(n)    ((n) + MPEG_STREAM_ID_BASE_VIDEO)
#define MPEG_STREAM_AUDIO(n)    ((n) + MPEG_STREAM_ID_BASE_AUDIO)
#define MPEG_STREAM_AC3(n)      ((n) + MPEG_STREAM_ID_BASE_AC3)

#define MPEG_STREAM_TYPE_VIDEO                      1
#define MPEG_STREAM_TYPE_AUDIO                      2
#define MPEG_STREAM_TYPE_MULTIPLEX                  3

#define MPEG_STREAM_COMMON \
    int stream_type; \
    int stream_id; \
    char *codec; \
    uint64_t start_time; \
    int index; \
    int flags; \
    int bit_rate

typedef struct mpeg_video_stream mpeg_video_stream_t;
struct mpeg_video_stream {
    MPEG_STREAM_COMMON;
    mpeg_fraction_t frame_rate;
    int width;    /* pixels */
    int height;   /* pixels */
    mpeg_fraction_t aspect;
    uint32_t frames;
};

typedef struct mpeg_audio_stream mpeg_audio_stream_t;
struct mpeg_audio_stream {
    MPEG_STREAM_COMMON;
    int sample_rate;    /* Hz */
    int channels;
    uint32_t samples;
    int block_align;
    int sample_size;    /* bits */
};

typedef union mpeg_stream mpeg_stream_t;
union mpeg_stream {
    int stream_type;
    struct {
        MPEG_STREAM_COMMON;
    } common;
    mpeg_video_stream_t video;
    mpeg_audio_stream_t audio;
};

typedef enum mpeg_type_e mpeg_type_t;
enum mpeg_type_e {
    MPEG_TYPE_NONE = -1,
    MPEG_TYPE_ANY = 0,
    MPEG_TYPE_ES,
    MPEG_TYPE_PS
};

typedef struct mpeg_pes_packet mpeg_pkt_t;
struct mpeg_pes_packet {
    int type;           /* used only by PS support */	
    int stream_id;
    int flags;
    uint64_t pts;
    uint64_t dts;
    uint16_t size;      /* data size */
    uint16_t hdrsize;   /* header size */
    /* total packet length will always be hdrsize + size */
    uint8_t *data;
    uint8_t *hdr;
};

typedef struct mpeg_s mpeg_t;
struct mpeg_s {
    /* general */
    int type;   /* it's a PS, TS, ES or what? */
    int n_streams;  /* how many streams we have here? */
    mpeg_stream_t *streams; /* stream descriptors */
    int *smap; /** stream reorder map to ensure compatibility with tc */ 

    uint64_t time;
    void *priv; /* private data for each MPEG type */

    mpeg_file_t *MFILE; /* data source */

    mpeg_err_t errcode; /* last error code, like errno */
    
    /* methods */
    const mpeg_pkt_t* (*read_packet)(mpeg_t *MPEG, int stream_id);
    mpeg_res_t (*probe)(mpeg_t *MPEG);
    mpeg_res_t (*close)(mpeg_t *MPEG);
};

/****************************************************************************
 * exported API                                                             * 
 ****************************************************************************/

/* utilities ****************************************************************/

/**
 * builtin default FILE wrapper open function. A decorator on fopen().
 *
 * @param filename  path of file to open
 * @param mode      opening mode. Just like fopen.
 *
 * @return a pointer to a new valid mpeg_file_t descriptor, 
 * or NULL if something fails.
 *
 * @see mpeg_file_open_link
 * @see mpeg_file_close
 */
mpeg_file_t *mpeg_file_open(const char *filename, const char *mode);

/**
 * Link a FILE wrapper using an already open regular FILE. The last one
 * MUST be open with right mod, and this function DOES NOT check this.
 *
 * @param f     pointer to FILE to wrap
 *
 * @return a pointer to a new valid mpeg_file_t descriptor, 
 * or NULL if something fails. (This should never happen).
 * 
 * @see mpeg_file_open
 * @see mpeg_file_close
 */
mpeg_file_t *mpeg_file_open_link(FILE *f);

/**
 * Close a FILE wrapper descriptor, freeing all resources acquired. 
 * A decorator on fclose().
 *
 * @param MFILE     pointer to FILE wrapper to close
 *
 * @return -1 if error occurs, 0 otherwise.
 *
 * @see mpeg_file_open
 * @see mpeg_file_open_link
 */
mpeg_res_t mpeg_file_close(mpeg_file_t *MFILE);

/**
 * hook for acquiring a chunk of memory. 
 * Implementation MUST guarantee to caller (MPEGlib itself) that returned 
 * pointer points to a chunk of at least <size> continuous bytes, which 
 * MUST always be avalaible until related mpeg_mrelease_fn_t callback 
 * invocation.
 * See README for more informations (not yet written as version 0.2.2).
 * 
 * @param size      minumum dimension, in bytes, of returned memory chunk.
 *                  calling environment is supposed to use *at most* <size>
 *                  bytes, but callback can return more than <size> bytes.
 * 
 * @return base pointer of memory chunk
 * 
 * @see mpeg_mrelease_fn_t
 */
typedef void* (*mpeg_macquire_fn_t)(size_t size);

/**
 * hook for releasing a chunk of memory. After calling this hook, caller
 * (MPEGlib iself) MUST NOT assert anything about validity of given
 * memory area.
 * See README for more informations (not yet written as version 0.2.2).
 *
 * @param ptr       base pointer of memory to be released, as returned
 *                  from mpeg_macquire_fn_t
 *
 * @see mpeg_macquire_fn_t
 */
typedef void (*mpeg_mrelease_fn_t)(void *ptr);

/**
 * setup above hooks for memory handling. Please note that 
 * (as in MPEGlib 0.2.2) those hooks are LIBRARY-wide, *NOT* INSTANCE-wide. 
 * All instances share the same hooks.
 * See README for more informations (not yet written as version 0.2.2).
 *
 * @param acquire   new acquiring callback
 * @param release   new releasing callback
 *
 * @see mpeg_macquire_fn_t
 * @see mpeg_mrelease_fn_t
 */
void mpeg_set_mem_handling(mpeg_macquire_fn_t acquire,
                           mpeg_mrelease_fn_t release);           

/**
 * Logger hook. Default logging it's on stderr.
 *
 * @param dest      logs destination. Opaque pointer.
 * @param level     logging level for this message.
 * @param fmt       log format descriptor. Like *printf.
 * @param ap        log data
 *
 * @return -1 if an error occors, 0 otherwise.
 */
typedef mpeg_res_t (*mpeg_log_fn_t)(void *dest, int level, 
            const char *fmt, va_list ap);

/**
 * Change the current logger handler and/or the logging destination.
 * WARNING: logging handler and destination are *per library*, not
 * *per instance*. All mpeg_t instances share those values.
 *
 * @param nlogger   new logger handler. Give 'NULL' if you
 *          DO NOT want to change this parameter.
 * @param nlogdest  new logger destination. Give 'NULL' if you
 *          DO NOT want to change this parameter.
 * @see mpeg_log_file
 * @see mpeg_log_null
 */
void mpeg_set_logging(mpeg_log_fn_t nlogger, void *nlogdest);

/** Default log handler. Write log messages on stderr */
mpeg_res_t mpeg_log_file(void *dest, int level, const char *fmt, va_list ap);

/** Fake log handler. Do nothing and always return success. */
mpeg_res_t mpeg_log_null(void *dest, int level, const char *fmt, va_list ap);

/**
 * Log a message using library log handler on library log destination.
 * 
 * @param level     log level (see mpeg_log_level_t)
 * @param fmt       format string, like *printf()
 * 
 * @return MPEG_OK if no error occurs, MPEG_ERR otherwise (message
 * is lost)
 *
 * @see mpeg_set_logging
 */
mpeg_res_t mpeg_log(int level, const char *fmt, ...);

/**
 * Allocate enough data for a new PES packet (mpeg_pkt_t structure). 
 * You must use the companion function for free() resources acquired with 
 * this function.
 * 
 * @param size      size of this packet
 *
 * @return a pointer to a new mpeg_pkt_t, or NULL if an error occurs.
 *
 * @see mpeg_pkt_del
 */
mpeg_pkt_t* mpeg_pkt_new(size_t size);

/**
 * Free resources acquired with mpeg_pkt_new(). 
 * You DO NOT NEVER USE standard free(). ALWAYS use this function.
 *
 * @param pes       pointer to a mpeg_pkt_t acquired via
 *          mpeg_pkt_new()
 *          
 * @see mpeg_pkt_new
 */
void mpeg_pkt_del(const mpeg_pkt_t *pes);

/**
 * Pretty-print some informatiosn about a mpeg stream on a given file.
 *
 * @param mps       pointer to a mpeg stream descriptor, usually obtained
 *          using mpeg_get_stream_info().
 * @param f     print here the stream information
 */
void mpeg_print_stream_info(const mpeg_stream_t *mps, FILE *f);

/* core API *****************************************************************/

/**
 * Opens a new MPEG descriptor. You need this in order to fetch packets
 * or probe content. This descriptor MUST NOT be free()d using the standard
 * free(), always use mpeg_close() instead.
 *
 * MPEGlib currently provides *only* READ-ONLY access to files.
 *
 * @param type      selects the kind og MPEG file to open. Currently
 *                  only ES and PS files are supported. Autoprobe
 *                  (MPEG_TYPE_ANY) still does not work.
 * @param MFILE     already open (via mpeg_file_open or what you like)
 *                  FILE wrapper pointing to MPEG file to deal with.
 *                  MFILE MUST be open in right mode (es: "r" to read,
 *                  or "w+" to write). Actually MPEGlib doesn't check
 *                  mode at all (this should be corrected in future 
 *                  releases)
 * @param flags     opening flags. See MPEG_FLAG_* for description.
 *                  Sensible defaults are provided with MPEG_DEFAULT_FLAGS
 *                  macro.
 * @param errcode   Pointer to integer representing the error occurred if
 *                  this functions fails. can be NULL (no error reporting)
 *
 * @return a pointer to a new valid mpeg descriptor, or NULL if something 
 * failes (file is not what is declared, is recognized, read error...)
 *
 * @see mpeg_file_open
 */
mpeg_t *mpeg_open(int type, mpeg_file_t *MFILE, uint32_t flags, int *errcode);

/**
 * Report last error code produced by a function invoked on given descriptor.
 * Please note that error code IS NOT reset'd after a succesfully completed 
 * functions. An error code can survive for an undefinied timespan.
 *
 * @param MPEG      fetch error code from this descriptor
 * @return the code of last error occurred
 */
mpeg_err_t mpeg_get_last_error(mpeg_t *MPEG);

/**
 * Probe the stream attached at this MPEG descriptor. Updates mpeg_stream_t
 * descriptors. Please note that, by default, mpeg_open() probes stream.
 * Of course, calling mpeg_get_stream* on a stream which isn't probe is
 * a non-sense and produeces undefined result.
 *
 * mpeg_probe() is composed by a frontend (documented here) and a backend,
 * one for each kind of MPEG stream handled by MPEGlib.
 * (ES, PS, as MPEGlib 0.2.x).
 * The frontend ensures that backend, which does real probing work, is
 * executed in a controlled environment. In a nutshell, frontend rewind
 * file to begin before invoking backend, and restore original position
 * of stram before it's end.
 *
 * WARNING: (as 0.2.x) delayed probe isn't well supported, using this 
 * function *may* lead to nasty bugs. Be prepared.
 *
 * @param MPEG      probe the stream (mpeg_file_t) attached at this 
 *                  descriptor
 * @return MPEG_OK if probe has success, or MPEG_ERR otherwise. If so, check
 * the MPEG->errcode value.
 * 
 * @see mpeg_open
 * @see mpeg_file_open
 * @see mpeg_get_stream_info.
 */
mpeg_res_t mpeg_probe(mpeg_t *MPEG);

/**
 * Enumerate the A/V streams found in an open MPEG file.
 *
 * @param MPEG      MPEG descriptor, obtained with mpeg_open().
 * 
 * @return the number of A/V streams found, or MPEG_ERR if some error occurs.
 *
 * @see mpeg_open
 */
int mpeg_get_stream_number(mpeg_t *MPEG);

/**
 * Get informations about an A/V stream on this (open) MPEG file.
 *
 * @param MPEG       MPEG descriptor, obtained with mpeg_open().
 * @param stream_num Number of stream to get informations.
 *
 * @return a pointer to the stream descriptor of desired stream, or
 * NULL if wanted stream doesn't exist.
 * THERE IS NO NEED to free() it. No memory leak will occur, it's handled
 * automatically.
 */
const mpeg_stream_t *mpeg_get_stream_info(mpeg_t *MPEG, int stream_num);

/**
 * Fetch a new PES packet belonging to a given stream from an open MPEG 
 * descriptor. Use mpeg_get_stream_{number,info} to find right stream_id
 * for you.
 * 
 * WARNING: actually mpeg_read_packet *MAY* silently discard any encountered 
 * packet which NOT belongs to desired stream. So, if you want fetch multiple 
 * streams from the same file, you MUST use multiple MPEG descriptor pointing
 * to the same file. This is obviously ugly and should be removed or at least
 * mitigated in future releases.
 *
 * @param MPEG      MPEG descriptor from which fetch packets.
 * @param stream_id stream identificator of desired stream
 *
 * @return a pointer to a new PES packet with new data, or NULL if something 
 * failes or even if stream ends, or simply not exists. Always free this packet 
 * using mpeg_pkt_del().
 *
 * @see mpeg_open
 * @see mpeg_get_stream_number
 * @see mpeg_get_stream_info
 * @see mpeg_pkt_del
 */ 
const mpeg_pkt_t *mpeg_read_packet(mpeg_t *MPEG, int stream_id);

/**
 * Close and finalize an MPEG descriptor, freeing all acquired resources.
 * PLEASE NOTE: this function DO NOT close the FILE wrapper given to
 * companion mpeg_open(), it must be closed indipendentely using 
 * mpeg_file_close or something like it.
 *
 * @param MPEG      MPEG descriptor to close.
 *
 * @return MPEG_ERR if an error occurs, MPEG_OK otherwise.
 *
 * @see mpeg_open
 */
mpeg_res_t mpeg_close(mpeg_t *MPEG);

#endif // _MPEGLIB_H
