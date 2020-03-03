/*
 * import_pvn.c -- module for importing PVN video streams
 * (http://www.cse.yorku.ca/~jgryn/research/pvnspecs.html)
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#define MOD_NAME        "import_pvn.so"
#define MOD_VERSION     "v1.0 (2006-10-07)"
#define MOD_CAP         "Imports PVN video"
#define MOD_AUTHOR      "Andrew Church"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_DEMULTIPLEX|TC_MODULE_FEATURE_DECODE|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

#include "src/transcode.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"
#include "libtcvideo/tcvideo.h"

#include <math.h>

/*************************************************************************/

/* Local data structure. */

typedef enum {
    UNSET,
    BIT,
    UINT8, UINT16, UINT24, UINT32,
    SINT8, SINT16, SINT24, SINT32,
    SINGLE,  // single precision floating point
    DOUBLE   // double precision floating point
} PVNDataType;
typedef struct {
    int fd;
    enum { BITMAP = 4, GREY, RGB } imagetype;  // PV4, PV5, PV6 respectively
    PVNDataType datatype;
    float single_base, single_range;   // Only set for SINGLE_FLOAT
    double double_base, double_range;  // Only set for DOUBLE_FLOAT
    int width, height, nframes;
    double framerate;
    int samplebits, linesize, framesize;
    uint8_t *buffer;
} PrivateData;

/*************************************************************************/
/*************************************************************************/

/* pvn_read_field:  Helper routine for parse_pvn_header() to read a single
 * whitespace-delimited field from a PVN header.  End of stream, buffer
 * overflow, and the presence of a null byte ('\0') in the header are all
 * treated as errors, and the contents of the buffer are undefined in such
 * cases.
 *
 * Parameters:
 *          fd: File descriptor to read from.
 *      buffer: Buffer to place null-terminated field string in.
 *     bufsize: Size of buffer.
 * Return value:
 *     The whitespace character that terminated the field, or 0 if an error
 *     was encountered.
 * Side effects:
 *     Outputs an error message (using tc_log_XXX()) if an error is
 *     encountered.
 * Preconditions:
 *     fd >= 0
 *     buffer != NULL
 *     bufsize > 0
 */

static int pvn_read_field(int fd, char *buffer, int bufsize)
{
    int len = 0;
    int ch;
    int incomment = 0;

    do {
        if (read(fd, &buffer[len], 1) != 1) {
            tc_log_error(MOD_NAME, "End of stream while reading header");
            ch = -1;
        } else if (len >= bufsize-1) {
            tc_log_error(MOD_NAME, "Buffer overflow while reading header");
            ch = -1;
        } else if ((ch = (int)buffer[len] & 0xFF) == 0) {
            tc_log_error(MOD_NAME, "Null byte in header");
            ch = -1;
        } else if (ch == '#') {
            incomment = 1;
        } else if (ch == '\n') {
            incomment = 0;
        } else if (!strchr(" \t\r\n", ch) && !incomment) {
            len++;
        }
    } while (ch > 0 && (len == 0 || !strchr(" \t\r\n", ch)));
    if (ch > 0)
        buffer[len] = 0;
    return ch;
}

/*************************************************************************/

/**
 * parse_pvn_header:  Parses the header of a PVN stream, filling in the
 * PrivateData structure appropriately.
 *
 * Parameters:
 *     pd: PrivateData structure for this module instance.
 * Return value:
 *     Nonzero on success, zero on error.
 * Side effects:
 *     Outputs an error message (using tc_log_XXX()) if an error is
 *     encountered.
 * Preconditions:
 *     pd != NULL
 *     pd->fd contains the file descriptor to use for reading from the
 *        stream.
 */

static int parse_pvn_header(PrivateData *pd)
{
    char fieldbuf[1000], *s;
    int ch;

    /* Read file type */
    if (!pvn_read_field(pd->fd, fieldbuf, sizeof(fieldbuf)))
        return TC_OK;
    if (fieldbuf[0] != 'P' || fieldbuf[1] != 'V'
     || (fieldbuf[2] != '4' && fieldbuf[2] != '5' &&  fieldbuf[2] != '6')
     || (fieldbuf[3] != 'a' && fieldbuf[3] != 'b'
      && fieldbuf[3] != 'd' && fieldbuf[3] != 'f')
     || fieldbuf[4] != 0
     || (fieldbuf[2] == '4' && fieldbuf[3] != 'a')  // PV4[bdf] not allowed
    ) {
        tc_log_error(MOD_NAME, "PVN header not found");
        return TC_OK;
    }
    pd->imagetype = (fieldbuf[2]=='4' ? BITMAP :
                     fieldbuf[2]=='5' ? GREY : RGB);
    pd->datatype  = (pd->imagetype==BITMAP ? BIT :
                     fieldbuf[3]=='a' ? UINT8 :  // int size will be set later
                     fieldbuf[3]=='b' ? SINT8 :
                     fieldbuf[3]=='f' ? SINGLE : DOUBLE);

    /* Read width */
    if (!pvn_read_field(pd->fd, fieldbuf, sizeof(fieldbuf)))
        return TC_OK;
    pd->width = (int)strtol(fieldbuf, &s, 10);
    if (*s || pd->width <= 0) {
        tc_log_error(MOD_NAME, "Invalid width in header: %s", fieldbuf);
        return TC_OK;
    }

    /* Read height */
    if (!pvn_read_field(pd->fd, fieldbuf, sizeof(fieldbuf)))
        return TC_OK;
    pd->height = (int)strtol(fieldbuf, &s, 10);
    if (*s || pd->width <= 0) {
        tc_log_error(MOD_NAME, "Invalid height in header: %s", fieldbuf);
        return TC_OK;
    }

    /* Read number of frames */
    if (!pvn_read_field(pd->fd, fieldbuf, sizeof(fieldbuf)))
        return TC_OK;
    pd->nframes = (int)strtol(fieldbuf, &s, 10);
    if (*s || pd->width <= 0) {
        tc_log_error(MOD_NAME, "Invalid frame count in header: %s", fieldbuf);
        return TC_OK;
    }

    /* Read maxval */
    if (!pvn_read_field(pd->fd, fieldbuf, sizeof(fieldbuf)))
        return TC_OK;
    if (pd->imagetype == BITMAP) {
        long maxval = strtol(fieldbuf, &s, 10);
        if (*s || maxval != 1) {
            tc_log_error(MOD_NAME, "Invalid maxval in header (must be 1 for"
                         " bitmaps): %s", fieldbuf);
            return TC_OK;
        }
    } else if (pd->datatype == SINGLE || pd->datatype == DOUBLE) {
        double maxval = strtod(fieldbuf, &s), base, range;
        if (*s || maxval == 0) {
            tc_log_error(MOD_NAME, "Invalid maxval in header: %s", fieldbuf);
            return TC_OK;
        }
        if (*fieldbuf == '+') {
            base = 0;
            range = maxval;
        } else if (*fieldbuf == '-') {
            base = maxval;
            range = -maxval;
        } else {
            base = -maxval;
            range = maxval*2;
        }
        if (pd->datatype == SINGLE) {
            pd->single_base  = (float)base;
            pd->single_range = (float)range;
        } else {
            pd->double_base  = base;
            pd->double_range = range;
        }
    } else {  // integer datatype
        /* FP value allowed for maxval, as long as it evaluates to an integer*/
        double maxval_d = strtod(fieldbuf, &s);
        int maxval = (int)maxval_d;
        if (*s || (double)maxval != maxval_d
         || (maxval!=8 && maxval!=16 && maxval!=24 && maxval!=32)
        ) {
            tc_log_error(MOD_NAME, "Invalid maxval in header: %s", fieldbuf);
            return TC_OK;
        }
        if (maxval >= 16)
            pd->datatype++;
        if (maxval >= 24)
            pd->datatype++;
        if (maxval == 32)
            pd->datatype++;
    }

    /* Read frame rate */
    if (!(ch = pvn_read_field(pd->fd, fieldbuf, sizeof(fieldbuf))))
        return TC_OK;
    pd->framerate = strtod(fieldbuf, &s);
    if (*s || pd->framerate < 0) {
        tc_log_error(MOD_NAME, "Invalid frame rate in header: %s", fieldbuf);
        return TC_OK;
    } else if (pd->framerate == 0) {
        pd->framerate = 15.0;  // default
    }

    /* Skip past the final newline */
    while (ch != '\n') {
        uint8_t byte;
        if (read(pd->fd, &byte, 1) != 1) {
            tc_log_error(MOD_NAME, "End of stream while reading header");
            return TC_OK;
        }
        ch = byte;
    }

    /* All successful, calculate frame size and return */
    switch (pd->datatype) {
        case BIT   : pd->samplebits =  1; break;
        case UINT8 : pd->samplebits =  8; break;
        case UINT16: pd->samplebits = 16; break;
        case UINT24: pd->samplebits = 24; break;
        case UINT32: pd->samplebits = 32; break;
        case SINT8 : pd->samplebits =  8; break;
        case SINT16: pd->samplebits = 16; break;
        case SINT24: pd->samplebits = 24; break;
        case SINT32: pd->samplebits = 32; break;
        case SINGLE: pd->samplebits = 32; break;
        case DOUBLE: pd->samplebits = 64; break;
        case UNSET :
            tc_log_error(MOD_NAME, "Internal error: pd->datatype unset");
            return TC_OK;
    }
    pd->linesize  = (pd->samplebits * pd->width * (pd->imagetype==RGB?3:1)
                     + 7) / 8;
    pd->framesize = pd->linesize * pd->height;
    return 1;
}

/*************************************************************************/
/*************************************************************************/

#ifndef PROBE_ONLY  /* to probe_pvn() */

/* Module interface routines and data. */

/*************************************************************************/

/**
 * pvn_init:  Initialize this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int pvn_init(TCModuleInstance *self, uint32_t features)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    self->userdata = pd = tc_malloc(sizeof(PrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }
    pd->fd = -1;
    pd->datatype = UNSET;
    pd->single_base = pd->single_range = 0;
    pd->double_base = pd->double_range = 0;
    pd->buffer = NULL;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * pvn_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int pvn_configure(TCModuleInstance *self,
                         const char *options,
                         TCJob *vob,
                         TCModuleExtraData *xdata[])
{
    TC_MODULE_SELF_CHECK(self, "configure");

    return TC_OK;
}

//*************************************************************************/

/**
 * pvn_inspect:  Return the value of an option in this instance of the
 * module.  See tcmodule-data.h for function details.
 */

static int pvn_inspect(TCModuleInstance *self,
                       const char *param, const char **value)
{
    static char buf[TC_BUF_MAX];

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    if (optstr_lookup(param, "help")) {
        tc_snprintf(buf, sizeof(buf),
                    "Overview:\n"
                    "    Imports PVN video streams.\n"
                    "No options available.\n");
        *value = buf;
    }
    return TC_IMPORT_OK;
}

/*************************************************************************/

/**
 * pvn_stop:  Reset this instance of the module.  See tcmodule-data.h for
 * function details.
 */

static int pvn_stop(TCModuleInstance *self)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    if (pd->fd != -1) {
        close(pd->fd);
        pd->fd = -1;
    }
    tc_buffree(pd->buffer);
    pd->buffer = NULL;
    pd->datatype = UNSET;
    pd->single_base = pd->single_range = 0;
    pd->double_base = pd->double_range = 0;

    return TC_OK;
}

/*************************************************************************/

/**
 * pvn_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int pvn_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    pvn_stop(self);
    tc_free(self->userdata);
    self->userdata = NULL;
    return TC_OK;
}

/*************************************************************************/

/**
 * pvn_read_video:  Demultiplex a frame of data.  See tcmodule-data.h for
 * function details.
 *
 * Notes:
 *     For PVN, we perform the "decode" step here as well, to avoid
 *     saddling the core code with the bizarre situation of an encoded
 *     frame that takes 8x as much space (for PV6d) as the decoded frame.
 */

#undef USE_DECODE_PVN_SSE2
#if defined(HAVE_ASM_SSE2) && defined(ATTRIBUTE_ALIGNED_MAX)
# if ATTRIBUTE_ALIGNED_MAX >= 16
#  define USE_DECODE_PVN_SSE2 1
# endif
#endif

#if USE_DECODE_PVN_SSE2
/* Accelerated decode function, defined below */
static int decode_pvn_sse2(const PrivateData *pd, uint8_t *video_buf);
#endif

static int pvn_read_video(TCModuleInstance *self,
                          TCFrameVideo *vframe)
{
    int tc_accel = tc_get_session()->acceleration; /* XXX ugly */
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "demultiplex");

    pd = self->userdata;
    if (pd->fd < 0) {
        tc_log_error(MOD_NAME, "demultiplex: no file opened!");
        return TC_ERROR;
    }

    if (tc_pread(pd->fd, pd->buffer, pd->framesize) != pd->framesize) {
        if (verbose)
            tc_log_info(MOD_NAME, "End of stream reached");
        return TC_ERROR;
    }

    /* Shortcuts for RGB->RGB */
    if (pd->imagetype == RGB) {
        if (pd->datatype == UINT8) {
            ac_memcpy(vframe->video_buf, pd->buffer, pd->framesize);
            return pd->framesize;
#if USE_DECODE_PVN_SSE2
        } else if ((tc_accel & AC_SSE2)
                && decode_pvn_sse2(pd, vframe->video_buf)
        ) {
            return pd->framesize;
#endif
        }
    }

    {
        /* Local copies of PrivateData variables, for compiler optimization */
        const int isgrey = (pd->imagetype != RGB);
        const PVNDataType datatype = pd->datatype;
        const int width = pd->width;
        const int height = pd->height;
        const float single_base = pd->single_base;
        const float single_range = pd->single_range;
        const float double_base = pd->double_base;
        const float double_range = pd->double_range;
        const int xlimit = isgrey ? width : width*3;
        int y;

        for (y = 0; y < height; y++) {
            const uint8_t *inptr = pd->buffer + (y * pd->linesize);
            uint8_t *outptr = vframe->video_buf + (y * width * 3);
            int x;
            for (x = 0; x < xlimit; x++) {
                uint8_t val = 0;
                switch (datatype) {
                  case UNSET:
                    break;
                  case BIT:
                    val = (inptr[x/8] >> (7-(x&7))) & 1
                        ? 255 : 0;
                    break;
                  case UINT8:
                    val = inptr[x];
                    break;
                  case UINT16:
                    val = inptr[x*2];
                    break;
                  case UINT24:
                    val = inptr[x*3];
                    break;
                  case UINT32:
                    val = inptr[x*4];
                    break;
                  case SINT8:
                    val = inptr[x] ^ 0x80;
                    break;
                  case SINT16:
                    val = inptr[x*2] ^ 0x80;
                    break;
                  case SINT24:
                    val = inptr[x*3] ^ 0x80;
                    break;
                  case SINT32:
                    val = inptr[x*4] ^ 0x80;
                    break;
                  case SINGLE: {
                    /* Convert from big-endian to native format */
                    union {
                        float f;
                        uint32_t i;
                    } u;
                    u.i = (uint32_t)(inptr[x*4  ]) << 24
                        | (uint32_t)(inptr[x*4+1]) << 16
                        | (uint32_t)(inptr[x*4+2]) <<  8
                        | (uint32_t)(inptr[x*4+3]);
                    val = (int)floor(((u.f - single_base) / single_range)
                                     * 255 + 0.5);
                    break;
                  } // SINGLE
                  case DOUBLE: {
                    /* Convert from big-endian to native format */
                    union {
                        float d;
                        uint64_t i;
                    } u;
                    u.i = (uint64_t)(inptr[x*8  ]) << 56
                        | (uint64_t)(inptr[x*8+1]) << 48
                        | (uint64_t)(inptr[x*8+2]) << 40
                        | (uint64_t)(inptr[x*8+3]) << 32
                        | (uint64_t)(inptr[x*8+4]) << 24
                        | (uint64_t)(inptr[x*8+5]) << 16
                        | (uint64_t)(inptr[x*8+6]) <<  8
                        | (uint64_t)(inptr[x*8+7]);
                    val = (int)floor(((u.d - double_base) / double_range)
                                     * 255 + 0.5);
                    break;
                  } // DOUBLE
                } // switch (datatype) 
                if (isgrey) {
                    outptr[x*3  ] = val;
                    outptr[x*3+1] = val;
                    outptr[x*3+2] = val;
                } else {
                    outptr[x] = val;
                }
            } // for x
        } // for y
    } // decoding block

    return pd->framesize;
}

/************************************/

#if USE_DECODE_PVN_SSE2

/**
 * decode_pvn_sse2:  Accelerated routine to convert PVN frames to RGB.
 *
 * Parameters:
 *            pd: PrivateData structure for the module instance.
 *     video_buf: Output video buffer.
 * Return value:
 *     Nonzero on success, zero on failure.
 * Prerequisites:
 *     pd != NULL
 *     pd->imagetype == RGB
 *     video_buf != NULL
 * Notes:
 *     UINT8 is not handled here, as pvn_demultiplex() just calls
 *     ac_memcpy() for that case.
 *     The wisdom of including an architecture-specific accelerated routine
 *     directly in an import module is debatable, but cross-platform
 *     handling of big-endian values, especially floats, is _slow_...
 */

/* Register renaming for x86-64 */
#ifdef ARCH_X86_64
# define EAX "%%rax"
# define ECX "%%rcx"
# define EDX "%%rdx"
# define ESI "%%rsi"
# define EDI "%%rdi"
#else
# define EAX "%%eax"
# define ECX "%%ecx"
# define EDX "%%edx"
# define ESI "%%esi"
# define EDI "%%edi"
#endif

/* Generic loop wrapper for SIMD blocks (also works for leftover elements).
 * Register setup: EAX = (scratch), ECX = count (index+blocksize), EDX = size,
 *                 ESI = source_base, EDI = dest_base
 * Labels 8 and 9 are used by the wrapper. */
#define SSE2_LOOP(blocksize,body) \
    asm volatile("\n jmp 9f; 8:\n" body "\n9: add %1, %%ecx;\n"             \
        " cmp %%edx, %%ecx;\n jbe 8b; sub %1, %%ecx"                        \
        : "=c" (count)                                                      \
        : "i" (blocksize), "0" (count), "d" (size),                         \
          "S" (pd->buffer), "D" (video_buf)                                 \
        : "eax", "memory")                                                  \

static int decode_pvn_sse2(const PrivateData *pd, uint8_t *video_buf)
{
    int size = pd->height * pd->width * 3, count = 0;
    /* Data for flipping sign bits */
    static const struct {uint8_t b[80];} __attribute__((aligned(16)))
        data80 = {{0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
                   0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80}};

    switch (pd->datatype) {
      case SINT8:
        asm("movdqa ("ESI"), %%xmm7" : : "S" (&data80), "m" (data80));
        SSE2_LOOP(112, "movdqa -0x70("ESI","ECX"), %%xmm0       \n\
                        movdqa -0x60("ESI","ECX"), %%xmm1       \n\
                        movdqa -0x50("ESI","ECX"), %%xmm2       \n\
                        movdqa -0x40("ESI","ECX"), %%xmm3       \n\
                        movdqa -0x30("ESI","ECX"), %%xmm4       \n\
                        movdqa -0x20("ESI","ECX"), %%xmm5       \n\
                        movdqa -0x10("ESI","ECX"), %%xmm6       \n\
                        pxor %%xmm7, %%xmm0                     \n\
                        pxor %%xmm7, %%xmm1                     \n\
                        pxor %%xmm7, %%xmm2                     \n\
                        pxor %%xmm7, %%xmm3                     \n\
                        pxor %%xmm7, %%xmm4                     \n\
                        pxor %%xmm7, %%xmm5                     \n\
                        pxor %%xmm7, %%xmm6                     \n\
                        movdqu %%xmm0, -0x70("EDI","ECX")       \n\
                        movdqu %%xmm1, -0x60("EDI","ECX")       \n\
                        movdqu %%xmm2, -0x50("EDI","ECX")       \n\
                        movdqu %%xmm3, -0x40("EDI","ECX")       \n\
                        movdqu %%xmm4, -0x30("EDI","ECX")       \n\
                        movdqu %%xmm5, -0x20("EDI","ECX")       \n\
                        movdqu %%xmm6, -0x10("EDI","ECX")       \n");
        SSE2_LOOP(  1, "mov -1("ESI","ECX"), %%al               \n\
                        xor $0x80, %%al                         \n\
                        mov %%al, -1("EDI","ECX")               \n");
        break;

      case SINT16:
        asm("movdqa ("ESI"), %%xmm7" : : "S" (&data80), "m" (data80));
        asm("pcmpeqd %%xmm6, %%xmm6; psrlw $8, %%xmm6" : : );  // byte mask
        SSE2_LOOP( 48, "movdqa -0x60("ESI","ECX",2), %%xmm0     \n\
                        movdqa -0x50("ESI","ECX",2), %%xmm1     \n\
                        movdqa -0x40("ESI","ECX",2), %%xmm2     \n\
                        movdqa -0x30("ESI","ECX",2), %%xmm3     \n\
                        movdqa -0x20("ESI","ECX",2), %%xmm4     \n\
                        movdqa -0x10("ESI","ECX",2), %%xmm5     \n\
                        pand %%xmm6, %%xmm0                     \n\
                        pand %%xmm6, %%xmm1                     \n\
                        pand %%xmm6, %%xmm2                     \n\
                        pand %%xmm6, %%xmm3                     \n\
                        pand %%xmm6, %%xmm4                     \n\
                        pand %%xmm6, %%xmm5                     \n\
                        packuswb %%xmm1, %%xmm0                 \n\
                        packuswb %%xmm3, %%xmm2                 \n\
                        packuswb %%xmm5, %%xmm4                 \n\
                        pxor %%xmm7, %%xmm0                     \n\
                        pxor %%xmm7, %%xmm2                     \n\
                        pxor %%xmm7, %%xmm4                     \n\
                        movdqu %%xmm0, -0x30("EDI","ECX")       \n\
                        movdqu %%xmm2, -0x20("EDI","ECX")       \n\
                        movdqu %%xmm4, -0x10("EDI","ECX")       \n");
        SSE2_LOOP(  1, "mov -2("ESI","ECX",2), %%al             \n\
                        xor $0x80, %%al                         \n\
                        mov %%al, -1("EDI","ECX")               \n");
        break;

      case UINT16:
        asm("pcmpeqd %%xmm6, %%xmm6; psrlw $8, %%xmm6" : : );  // byte mask
        SSE2_LOOP( 48, "movdqa -0x60("ESI","ECX",2), %%xmm0     \n\
                        movdqa -0x50("ESI","ECX",2), %%xmm1     \n\
                        movdqa -0x40("ESI","ECX",2), %%xmm2     \n\
                        movdqa -0x30("ESI","ECX",2), %%xmm3     \n\
                        movdqa -0x20("ESI","ECX",2), %%xmm4     \n\
                        movdqa -0x10("ESI","ECX",2), %%xmm5     \n\
                        pand %%xmm6, %%xmm0                     \n\
                        pand %%xmm6, %%xmm1                     \n\
                        pand %%xmm6, %%xmm2                     \n\
                        pand %%xmm6, %%xmm3                     \n\
                        pand %%xmm6, %%xmm4                     \n\
                        pand %%xmm6, %%xmm5                     \n\
                        packuswb %%xmm1, %%xmm0                 \n\
                        packuswb %%xmm3, %%xmm2                 \n\
                        packuswb %%xmm5, %%xmm4                 \n\
                        movdqu %%xmm0, -0x30("EDI","ECX")       \n\
                        movdqu %%xmm2, -0x20("EDI","ECX")       \n\
                        movdqu %%xmm4, -0x10("EDI","ECX")       \n");
        SSE2_LOOP(  1, "mov -2("ESI","ECX",2), %%al             \n\
                        xor $0x80, %%al                         \n\
                        mov %%al, -1("EDI","ECX")               \n");
        break;

      case SINT32:
        asm("movdqa ("ESI"), %%xmm7" : : "S" (&data80), "m" (data80));
        asm("pcmpeqd %%xmm6, %%xmm6; psrld $24, %%xmm6" : : );  // byte mask
        SSE2_LOOP( 16, "movdqa -0x40("ESI","ECX",4), %%xmm0     \n\
                        movdqa -0x30("ESI","ECX",4), %%xmm1     \n\
                        movdqa -0x20("ESI","ECX",4), %%xmm2     \n\
                        movdqa -0x10("ESI","ECX",4), %%xmm3     \n\
                        pand %%xmm6, %%xmm0                     \n\
                        pand %%xmm6, %%xmm1                     \n\
                        pand %%xmm6, %%xmm2                     \n\
                        pand %%xmm6, %%xmm3                     \n\
                        packuswb %%xmm1, %%xmm0                 \n\
                        packuswb %%xmm3, %%xmm2                 \n\
                        packuswb %%xmm2, %%xmm0                 \n\
                        pxor %%xmm7, %%xmm0                     \n\
                        movdqu %%xmm0, -0x10("EDI","ECX")       \n");
        SSE2_LOOP(  1, "mov -4("ESI","ECX",4), %%al             \n\
                        xor $0x80, %%al                         \n\
                        mov %%al, -1("EDI","ECX")               \n");
        break;

      case UINT32:
        asm("pcmpeqd %%xmm6, %%xmm6; psrld $24, %%xmm6" : : );  // byte mask
        SSE2_LOOP( 16, "movdqa -0x40("ESI","ECX",4), %%xmm0     \n\
                        movdqa -0x30("ESI","ECX",4), %%xmm1     \n\
                        movdqa -0x20("ESI","ECX",4), %%xmm2     \n\
                        movdqa -0x10("ESI","ECX",4), %%xmm3     \n\
                        pand %%xmm6, %%xmm0                     \n\
                        pand %%xmm6, %%xmm1                     \n\
                        pand %%xmm6, %%xmm2                     \n\
                        pand %%xmm6, %%xmm3                     \n\
                        packuswb %%xmm1, %%xmm0                 \n\
                        packuswb %%xmm3, %%xmm2                 \n\
                        packuswb %%xmm2, %%xmm0                 \n\
                        movdqu %%xmm0, -0x10("EDI","ECX")       \n");
        SSE2_LOOP(  1, "mov -4("ESI","ECX",4), %%al             \n\
                        xor $0x80, %%al                         \n\
                        mov %%al, -1("EDI","ECX")               \n");
        break;

      case SINGLE: {
        uint32_t mxcsr, mxcsr_save;
        /* This could theoretically go on the stack, but at least some GCC
         * versions seem to forget to align it in that case...  --AC */
        static struct {float f[8];} __attribute__((aligned(16))) single_data;
        single_data.f[0] = single_data.f[1] =
            single_data.f[2] = single_data.f[3] = pd->single_base;
        single_data.f[4] = single_data.f[5] =
            single_data.f[6] = single_data.f[7] = pd->single_range/255;
        asm("movdqa ("ESI"), %%xmm6; movdqa 16("ESI"), %%xmm7"
            : : "S" (&single_data), "m" (single_data));
        asm("stmxcsr %0" : "=m" (mxcsr_save));
        mxcsr = mxcsr_save & 0x9FFF;  // round to nearest
        asm("ldmxcsr %0" : : "m" (mxcsr));
        SSE2_LOOP(  8, "movdqa -0x20("ESI","ECX",4), %%xmm0     \n\
                        movdqa -0x10("ESI","ECX",4), %%xmm1     \n\
                        pshuflw $0xB1, %%xmm0, %%xmm0           \n\
                        pshufhw $0xB1, %%xmm0, %%xmm0           \n\
                        pshuflw $0xB1, %%xmm1, %%xmm1           \n\
                        pshufhw $0xB1, %%xmm1, %%xmm1           \n\
                        movdqa %%xmm0, %%xmm2                   \n\
                        movdqa %%xmm1, %%xmm3                   \n\
                        psrlw $8, %%xmm0                        \n\
                        psrlw $8, %%xmm1                        \n\
                        psllw $8, %%xmm2                        \n\
                        psllw $8, %%xmm3                        \n\
                        por %%xmm2, %%xmm0                      \n\
                        por %%xmm3, %%xmm1                      \n\
                        subps %%xmm6, %%xmm0                    \n\
                        subps %%xmm6, %%xmm1                    \n\
                        divps %%xmm7, %%xmm0                    \n\
                        divps %%xmm7, %%xmm1                    \n\
                        cvtps2dq %%xmm0, %%xmm0                 \n\
                        cvtps2dq %%xmm1, %%xmm1                 \n\
                        packssdw %%xmm1, %%xmm0                 \n\
                        packuswb %%xmm0, %%xmm0                 \n\
                        movq %%xmm0, -8("EDI","ECX")            \n");
        SSE2_LOOP(  1, "mov -4("ESI","ECX",4), %%eax            \n\
                        bswap %%eax                             \n\
                        movd %%eax, %%xmm0                      \n\
                        subss %%xmm6, %%xmm0                    \n\
                        divss %%xmm7, %%xmm0                    \n\
                        cvtss2si %%xmm0, %%eax                  \n\
                        mov %%al, -1("EDI","ECX")               \n");
        asm("ldmxcsr %0" : : "m" (mxcsr_save));
        break;
      }  // SINGLE

      case DOUBLE: {
        uint32_t mxcsr, mxcsr_save;
        static struct {double d[4];} __attribute__((aligned(16))) double_data;
        double_data.d[0] = double_data.d[1] = pd->double_base;
        double_data.d[2] = double_data.d[3] = pd->double_range/255;
        asm("movdqa ("ESI"), %%xmm6; movdqa 16("ESI"), %%xmm7"
            : : "S" (&double_data), "m" (double_data));
        asm("stmxcsr %0" : "=m" (mxcsr_save));
        mxcsr = mxcsr_save & 0x9FFF;  // round to nearest
        asm("ldmxcsr %0" : : "m" (mxcsr));
        SSE2_LOOP(  4, "movdqa -0x20("ESI","ECX",8), %%xmm0     \n\
                        movdqa -0x10("ESI","ECX",8), %%xmm1     \n\
                        pshuflw $0x1B, %%xmm0, %%xmm0           \n\
                        pshufhw $0x1B, %%xmm0, %%xmm0           \n\
                        pshuflw $0x1B, %%xmm1, %%xmm1           \n\
                        pshufhw $0x1B, %%xmm1, %%xmm1           \n\
                        movdqa %%xmm0, %%xmm2                   \n\
                        movdqa %%xmm1, %%xmm3                   \n\
                        psrlw $8, %%xmm0                        \n\
                        psrlw $8, %%xmm1                        \n\
                        psllw $8, %%xmm2                        \n\
                        psllw $8, %%xmm3                        \n\
                        por %%xmm2, %%xmm0                      \n\
                        por %%xmm3, %%xmm1                      \n\
                        subpd %%xmm6, %%xmm0                    \n\
                        subpd %%xmm6, %%xmm1                    \n\
                        divpd %%xmm7, %%xmm0                    \n\
                        divpd %%xmm7, %%xmm1                    \n\
                        cvtpd2dq %%xmm0, %%xmm0                 \n\
                        cvtpd2dq %%xmm1, %%xmm1                 \n\
                        pslldq $8, %%xmm1                       \n\
                        por %%xmm1, %%xmm0                      \n\
                        packssdw %%xmm0, %%xmm0                 \n\
                        packuswb %%xmm0, %%xmm0                 \n\
                        movd %%xmm0, -4("EDI","ECX")            \n");
        SSE2_LOOP(  1, "mov -8("ESI","ECX",8), %%eax            \n\
                        bswap %%eax                             \n\
                        movd %%eax, %%xmm1                      \n\
                        mov -4("ESI","ECX",8), %%eax            \n\
                        bswap %%eax                             \n\
                        movd %%eax, %%xmm0                      \n\
                        pslldq $4, %%xmm1                       \n\
                        por %%xmm1, %%xmm0                      \n\
                        subsd %%xmm6, %%xmm0                    \n\
                        divsd %%xmm7, %%xmm0                    \n\
                        cvtsd2si %%xmm0, %%eax                  \n\
                        mov %%al, -1("EDI","ECX")               \n");
        asm("ldmxcsr %0" : : "m" (mxcsr_save));
        break;
      }  // DOUBLE

      default:
        return TC_OK;
    }

    return 1;
}

#endif  // USE_DECODE_PVN_SSE2

/*************************************************************************/

static const TCCodecID pvn_codecs_video_in[] = { 
    TC_CODEC_ERROR 
};
static const TCCodecID pvn_codecs_video_out[] = { 
    TC_CODEC_RGB24, TC_CODEC_ERROR 
};
static const TCFormatID pvn_formats_in[] = { 
    TC_FORMAT_PVN, TC_FORMAT_ERROR 
};
static const TCFormatID pvn_formats_out[] = { 
    TC_FORMAT_ERROR 
};
TC_MODULE_AUDIO_UNSUPPORTED(pvn);

TC_MODULE_INFO(pvn);

static const TCModuleClass pvn_class = {
    TC_MODULE_CLASS_HEAD(pvn),

    .init         = pvn_init,
    .fini         = pvn_fini,
    .configure    = pvn_configure,
    .stop         = pvn_stop,
    .inspect      = pvn_inspect,

    /* BIG FAT FIXME */
    .read_video   = pvn_read_video,
};

TC_MODULE_ENTRY_POINT(pvn)

/*************************************************************************/
/*************************************************************************/

/* Old-fashioned module interface. */

static TCModuleInstance mod;

static int verbose_flag;
static int capability_flag = TC_CAP_RGB;
#define MOD_PRE pvn
#define MOD_CODEC "(video) PVN"
#include "import_def.h"

/*************************************************************************/

MOD_open
{
    PrivateData *pd = NULL;

    if (param->flag != TC_VIDEO)
        return TC_ERROR;
    /* XXX */
    if (pvn_init(&mod, TC_MODULE_FEATURE_DEMULTIPLEX) < 0)
        return TC_ERROR;
    pd = mod.userdata;

    if (vob->im_v_codec != TC_CODEC_RGB24) {
        tc_log_error(MOD_NAME, "The import_pvn module requires -V rgb24");
        return TC_ERROR;
    }

    param->fd = NULL;  /* we handle the reading ourselves */
    /* FIXME: stdin should be handled in a more standard fashion */
    if (strcmp(vob->video_in_file, "-") == 0) {  // allow /dev/stdin too?
        pd->fd = 0;
    } else {
        pd->fd = open(vob->video_in_file, O_RDONLY);
        if (pd->fd < 0) {
            tc_log_error(MOD_NAME, "Unable to open %s: %s",
                         vob->video_in_file, strerror(errno));
            pvn_fini(&mod);
            return TC_ERROR;
        }
    }
    if (!parse_pvn_header(pd)) {
        pvn_fini(&mod);
        return TC_ERROR;
    }
    pd->buffer = tc_bufalloc(pd->framesize);
    if (!pd->buffer) {
        tc_log_error(MOD_NAME, "No memory for import frame buffer");
        pvn_fini(&mod);
        return TC_ERROR;
    }

    return TC_OK;
}

/*************************************************************************/

MOD_close
{
    if (param->flag != TC_VIDEO)
        return TC_ERROR;
    pvn_fini(&mod);
    return TC_OK;
}

/*************************************************************************/

MOD_decode
{
    PrivateData *pd = NULL;
    TCFrameVideo vframe;

    if (param->flag != TC_VIDEO)
        return TC_ERROR;
    pd = mod.userdata;

    if (pd->fd < 0) {
        tc_log_error(MOD_NAME, "No file open in decode!");
        return TC_ERROR;
    }

    vframe.video_buf = param->buffer;
    if (pvn_read_video(&mod, &vframe) < 0)
        return TC_ERROR;
    param->size = vframe.video_size;
    return TC_OK;
}

#endif  /* !PROBE_ONLY */

/*************************************************************************/
/*************************************************************************/

#ifdef PROBE_ONLY

#include "tcinfo.h"
#include "tc.h"
#include "magic.h"

void probe_pvn(info_t *ipipe)
{
    PrivateData pd;

    pd.fd = ipipe->fd_in;
    if (!parse_pvn_header(&pd)) {
        ipipe->error = 1;
        return;
    }

    ipipe->probe_info->magic = TC_MAGIC_PVN;
    ipipe->probe_info->codec = TC_CODEC_RGB24;
    ipipe->probe_info->width = pd.width;
    ipipe->probe_info->height = pd.height;
    ipipe->probe_info->fps = pd.framerate;
    ipipe->probe_info->num_tracks = 0;
}

#endif

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
