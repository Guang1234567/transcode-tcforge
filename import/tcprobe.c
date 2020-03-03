/*
 *  tcprobe.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  updated by
 *  Francesco Romani - April 206
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

#include "src/transcode.h"
#include "tccore/tcinfo.h"
#include "libtc/libtc.h"
#include "libtc/ratiocodes.h"
#include "libtcutil/xio.h"

#include "ioaux.h"
#include "tc.h"
#include "demuxer.h"
#include "dvd_reader.h"
#include "x11source.h"

#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>


#define EXE "tcprobe"

int verbose = TC_INFO;

int bitrate = ABITRATE;
int binary_dump = 0;

void import_exit(int code)
{
    if (verbose >= TC_DEBUG) {
        tc_log_msg(EXE, "(pid=%d) exit (code %d)", (int) getpid(), code);
    }
    exit(code);
}

/*************************************************************************/

/*
 * enc_bitrate:  Print bitrate information about the source data.
 *
 * Parameters:
 *       frames: Number of frames in the source.
 *          fps: Frames per second of the source.
 *     abitrate: Audio bitrate (bits per second).
 *     discsize: User-specified disc size in bytes, or 0 for none.
 * Return value:
 *     None.
 * Notes:
 *     This function is copied from tcscan.c.  Ideally, tcprobe should
 *     only print basic source information, and this extended information
 *     should be handled by tcscan (or alternatively, tcscan should be
 *     merged into tcprobe).
 */

static void enc_bitrate(long frames, double fps, int abitrate, double discsize)
{
    static const int defsize[] = { 650, 700, 1300, 1400 };
    double audiosize, videosize, vbitrate;
    long time;

    if (frames <= 0 || fps <= 0.0) {
	    return;
    }
    time = frames / fps;
    audiosize = (double)abitrate/8 * time;

    /* Print basic source information */
    printf("V: %ld frames, %ld sec @ %.3f fps\n",
           frames, time, fps);
    printf("A: %.2f MB @ %d kbps\n",
           audiosize/(1024*1024), abitrate/1000);

    /* Print recommended bitrates for user-specified or default disc sizes */
    if (discsize) {
        videosize = discsize - audiosize;
        vbitrate = videosize / time;
        printf("USER CDSIZE: %4d MB | V: %6.1f MB @ %.1f kbps\n",
               (int)floor(discsize/(1024*1024)), videosize/(1024*1024),
               vbitrate);
    } else {
        int i;
        for (i = 0; i < sizeof(defsize) / sizeof(*defsize); i++) {
            videosize = defsize[i] - audiosize;
            vbitrate = videosize / time;
            printf("USER CDSIZE: %4d MB | V: %6.1f MB @ %.1f kbps\n",
                   defsize[i], videosize/(1024*1024),
                   vbitrate);
        }
    }
}

/*************************************************************************/

/* 
 * we don't want to scan the full directory since it can be a HUGE number
 * of entries (think to images -> clip transcoding [i.e. jpeg -> AVI])
 */
#define TC_SCAN_MAX_FILES       32

/* informations for one stream found in given directory */
typedef struct tcdirentryinfo_ TCDirEntryInfo;
struct tcdirentryinfo_ {
    uint32_t magic;
    size_t count;
    /* how many times a file with this magic was found on directory? */ 
    int fd;
    /* file descriptor of the FIRST file encountered with this magic */
};

/*
 * tc_entry_find_magic:
 *      find the element in an array of TCDirEntryInfo strucutes
 *      that has it's magic equals to a given magic id.
 *
 * Parameters:
 *      info: pointer to an array of TCDirEntryInfo strucutre to be scanned.
 *      size: number of elements in given array.
 *     magic: magic ID to find
 * Return value:
 *      -1: given magic ID not found on given array.
 *     >=0: index in array of element with same magic ID as given one.
 */
static int tc_entry_info_find_magic(const TCDirEntryInfo *infos,
                                    size_t size, uint32_t magic)
{
    int ret = -1;
    size_t i = 0;

    if (infos != NULL && size > 0) {
        for (i = 0; i < size; i++) {
            if (infos[i].magic == magic) {
                ret = (int)i;
                break;
            }
        }
    }
    return ret;
}
/*
 * tc_entry_find_max_count:
 *      find the element in an array of TCDirEntryInfo strucutes
 *      that has the maximum count value.
 *
 * Parameters:
 *      info: pointer to an array of TCDirEntryInfo strucutre to be scanned.
 *      size: number of elements in given array.
 * Return value:
 *      index in array of element with most high 'count' value.
 */
static int tc_entry_info_find_max_count(const TCDirEntryInfo *infos,
                                        size_t size)
{
    int ret = 0; /* start pointing to first element */
    size_t i = 0;

    if (infos != NULL && size > 0) {
        for (i = 1; i < size; i++) {
            if (infos[i].count > infos[ret].count) {
                ret = (int)i;
            }
        }
    }
    return ret;
}

/*
 * tc_entry_info_free:
 *      release resources linked to a TCDirEntryInfo structure.
 *
 * Parameters:
 *      de: pointer to a TCDirEntryInfo to be released.
 * Return value:
 *      None
 */
static void tc_entry_info_free(TCDirEntryInfo *de)
{
    if (de != NULL) {
        xio_close(de->fd);
        de->fd = -1;
    }
}

/*
 * tc_scan_directory_info:
 *      Partially scan a given directory and optionally filla TCDirEntryInfo
 *      structure with data about most common stream format found in.
 *
 *      Partial scanning is done in order to avoid to waste too much
 *      time/resources in scanning phase. Anyway, partial scan ti's supposed
 *      to give results reliable enough.
 *      Filled TCDirEntryInfo structure will have an already open file
 *      descriptor pointing to the first file of the biggest set of files
 *      with the same magic id.
 *
 *      Use tc_entry_info_free() to release resources acquired using this
 *      function, do not free()/close() things by hand to avoid undefined
 *      behaviours.
 *
 * Parameters:
 *          dname: path of dicrectory to scan.
 *      candidate: optional pointer of TCDirEntryInfo structure to be filled.
 *                 can safely be NULL.
 * Return value:
 *      -1: internal error
 *       0: succesfull, but directory seems to have mixed content
 *       1: succesfull, and directory seems to have homogeneous content
 * Side effects:
 *      Some files (first TC_SCAN_MAX_FILES in filesystem order) in directory
 *      are open and scanned to detect their magic number.
 */
static int tc_scan_directory_info(const char *dname,
                                  TCDirEntryInfo *candidate)
{
    TCDirEntryInfo dinfo[TC_SCAN_MAX_FILES];
    struct dirent *entry = NULL;
    size_t i = 0, j = 0, last = 0, probed = 0;
    int ret = -1;
    DIR *dir = opendir(dname);

    /* base sanity check first */
    if (dir == NULL) {
        return -1;
    }

    /* round one: collect some stuff */
    for (i = 0; i < TC_SCAN_MAX_FILES; i++) {
        char path_buf[PATH_MAX + 1];
        uint32_t magic = TC_MAGIC_UNKNOWN;
        struct stat stat_buf;
        int fd = -1, err = 0;

        entry = readdir(dir);
        if (entry == NULL) {
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            /* it's safe to skip them */
            continue;
        }

        tc_snprintf(path_buf, sizeof(path_buf), "%s/%s",
                    dname, entry->d_name);
        err = stat(path_buf, &stat_buf);
        if (err || (!S_ISREG(stat_buf.st_mode) && !S_ISDIR(stat_buf.st_mode))) {
            if (verbose >= TC_DEBUG) { /* uhm */
                tc_log_warn(EXE, "opening '%s': is not a file",
                            path_buf);
            }
            continue;
        }
        if (S_ISDIR(stat_buf.st_mode)) {
            if (strcmp(entry->d_name, "VIDEO_TS") == 0) {
                /* dname is a DVD image directory */
                magic = TC_MAGIC_DVD;
            } else {
                continue;
            }
        } else {  // S_ISREG(stat_buf.st_mode)
            fd = xio_open(path_buf, O_RDONLY);
            if (fd == -1) {
                /* switch to tc_log_perror? */
                tc_log_error(EXE, "opening '%s': %s",
                             path_buf, strerror(errno));
                continue; /* assume non-fatal error */
            }
            magic = fileinfo(fd, 0);
        }
        j = tc_entry_info_find_magic(dinfo, last, magic);
        if (j != -1) { /* entry already encountered */
            dinfo[j].count++;
            xio_close(fd);
            /* we want only the first file descriptor of a given set */
        } else {
            dinfo[last].fd = fd;
            dinfo[last].magic = magic;
            dinfo[last].count = 1;
            last++;
        }
        probed++;
    }
    closedir(dir);

    /* round two: let's make a choice */
    if (last > 0) { /* at least one file scanned succesfully */
        if (last == 1) {
            j = 0; /* pretty simple, uh? ;) */
            ret = 1; /* obviously homogeneous */
        } else {
            j = tc_entry_info_find_max_count(dinfo, last);
            /* save only candidate entry info */
            for (i = 0; i < last; i++) {
                if (i != j) {
                    tc_entry_info_free(&dinfo[i]);
                }
            }
            if (dinfo[j].count < probed + 1) {
                ret = 0;
            } else {
                ret = 1;
            }
        }
        if (candidate != NULL) {
            candidate->fd = dinfo[j].fd;
            candidate->magic = dinfo[j].magic;
            candidate->count = dinfo[j].count;
        }
    }
    return ret;
}


#define OPEN_FILE(ipipe, name) do { \
    (ipipe)->fd_in = xio_open((name), O_RDONLY); \
    if ((ipipe)->fd_in < 0) { \
        tc_log_perror(EXE, "file open"); \
        return TC_IMPORT_ERROR; \
    } \
} while (0)
 
#define PROBE_DIR(ipipe) do { \
    TCDirEntryInfo info; \
    int ret = tc_scan_directory_info((ipipe)->name, &info); \
    if (ret < 0) { \
        tc_log_error(EXE, "unrecognized filetype for '%s'", \
                     (ipipe)->name); \
        return TC_IMPORT_ERROR; \
    } else if (ret == 0) { \
        tc_log_warn(EXE, "non-homogeneous directory content" \
                         " (different stream type detected)"); \
    } \
    (ipipe)->fd_in = info.fd; \
    (ipipe)->magic = info.magic; \
} while (0)

/*
 * info_setup:
 *
 *      perform second-step initialization on a info_t  structure.
 *      While first-step setup can be done statically with simple
 *      assignements, intialization performedon this stage is based
 *      on data given previosuly (i.e.: name).
 *      This function is a catch-all for all the black magic things.
 *      Of course there is still a lot of room for cleaning up and
 *      refactoring.
 *      My thought is that doing things in a _really_ clean way means
 *      rewriting almoost from the ground up the probing infrastructure.
 *
 * Parameters:
 *         ipipe: info_t structure to (finish to) initialize
 *          skip: amount of bytes to skip when analyzing a regular file.
 *                Ignored otherwise
 * mplayer_probe: if input it's a regular file, do the real probing through
 *                mplayer (if avalaible); ignored otherwise.
 *      want_dvd: if !0 and the source looks likea DVD, handle it like a DVD.
 *                I know this is a bit obscure and maybe even sick, it's
 *                legacy and should go away in the future.
 * Return Value:
 *      TC_IMPORT_OK -> succesfull,
 *      TC_IMPORT_ERROR -> otherwise.
 *      messages are sent to user using tc_log_*() in both cases.
 * Side effects:
 *      quite a lot =)
 *      Input source is open and read to guess the source type.
 *      This function can do (and usually does) several read attempts.
 * Preconditions:
 *      given info_t structure is already basically initialized (see
 *      first-step setup above). This measn set at least:
 *          ipipe.verbose, ipipe.fd_in, ipipe.name = name;
 * Postconditions:
 *      given info_t is ready to be used in probe_stream()
 *
 */
static int info_setup(info_t *ipipe, int skip, int mplayer_probe, int want_dvd)
{
    int file_kind = tc_probe_path(ipipe->name);

    switch (file_kind) {
      case TC_PROBE_PATH_FILE:	/* regular file */
        if (mplayer_probe) {
            ipipe->magic = TC_MAGIC_MPLAYER;
        } else if (want_dvd && dvd_is_valid(ipipe->name)) {
            ipipe->magic = TC_MAGIC_DVD;
        } else {
            OPEN_FILE(ipipe, ipipe->name);
            ipipe->magic = fileinfo(ipipe->fd_in, skip);
            ipipe->seek_allowed = 1;
        }
        break;
      case TC_PROBE_PATH_RELDIR:        /* relative path to directory */
        PROBE_DIR(ipipe);
        break;
      case TC_PROBE_PATH_ABSPATH:       /* absolute path */
        if (dvd_is_valid(ipipe->name)) {
            ipipe->magic = TC_MAGIC_DVD;
        } else {
            PROBE_DIR(ipipe);
        }
        break;
      /* now the easy stuff */
      case TC_PROBE_PATH_BKTR:	/* bktr device */
        ipipe->magic = TC_MAGIC_BKTR_VIDEO;
        break;
      case TC_PROBE_PATH_SUNAU:	/* sunau device */
        ipipe->magic = TC_MAGIC_SUNAU_AUDIO;
        break;
      case TC_PROBE_PATH_OSS:	/* OSS device */
        ipipe->magic = TC_MAGIC_OSS_AUDIO;
        break;
      case TC_PROBE_PATH_V4L_VIDEO:	/* v4l video device */
        ipipe->magic = TC_MAGIC_V4L_VIDEO;
        break;
      case TC_PROBE_PATH_V4L_AUDIO:	/* v4l audio device */
        ipipe->magic = TC_MAGIC_V4L_AUDIO;
        break;
      case TC_PROBE_PATH_INVALID:	/* non-existent source */
      default:                      /* fallthrough */
        tc_log_error(EXE, "can't determine the file kind");
        return TC_IMPORT_ERROR;
    } /* probe_path */
    return TC_IMPORT_OK;
}

#undef OPEN_FILE
#undef PROBE_DIR

/*
 * info_teardown:
 *
 *      reverse initialization done in info_setup
 *
 * Parameters:
 *      ipipe: info_t structure to (finish to) initialize
 * Return Value:
 *      None
 */
static void info_teardown(info_t *ipipe)
{
    if (ipipe->fd_in != STDIN_FILENO) {
        xio_close(ipipe->fd_in);
    }
}

/*************************************************************************/

/* new fancy output handlers */

/*
 * generic info dump function handler
 */
typedef void (*InfoDumpFn)(info_t *ipipe);

/*
 * dump_info_binary:
 *
 *      dump a ProbeInfo structure in binary (and platform-dependent,
 *      and probably even not fully safe) way to stdout.
 *      This dump mode is used by tcprobe to communicate with transcode.
 *      Legacy, I'd like to change this communication mode in future
 *      releases.
 *
 * Parameters:
 *      ipipe: info_t structure holding the ProbeInfo data to dump.
 * Return Value:
 *      None
 */
static void dump_info_binary(info_t *ipipe)
{
    pid_t pid = getpid();
    tc_pwrite(STDOUT_FILENO, (uint8_t *) &pid, sizeof(pid_t));
    tc_pwrite(STDOUT_FILENO, (uint8_t *) ipipe->probe_info,
              sizeof(ProbeInfo));
}


#define PROBED_NEW  "(*)"   /* value different from tc's defaults */
#define PROBED_STD  ""      /* value equals to tc's defaults */

/*
 * user mode:
 * recommended transcode command line options:
 */
#define MARK_EXPECTED(ex) ((ex) ?(PROBED_STD) :(PROBED_NEW))
#define CHECK_MARK_EXPECTED(probed, val) \
        (((val) == (probed)) ?(PROBED_STD) :(PROBED_NEW))

/*
 * dump_info_old:
 *      dump a ProbeInfo structure in a human-readable format.
 *
 * Parameters:
 *      ipipe: info_t structure holding the ProbeInfo data to dump.
 * Return Value:
 *      None
 */

static void dump_info_old(info_t *ipipe)
{
    long frame_time = 0;
    int is_std = TC_TRUE; /* flag: select PROBED_??? above */
    int nsubs = 0, i = 0;
    char extrabuf[TC_BUF_MIN] = { '\0' };
    int extrabuf_ready = TC_FALSE;
    size_t len = 0;

    /* full-blown back-compatibility */
    fprintf(stderr, "[%s] %s\n", EXE, filetype(ipipe->magic));
    printf("[%s] summary for %s, %s = not default, 0 = not detected\n",
           EXE, ((ipipe->magic == TC_STYPE_STDIN) ?"-" :ipipe->name),
           PROBED_NEW);

    if (ipipe->probe_info->width != PAL_W
     || ipipe->probe_info->height != PAL_H) {
        is_std = TC_FALSE;
    }

    /* video first. */
    if (ipipe->probe_info->width > 0 && ipipe->probe_info->height > 0) {
        int n, d, ret;

        extrabuf_ready = TC_FALSE;

        printf("%18s %s %dx%d [%dx%d] %s\n",
               "import frame size:", "-g",
               ipipe->probe_info->width, ipipe->probe_info->height,
               PAL_W, PAL_H, MARK_EXPECTED(is_std));
 
        ret = tc_asr_code_to_ratio(ipipe->probe_info->asr, &n, &d);
        if (ret != TC_NULL_MATCH && (n > 0 && d > 0)) {
            /* back compatibility little hack */
            printf("%18s %i:%i %s\n",
                   "aspect ratio:", n, d,
                   CHECK_MARK_EXPECTED(ipipe->probe_info->asr, 1));
        }

        frame_time = (ipipe->probe_info->fps != 0) ?
                     (long)(1. / ipipe->probe_info->fps * 1000) : 0;

        printf("%18s %s %.3f [%.3f] frc=%d %s\n", "frame rate:", "-f",
               ipipe->probe_info->fps, PAL_FPS, ipipe->probe_info->frc,
               CHECK_MARK_EXPECTED(ipipe->probe_info->frc, 3));

        tc_snprintf(extrabuf, sizeof(extrabuf), "%18s ", "");
        /* empty string to have a nice justification */
        /* video track extra info */
        if (ipipe->probe_info->pts_start) {
            len = strlen(extrabuf);
            tc_snprintf(extrabuf + len, sizeof(extrabuf) - len,
                        "PTS=%.4f, frame_time=%ldms",
                        ipipe->probe_info->pts_start, frame_time);
            if (ipipe->probe_info->bitrate) {
                len = strlen(extrabuf);
                tc_snprintf(extrabuf + len, sizeof(extrabuf) - len,
                            "%sbitrate=%li kbps",
                            (extrabuf_ready) ?", " :" ",
                            /*
                             * add seeparator only if we alread
                             * written something in buffer
                             */
                            ipipe->probe_info->bitrate);
            }
            /* at this point extrabuf flag will always be set to on */
            extrabuf_ready = TC_TRUE;
        }
        if (extrabuf_ready) {
            printf("%s\n", extrabuf);
        }
    }

    /* audio next. */
    for (i = 0; i < TC_MAX_AUD_TRACKS; i++) {
        int D_arg = 0, D_arg_ms = 0;
        double pts_diff = 0.;

        if (ipipe->probe_info->track[i].format != 0
         && ipipe->probe_info->track[i].chan > 0) {
            extrabuf_ready = TC_FALSE;
            extrabuf[0] = '\0';

	        if (ipipe->probe_info->track[i].samplerate != RATE
             || ipipe->probe_info->track[i].chan != CHANNELS
             || ipipe->probe_info->track[i].bits != BITS
             || ipipe->probe_info->track[i].format != TC_CODEC_AC3) {
                is_std = TC_FALSE;
            } else {
                is_std = TC_TRUE;
            }

            printf("%18s -a %d [0] -e %d,%d,%d [%d,%d,%d] -n 0x%x [0x%x] %s\n",
                   "audio track:",
                   ipipe->probe_info->track[i].tid,
                   ipipe->probe_info->track[i].samplerate,
                   ipipe->probe_info->track[i].bits,
                   ipipe->probe_info->track[i].chan,
                   RATE, BITS, CHANNELS,
                   ipipe->probe_info->track[i].format,
                   TC_CODEC_AC3,
                   MARK_EXPECTED(is_std));

            /* audio track extra info */
            if (ipipe->probe_info->track[i].pts_start) {
                tc_snprintf(extrabuf, sizeof(extrabuf), "PTS=%.4f",
                            ipipe->probe_info->track[i].pts_start);
                extrabuf_ready = TC_TRUE;
            }
            if (ipipe->probe_info->track[i].bitrate) {
                size_t len = strlen(extrabuf);
                tc_snprintf(extrabuf + len, sizeof(extrabuf) - len,
                            "%sbitrate=%i kbps", (extrabuf_ready) ?", " :"",
                            ipipe->probe_info->track[i].bitrate);
                extrabuf_ready = TC_TRUE;
            }
            if (extrabuf_ready) {
                printf("%18s %s\n",
                       "", /* empty string for a nice justification */
                       extrabuf);
            }

            /* audio track A/V sync suggestion */
            if (ipipe->probe_info->pts_start > 0
             && ipipe->probe_info->track[i].pts_start > 0
             && ipipe->probe_info->fps != 0) {
                pts_diff = ipipe->probe_info->pts_start \
                           - ipipe->probe_info->track[i].pts_start;
                D_arg = (int)(pts_diff * ipipe->probe_info->fps);
                D_arg_ms = (int)((pts_diff - D_arg/ipipe->probe_info->fps)*1000);

	            printf("%18s -D %d --av_fine_ms %d (frames & ms) [0] [0]\n",
                       " ", D_arg, D_arg_ms);
            }
        }
        /* have subtitles here? */
        if (ipipe->probe_info->track[i].attribute & PACKAGE_SUBTITLE) {
            nsubs++;
        }
    }

    /* no audio */
    if (ipipe->probe_info->num_tracks == 0) {
        printf("%18s %s", "no audio track:",
               "(use \"null\" import module for audio)\n");
    }

    if (nsubs > 0) {
        printf("detected (%d) subtitle(s)\n", nsubs);
    }

    /* P-units */
    if (ipipe->probe_info->unit_cnt) {
        printf("detected (%d) presentation unit(s) (SCR reset)\n",
                    ipipe->probe_info->unit_cnt+1);
    }

    /* DVD only: coder bitrate infos */
    if (ipipe->magic == TC_MAGIC_DVD_PAL || ipipe->magic == TC_MAGIC_DVD_NTSC
     || ipipe->magic == TC_MAGIC_DVD) {
        enc_bitrate((long)ceil(ipipe->probe_info->fps * ipipe->probe_info->time),
                     ipipe->probe_info->fps, bitrate*1000, 0);
    } else {
        if (ipipe->probe_info->frames > 0) {
            unsigned long dur_ms;
            unsigned int dur_h, dur_min, dur_s;
            if (ipipe->probe_info->fps < 0.100) {
                dur_ms = (long)ipipe->probe_info->frames*frame_time;
            } else {
                dur_ms = (long)((float)ipipe->probe_info->frames * 1000
                                /ipipe->probe_info->fps);
            }
            dur_h = dur_ms/3600000;
            dur_min = (dur_ms %= 3600000)/60000;
            dur_s = (dur_ms %= 60000)/1000;
            dur_ms %= 1000;
            printf("%18s %ld frames, frame_time=%ld msec,"
                        " duration=%u:%02u:%02u.%03lu\n",
                   "length:",
                   ipipe->probe_info->frames, frame_time,
                   dur_h, dur_min, dur_s, dur_ms);
        }
    }
}


/*
 * dump_track_info_raw:
 *
 *      dump a ProbeTrackInfo structure in a human-readable but machine-friendly
 *      format, resembling, or identical where feasible, the mplayer -identify
 *      output.
 *      Print one field at line, in the format KEY=value.
 *
 * Parameters:
 *      tracks: pointer to ProbeTrackInfo array to be dumped
 *           i: dump only the i-th structure on array.
 * Return Value:
 *      None
 */
static void dump_track_info_raw(ProbeTrackInfo *ti, int i)
{
    if (ti != NULL && i >= 0) { /* paranoia */
        const char *ext = "";
        /* extension to identifiers for non-zero (not first) track */
        char ext_buf[24];
        
        if (i > 0) {
            tc_snprintf(ext_buf, sizeof(ext_buf), "_%i", i);
            ext = ext_buf;
        }
        
        if (ti[i].format != 0 && ti[i].chan > 0) {
            printf("ID_AUDIO_CODEC%s=%s\n", ext,
                   tc_codec_to_string(ti[i].format));
            printf("ID_AUDIO_FORMAT%s=%i\n", ext, ti[i].format);
            printf("ID_AUDIO_BITRATE%s=%i\n", ext, ti[i].bitrate);
            printf("ID_AUDIO_RATE%s=%i\n", ext, ti[i].samplerate);
            printf("ID_AUDIO_NCH%s=%i\n", ext, ti[i].chan);
            printf("ID_AUDIO_BITS%s=%i\n", ext, ti[i].bits);
        }
    }
}

/*
 * dump_info_raw:
 *
 *      dump a ProbeInfo structure in a human-readable but machine-friendly
 *      format, resembling, or identical where feasible, the mplayer -identify
 *      output.
 *      Print one field at line, in the format KEY=value.
 *
 * Parameters:
 *      ipipe: info_t structure holding the ProbeInfo data to dump.
 * Return Value:
 *      None
 */
static void dump_info_raw(info_t *ipipe)
{
    int i;
    double duration = 0.0; /* seconds */

    /* general information */
    printf("ID_FILENAME=\"%s\"\n", ipipe->name);
    printf("ID_FILETYPE=\"%s\"\n", filetype(ipipe->magic));

    /* video track, only the first */
    printf("ID_VIDEO_WIDTH=%i\n", ipipe->probe_info->width);
    printf("ID_VIDEO_HEIGHT=%i\n", ipipe->probe_info->height);
    printf("ID_VIDEO_FPS=%.3f\n", ipipe->probe_info->fps);
    printf("ID_VIDEO_FRC=%i\n", ipipe->probe_info->frc);
    printf("ID_VIDEO_ASR=%i\n", ipipe->probe_info->asr);
    printf("ID_VIDEO_FORMAT=%s\n",
              tc_codec_to_string(ipipe->probe_info->codec));
    printf("ID_VIDEO_BITRATE=%li\n", ipipe->probe_info->bitrate);

    /* audio stuff */
    for (i = 0; i < TC_MAX_AUD_TRACKS; i++) {
        dump_track_info_raw(ipipe->probe_info->track, i);
    }
   
    if (ipipe->probe_info->fps != 0.0) {
        /* seconds */
        duration = ((double)ipipe->probe_info->frames/ipipe->probe_info->fps);
    }
    /* general information, reprise */
    printf("ID_LENGTH=%.2f\n", duration);
}

/*
 * dump_info_new:
 *      dump a ProbeInfo structure in new, better
 *      human-readable format.
 *
 * Parameters:
 *      ipipe: info_t structure holding the ProbeInfo data to dump.
 * Return Value:
 *      None
 */
static void dump_info_new(info_t *ipipe)
{
    int i = 0, j = 0;
    unsigned long dur_ms = 0;
    unsigned int dur_h = 0, dur_min = 0, dur_s = 0;
    long frame_time = (ipipe->probe_info->fps != 0) 
                       ? (long)(1. / ipipe->probe_info->fps * 1000) : 0;

    if (ipipe->probe_info->fps < 0.100) {
        dur_ms = (long)ipipe->probe_info->frames * frame_time;
    } else {
        dur_ms = (long)((float)ipipe->probe_info->frames * 1000
                   /ipipe->probe_info->fps);
    }
    dur_h = dur_ms / 3600000;
    dur_min = (dur_ms %= 3600000) / 60000;
    dur_s = (dur_ms %= 60000) / 1000;
    dur_ms %= 1000;
 
    printf("* container:\n");
    printf("%18s: %s\n", "format",
           filetype(ipipe->probe_info->magic));
    printf("%18s: '%s'\n", "source",
           ((ipipe->magic == TC_STYPE_STDIN) ?"-" :ipipe->name));
    printf("%18s: %li\n", "frames",
           ipipe->probe_info->frames);
    printf("%18s: %u:%02u:%02u.%03lu\n", "duration",
           dur_h, dur_min, dur_s, dur_ms);
    printf("%18s: %i\n", "SCR reset",
           ipipe->probe_info->unit_cnt + 1);

    /* video first. */
    if (ipipe->probe_info->width > 0 && ipipe->probe_info->height > 0) {
        int n, d;

        tc_asr_code_to_ratio(ipipe->probe_info->asr, &n, &d);

        printf("* video track #0:\n");
        printf("%18s: %s\n", "format",
               tc_codec_to_string(ipipe->probe_info->codec));
        printf("%18s: %ix%i\n", "frame size",
               ipipe->probe_info->width, ipipe->probe_info->height);
        printf("%18s: %i:%i (asr=%i)\n", "aspect ratio",
               n, d, ipipe->probe_info->asr);
        printf("%18s: %.3f (frc=%i)\n", "frame rate",
               ipipe->probe_info->fps, ipipe->probe_info->frc);
        printf("%18s: %li kbps\n", "bitrate", ipipe->probe_info->bitrate);
        printf("%18s: %.4f\n", "starting PTS",
               ipipe->probe_info->pts_start);
        printf("%18s: %li ms\n", "frame time", frame_time);
    }

    j = 0;
    for (i = 0; i < TC_MAX_AUD_TRACKS; i++) {
        if (ipipe->probe_info->track[i].format != 0
         && ipipe->probe_info->track[i].chan > 0) {
            double pts_diff = 0.0;
            int hint_frames = 0, hint_ms = 0;
            if (ipipe->probe_info->pts_start > 0
             && ipipe->probe_info->track[i].pts_start > 0
             && ipipe->probe_info->fps != 0) {
                pts_diff = ipipe->probe_info->pts_start - ipipe->probe_info->track[i].pts_start;
                hint_frames = (int)(pts_diff * ipipe->probe_info->fps);
                hint_ms = (int)((pts_diff - hint_frames / ipipe->probe_info->fps) * 1000);
            }

            printf("* audio track #%i:\n", j);
            /* XXX */
            printf("%18s: %i\n", "track id",
                   ipipe->probe_info->track[i].tid);
            /* XXX */
            printf("%18s: 0x%x\n", "format",
                   ipipe->probe_info->track[i].format);
            printf("%18s: %i\n", "channels",
                   ipipe->probe_info->track[i].chan);
            printf("%18s: %i Hz\n", "sample rate",
                   ipipe->probe_info->track[i].samplerate);
            printf("%18s: %i\n", "bits for sample",
                   ipipe->probe_info->track[i].bits);

            printf("%18s: %i kbps\n", "bitrate",
                   ipipe->probe_info->track[i].bitrate);
            printf("%18s: %.4f\n", "starting PTS",
                   ipipe->probe_info->track[i].pts_start);
            printf("%18s: %i frames/%i ms\n", "A/V sync hint",
                    hint_frames, hint_ms);
            /* have subtitles here? */
            printf("%18s: %s\n", "subtitles", 
                   (ipipe->probe_info->track[i].attribute & PACKAGE_SUBTITLE) ?"yes" :"no");
            j++;
        }
    }
}


/*************************************************************************/

/* ------------------------------------------------------------
 *
 * print a usage/version message
 *
 * ------------------------------------------------------------*/

void version(void)
{
    /* print id string to stderr */
    printf("%s (%s v%s) (C) 2001-2010 Thomas Oestreich, Transcode team\n",
           EXE, PACKAGE, VERSION);
}


static void usage(int status)
{
    version();

    printf("Usage: %s [options] [-]\n", EXE);
    printf("    -i name        input file/directory/device/host"
                    " name [stdin]\n");
    printf("    -B             binary output to stdout"
           " (used by transcode) [off]\n");
    printf("    -M             use EXPERIMENTAL mplayer probe [off]\n");
    printf("    -R             raw mode: produce machine-friendly"
           " output [off]\n");
    printf("    -X             new extended output mode [off]\n");
    printf("    -H n           probe n MB of stream [1]\n");
    printf("    -s n           skip first n bytes of stream [0]\n");
    printf("    -T title       probe for DVD title [off]\n");
    printf("    -b bitrate     audio encoder bitrate kBits/s [%d]\n",
           ABITRATE);
    printf("    -f seekfile    seek/index file [off]\n");
    printf("    -d verbosity   verbosity mode [1]\n");
    printf("    -v             print version\n");

    exit(status);
}

/* ------------------------------------------------------------
 * universal probing code frontend
 * ------------------------------------------------------------*/

/* very basic option sanity check */
#define VALIDATE_OPTION \
    if (optarg[0]=='-') { \
        usage(EXIT_FAILURE); \
    }

#define VALIDATE_PARAM(parm, opt, min) \
    if ((parm) < (min)) { \
        tc_log_error(EXE, "invalid parameter for option %s", (opt)); \
        exit(16); \
    }


int main(int argc, char *argv[])
{
    info_t ipipe;
    InfoDumpFn output_handler =  dump_info_old;
    /* standard old style output */

    int mplayer_probe = TC_FALSE;
    int ch, skip = 0, want_dvd = 0, ret;
    const char *name = NULL;

    /* proper initialization */
    memset(&ipipe, 0, sizeof(info_t));
    ipipe.stype = TC_STYPE_UNKNOWN;
    ipipe.seek_allowed = 0;
    ipipe.factor = 1;
    ipipe.dvd_title = 1;

    libtc_init(&argc, &argv);

    while ((ch = getopt(argc, argv, "i:vBMRXd:T:f:b:s:H:?h")) != -1) {
        switch (ch) {
          case 'b':
            VALIDATE_OPTION;
            bitrate = atoi(optarg);
            VALIDATE_PARAM(bitrate, "-b", 0);
            break;
          case 'i':
            VALIDATE_OPTION;
	        name = optarg;
            break;
          case 'f':
            VALIDATE_OPTION;
	        ipipe.nav_seek_file = optarg;
            break;
          case 'd':
            VALIDATE_OPTION;
	        verbose = atoi(optarg);
            break;
          case 's':
            VALIDATE_OPTION;
	        skip = atoi(optarg);
            break;
          case 'H':
            VALIDATE_OPTION;
	        ipipe.factor = atoi(optarg); /* how much data for probing? */
            VALIDATE_PARAM(bitrate, "-H", 0);
            break;
          case 'B':
            output_handler = dump_info_binary;
            binary_dump = 1; /* XXX: compatibility with  probe_mov -- FR */
            break;
          case 'M':
            mplayer_probe = TC_TRUE;
            break;
          case 'R':
            output_handler = dump_info_raw;
            break;
          case 'X':
            output_handler = dump_info_new;
            break;
          case 'T':
            VALIDATE_OPTION;
	        ipipe.dvd_title = atoi(optarg);
            want_dvd = 1;
            break;
          case 'v':
            version();
            exit(0);
            break;
          case 'h':
            usage(EXIT_SUCCESS);
            break;
          default:
            usage(EXIT_FAILURE);
        }
    }

    /* need at least a file name */
    if (argc == 1) {
        usage(EXIT_FAILURE);
    }

    if (optind < argc) {
        if (strcmp(argv[optind],"-") != 0) {
            usage(EXIT_FAILURE);
        }
        ipipe.stype = TC_STYPE_STDIN;
    }

    /* assume defaults */
    if (name == NULL) {
        ipipe.stype = TC_STYPE_STDIN;
    } else {
        if (tc_x11source_is_display_name(name)) {
            ipipe.stype = TC_STYPE_X11;
        }
    }
    ipipe.verbose = verbose;
    ipipe.fd_out = STDOUT_FILENO;
    ipipe.codec = TC_CODEC_UNKNOWN;
    ipipe.name = name;

    /* do not try to mess with the stream */
    if (ipipe.stype == TC_STYPE_STDIN) {
        ipipe.fd_in = STDIN_FILENO;
        ipipe.magic = streaminfo(ipipe.fd_in);
    } else if (ipipe.stype == TC_STYPE_X11) {
        ipipe.fd_in = STDIN_FILENO; /* XXX */
        ipipe.magic = TC_MAGIC_X11;
    } else {
        ret = info_setup(&ipipe, skip, mplayer_probe, want_dvd);
        if (ret != TC_IMPORT_OK) {
            /* already logged out why */
            exit(1);
        }
    }

    /* ------------------------------------------------------------
     * codec specific section
     * note: user provided values overwrite autodetection!
     * ------------------------------------------------------------*/

    probe_stream(&ipipe);

    if (ipipe.error == 0) {
        output_handler(&ipipe);
    } else if (ipipe.error == 1) {
        if (verbose) {
            tc_log_error(EXE, "failed to probe source");
        }
    } else if (ipipe.error == 2) {
        if (verbose) {
            tc_log_error(EXE, "filetype/codec not yet supported by '%s'",
                         PACKAGE);
        }
    }

    info_teardown(&ipipe);
    return ipipe.error;
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
