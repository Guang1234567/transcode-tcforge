/*
 *  tcdecode.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
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

#include "aclib/ac.h"
#include "src/transcode.h"
#include "tccore/tcinfo.h"

#include "libtcutil/xio.h"

#include "ioaux.h"
#include "tc.h"

#include <limits.h>

#define EXE "tcdecode"

extern long fileinfo(int fd, int skip);

int verbose = TC_QUIET;

void import_exit(int code)
{
    if (verbose & TC_DEBUG)
        tc_log_msg(EXE, "(pid=%d) exit (code %d)", (int) getpid(), code);
    exit(code);
}

void version(void)
{
    /* print id string to stderr */
    fprintf(stderr, "%s (%s v%s) (C) 2001-2003 Thomas Oestreich,"
                                    " 2003-2010 Transcode Team\n",
                    EXE, PACKAGE, VERSION);
}

/*************************************************************************/

struct decode_handle {
    const char *name;
    int codec;
    void (*decoder)(decode_t *d);
};

static const struct decode_handle handlers[] = {
    { "mpeg2",   TC_CODEC_MPEG2,   decode_mpeg2 },
    { "ogg",     TC_CODEC_VORBIS,  decode_ogg   },
    { "ac3",     TC_CODEC_AC3,     decode_a52   },
    { "mp3",     TC_CODEC_MP3,     decode_mp3   },
    { "mp2",     TC_CODEC_MP3,     decode_mp2   },
    /* codec intentionally identical */
    { "dv",      TC_CODEC_DV,      decode_dv    },
    { "yuv420p", TC_CODEC_YUV420P, decode_yuv   },
    { "mov",     TC_CODEC_UNKNOWN, decode_mov   },
    /* intentionally unknwon */
    { "lzo",     TC_CODEC_UNKNOWN, decode_lzo   },
    /* intentionally unknwon: lzo1 or lzo2? */
    { "ulaw",    TC_CODEC_ULAW,    decode_ulaw  },
    { NULL,      TC_CODEC_UNKNOWN, NULL },
};

static int decode_stream(const char *codec, decode_t *d)
{
    int i, done = 0;

    for (i = 0; handlers[i].name != NULL; i++) {
        if (!strcmp(codec, handlers[i].name)) {
	        d->codec = handlers[i].codec;
            handlers[i].decoder(d);

            done = 1;
            break;
        }
    }
    return done;
}

/*************************************************************************/

/* ------------------------------------------------------------
 *
 * print a usage/version message
 *
 * ------------------------------------------------------------*/

static void usage(int status)
{
    version();

    fprintf(stderr,"\nUsage: %s [options]\n", EXE);

    fprintf(stderr,"    -i file           input file [stdin]\n");
    fprintf(stderr,"    -x codec          source codec (required)\n");
    fprintf(stderr,"    -t package        codec package\n");
    fprintf(stderr,"    -g wxh            stream frame size [autodetect]\n");
    fprintf(stderr,"    -y format         output raw stream format [rgb]\n");
    fprintf(stderr,"    -Q mode           decoding quality (0=fastest-5=best) [%d]\n", VQUALITY);
    fprintf(stderr,"    -d mode           verbosity mode\n");
    fprintf(stderr,"    -s c,f,r          audio gain for ac3 downmixing [1,1,1]\n");
    fprintf(stderr,"    -A n              A52 decoder flag [0]\n");
    fprintf(stderr,"    -C s,e            decode only from start to end ((V) frames/(A) bytes) [all]\n");
    fprintf(stderr,"    -Y                use libdv YUY2 decoder mode\n");
    fprintf(stderr,"    -z r              convert zero padding to silence\n");
    fprintf(stderr,"    -X type[,type]    override CPU acceleration flags (for debugging)\n");
    fprintf(stderr,"    -v                print version\n");

    exit(status);
}

/* ------------------------------------------------------------
 *
 * universal decode thread frontend
 *
 * ------------------------------------------------------------*/

#define CHECK_OPT do { \
    if (optarg[0] == '-') \
        usage(EXIT_FAILURE); \
} while (0)

int main(int argc, char *argv[])
{
    const char *codec = NULL, *format = "rgb", *magic = "none";
    decode_t decode;
    int ch, done=0;

    memset(&decode, 0, sizeof(decode));
    decode.magic          = TC_MAGIC_UNKNOWN;
    decode.stype          = TC_STYPE_UNKNOWN;
    decode.quality        = VQUALITY;
    decode.ac3_gain[0]    = 1.0;
    decode.ac3_gain[1]    = 1.0;
    decode.ac3_gain[2]    = 1.0;
    decode.frame_limit[0] = 0;
    decode.frame_limit[1] = LONG_MAX;
    decode.accel   = AC_ALL;

    libtc_init(&argc, &argv);

    while ((ch = getopt(argc, argv, "Q:t:d:x:i:a:g:vy:s:YC:A:X:z:?h")) != -1) {
        switch (ch) {
          case 'i':
            CHECK_OPT;
	        decode.name = optarg;
            break;
          case 'd':
            CHECK_OPT;
	        verbose = atoi(optarg);
            break;
          case 'Q':
            CHECK_OPT;
	        decode.quality = atoi(optarg);
            break;
          case 'A':
            CHECK_OPT;
	        decode.a52_mode = atoi(optarg);
            break;
	      case 'x':
            CHECK_OPT;
	        codec = optarg;
            break;
          case 't':
            CHECK_OPT;
	        magic = optarg;
            break;
          case 'y':
            CHECK_OPT;
	        format = optarg;
            break;
          case 'g':
            CHECK_OPT;
	        if (2 != sscanf(optarg,"%dx%d", &decode.width, &decode.height))
                usage(EXIT_FAILURE);
            break;
          case 'v':
            version();
            exit(0);
	        break;
          case 'Y':
            decode.dv_yuy2_mode=1;
            break;
          case 's':
            CHECK_OPT;
	        if (3 != sscanf(optarg,"%lf,%lf,%lf",
                            &decode.ac3_gain[0], &decode.ac3_gain[1], &decode.ac3_gain[2]))
                usage(EXIT_FAILURE);
            break;
          case 'C':
            CHECK_OPT;
	        if (2 != sscanf(optarg,"%ld,%ld",
                            &decode.frame_limit[0], &decode.frame_limit[1]))
                usage(EXIT_FAILURE);
            if (decode.frame_limit[0] >= decode.frame_limit[1]) {
                tc_log_error(EXE, "Invalid -C options");
                usage(EXIT_FAILURE);
	        }
	        break;
          case 'X':
            CHECK_OPT;
            int parsed = ac_parseflags(optarg, &(decode.accel));
            if (!parsed) {
                tc_log_error(EXE, "Invalid -X options");
                usage(EXIT_FAILURE);
            }
          case 'z':
            CHECK_OPT;
	        decode.padrate = atoi(optarg);
            break;
          case 'h':
            usage(EXIT_SUCCESS);
          default:
            usage(EXIT_FAILURE);
        }
    }

    ac_init(decode.accel);

    /* ------------------------------------------------------------
     * fill out defaults for info structure
     * ------------------------------------------------------------*/

    // assume defaults
    if (decode.name == NULL)
        decode.stype = TC_STYPE_STDIN;

    // no autodetection yet
    if (codec == NULL) {
        tc_log_error(EXE, "codec must be specified");
    	usage(EXIT_FAILURE);
    }

    // do not try to mess with the stream
    if (decode.stype == TC_STYPE_STDIN) {
        decode.fd_in = STDIN_FILENO;
    } else {
	    if (tc_file_check(decode.name))
            exit(1);
        decode.fd_in = xio_open(decode.name, O_RDONLY);
        if (decode.fd_in < 0) {
    	    tc_log_perror(EXE, "open file");
	        exit(1);
	    }

	    // try to find out the filetype
    	decode.magic = fileinfo(decode.fd_in, 0);
	    if (verbose)
	        tc_log_msg(EXE, "(pid=%d) %s", getpid(), filetype(decode.magic));
    }

    decode.fd_out  = STDOUT_FILENO;
    decode.codec   = TC_CODEC_UNKNOWN;
    decode.verbose = verbose;
    decode.width   = TC_MAX(0, decode.width);
    decode.height  = TC_MAX(0, decode.height);

    /* ------------------------------------------------------------
     * output raw stream format
     * ------------------------------------------------------------*/

    if (!strcmp(format, "rgb")) decode.format = TC_CODEC_RGB24;
    else if (!strcmp(format, "yuv420p")) decode.format = TC_CODEC_YUV420P;
    else if (!strcmp(format, "yuv2")) decode.format = TC_CODEC_YUV2;
    else if (!strcmp(format, "yuy2")) decode.format = TC_CODEC_YUY2;
    else if (!strcmp(format, "pcm")) decode.format = TC_CODEC_PCM;
    else if (!strcmp(format, "raw")) decode.format = TC_CODEC_RAW;

    /* ------------------------------------------------------------
     * codec specific section
     * note: user provided values overwrite autodetection!
     * ------------------------------------------------------------*/

    // FFMPEG can decode a lot
    if(!strcmp(magic, "ffmpeg") || !strcmp(magic, "lavc")) {
        /* FIXME: swotch to tabel or, better, to tccodecs facilities */
    	if (!strcmp(codec, "mpeg2"))           decode.codec = TC_CODEC_MPEG2;
	    else if (!strcmp(codec, "mpeg2video")) decode.codec = TC_CODEC_MPEG2;
    	else if (!strcmp(codec, "mpeg1video")) decode.codec = TC_CODEC_MPEG1;
    	else if (!strcmp(codec, "divx3"))      decode.codec = TC_CODEC_DIVX3;
    	else if (!strcmp(codec, "divx"))       decode.codec = TC_CODEC_DIVX4;
    	else if (!strcmp(codec, "divx4"))      decode.codec = TC_CODEC_DIVX4;
    	else if (!strcmp(codec, "mp42"))       decode.codec = TC_CODEC_MP42;
    	else if (!strcmp(codec, "mjpg"))       decode.codec = TC_CODEC_MJPEG;
        else if (!strcmp(codec, "mjpeg"))      decode.codec = TC_CODEC_MJPEG;
    	else if (!strcmp(codec, "rv10"))       decode.codec = TC_CODEC_RV10;
    	else if (!strcmp(codec, "svq1"))       decode.codec = TC_CODEC_SVQ1;
    	else if (!strcmp(codec, "svq3"))       decode.codec = TC_CODEC_SVQ3;
    	else if (!strcmp(codec, "vp3"))        decode.codec = TC_CODEC_VP3;
    	else if (!strcmp(codec, "4xm"))        decode.codec = TC_CODEC_4XM;
    	else if (!strcmp(codec, "wmv1"))       decode.codec = TC_CODEC_WMV1;
    	else if (!strcmp(codec, "wmv2"))       decode.codec = TC_CODEC_WMV2;
    	else if (!strcmp(codec, "hfyu"))       decode.codec = TC_CODEC_HUFFYUV;
    	else if (!strcmp(codec, "indeo3"))     decode.codec = TC_CODEC_INDEO3;
    	else if (!strcmp(codec, "h263p"))      decode.codec = TC_CODEC_H263P;
    	else if (!strcmp(codec, "h263i"))      decode.codec = TC_CODEC_H263I;
    	else if (!strcmp(codec, "dvvideo"))    decode.codec = TC_CODEC_DV;
    	else if (!strcmp(codec, "dv"))         decode.codec = TC_CODEC_DV;
    	else if (!strcmp(codec, "vag"))        decode.codec = TC_CODEC_VAG;

	    decode_lavc(&decode);
    }

    done = decode_stream(codec, &decode);

    if (!done) {
	    tc_log_error(EXE, "(pid=%d) unable to handle codec %s", getpid(), codec);
	    exit(1);
    }

    if (decode.fd_in != STDIN_FILENO)
        xio_close(decode.fd_in);

    return 0;
}

#include "libtcutil/static_xio.h"

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
