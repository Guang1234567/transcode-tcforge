/*
 *  avifix.c
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

#include "transcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "avilib/avilib.h"
#include "avimisc.h"

#define EXE "avifix"

void version(void)
{
    printf("%s (%s v%s) (C) 2001-2003 Thomas Oestreich,"
                          " 2003-2010 Transcode Team\n",
                        EXE, PACKAGE, VERSION);
}

static void usage(int status)
{
    version();
    printf("\nUsage: %s [options]\n", EXE);
    printf("    -i name           AVI file name\n");
    printf("    -F string         video codec FOURCC\n");
    printf("    -f val1,val2      video frame rate (fps=val1/val2)\n");
    printf("    -N 0xnn           audio format identifier\n");
    printf("    -b bitrate        audio encoder bitrate (kbps)\n");
    printf("    -e r[,b[,c]]      audio stream parameter (samplerate,bits,channels)\n");
    printf("    -a num            audio track number [0]\n");
    printf("    -d                print debug information\n");
    printf("    -v                print version\n");
    exit(status);
}

/* read or die! */
static void hdr_read(const char *tag, int fd, void *hdr, size_t bytes)
{
    ssize_t r = read(fd, hdr, bytes);
    if (bytes != r) {
        tc_log_error(EXE, "(%s) error reading AVI-file", tag);
        exit(1);
    }
}

/* write or die! */
static void hdr_write(const char *tag, int fd, const void *hdr, size_t bytes)
{
    ssize_t w = write(fd, hdr, bytes);
    if (bytes != w) {
        tc_log_error(EXE, "(%s) error writing AVI-file", tag);
        exit(1);
    }
}


#define VALIDATE_OPTION if (optarg[0]=='-') usage(EXIT_FAILURE)

enum {
    /* video in the lower bits */
    CHANGE_VIDEO_FOURCC = (1UL),
    CHANGE_VIDEO_FPS    = (1UL << 1),
    /* audio in the higher bits */
    CHANGE_AUDIO_FMT    = (1UL << 16),
    CHANGE_AUDIO_BR     = (1UL << 17),
    CHANGE_AUDIO_RATE   = (1UL << 18),
    CHANGE_AUDIO_BITS   = (1UL << 19),
    CHANGE_AUDIO_CHANS  = (1UL << 20)
};
#define CHANGE_NOTHING      (0UL)
#define NEED_AUDIO_CHANGE(FLAGS)    (FLAGS & 0xFFFF0000)
#define NEED_VIDEO_CHANGE(FLAGS)    (FLAGS & 0x0000FFFF)



int main(int argc, char *argv[])
{
    struct common_struct rtf;
    struct AVIStreamHeader ash, vsh;
    avi_t *avifile;
    int err, fd, id = 0, track_num = 0, n, ch, debug = TC_FALSE;
    int brate = 0, val1 = 0, val2 = 1, a_rate, a_chan, a_bits;
    long ah_off = 0, af_off = 0, vh_off = 0, vf_off = 0;
    char codec[5], *str = NULL, *filename = NULL;
    uint32_t change = CHANGE_NOTHING;

    ac_init(AC_ALL);

    if (argc==1) usage(EXIT_FAILURE);

    while ((ch = getopt(argc, argv, "df:i:N:F:vb:e:a:?h")) != -1) {
        switch (ch) {
          case 'N':
            VALIDATE_OPTION;
            id = strtol(optarg, NULL, 16);
            if (id <  0) {
                tc_log_error(EXE, "invalid parameter set for option -N");
            } else {
                change |= CHANGE_AUDIO_FMT;
            }
            break;

          case 'a':
            VALIDATE_OPTION;
            track_num = atoi(optarg);
            if (track_num < 0)
                usage(EXIT_FAILURE);
            break;

          case 'f':
            VALIDATE_OPTION;
	        n = sscanf(optarg,"%d,%d", &val1, &val2);
            if (n != 2 || val1 < 0 || val2 < 0) {
                tc_log_error(EXE, "invalid parameter set for option -f");
            } else {
                change |= CHANGE_VIDEO_FPS;
            }
            break;

          case 'F':
            VALIDATE_OPTION;
            str = optarg;
            if(strlen(str) > 4 || strlen(str) == 0) {
                tc_log_error(EXE, "invalid parameter set for option -F");
            } else {
                change |= CHANGE_VIDEO_FOURCC;
            }
            break;

          case 'i':
            VALIDATE_OPTION;
            filename = optarg;
            break;

          case 'b':
            VALIDATE_OPTION;
            brate = atoi(optarg);
            change |= CHANGE_AUDIO_BR;
            break;

          case 'v':
            version();
            exit(0);

          case 'e':
            VALIDATE_OPTION;
            n = sscanf(optarg,"%d,%d,%d", &a_rate, &a_bits, &a_chan);
            switch (n) {
              case 3:
                change |= CHANGE_AUDIO_RATE;
              case 2:
                change |= CHANGE_AUDIO_BITS;
              case 1:
                change |= CHANGE_AUDIO_CHANS;
                break;
              default:
                tc_log_error(EXE, "invalid parameter set for option -e");
            }
            break;

          case 'd':
            debug = TC_TRUE;
            break;
          case 'h':
            usage(EXIT_SUCCESS);
          default:
            usage(EXIT_FAILURE);
        }
    }

    if (!filename)
        usage(EXIT_FAILURE);

    tc_log_info(EXE, "scanning AVI-file %s for header information", filename);

    avifile = AVI_open_input_file(filename, 1);
    if (!avifile) {
        AVI_print_error("AVI open");
        exit(1);
    }

    AVI_info(avifile);

    if (AVI_set_audio_track(avifile, track_num) < 0) {
        tc_log_error(EXE, "invalid audio track");
    }

    ah_off = AVI_audio_codech_offset(avifile);
    af_off = AVI_audio_codecf_offset(avifile);
    vh_off = AVI_video_codech_offset(avifile);
    vf_off = AVI_video_codecf_offset(avifile);

    if (debug) {
        tc_log_info(EXE,
                    "offsets: ah=%li af=%li vh=%li vf=%li",
                    ah_off, af_off, vh_off, vf_off);
    }

    AVI_close(avifile);

    fd = open(filename, O_RDWR);
    if (fd < 0) {
        perror("open");
        exit(1);
    }

    lseek(fd, vh_off, SEEK_SET);
    hdr_read("video codec [h]", fd, codec, 4);
    codec[4] = 0;

    lseek(fd, vf_off, SEEK_SET);
    hdr_read("video codec [f]", fd, codec, 4);
    codec[4] = 0;

    if (change & CHANGE_VIDEO_FPS) {
        lseek(fd, vh_off-4, SEEK_SET);
        hdr_read("video fps", fd, &vsh, sizeof(vsh));

	    vsh.dwRate  = (long)val1;
	    vsh.dwScale = (long)val2;

        lseek(fd, vh_off-4, SEEK_SET);
        hdr_write("video fps", fd, &vsh, sizeof(vsh));
    }

    if (change & CHANGE_VIDEO_FOURCC) {
        lseek(fd,vh_off,SEEK_SET);

        if (strncmp(str,"RGB",3) == 0) {
            hdr_write("video 4cc", fd, codec, 4);
        } else {
            hdr_write("video 4cc", fd, str, 4);
        }

        lseek(fd,vf_off,SEEK_SET);

        if(strncmp(str,"RGB",3)==0) {
	        memset(codec, 0, 4);
            hdr_write("video 4cc", fd, codec, 4);
        } else {
            hdr_write("video 4cc", fd, str, 4);
        }
    }

    if (NEED_AUDIO_CHANGE(change)) {
        lseek(fd, ah_off, SEEK_SET);
        hdr_read("audio header [h]", fd, &ash, sizeof(ash));

        lseek(fd, af_off, SEEK_SET);
        hdr_read("audio header [f]", fd, &rtf, sizeof(rtf));

        if (change & CHANGE_AUDIO_FMT) {
            rtf.wFormatTag = (unsigned short) id;
        }
        if (change & CHANGE_AUDIO_BR) {
	        rtf.dwAvgBytesPerSec = (long) 1000*brate/8;
        	ash.dwRate = (long) 1000*brate/8;
	        ash.dwScale = 1;
        }
        if (change & CHANGE_AUDIO_CHANS) {
            rtf.wChannels = (short) a_chan;
        }
        if (change & CHANGE_AUDIO_BITS) {
            rtf.wBitsPerSample = (short) a_bits;
        }
        if (change & CHANGE_AUDIO_RATE) {
            rtf.dwSamplesPerSec = (long) a_rate;
        }

        lseek(fd, ah_off ,SEEK_SET);
        hdr_write("audio header [h]", fd, &ash, sizeof(ash));
        lseek(fd, af_off ,SEEK_SET);
        hdr_write("audio header [f]", fd, &rtf, sizeof(rtf));
    }

    err = close(fd);
    if (err) {
        perror("close");
        exit(1);
    }

    avifile = AVI_open_input_file(filename, 1);
    if (!avifile) {
        AVI_print_error("AVI open");
        exit(1);
    }

    tc_log_info(EXE, "updated AVI file %s", filename);

    AVI_info(avifile);

    AVI_close(avifile);

    return 0;
}

#include "libtcutil/static_tcutil.h"

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
