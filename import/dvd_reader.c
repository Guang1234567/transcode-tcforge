/*
 *  dvd_reader.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Copyright (C) 2001 Billy Biggs <vektor@dumbterm.net>.
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
#include "libtc/libtc.h"
#include "probe.h"
#include "magic.h"

#include <assert.h>

#include "dvd_reader.h"

#ifdef HAVE_LIBDVDREAD

#ifdef HAVE_LIBDVDREAD_INC
#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_types.h>
#include <dvdread/ifo_read.h>
#include <dvdread/nav_read.h>
#include <dvdread/nav_print.h>
#else
#include <dvd_reader.h>
#include <ifo_types.h>
#include <ifo_read.h>
#include <nav_read.h>
#include <nav_print.h>
#endif


static char lock_file[] = "/tmp/LCK..dvd";

/*
 * lock - create a lock file for the device
 */
static int lock(void)
{
    char lock_buffer[12];
    int fd, pid, n;

    while ((fd = open(lock_file, O_EXCL | O_CREAT | O_RDWR, 0644)) < 0) {
       if (errno != EEXIST) {
            tc_log_warn(__FILE__, "Can't create lock file %s: %s",
                                  lock_file, strerror(errno));
            break;
        }

        /* Read the lock file to find out who has the device locked. */
        fd = open(lock_file, O_RDONLY, 0);
        if (fd < 0) {
            if (errno == ENOENT) /* This is just a timing problem. */
                continue;
            tc_log_warn(__FILE__, "Can't open existing lock file %s: %s",
                                  lock_file, strerror(errno));
            break;
        }
        n = read(fd, lock_buffer, 11);
        close(fd);
        fd = -1;
        if (n <= 0) {
            tc_log_warn(__FILE__, "Can't read pid from lock file %s",
                                  lock_file);
            break;
        }

        /* See if the process still exists. */
        lock_buffer[n] = 0;
        pid = atoi(lock_buffer);
        if (pid == getpid())
            return 0;       /* somebody else locked it for us */
        if (pid == 0 || (kill(pid, 0) == -1 && errno == ESRCH)) {
            if (unlink (lock_file) == 0) {
                tc_log_warn(__FILE__, "Removed stale lock (pid %d)", pid);
                continue;
            }
            tc_log_warn(__FILE__, "Couldn't remove stale lock");
        }
        break;
    }

    if (fd < 0) {
        return 1;
    }

    pid = getpid();
    tc_snprintf(lock_buffer, sizeof(lock_buffer), "%10d", pid);
    if (write(fd, lock_buffer, 11) != 11) {
        tc_log_warn(__FILE__, "Couldn't write to lock file");
	close(fd);
	unlink(lock_file);
	return 1;
    }
    close(fd);

    return 0;
}

/*
 * unlock - remove our lockfile
 */
static void unlock(void)
{
    unlink(lock_file);
}


/**
 * Returns true if the pack is a NAV pack.  This check is clearly insufficient,
 * and sometimes we incorrectly think that valid other packs are NAV packs.  I
 * need to make this stronger.
 */
static int is_nav_pack(unsigned char *buffer)
{
    return buffer[41] == 0xbf && buffer[1027] == 0xbf;
}

static dvd_reader_t *dvd=NULL;
static unsigned char *data=NULL;

static const char *ifoPrint_time(const dvd_time_t *time, long *playtime_ret)
{
  long playtime;
  const char *rate;
  int i;
  static char outbuf[TC_BUF_MIN];
  char *outptr;

  assert((time->hour>>4) < 0xa && (time->hour&0xf) < 0xa);
  assert((time->minute>>4) < 0x7 && (time->minute&0xf) < 0xa);
  assert((time->second>>4) < 0x7 && (time->second&0xf) < 0xa);
  assert((time->frame_u&0xf) < 0xa);

  i = tc_snprintf(outbuf, sizeof(outbuf), "%02x:%02x:%02x.%02x",
                  time->hour,
                  time->minute,
                  time->second,
                  time->frame_u & 0x3f);
  if (i > 0)
      outptr = outbuf+i;

  i=time->hour>>4;
  playtime =  (i*10 + time->hour-(i<<4))*60*60;

  i=(time->minute>>4);
  playtime += (i*10 + time->minute-(i<<4))*60;

  i=(time->second>>4);
  playtime +=  i*10 + time->second-(i<<4);

  playtime++;

  switch((time->frame_u & 0xc0) >> 6) {
  case 1:
    rate = "25.00";
    break;
  case 3:
    rate = "29.97";
    break;
  default:
    if(time->hour == 0 && time->minute == 0
       && time->second == 0 && time->frame_u == 0) {
      rate = "no";
    } else
      rate = "(please send a bug report)";
    break;
  }
  //tc_snprintf(outptr, sizeof(outbuf) - (outptr-outbuf), " @ %s fps", rate);
  if (playtime_ret)
    *playtime_ret = playtime;
  return outbuf;
}

static void stats_video_attributes(video_attr_t *attr, ProbeInfo *probe_info)
{
  const char *version, *display, *dar, *wide, *ntsc_cc, *lbox, *mode;
  char unknown1[50], size[50];

  /* The following test is shorter but not correct ISO C,
     memcmp(attr,my_friendly_zeros, sizeof(video_attr_t)) */
  if(attr->mpeg_version == 0
     && attr->video_format == 0
     && attr->display_aspect_ratio == 0
     && attr->permitted_df == 0
     && attr->unknown1 == 0
     && attr->line21_cc_1 == 0
     && attr->line21_cc_2 == 0
     && attr->video_format == 0
     && attr->letterboxed == 0
     && attr->film_mode == 0) {
    tc_log_info(__FILE__, "-- Unspecified Video --");
    return;
  }

  switch(attr->mpeg_version) {
  case 0:
    version = "mpeg1 ";
    probe_info->codec=TC_CODEC_MPEG1;
    break;
  case 1:
    version = "mpeg2 ";
    probe_info->codec=TC_CODEC_MPEG2;
    break;
  default:
    version = "(please send a bug report)";
  }

  switch(attr->video_format) {
  case 0:
    display = "ntsc ";
    probe_info->magic=TC_MAGIC_NTSC;
    break;
  case 1:
    display = "pal ";
    probe_info->magic=TC_MAGIC_PAL;
    break;
  default:
    display = "(please send a bug report) ";
  }

  switch(attr->display_aspect_ratio) {
  case 0:
    dar = "4:3 ";
    probe_info->asr=2;
    break;
  case 3:
    dar = "16:9 ";
    probe_info->asr=3;
    break;
  default:
    dar = "(please send a bug report) ";
  }

  // Wide is allways allowed..!!!
  switch(attr->permitted_df) {
  case 0:
    wide = "pan&scan+letterboxed ";
    break;
  case 1:
    wide = "only pan&scan ";  //???
    break;
  case 2:
    wide = "only letterboxed ";
    break;
  case 3:
    wide = "";  // not specified
    break;
  default:
    wide = "(please send a bug report) ";
  }

  tc_snprintf(unknown1, sizeof(unknown1), "U%x ", attr->unknown1);
  assert(!attr->unknown1);

  if(attr->line21_cc_1 && attr->line21_cc_2) {
    ntsc_cc = "NTSC CC 1 2 ";
  } else if(attr->line21_cc_1) {
    ntsc_cc = "NTSC CC 1 ";
  } else if(attr->line21_cc_2) {
    ntsc_cc = "NTSC CC 2 ";
  } else {
    ntsc_cc = "";
  }

  {
    int height = 480;
    if(attr->video_format != 0)
      height = 576;
    switch(attr->picture_size) {
    case 0:
      tc_snprintf(size, sizeof(size), "720x%d ", height);
      probe_info->width=720;
      probe_info->height =  height;
      break;
    case 1:
      tc_snprintf(size, sizeof(size), "704x%d ", height);
      probe_info->width=704;
      probe_info->height =  height;
      break;
    case 2:
      tc_snprintf(size, sizeof(size), "352x%d ", height);
      probe_info->width=352;
      probe_info->height =  height;
      break;
    case 3:
      tc_snprintf(size, sizeof(size), "352x%d ", height/2);
      probe_info->width=352;
      probe_info->height =  height/2;
      break;
    default:
      tc_snprintf(size, sizeof(size), "(please send a bug report) ");
    }

  }

  if(attr->letterboxed) {
    lbox = "letterboxed ";
  } else {
    lbox = "";
  }

  if(attr->film_mode) {
    mode = "film";
  } else {
    mode = "video";  //camera
  }

  if (verbose >= TC_INFO) {
      tc_log_info(__FILE__, "%s%s%s%s%s%s%s%s%s",
                  version, display, dar, wide, unknown1, ntsc_cc, size, lbox, mode);
  }
}

static void stats_audio_attributes(audio_attr_t *attr, int track, ProbeInfo *probe_info)
{
  const char *format, *mcext, *lang, *appmode, *quant, *freq, *langext;
  char langbuf[4], channels[50];

  if(attr->audio_format == 0
     && attr->multichannel_extension == 0
     && attr->lang_type == 0
     && attr->application_mode == 0
     && attr->quantization == 0
     && attr->sample_frequency == 0
     && attr->channels == 0
     && attr->lang_extension == 0
     && attr->unknown1 == 0
     && attr->unknown1 == 0) {
    tc_log_info(__FILE__, "-- Unspecified Audio --");
    return;
  }

  //defaults for all tracks:
  ++probe_info->num_tracks;
  probe_info->track[track].chan = 2;
  probe_info->track[track].bits = 16;
  probe_info->track[track].tid = track;

  switch(attr->audio_format) {
  case 0:
    format = "ac3 ";
    probe_info->track[track].format = TC_CODEC_AC3;
    break;
  case 1:
    format = "(please send a bug report) ";
    break;
  case 2:
    format = "mpeg1 ";
    probe_info->track[track].format = TC_CODEC_MP2;
    break;
  case 3:
    format = "mpeg2ext ";
    break;
  case 4:
    format = "lpcm ";
    probe_info->track[track].format = TC_CODEC_LPCM;
    break;
  case 5:
    format = "(please send a bug report) ";
    break;
  case 6:
    format = "dts ";
    probe_info->track[track].format = TC_CODEC_DTS;
    break;
  default:
    format = "(please send a bug report) ";
  }

  if(attr->multichannel_extension) {
    mcext = "multichannel_extension ";
  } else {
    mcext = "";
  }

  switch(attr->lang_type) {
  case 0:
    lang = "";  // not specified
    probe_info->track[track].lang=0;
    break;
  case 1:
    langbuf[0] = attr->lang_code>>8;
    langbuf[1] = attr->lang_code & 0xff;
    langbuf[2] = ' ';
    langbuf[3] = 0;
    lang = langbuf;
    probe_info->track[track].lang=attr->lang_code;
    break;
  default:
    lang = "(please send a bug report) ";
  }

  switch(attr->application_mode) {
  case 0:
    appmode = "";  // not specified
    break;
  case 1:
    appmode = "karaoke mode ";
    break;
  case 2:
    appmode = "surround sound mode ";
    break;
  default:
    appmode = "(please send a bug report) ";
  }

  switch(attr->quantization) {
  case 0:
    quant = "16bit ";
    probe_info->track[track].bits = 16;
    break;
  case 1:
    quant = "20bit ";
    probe_info->track[track].bits = 20;
    break;
  case 2:
    quant = "24bit ";
    probe_info->track[track].bits = 24;
    break;
  case 3:
    quant = "drc ";
    break;
  default:
    quant = "(please send a bug report) ";
  }

  switch(attr->sample_frequency) {
  case 0:
    freq = "48kHz ";
    probe_info->track[track].samplerate = 48000;
    break;
  case 1:
    freq = "96kHz ";
    probe_info->track[track].samplerate = 96000;
    break;
  case 2:
    freq = "44.1kHz ";
    probe_info->track[track].samplerate = 44100;
    break;
  case 3:
    freq = "32kHz ";
    probe_info->track[track].samplerate = 32000;
    break;
  default:
    freq = "(please send a bug report) ";
  }

  tc_snprintf(channels, sizeof(channels), "%dCh ", attr->channels + 1);

  switch(attr->lang_extension) {
  case 0:
    langext = "";  // "Not specified ";
    break;
  case 1: // Normal audio
    langext = "Normal Caption ";
    break;
  case 2: // visually imparied
    langext = "Audio for visually impaired ";
    break;
  case 3: // Directors 1
    langext = "Director's comments #1 ";
    break;
  case 4: // Directors 2
    langext = "Director's comments #2 ";
    break;
  //case 4: // Music score ?
  default:
    langext = "(please send a bug report) ";
  }

  if (verbose >= TC_INFO) {
    tc_log_info(__FILE__, "%s%s%s%s%s%s%s%s",
              format, mcext, lang, appmode, quant, freq, channels, langext);
  }
}

static void stats_subp_attributes(subp_attr_t *attr, int track, ProbeInfo *probe_info)
{
  char buf1[50] = {0}, buf2[50] = {0};

  if (attr->type == 0
     && attr->zero1 == 0
     && attr->lang_code == 0
     && attr->lang_extension == 0
     && attr->zero2 == 0) {
    tc_log_info(__FILE__, "-- Unspecified Subs --");
    return;
  }


  if (attr->type) {
    tc_snprintf(buf1, sizeof(buf1), "subtitle %02d=<%c%c> ", track,
        attr->lang_code>>8, attr->lang_code & 0xff);
    if (attr->lang_extension)
      tc_snprintf(buf2, sizeof(buf2), "ext=%d", attr->lang_extension);
  }

  if (verbose >= TC_DEBUG) {
      tc_log_info(__FILE__, "%s%s", buf1, buf2);
  }
}


int dvd_query(int title, int *arg_chapter, int *arg_angle)
{
    int             ttn, pgc_id, titleid;
    tt_srpt_t      *tt_srpt;
    ifo_handle_t   *vmg_file;
    pgc_t          *cur_pgc;
    ifo_handle_t   *vts_file;
    vts_ptt_srpt_t *vts_ptt_srpt;

    vmg_file = ifoOpen(dvd, 0);
    if (!vmg_file) {
        tc_log_error(__FILE__, "Can't open VMG info.");
        return -1;
    }
    tt_srpt = vmg_file->tt_srpt;

    /**
     * Make sure our title number is valid.
     */

    titleid = title-1;

    if (titleid < 0 || titleid >= tt_srpt->nr_of_srpts) {
        tc_log_error(__FILE__, "Invalid title %d.", titleid + 1);
        goto bad_title;
    }

    // display title infos
    if (verbose & TC_DEBUG)
        tc_log_msg(__FILE__, "DVD title %d: %d chapter(s), %d angle(s)",
                   title, tt_srpt->title[ titleid ].nr_of_ptts,
                   tt_srpt->title[ titleid ].nr_of_angles);

    /**
     * Load the VTS information for the title set our title is in.
     */

    vts_file = ifoOpen( dvd, tt_srpt->title[ titleid ].title_set_nr );
    if (!vts_file) {
        tc_log_error(__FILE__, "Can't open the title %d info file.",
                     tt_srpt->title[ titleid ].title_set_nr);
        goto bad_title;
    }

    ttn = tt_srpt->title[ titleid ].vts_ttn;
    vts_ptt_srpt = vts_file->vts_ptt_srpt;
    pgc_id = vts_ptt_srpt->title[ ttn - 1 ].ptt[0].pgcn;
    cur_pgc = vts_file->vts_pgcit->pgci_srp[ pgc_id - 1 ].pgc;

    if(verbose & TC_DEBUG) {
        tc_log_msg(__FILE__, "DVD playback time: %s",
                   ifoPrint_time(&cur_pgc->playback_time, NULL));
    }

    //return info
    *arg_chapter = tt_srpt->title[ titleid ].nr_of_ptts;
    *arg_angle = tt_srpt->title[ titleid ].nr_of_angles;

    return 0;

bad_title:
    ifoClose(vmg_file);
    return -1;
}

int dvd_probe(int title, ProbeInfo *info)
{
    int             ttn, pgn, pgc_id, titleid, start_cell, end_cell, i, j;
    tt_srpt_t      *tt_srpt;
    ifo_handle_t   *vmg_file;
    pgc_t          *cur_pgc;
    ifo_handle_t   *vts_file;
    vts_ptt_srpt_t *vts_ptt_srpt;
    video_attr_t   *v_attr;
    audio_attr_t   *a_attr;
    subp_attr_t    *s_attr;

    dvd_time_t     *dt;
    double          fps;
    long            hour, minute, second, ms, overall_time, cur_time, playtime;
    const char     *s;

    vmg_file = ifoOpen( dvd, 0 );
    if (!vmg_file) {
        return -1;
    }

    tt_srpt = vmg_file->tt_srpt;

    // Make sure our title number is valid
    titleid = title-1;

    if (titleid < 0 || titleid >= tt_srpt->nr_of_srpts) {
        tc_log_error(__FILE__, "Invalid title %d.", titleid + 1);
        goto bad_title;
    }

    vts_file = ifoOpen(dvd, tt_srpt->title[ titleid ].title_set_nr);
    if (!vts_file) {
        tc_log_error(__FILE__, "Can't open the title %d info file.",
                     tt_srpt->title[ titleid ].title_set_nr );
        goto bad_title;
    }

    if (vts_file->vtsi_mat) {
        v_attr = &vts_file->vtsi_mat->vts_video_attr;

        stats_video_attributes(v_attr, info);

        for (i = 0; i < vts_file->vtsi_mat->nr_of_vts_audio_streams; i++) {
            a_attr = &vts_file->vtsi_mat->vts_audio_attr[i];
            stats_audio_attributes(a_attr, i, info);
        }

        for (i = 0; i < vts_file->vtsi_mat->nr_of_vts_subp_streams; i++) {
            s_attr = &vts_file->vtsi_mat->vts_subp_attr[i];
            stats_subp_attributes(s_attr, i, info);
        }
    } else {
        tc_log_error(__FILE__, "failed to probe DVD title information");
        goto bad_title;
    }

    vts_file = NULL; /* uh?!? -- FR */

    vts_file = ifoOpen(dvd, tt_srpt->title[ titleid ].title_set_nr);
    if (!vts_file) {
        tc_log_error(__FILE__, "Can't open the title %d info file.",
                               tt_srpt->title[ titleid ].title_set_nr);
        goto bad_title;
    }

    ttn = tt_srpt->title[ titleid ].vts_ttn;
    vts_ptt_srpt = vts_file->vts_ptt_srpt;
    pgc_id = vts_ptt_srpt->title[ ttn - 1 ].ptt[0].pgcn;
    cur_pgc = vts_file->vts_pgcit->pgci_srp[ pgc_id - 1 ].pgc;

    switch (((cur_pgc->playback_time).frame_u & 0xc0) >> 6) {
      case 1:
        info->fps   = PAL_FPS;
        info->frc   = 3;
        info->magic = TC_MAGIC_DVD_PAL;
        break;
      case 3:
        info->fps   = NTSC_FILM;
        info->frc   = 1;
        info->magic = TC_MAGIC_DVD_NTSC;
        break;
    }

    tc_log_info(__FILE__, "DVD title %d/%d: %d chapter(s), %d angle(s), title set %d",
                         title, tt_srpt->nr_of_srpts,
                        tt_srpt->title[ titleid ].nr_of_ptts,
                        tt_srpt->title[ titleid ].nr_of_angles,
                        tt_srpt->title[ titleid].title_set_nr);
                        s = ifoPrint_time(&cur_pgc->playback_time, &playtime);
    tc_log_info(__FILE__, "title playback time: %s  %ld sec", s, playtime);

    info->time = playtime;

    // stolen from ogmtools-1.0.2 dvdxchap -- tibit
    ttn = tt_srpt->title[titleid].vts_ttn;
    vts_ptt_srpt = vts_file->vts_ptt_srpt;
    overall_time = 0;

    for (i = 0; i < tt_srpt->title[titleid].nr_of_ptts - 1; i++) {
        pgc_id   = vts_ptt_srpt->title[ttn - 1].ptt[i].pgcn;
        pgn      = vts_ptt_srpt->title[ttn - 1].ptt[i].pgn;
        cur_pgc  = vts_file->vts_pgcit->pgci_srp[pgc_id - 1].pgc;

        start_cell = cur_pgc->program_map[pgn - 1] - 1;
        pgc_id     = vts_ptt_srpt->title[ttn - 1].ptt[i + 1].pgcn;
        pgn        = vts_ptt_srpt->title[ttn - 1].ptt[i + 1].pgn;
        if (pgn < 1)
            continue;
        cur_pgc    = vts_file->vts_pgcit->pgci_srp[pgc_id - 1].pgc;
        end_cell   = cur_pgc->program_map[pgn - 1] - 2;
        cur_time   = 0;
        for (j = start_cell; j <= end_cell; j++) {
            dt = &cur_pgc->cell_playback[j].playback_time;
            hour = ((dt->hour & 0xf0) >> 4) * 10 + (dt->hour & 0x0f);
            minute = ((dt->minute & 0xf0) >> 4) * 10 + (dt->minute & 0x0f);
            second = ((dt->second & 0xf0) >> 4) * 10 + (dt->second & 0x0f);
            if (((dt->frame_u & 0xc0) >> 6) == 1)
                fps = 25.00;
            else
                fps = 29.97;
            dt->frame_u &= 0x3f;
            dt->frame_u = ((dt->frame_u & 0xf0) >> 4) * 10 + (dt->frame_u & 0x0f);
            ms = (double)dt->frame_u * 1000.0 / fps;
            cur_time += (hour * 60 * 60 * 1000 + minute * 60 * 1000 + second * 1000 + ms);
        }
        if (verbose >= TC_DEBUG) {
            tc_log_info(__FILE__, "[Chapter %02d] %02ld:%02ld:%02ld.%03ld , block from %d to %d", i + 1,
                                  overall_time / 60 / 60 / 1000, (overall_time / 60 / 1000) % 60,
                                  (overall_time / 1000) % 60, overall_time % 1000,
                                  cur_pgc->cell_playback[i].first_sector,
                                  cur_pgc->cell_playback[i].last_sector);
        }
        overall_time += cur_time;
    }
    if (verbose >= TC_DEBUG) {
        tc_log_info(__FILE__, "[Chapter %02d] %02ld:%02ld:%02ld.%03ld , block from %d to %d", i + 1,
                            overall_time / 60 / 60 / 1000, (overall_time / 60 / 1000) % 60,
                            (overall_time / 1000) % 60, overall_time % 1000,
                            cur_pgc->cell_playback[i].first_sector,
                            cur_pgc->cell_playback[i].last_sector);
    }
    return 0;

bad_title:
     ifoClose(vmg_file);
     return -1;
}

int dvd_is_valid(const char *dvd_path)
{
    dvd_reader_t *_dvd = NULL;
    ifo_handle_t *vmg_file = NULL;

    _dvd = DVDOpen(dvd_path);

    if (_dvd == NULL) {
        return TC_FALSE;
    }
    
    vmg_file = ifoOpen( _dvd, 0);
    if (vmg_file == NULL) {
        DVDClose(_dvd);
        return TC_FALSE;
    }

    DVDClose(_dvd);
    return TC_TRUE;
}


int dvd_init(const char *dvd_path, int *titles, int verb)
{

    tt_srpt_t *tt_srpt;
    ifo_handle_t *vmg_file;

    // copy verbosity flag
    verbose = verb;

    /**
     * Open the disc.
     */

    if (dvd == NULL) {
        dvd = DVDOpen(dvd_path);
        if (!dvd)
            return -1;
    }

    //workspace

    if (data == NULL) {
        data = tc_malloc(1024 * DVD_VIDEO_LB_LEN);
        if (data == NULL) {
            tc_log_error(__FILE__, "out of memory");
            DVDClose(dvd);
            return -1;
        }
    }


    vmg_file = ifoOpen(dvd, 0);
    if (!vmg_file) {
        tc_log_error(__FILE__, "Can't open VMG info.");
        DVDClose(dvd);
        tc_free(data);
          return -1;
    }

    tt_srpt = vmg_file->tt_srpt;

    *titles = tt_srpt->nr_of_srpts;

    return 0;
}

int dvd_close(void)
{
    if (data != NULL) {
        tc_free(data);
        data = NULL;
    }

    if (dvd != NULL) {
        DVDClose(dvd);
        dvd = NULL;
    }

    return 0;
}

int dvd_read(int arg_title, int arg_chapter, int arg_angle)
{
    int pgc_id, len, start_cell, cur_cell, last_cell, next_cell;
    unsigned int cur_pack;
    int ttn, pgn;
    int lockretries;

    dvd_file_t *title;
    ifo_handle_t *vmg_file;
    tt_srpt_t *tt_srpt;
    ifo_handle_t *vts_file;
    vts_ptt_srpt_t *vts_ptt_srpt;
    pgc_t *cur_pgc;
    int titleid, angle, chapid;

    chapid  = arg_chapter - 1;
    titleid = arg_title - 1;
    angle   = arg_angle - 1;


    /**
     * Load the video manager to find out the information about the titles on
     * this disc.
     */


    vmg_file = ifoOpen( dvd, 0 );
    if( !vmg_file ) {
    tc_log_error(__FILE__, "Can't open VMG info.");
    return -1;
    }

    tt_srpt = vmg_file->tt_srpt;


    /**
     * Make sure our title number is valid.
     */
    if( titleid < 0 || titleid >= tt_srpt->nr_of_srpts ) {
        tc_log_error(__FILE__, "Invalid title %d.", titleid + 1);
        ifoClose( vmg_file );
        return -1;
    }

    /**
     * Make sure the chapter number is valid for this title.
     */
    if( chapid < 0 || chapid >= tt_srpt->title[ titleid ].nr_of_ptts ) {
        tc_log_error(__FILE__, "Invalid chapter %d.", chapid + 1);
        ifoClose( vmg_file );
        return -1;
    }


    /**
     * Make sure the angle number is valid for this title.
     */
    if( angle < 0 || angle >= tt_srpt->title[ titleid ].nr_of_angles ) {
        tc_log_error(__FILE__, "Invalid angle %d.", angle + 1);
        ifoClose( vmg_file );
        return -1;
    }

    /**
     * Load the VTS information for the title set our title is in.
     */
    vts_file = ifoOpen( dvd, tt_srpt->title[ titleid ].title_set_nr );
    if( !vts_file ) {
        tc_log_error(__FILE__, "Can't open the title %d info file.",
             tt_srpt->title[ titleid ].title_set_nr );
        ifoClose( vmg_file );
        return -1;
    }


    /**
     * Determine which program chain we want to watch.  This is based on the
     * chapter number.
     */

    ttn = tt_srpt->title[ titleid ].vts_ttn;
    vts_ptt_srpt = vts_file->vts_ptt_srpt;
    pgc_id = vts_ptt_srpt->title[ ttn - 1 ].ptt[ chapid ].pgcn;
    pgn = vts_ptt_srpt->title[ ttn - 1 ].ptt[ chapid ].pgn;
    cur_pgc = vts_file->vts_pgcit->pgci_srp[ pgc_id - 1 ].pgc;
    start_cell = cur_pgc->program_map[ pgn - 1 ] - 1;


    //ThOe

    if (chapid+1 == tt_srpt->title[ titleid ].nr_of_ptts) {
      last_cell = cur_pgc->nr_of_cells;
    } else {

      last_cell = cur_pgc->program_map[ (vts_ptt_srpt->title[ ttn - 1 ].ptt[ chapid+1 ].pgn) - 1 ] - 1;
    }

    /**
     * We've got enough info, time to open the title set data.
     */

    for (lockretries=0; lock() && lockretries < 180; lockretries++ ) {
        sleep(1);
    }

    if( lockretries >= 180 ) {
        tc_log_error(__FILE__, "Can't acquire lock.");
    }

    title = DVDOpenFile( dvd, tt_srpt->title[ titleid ].title_set_nr,
                         DVD_READ_TITLE_VOBS);

    unlock();

    if( !title ) {
        tc_log_error(__FILE__, "Can't open title VOBS (VTS_%02d_1.VOB).",
             tt_srpt->title[ titleid ].title_set_nr );
        ifoClose( vts_file );
        ifoClose( vmg_file );
        return -1;
    }

    /**
     * Playback the cells for our chapter.
     */

    next_cell = start_cell;

    for( cur_cell = start_cell; next_cell < last_cell; ) {

        cur_cell = next_cell;

        /* Check if we're entering an angle block. */
        if( cur_pgc->cell_playback[ cur_cell ].block_type
                                        == BLOCK_TYPE_ANGLE_BLOCK ) {
            int i;

            cur_cell += angle;
            for( i = 0;; ++i ) {
                if( cur_pgc->cell_playback[ cur_cell + i ].block_mode
                                          == BLOCK_MODE_LAST_CELL ) {
                    next_cell = cur_cell + i + 1;
                    break;
                }
            }
        } else {
      next_cell = cur_cell + 1;
        }

    /**
     * We loop until we're out of this cell.
     */

    for( cur_pack = cur_pgc->cell_playback[ cur_cell ].first_sector;
         cur_pack < cur_pgc->cell_playback[ cur_cell ].last_sector; ) {

      dsi_t dsi_pack;
      unsigned int next_vobu, next_ilvu_start, cur_output_size;


      /**
       * Read NAV packet.
       */

         nav_retry:

      len = DVDReadBlocks( title, (int) cur_pack, 1, data );
      if( len != 1 ) {
        tc_log_error(__FILE__, "Read failed for block %d", cur_pack);
        ifoClose( vts_file );
        ifoClose( vmg_file );
        DVDCloseFile( title );
        return -1;
      }

      //assert( is_nav_pack( data ) );
      if(!is_nav_pack(data)) {
        cur_pack++;
        goto nav_retry;
      }

      /**
       * Parse the contained dsi packet.
       */
      navRead_DSI( &dsi_pack, &(data[ DSI_START_BYTE ]));

      if(!( cur_pack == dsi_pack.dsi_gi.nv_pck_lbn)) {
        cur_output_size = 0;
        dsi_pack.vobu_sri.next_vobu = SRI_END_OF_CELL;
      }


      /**
       * Determine where we go next.  These values are the ones we mostly
       * care about.
       */
      next_ilvu_start = cur_pack
        + dsi_pack.sml_agli.data[ angle ].address;
      cur_output_size = dsi_pack.dsi_gi.vobu_ea;


      /**
       * If we're not at the end of this cell, we can determine the next
       * VOBU to display using the VOBU_SRI information section of the
       * DSI.  Using this value correctly follows the current angle,
       * avoiding the doubled scenes in The Matrix, and makes our life
       * really happy.
       *
       * Otherwise, we set our next address past the end of this cell to
       * force the code above to go to the next cell in the program.
       */
      if( dsi_pack.vobu_sri.next_vobu != SRI_END_OF_CELL ) {
        next_vobu = cur_pack
          + ( dsi_pack.vobu_sri.next_vobu & 0x7fffffff );
      } else {
        next_vobu = cur_pack + cur_output_size + 1;
      }

      assert( cur_output_size < 1024 );
      cur_pack++;

      /**
       * Read in and output cursize packs.
       */
      len = DVDReadBlocks( title, (int) cur_pack, cur_output_size, data );
      if( len != (int) cur_output_size ) {
        tc_log_error(__FILE__, "Read failed for %d blocks at %d",
             cur_output_size, cur_pack );
        ifoClose( vts_file );
        ifoClose( vmg_file );
        DVDCloseFile( title );
        return -1;
      }

      if (fwrite( data, DVD_VIDEO_LB_LEN, cur_output_size, stdout )
	  != cur_output_size
      ) {
	tc_log_perror(__FILE__, "Write failed");
        ifoClose( vts_file );
        ifoClose( vmg_file );
        DVDCloseFile( title );
        return -1;
      }

      if(verbose & TC_STATS)
        tc_log_msg(__FILE__, "%d %d", cur_pack, cur_output_size);
      cur_pack = next_vobu;
    }
    }
    ifoClose( vts_file );
    ifoClose( vmg_file );
    DVDCloseFile( title );

    return 0;
}

static long startsec;
static long startusec;

static void rip_counter_init(long int *t1, long int *t2)
{
#ifdef HAVE_GETTIMEOFDAY
  struct timeval tv;
  struct timezone tz={0,0};

  gettimeofday(&tv,&tz);
  *t1=tv.tv_sec;
  *t2=tv.tv_usec;
#else
  *t1 = time(NULL);
  *t2 = 0;
#endif
}

static void rip_counter_close(void)
{
    fprintf(stderr,"\n");
}

static long range_a = -1, range_b = -1;
static long range_starttime = -1;

static void rip_counter_set_range(long from, long to)
{
  range_a = from;
  range_b = to-1;
}

static void counter_print(long int pida, long int pidn, long int t1, long int t2)
{
  double fps;

#ifdef HAVE_GETTIMEOFDAY
  struct timeval tv;
  struct timezone tz={0,0};
  if(gettimeofday(&tv,&tz)<0) return;
#else
  struct {long tv_sec, tv_usec;} tv;
  tv.tv_sec = time(NULL);
  tv.tv_usec = 0;
#endif

  fps=(pidn-pida)/((tv.tv_sec+tv.tv_usec/1000000.0)-(t1+t2/1000000.0));

  fps = (2048 * fps) / (1<<20);

  if(fps>0) {
      if(range_b != -1 && pidn>=range_a) {
          double done;
          long secleft;

          if(range_starttime == -1) range_starttime = tv.tv_sec;
          done = (double)(pidn-range_a)/(range_b-range_a);
          secleft = (long)((1-done)*(double)(tv.tv_sec-range_starttime)/done);

      fprintf(stderr, "extracting blocks [%08ld], %4.1f MB/s, %4.1f%%, ETA: %ld:%02ld:%02ld   \r", pidn-pida, fps, 100*done,
         secleft/3600, (secleft/60) % 60, secleft % 60);
      }
  }
}

int dvd_stream(int arg_title,int arg_chapid)
{
    int pgc_id, len, start_cell;
    unsigned long cur_pack=0, max_sectors=0, blocks_left=0, blocks_written=0, first_block=0;
    int ttn, pgn;

    int end_cell;
    int e_pgc_id, e_pgn;
    pgc_t *e_cur_pgc;

    dvd_file_t *title;
    ifo_handle_t *vmg_file;
    tt_srpt_t *tt_srpt;
    ifo_handle_t *vts_file;
    vts_ptt_srpt_t *vts_ptt_srpt;
    pgc_t *cur_pgc;
    int titleid, angle, chapid;
    unsigned int cur_output_size=1024, blocks=0;

    chapid  = arg_chapid - 1;
    titleid = arg_title - 1;
    angle   = 0;

    /**
     * Load the video manager to find out the information about the titles on
     * this disc.
     */


    vmg_file = ifoOpen( dvd, 0 );
    if( !vmg_file ) {
    tc_log_error(__FILE__, "Can't open VMG info.");
    return -1;
    }

    tt_srpt = vmg_file->tt_srpt;


    /**
     * Make sure our title number is valid.
     */
    if( titleid < 0 || titleid >= tt_srpt->nr_of_srpts ) {
        tc_log_error(__FILE__, "Invalid title %d.", titleid + 1);
        ifoClose( vmg_file );
        return -1;
    }

    /**
     * Make sure the chapter number is valid for this title.
     */
    if( chapid < 0 || chapid >= tt_srpt->title[ titleid ].nr_of_ptts ) {
        tc_log_error(__FILE__, "Invalid chapter %d.", chapid + 1);
        ifoClose( vmg_file );
        return -1;
    }


    /**
     * Make sure the angle number is valid for this title.
     */
    if( angle < 0 || angle >= tt_srpt->title[ titleid ].nr_of_angles ) {
        tc_log_error(__FILE__, "Invalid angle %d.", angle + 1);
        ifoClose( vmg_file );
        return -1;
    }

    /**
     * Load the VTS information for the title set our title is in.
     */
    vts_file = ifoOpen( dvd, tt_srpt->title[ titleid ].title_set_nr );
    if( !vts_file ) {
        tc_log_error(__FILE__, "Can't open the title %d info file.",
             tt_srpt->title[ titleid ].title_set_nr);
        ifoClose( vmg_file );
        return -1;
    }


    /**
     * Determine which program chain we want to watch.  This is based on the
     * chapter number.
     */

    ttn = tt_srpt->title[ titleid ].vts_ttn;
    vts_ptt_srpt = vts_file->vts_ptt_srpt;
    pgc_id = vts_ptt_srpt->title[ ttn - 1 ].ptt[ chapid ].pgcn;
    pgn = vts_ptt_srpt->title[ ttn - 1 ].ptt[ chapid ].pgn;
    cur_pgc = vts_file->vts_pgcit->pgci_srp[ pgc_id - 1 ].pgc;
    start_cell = cur_pgc->program_map[ pgn - 1 ] - 1;


    if ( chapid+1 >= tt_srpt->title[ titleid ].nr_of_ptts ) {
      end_cell = cur_pgc->nr_of_cells - 1;
    } else {
      e_pgc_id = vts_ptt_srpt->title[ ttn - 1 ].ptt[ chapid+1 ].pgcn;
      e_pgn = vts_ptt_srpt->title[ ttn - 1 ].ptt[ chapid+1 ].pgn;
      e_cur_pgc = vts_file->vts_pgcit->pgci_srp[ e_pgc_id - 1 ].pgc;
      end_cell = ( e_cur_pgc->program_map[ e_pgn - 1 ] - 1 ) -1;
    }

    /**
     * We've got enough info, time to open the title set data.
     */

    title = DVDOpenFile( dvd, tt_srpt->title[ titleid ].title_set_nr,
                         DVD_READ_TITLE_VOBS);

    if( !title ) {
        tc_log_error(__FILE__, "Can't open title VOBS (VTS_%02d_1.VOB).",
             tt_srpt->title[ titleid ].title_set_nr);
        ifoClose( vts_file );
        ifoClose( vmg_file );
        return -1;
    }

    /**
     * Playback the cells for our title
     */
    if (start_cell==end_cell)
      tc_log_msg(__FILE__, "Title %d in VTS %02d is defined by PGC %d with %d cells, exporting cell %d",
         titleid+1,tt_srpt->title[ titleid ].title_set_nr,pgc_id,
         cur_pgc->nr_of_cells,start_cell+1);
    else
      tc_log_msg(__FILE__, "Title %d in VTS %02d is defined by PGC %d with %d cells, exporting from cell %d to cell %d",
         titleid+1,tt_srpt->title[ titleid ].title_set_nr,pgc_id,
         cur_pgc->nr_of_cells,start_cell+1,end_cell+1);

    cur_pack = cur_pgc->cell_playback[start_cell].first_sector;

    max_sectors = (long) cur_pgc->cell_playback[end_cell].last_sector;
    tc_log_msg(__FILE__, "From block %ld to block %ld",
           (long)cur_pack,(long)max_sectors);

    first_block = cur_pack;

    //tc_log_msg(__FILE__, "title %02d, %ld blocks (%ld-%ld)", tt_srpt->title[ titleid ].title_set_nr, (long) DVDFileSize(title), (long) cur_pack, (long) max_sectors);

    if((long) DVDFileSize(title) <  max_sectors ||  cur_pack < 0)
      tc_log_error(__FILE__, "internal error");

    //sanity check
    if(max_sectors <= cur_pack) max_sectors = (long) DVDFileSize(title);

    /**
     * Read NAV packet.
     */

    len = DVDReadBlocks( title, (int) cur_pack, 1, data );

    if( len != 1 ) {
      tc_log_error(__FILE__, "Read failed for block %ld", cur_pack);
      ifoClose( vts_file );
      ifoClose( vmg_file );
      DVDCloseFile( title );
      return -1;
    }

    //write NAV packet
    if (fwrite(data, DVD_VIDEO_LB_LEN, 1, stdout) != 1) {
      tc_log_perror(__FILE__, "Write failed");
      ifoClose( vts_file );
      ifoClose( vmg_file );
      DVDCloseFile( title );
      return -1;
    }

    if(data[38]==0 && data[39]==0 && data[40]==1 && data[41]==0xBF &&
       data[1024]==0 && data[1025]==0 && data[1026]==1 && data[1027]==0xBF) {

      tc_log_msg(__FILE__, "navigation packet at offset %d", (int) cur_pack);
    }

    // loop until all packs of title are written

    blocks_left = max_sectors-cur_pack+1;
    rip_counter_set_range(1, blocks_left);
    rip_counter_init(&startsec, &startusec);

    while(blocks_left > 0) {

      blocks = (blocks_left>cur_output_size) ? cur_output_size:blocks_left;

      len = DVDReadBlocks( title, (int) cur_pack, blocks, data );
      if( len != (int) blocks) {

      rip_counter_close();

      if(len>=0) {
          if(len>0) {
	      if (fwrite(data, DVD_VIDEO_LB_LEN, len, stdout) != len) {
		  tc_log_perror(__FILE__, "Write failed");
		  ifoClose( vts_file );
		  ifoClose( vmg_file );
		  DVDCloseFile( title );
		  return -1;	
	      }
	  }
          tc_log_msg(__FILE__, "%ld blocks written", blocks_written+len);
      }

    ifoClose( vts_file );
    ifoClose( vmg_file );
    DVDCloseFile( title );
    return -1;
      }

      if (fwrite(data, DVD_VIDEO_LB_LEN, blocks, stdout) != blocks) {
	tc_log_perror(__FILE__, "Write failed");
	ifoClose( vts_file );
	ifoClose( vmg_file );
	DVDCloseFile( title );
	return -1;
      }
      blocks_written += blocks;

      counter_print(1, blocks_written, startsec, startusec);

      cur_pack += blocks;
      blocks_left -= blocks;


      if(verbose & TC_STATS)
    tc_log_msg(__FILE__, "%ld %d", cur_pack, cur_output_size);
    }
    rip_counter_close();

    tc_log_msg(__FILE__, "%ld blocks written", blocks_written);

    ifoClose( vts_file );
    ifoClose( vmg_file );
    DVDCloseFile( title );

    return 0;
}


#else

int dvd_query(int arg_title, int *arg_chapter, int *arg_angle)
{
    tc_log_error(__FILE__, "no support for DVD reading configured - exit.");
    return -1;
}

int dvd_init(const char *dvd_path, int *arg_title, int verb)
{
    tc_log_error(__FILE__, "no support for DVD reading configured - exit.");
    return -1;
}

int dvd_read(int arg_title, int arg_chapter, int arg_angle)
{
    tc_log_error(__FILE__, "no support for DVD reading configured - exit.");
    return -1;
}

int dvd_stream(int arg_title, int arg_chapid)
{
    tc_log_error(__FILE__, "no support for DVD reading configured - exit.");
    return -1;
}

int dvd_close(void)
{
    tc_log_error(__FILE__, "no support for DVD reading configured - exit.");
    return -1;
}

int dvd_verify(const char *name)
{
    tc_log_error(__FILE__, "no support for DVD reading configured - exit.");
    return -1;
}

int dvd_probe(int title, ProbeInfo *info)
{
    tc_log_error(__FILE__, "no support for DVD reading configured - exit.");
    return -1;
}

int dvd_is_valid(const char *dvd_path)
{
    tc_log_error(__FILE__, "no support for DVD reading configured - exit.");
    return TC_FALSE;
}

#endif
