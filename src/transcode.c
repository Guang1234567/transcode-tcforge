/*
 *  transcode.c
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

/* IMPORTANT:  Every source file MUST include either config.h or another
 * header that includes config.h (such as src/transcode.h) before including
 * any other header.  Failure to do so can cause bugs such as inability to
 * handle >2GB files, since including any system header may cause the
 * compilation environment to be set (and thus set incorrectly if config.h
 * has not been included). */

#include "transcode.h"
#include "decoder.h"
#include "dl_loader.h"
#include "framebuffer.h"
#include "counter.h"
#include "frame_threads.h"
#include "filter.h"
#include "probe.h"
#include "socket.h"
#include "split.h"

#include "cmdline.h"

#include "libtc/libtc.h"
#include "libtc/tccodecs.h"
#include "libtc/ratiocodes.h"
#include "libtcext/tc_ext.h"
#include "libtcutil/xio.h"
#include "libtcutil/cfgfile.h"
#include "libtcexport/export.h"
#include "libtcexport/export_profile.h"

#include <ctype.h>
#include <math.h>


/* ------------------------------------------------------------
 *
 * default options
 *
 * ------------------------------------------------------------*/


int rescale     = TC_FALSE;
int im_clip     = TC_FALSE;
int ex_clip     = TC_FALSE;
int pre_im_clip = TC_FALSE;
int post_ex_clip= TC_FALSE;
int resize1     = TC_FALSE;
int resize2     = TC_FALSE;

// global information structure
static vob_t *vob = NULL;
static TCSession *session = NULL;
int verbose = TC_INFO;

//-------------------------------------------------------------


/*************************************************************************/
/*********************** Exported utility routines ***********************/
/*************************************************************************/

/**
 * version:  Print a version message.  The message is only printed for the
 * first call.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 */

void version(void)
{
    static int tcversion = 0;
    if (tcversion++)
        return;
    fprintf(stderr, "%s v%s (C) 2001-2003 Thomas Oestreich,"
                              " 2003-2010 Transcode Team\n",
                              PACKAGE, VERSION);
}

/*************************************************************************/

/**
 * tc_get_vob:  Return a pointer to the global vob_t data structure.
 *
 * Parameters:
 *     None.
 * Return value:
 *     A pointer to the global vob_t data structure.
 */

vob_t *tc_get_vob()
{
    return vob;
}

/*************************************************************************/

/**
 * tc_get_session:  Return a pointer to the global TCSession data structure.
 *
 * Parameters:
 *     None.
 * Return value:
 *     A pointer to the global TCSession data structure.
 */

TCSession *tc_get_session()
{
    return session;
}


/*************************************************************************/

/**
 * validate_source_path:  Check whether the given string represents a valid
 * source pathname.
 *
 * Parameters:
 *     path: String to check.
 * Return value:
 *     Nonzero if the string is a valid source pathname, else zero.
 * Side effects:
 *     Prints an error message using tc_error() if the string is not a
 *     valid pathname.
 */

static int validate_source_path(const char *path)
{
    struct stat st;

    if (!path || !*path) {
        tc_error("No filename given");
        return 0;
    }
    if (strcmp(path, "-") == 0)  // allow stdin (maybe also /dev/stdin? FIXME)
        return 1;
    if (*path == '!' || *path == ':')  /* from transcode.c -- why? */
        return 1;
#if defined(__GLIBC__) && defined(__SIZEOF_SIZE_T__) && __SIZEOF_SIZE_T__ < 8 && !defined(__USE_FILE_OFFSET64)
#error __USE_FILE_OFFSET64 is not defined -- please check order of #includes
#endif
    if (xio_stat(path, &st) == 0)
        return 1;
    tc_error("Invalid filename \"%s\": %s", path, strerror(errno));
    return 0;
}

/*************************************************************************/

int tc_next_video_in_file(vob_t *vob)
{
    vob->video_in_file = tc_glob_next(vob->video_in_files);
    if (vob->video_in_file != NULL) {
        return TC_OK;
    }
    return TC_ERROR;
}

int tc_next_audio_in_file(vob_t *vob)
{
    vob->audio_in_file = tc_glob_next(vob->audio_in_files);
    if (vob->audio_in_file != NULL) {
        return TC_OK;
    }
    return TC_ERROR;
}

int tc_has_more_video_in_file(TCSession *session)
{
    int ret = TC_FALSE;

    if (session->core_mode == TC_MODE_DIRECTORY) {
        ret = tc_glob_has_more(session->job->video_in_files);
    }
    return ret;
}

int tc_has_more_audio_in_file(TCSession *session)
{
    int ret = TC_FALSE;

    if (session->core_mode == TC_MODE_DIRECTORY) {
        ret = tc_glob_has_more(session->job->audio_in_files);
    }
    return ret;
}

/*************************************************************************/
/*********************** Internal utility routines ***********************/
/*************************************************************************/

/*************************************************************************/
/*********************** Event thread support ****************************/
/*************************************************************************/

static pthread_t event_thread_id = (pthread_t)0;
static const char *signame = "unknown signal";

static void tc_stop_all(void)
{
    tc_stop();
    tc_framebuffer_interrupt();
} 

static void event_handler(int sig)
{
    switch (sig) {
      case SIGINT:
        signame = "SIGINT";
        break;
      case SIGTERM:
        signame = "SIGTERM";
        break;
      case SIGPIPE:
        signame = "SIGPIPE";
        break;
    }

    tc_interrupt();
    tc_framebuffer_interrupt();
}

/**
 * event_thread:  Thread that watches for certain termination signals,
 * terminating the transcode process cleanly.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 */

static void *event_thread(void* blocked_)
{
    TCSession *session = tc_get_session();
    struct sigaction handler;
    sigset_t *blocked = blocked_;

    /* catch everything */
    pthread_sigmask(SIG_UNBLOCK, blocked, NULL); /* XXX */

    /* install handlers, mutually exclusive */
    memset(&handler, 0, sizeof(struct sigaction));
    handler.sa_flags   = 0;
    handler.sa_mask    = *blocked;
    handler.sa_handler = event_handler;

    sigaction(SIGINT,  &handler, NULL); // XXX
    sigaction(SIGTERM, &handler, NULL); // XXX
/*  sigaction(SIGPIPE, &handler, NULL); */// XXX

    /* Loop waiting for external events */
    for (;;) {
        pthread_testcancel();

        tc_socket_wait();

        if (tc_interrupted()) {
            if (verbose >= TC_INFO)
                tc_log_info(PACKAGE, "(sighandler) %s received", signame);

            /* Kill the tcprobe process if it's running */
            if (session->tc_probe_pid > 0)
                kill(session->tc_probe_pid, SIGTERM);
        }
        pthread_testcancel();
    }
    return NULL;
}

/*************************************************************************/

/**
 * stop_event_thread:  Ensure that the event handling thread is destroyed.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 */

static void stop_event_thread(void)
{
    if (event_thread_id) {
        void *thread_status = NULL;

//        pthread_kill(event_thread_id, SIGINT);
        pthread_cancel(event_thread_id);
        pthread_join(event_thread_id, &thread_status);
    }
}

/*************************************************************************/
/*************************************************************************/

/**
 * load_all_filters:  Loads all filters specified by the -J option.
 *
 * Parameters:
 *     filter_list: String containing filter list from -J option.
 * Return value:
 *     None.
 * Side effects:
 *     Destroys `filter_list'.
 */

static void load_all_filters(char *filter_list)
{
    if (!filter_list)
        return;

    while (*filter_list) {
        char *s = filter_list + strcspn(filter_list, ",");
        char *options;

        while (*s && s > filter_list && s[-1] == '\\')
            s += 1 + strcspn(s+1, ",");
        if (*s)
            *s++ = 0;
        options = strchr(filter_list, '=');
        if (options)
            *options++ = 0;
        tc_filter_add(filter_list, options);
        filter_list = s;
    }
}

/*************************************************************************/

#define RETURN_IF(cond, msg, status) do { \
    if ((cond)) { \
        tc_log_error(PACKAGE, msg); \
        return status; \
    } \
} while (0)

/*************************************************************************/

static int transcode_find_modules(TCSession *S)
{
    const char *fmtname = NULL;
    TCRegistry registry = S->registry; /* shortcut */
    TCJob *job = S->job; /* shortcut */

    if (S->ex_vid_mod == NULL) {
        fmtname = tc_codec_to_string(job->ex_v_codec);
        S->ex_vid_mod = tc_get_module_name_for_format(registry, "encode",
                                                      fmtname);
    }
    RETURN_IF(S->ex_vid_mod == NULL,
              "unable to find the video encoder module and none specified",
              TC_ERROR);

    if (S->ex_aud_mod == NULL) {
        fmtname = tc_codec_to_string(job->ex_a_codec);
        S->ex_aud_mod = tc_get_module_name_for_format(registry, "encode",
                                                      fmtname);
    }
    RETURN_IF(S->ex_aud_mod == NULL,
              "unable to find the audio encoder module and none specified",
              TC_ERROR);

    if (S->ex_mplex_mod == NULL) {
        /* try by outfile extension */
        fmtname = strrchr(job->video_out_file, '.');
        if (fmtname) {
            S->ex_mplex_mod = tc_get_module_name_for_format(registry,
                                                            "multiplex",
                                                            fmtname);
        }
    }
    RETURN_IF(S->ex_mplex_mod == NULL,
              "unable to find the multiplexor module and none specified",
              TC_ERROR);

    if (job->audio_out_file != NULL) {
        if (S->ex_mplex_mod_aux == NULL) {
            /* try by outfile extension */
            fmtname = strrchr(job->audio_out_file, '.');
            if (fmtname) {
                S->ex_mplex_mod_aux = tc_get_module_name_for_format(registry,
                                                                    "multiplex",
                                                                    fmtname);
            }
        }
        RETURN_IF(S->ex_mplex_mod_aux == NULL,
                  "unable to find the aux multiplexor module and none specified",
                  TC_ERROR);
    }

    return TC_OK;
}


/**
 * transcode_init:  initialize the transcoding engine.
 *
 * Parameters:
 *      vob: Pointer to the global vob_t data structure.
 * Return value:
 *     TC_OK on success,
 *     TC_ERROR on error.
 */

static int transcode_init(TCSession *session, const TCFrameSpecs *specs)
{
    int ret;
    TCRunControl *runcontrol = tc_runcontrol_get_instance();
    TCJob *vob = session->job;

    /* load import modules and check capabilities */
    ret = tc_import_init(vob, session->im_aud_mod, session->im_vid_mod);
    RETURN_IF(ret < 0, "failed to init the import modules", TC_ERROR);

    /* load and initialize filters */
    tc_filter_init();
    load_all_filters(session->plugins_string);

    session->factory = tc_new_module_factory(vob->mod_path, verbose);
    RETURN_IF(session->factory == NULL,
              "failed to init the module factory", TC_ERROR);

    session->registry = tc_new_module_registry(session->factory,
                                               vob->reg_path, verbose);
    RETURN_IF(session->registry == NULL,
              "failed to init the module registry", TC_ERROR);

    /* load export modules and check capabilities
     * (only create a TCModule factory if a multiplex module was given) */
    ret = tc_export_new(vob, session->factory, runcontrol, specs);
    RETURN_IF(ret != TC_OK, "failed to init the export layer", TC_ERROR);

    tc_export_config(verbose, 1, session->cluster_mode);

    ret = transcode_find_modules(session);
    RETURN_IF(ret != TC_OK, "can't setup export modules", TC_ERROR);

    ret = tc_export_setup(session->ex_aud_mod, session->ex_vid_mod,
                          session->ex_mplex_mod, session->ex_mplex_mod_aux);
    RETURN_IF(ret != TC_OK, "failed to init the export modules", TC_ERROR);

    tc_export_rotation_limit_megabytes(session->split_size);
    tc_export_rotation_limit_frames(session->split_time);

    return TC_OK;
}

/**
 * transcode_fini:  finalize (shutdown) the transcoding engine.
 *
 * Parameters:
 *      session: Pointer to the transcode session to be finalized.
 * Return value:
 *     0 on success, -1 on error.
 */

/* FIXME: cleanup factory, registry */
static int transcode_fini(TCSession *session)
{
    /* unload import modules */
    tc_import_shutdown();
    /* unload filters */
    tc_filter_fini();
    /* unload export modules */
    tc_export_shutdown();
    tc_export_del();

    return 0;
}

/* -------------------------------------------------------------
 * single file continuous or interval mode
 * ------------------------------------------------------------*/

static int transcode_mode_default(TCSession *session)
{
    TCJob *vob = session->job;
    struct fc_time *tstart = NULL;
    int th_num = session->max_frame_threads; /* just a shortcut */

    tc_start();

    // init decoder and open the source
    if (0 != vob->ttime->vob_offset) {
        vob->vob_offset = vob->ttime->vob_offset;
    }
    if (tc_import_open(vob) < 0)
        tc_error("failed to open input source");

    // start the AV import threads that load the frames into transcode
    // this must be called after tc_import_open
    tc_import_threads_create(vob);

    // init encoder
    if (tc_export_init() != TC_OK)
        tc_error("failed to init encoder");

    // open output files
    if (tc_export_open() != TC_OK)
        tc_error("failed to open output");

    // tell counter about all encoding ranges
    counter_reset_ranges();
    if (!session->cluster_mode) {
        int last_etf = 0;
        for (tstart = vob->ttime; tstart; tstart = tstart->next) {
            if (tstart->etf == TC_FRAME_LAST) {
                // variable length range, oh well
                counter_reset_ranges();
                break;
            }
            if (tstart->stf > last_etf)
                counter_add_range(last_etf, tstart->stf-1, 0);
            counter_add_range(tstart->stf, tstart->etf-1, 1);
            last_etf = tstart->etf;
        }
    }

    // get start interval
    tstart = vob->ttime;

    while (tstart) {
        // set frame range (in cluster mode these will already be set)
        if (!session->cluster_mode) {
            session->frame_a = tstart->stf;
            session->frame_b = tstart->etf;
        }
        // main encoding loop, returns when done with all frames
        tc_export_loop(tc_get_ringbuffer(vob, th_num, th_num),
                       session->frame_a, session->frame_b);

        // check for user cancelation request
        if (tc_interrupted()) {
            break;
        }

        // next range
        tstart = tstart->next;
        // see if we're using vob_offset
        if ((tstart != NULL) && (tstart->vob_offset != 0)) {
            session->decoder_delay = 3;
            tc_import_threads_cancel();
            tc_import_close();
            tc_framebuffer_flush();
            vob->vob_offset = tstart->vob_offset;
            vob->sync = session->sync_seconds;
            if (tc_import_open(vob) < 0)
                tc_error("failed to open input source");
            tc_import_threads_create(vob);
        }
    }
    tc_stop_all();

    // flush buffered data
    tc_export_flush();
    // close output files
    tc_export_close();
    // stop encoder
    tc_export_stop();
    // cancel import threads
    tc_import_threads_cancel();
    // stop decoder and close the source
    tc_import_close();

    return TC_OK;
}

/* -------------------------------------------------------------
 * directory mode
 * ------------------------------------------------------------*/

static int transcode_mode_directory(TCSession *session)
{
    TCJob *vob = session->job;
    struct fc_time *tstart = NULL;
    int th_num = session->max_frame_threads; /* just a shortcut */
    
    tc_start();

    if (strcmp(vob->audio_in_file, vob->video_in_file) != 0)
        tc_error("directory mode DOES NOT support separate audio files (A=%s|V=%s)",
                 vob->audio_in_file, vob->video_in_file);

    tc_multi_import_threads_create(vob);

    if (tc_export_init() != TC_OK)
        tc_error("failed to init encoder");
    if (tc_export_open() != TC_OK)
        tc_error("failed to open output");

    // tell counter about all encoding ranges
    counter_reset_ranges();
    if (!session->cluster_mode) {
        int last_etf = 0;
        for (tstart = vob->ttime; tstart; tstart = tstart->next) {
            if (tstart->etf == TC_FRAME_LAST) {
                // variable length range, oh well
                counter_reset_ranges();
                break;
            }
            if (tstart->stf > last_etf)
                counter_add_range(last_etf, tstart->stf-1, 0);
            counter_add_range(tstart->stf, tstart->etf-1, 1);
            last_etf = tstart->etf;
        }
    }

    // get start interval
    for (tstart = vob->ttime;
         tstart != NULL && !tc_interrupted();
         tstart = tstart->next) {
        // set frame range (in cluster mode these will already be set)
        if (!session->cluster_mode) {
            session->frame_a = tstart->stf;
            session->frame_b = tstart->etf;
        }
        // main encoding loop, returns when done with all frames
        tc_export_loop(tc_get_ringbuffer(vob, th_num, th_num),
                       session->frame_a, session->frame_b);

    }

    tc_stop_all();

    tc_export_flush();
    tc_export_close();
    tc_export_stop();
    tc_multi_import_threads_cancel();

    return TC_OK;
}

/* ---------------------------------------------------------------
 * VOB PSU mode: transcode and split based on program stream units
 * --------------------------------------------------------------*/

static int transcode_mode_psu(TCSession *session, const char *psubase)
{
    TCJob *vob = session->job;
    char buf[TC_BUF_MAX];
    int fa, fb, psu_cur = vob->vob_psu_num1;
    int th_num = session->max_frame_threads; /* just a shortcut */

    if (tc_export_init() != TC_OK)
        tc_error("failed to init encoder");

    // open output
    if (no_split) {
        vob->video_out_file = psubase;
        if (tc_export_open() != TC_OK)
            tc_error("failed to open output");
    }

    session->decoder_delay = 3;

    counter_on();

    for (;;) {
        int ret;

        memset(buf, 0, sizeof buf);
        if (!no_split) {
            // create new filename
            tc_snprintf(buf, sizeof(buf), psubase, psu_cur);
            // update vob structure
            vob->video_out_file = buf;

            if (verbose >= TC_INFO)
                tc_log_info(PACKAGE, "using output filename %s",
                            vob->video_out_file);
        }

        // get seek/frame information for next PSU
        // need to process whole PSU
        vob->vob_chunk = 0;
        vob->vob_chunk_max = 1;

        ret = split_stream(vob, nav_seek_file, psu_cur, &fa, &fb, 0);

        if (verbose >= TC_DEBUG)
            tc_log_msg(PACKAGE,"processing PSU %d, -L %d -c %d-%d %s (ret=%d)",
                       psu_cur, vob->vob_offset, fa, fb, buf, ret);

        // exit condition
        if (ret < 0 || psu_cur == vob->vob_psu_num2)
            break;
    
        // do not process units with a small frame number, assume it is junk
        if ((fb-fa) > session->psu_frame_threshold) {
            tc_start();

            // start new decoding session with updated vob structure
            // this starts the full decoder setup, including the threads
            if (tc_import_open(vob) < 0)
                tc_error("failed to open input source");

            // start the AV import threads that load the frames into transcode
            tc_import_threads_create(vob);

            // frame threads may need a reboot too.
            tc_frame_threads_init(vob, th_num, th_num);

            // open new output file
            if (!no_split) {
                if (tc_export_open() != TC_OK)
                    tc_error("failed to open output");
            }

            // core
            // we try to encode more frames and let the decoder safely
            // drain the queue to avoid threads not stopping

            tc_export_loop(tc_get_ringbuffer(vob, th_num, th_num),
                           fa, TC_FRAME_LAST);
            tc_export_flush();

            // close output file
            if (!no_split) {
                if (tc_export_close() != TC_OK)
                    tc_warn("failed to close encoder - non fatal");
            }

            // for debug
            vframe_dump_status();
            aframe_dump_status();

            // cancel import threads
            tc_import_threads_cancel();
            // stop decoder and close the source
            tc_import_close();

            // flush all buffers before we proceed to next PSU
            tc_framebuffer_flush();

            vob->psu_offset += (double) (fb-fa);
        } else {
            if (verbose >= TC_INFO)
                tc_log_info(PACKAGE, "skipping PSU %d with %d frame(s)",
                            psu_cur, fb-fa);

        }

        psu_cur++;
        if (tc_interrupted())
            break;
    } //next PSU

    // close output
    if (no_split) {
        if (tc_export_close() != TC_OK)
            tc_warn("failed to close encoder - non fatal");
    }

    tc_stop_all();

    tc_export_stop();

    return TC_OK;
}

/* ------------------------------------------------------------
 * DVD chapter mode
 * ------------------------------------------------------------*/

static int transcode_mode_dvd(TCSession *session)
{
#ifdef HAVE_LIBDVDREAD
    TCJob *vob = session->job;
    char buf[TC_BUF_MAX];
    int ch1, ch2;
    int th_num = session->max_frame_threads; /* just a shortcut */

    tc_start();

    if (tc_export_init() != TC_OK)
        tc_error("failed to init encoder");

    // open output
    if (no_split) {
        // create new filename
        tc_snprintf(buf, sizeof(buf), "%s.avi", chbase);
        // update vob structure
        vob->video_out_file = buf;
        //vob->audio_out_file = buf; // FIXME

        if (tc_export_open() != TC_OK)
            tc_error("failed to open output");
    }

    // 1 sec delay after decoder closing
    session->decoder_delay = 1;

    // loop each chapter
    ch1 = vob->dvd_chapter1;
    ch2 = vob->dvd_chapter2;

    //ch = -1 is allowed but makes no sense
    if (ch1 < 0)
        ch1 = 1;

    for (;;) {
        vob->dvd_chapter1 = ch1;
        vob->dvd_chapter2 =  -1;

        if (!no_split) {
            // create new filename
            tc_snprintf(buf, sizeof(buf), "%s-ch%02d.avi", chbase, ch1);
            // update vob structure
            vob->video_out_file = buf;
            //vob->audio_out_file = buf; // FIXME
        }

        // start decoding with updated vob structure
        if (tc_import_open(vob) < 0)
            tc_error("failed to open input source");

        // start the AV import threads that load the frames into transcode
        tc_import_threads_create(vob);

        if (verbose >= TC_DEBUG)
            tc_log_msg(PACKAGE, "%d chapters for title %d detected",
                       vob->dvd_max_chapters, vob->dvd_title);

        // encode
        if (!no_split) {
            if (tc_export_open() != TC_OK)
                tc_error("failed to init encoder");
        }

        // main encoding loop, selecting an interval won't work
        tc_export_loop(tc_get_ringbuffer(vob, th_num, th_num),
                       session->frame_a, session->frame_b);
        tc_export_flush();

        if (!no_split) {
            if (tc_export_close() != TC_OK)
                tc_warn("failed to close encoder - non fatal");
        }

        // cancel import threads
        tc_import_threads_cancel();
        // stop decoder and close the source
        tc_import_close();

        // flush all buffers before we proceed
        tc_framebuffer_flush();

        // exit,  i) if import module could not determine max_chapters
        //       ii) all chapters are done
        //      iii) someone hit ^C

        if (vob->dvd_max_chapters ==- 1
         || ch1 == vob->dvd_max_chapters || ch1 == ch2
         || tc_interrupted())
            break;
        ch1++;
    }

    if (no_split) {
        if (tc_export_close() != TC_OK)
            tc_warn("failed to close encoder - non fatal");
    }

    tc_stop_all();
    tc_export_stop();
#endif

    return TC_OK;
}

/*************************************************************************/

/**
 * new_vob:  Create a new vob_t structure and fill it with appropriate
 * values.
 *
 * Parameters:
 *     None.
 * Return value:
 *     A pointer to the newly-created vob_t structure, or NULL on error.
 * Notes:
 *     On error, errno is valid.
 */

static vob_t *new_vob(void)
{
    vob_t *vob = tc_malloc(sizeof(vob_t));
    if (!vob)
        return NULL;

    vob->divxbitrate         = VBITRATE;
    vob->video_max_bitrate   = 0;           /* 0 = set by encoder */
    vob->divxkeyframes       = VKEYFRAMES;
    vob->divxquality         = VQUALITY;
    vob->divxmultipass       = VMULTIPASS;
    vob->divxcrispness       = VCRISPNESS;
    vob->m2v_requant         = M2V_REQUANT_FACTOR;

    vob->min_quantizer       = VMINQUANTIZER;
    vob->max_quantizer       = VMAXQUANTIZER;

    vob->mp3bitrate          = ABITRATE;
    vob->mp3frequency        = 0;
    vob->mp3quality          = AQUALITY;
    vob->mp3mode             = AMODE;
    vob->a_rate              = RATE;
    vob->a_stream_bitrate    = 0;
    vob->a_bits              = BITS;
    vob->a_chan              = CHANNELS;
    vob->a_padrate           = 0;

    vob->dm_bits             = 0;
    vob->dm_chan             = 0;

    vob->im_a_size           = SIZE_PCM_FRAME;
    vob->im_v_width          = PAL_W;
    vob->im_v_height         = PAL_H;
    vob->im_v_size           = SIZE_RGB_FRAME;
    vob->ex_a_size           = SIZE_PCM_FRAME;
    vob->ex_v_width          = PAL_W;
    vob->ex_v_height         = PAL_H;
    vob->ex_v_size           = SIZE_RGB_FRAME;
    vob->a_track             = 0;
    vob->v_track             = 0;
    vob->volume              = 0;
    vob->ac3_gain[0]         = 1.0;
    vob->ac3_gain[1]         = 1.0;
    vob->ac3_gain[2]         = 1.0;
    vob->audio_out_file      = NULL;
    vob->video_out_file      = NULL;
    vob->avifile_in          = NULL;
    vob->avifile_out         = NULL;
    vob->avi_comment_fd      = -1;
    vob->nav_seek_file       = NULL;
    vob->audio_file_flag     = 0;
    vob->audio_in_file       = NULL;
    vob->video_in_file       = NULL;
    vob->clip_count          = 0;
    vob->ex_a_codec          = TC_CODEC_MP3;   //or fall back to module default
    vob->ex_v_codec          = TC_CODEC_ERROR; //determined by export module type
    vob->ex_v_fcc            = NULL;
    vob->ex_a_fcc            = NULL;
    vob->ex_profile_name     = NULL;
    vob->fps                 = PAL_FPS;
    vob->ex_fps              = 0;
    vob->im_frc              = 0;
    vob->ex_frc              = 0;
    vob->pulldown            = 0;
    vob->im_clip_top         = 0;
    vob->im_clip_bottom      = 0;
    vob->im_clip_left        = 0;
    vob->im_clip_right       = 0;
    vob->ex_clip_top         = 0;
    vob->ex_clip_bottom      = 0;
    vob->ex_clip_left        = 0;
    vob->ex_clip_right       = 0;
    vob->resize1_mult        = 32;
    vob->vert_resize1        = 0;
    vob->hori_resize1        = 0;
    vob->resize2_mult        = 32;
    vob->vert_resize2        = 0;
    vob->hori_resize2        = 0;
    vob->sync                = 0;
    vob->sync_ms             = 0;
    vob->sync_samples        = 0;
    vob->dvd_title           = 1;
    vob->dvd_chapter1        = 1;
    vob->dvd_chapter2        = -1;
    vob->dvd_max_chapters    = -1;
    vob->dvd_angle           = 1;
    vob->pass_flag           = 0;
    vob->verbose             = TC_QUIET;
    vob->antialias           = 0;
    vob->deinterlace         = 0;
    vob->decolor             = 0;
    vob->im_a_codec          = TC_CODEC_PCM;
    vob->im_v_codec          = TC_CODEC_YUV420P;
    vob->mod_path            = tc_module_default_path();
    vob->audiologfile        = NULL;
    vob->divxlogfile         = NULL;
    vob->ps_unit             = 0;
    vob->ps_seq1             = 0;
    vob->ps_seq2             = TC_FRAME_LAST;
    vob->a_leap_frame        = TC_LEAP_FRAME;
    vob->a_leap_bytes        = 0;
    vob->demuxer             = -1;
    vob->a_codec_flag        = TC_CODEC_AC3;
    vob->gamma               = 0.0;
    vob->encoder_flush       = TC_TRUE;
    vob->has_video           = 1;
    vob->has_audio           = 1;
    vob->has_audio_track     = 1;
    vob->lang_code           = 0;
    vob->v_format_flag       = 0;
    vob->v_codec_flag        = 0;
    vob->a_format_flag       = 0;
    vob->im_asr              = 0;
    vob->im_par              = 0;
    vob->im_par_width        = 0;
    vob->im_par_height       = 0;
    vob->ex_asr              = -1;
    vob->ex_par              = 0;
    vob->ex_par_width        = 0;
    vob->ex_par_height       = 0;
    vob->quality             = VQUALITY;
    vob->amod_probed         = "null";
    vob->vmod_probed         = "null";
    vob->amod_probed_xml     = NULL;
    vob->vmod_probed_xml     = NULL;
    vob->a_vbr               = 0;
    vob->pts_start           = 0.0f;
    vob->vob_offset          = 0;
    vob->vob_chunk           = 0;
    vob->vob_chunk_max       = 0;
    vob->vob_chunk_num1      = 0;
    vob->vob_chunk_num2      = 0;
    vob->vob_psu_num1        = 0;
    vob->vob_psu_num2        = INT_MAX;
    vob->vob_info_file       = NULL;
    vob->vob_percentage      = 0;
    vob->im_a_string         = NULL;
    vob->im_v_string         = NULL;
    vob->ex_a_string         = NULL;
    vob->ex_v_string         = NULL;
    vob->ex_m_string         = NULL;

    vob->reduce_h            = 1;
    vob->reduce_w            = 1;

    //-Z
    vob->zoom_flag           = TC_FALSE;
    vob->zoom_width          = 0;
    vob->zoom_height         = 0;
    vob->zoom_filter         = TCV_ZOOM_LANCZOS3;
    vob->zoom_interlaced     = 0;

    vob->frame_interval      = 1; // write every frame

    //anti-alias
    vob->aa_weight           = TC_DEFAULT_AAWEIGHT;
    vob->aa_bias             = TC_DEFAULT_AABIAS;

    vob->a52_mode            = 0;
    vob->encode_fields       = TC_ENCODE_FIELDS_PROGRESSIVE;

    vob->ttime               = NULL;

    vob->psu_offset          = 0.0f;

    vob->ts_pid1             = 0x0;
    vob->ts_pid2             = 0x0;

    vob->dv_yuy2_mode        = 0;
    vob->hard_fps_flag       = 0;
    vob->ex_prof_name        = NULL;

    vob->attributes          = 0;
    vob->export_attributes   = TC_EXPORT_ATTRIBUTE_NONE;

    vob->resync_frame_interval = 0;
    vob->resync_frame_margin   = 1;

    vob->rgbswap             = TC_FALSE;
    vob->pcmswap             = TC_FALSE;
    vob->dgamma              = TC_FALSE;
    vob->keepasr             = TC_FALSE;
    vob->fast_resize         = TC_FALSE;
    vob->flip                = TC_FALSE;
    vob->mirror              = TC_FALSE;
 
    return vob;
}

/*************************************************************************/

/**
 * new_session:  Create a new TCSession structure and fill it with appropriate
 * values.
 *
 * Parameters:
 *     None.
 * Return value:
 *     A pointer to the newly-created TCSession structure, or NULL on error.
 * Notes:
 *     On error, errno is valid.
 */

static TCSession *new_session(TCJob *job)
{
    TCSession *session = tc_malloc(sizeof(vob_t));
    if (!session)
        return NULL;

    session->job                 = job;

    session->acceleration        = AC_ALL;

    session->tc_pid              = getpid();

    session->factory             = NULL;
    session->registry            = NULL;

    session->core_mode           = TC_MODE_DEFAULT;

    session->im_aud_mod          = NULL;
    session->im_vid_mod          = NULL;

    session->ex_aud_mod          = NULL;
    session->ex_vid_mod          = NULL;
    session->ex_mplex_mod        = NULL;
    session->ex_mplex_mod_aux    = NULL;

    session->plugins_string      = NULL;

    session->nav_seek_file       = NULL;
    session->socket_file         = NULL;
    session->chbase              = NULL;
    memset(session->base, 0, sizeof(session->base));

    session->psu_frame_threshold = 12;
    /* FIXME: magic number */
    
    // FIXME: those must go away soon
    // begin
    session->no_vin_codec        = 1;
    session->no_ain_codec        = 1;
    session->no_v_out_codec      = 1;
    session->no_a_out_codec      = 1;
    // end
    
    session->frame_a             = TC_FRAME_FIRST;
    /* processing interval: start frame */
    session->frame_b             = TC_FRAME_LAST;
    /* processing interval: stop frame */

    session->split_time          = 0; /* disabled*/
    session->split_size          = 0; /* disabled*/
    session->psu_mode            = TC_FALSE;

    session->preset_flag         = 0;
    session->auto_probe          = 1;
    session->seek_range          = 1;

    session->audio_adjust        = TC_TRUE;
    session->split               = TC_TRUE;

    session->fc_ttime_string     = NULL;

    session->sync_seconds        = 0;

    session->max_frame_buffers   = 10; /* FIXME magic number */
    session->hw_threads          = 1;  /* sane fallback */
    tc_sys_get_hw_threads(&(session->hw_threads));
    session->max_frame_threads   = session->hw_threads;

    session->progress_meter      = -1;
    session->progress_rate       = 1;

    return session;
}
 
/*************************************************************************/

/*
 * parse_navigation_file:
 *      parse navigation data file and setup vob data fields accordingly.
 *      This function handle both aviindex and tcdemux -W generated files.
 *
 * Parameters:
 *                vob: Pointer to the global vob_t data structure.
 *      nav_seek_file: Path of navigation file.
 * Return Value:
 *      None
 */
static void parse_navigation_file(vob_t *vob, const char *nav_seek_file)
{
    if (nav_seek_file) {
        FILE *fp = NULL;
        struct fc_time *tmptime = NULL;
        char buf[TC_BUF_MIN];
        int line_count = 0;
        int flag = 0;
        int is_aviindex = 0;

        if (vob->vob_offset) {
            tc_warn("-L and --nav_seek are incompatible.");
        }

        fp = fopen(nav_seek_file, "r");
        if (NULL == fp) {
            tc_error("unable to open: %s", nav_seek_file);
        }

        tmptime = vob->ttime;
        line_count = 0;

        // check if this is an AVIIDX1 file
        if (fgets(buf, sizeof(buf), fp)) {
            if (strncasecmp(buf, "AVIIDX1", 7) == 0)
                is_aviindex=1;
            fseek(fp, 0, SEEK_SET);
        } else {
            tc_error("An error happend while reading the nav_seek file");
        }

        if (!is_aviindex) {
            while (tmptime){
                flag = 0;
                for (; fgets(buf, sizeof(buf), fp); line_count++) {
                    int L, new_frame_a;

                    if (2 == sscanf(buf, "%*d %*d %*d %*d %d %d ", &L, &new_frame_a)) {
                        if (line_count == tmptime->stf) {
                            int len = tmptime->etf - tmptime->stf;
                            tmptime->stf = session->frame_a = new_frame_a;
                            tmptime->etf = session->frame_b = new_frame_a + len;
                            tmptime->vob_offset = L;
                            flag = 1;
                            line_count++;
                            break;
                        }
                    }
                }
                tmptime = tmptime->next;
            }
        } else { // is_aviindex==1
            char *dummy;  // Avoid compiler warnings
            dummy = fgets(buf, sizeof(buf), fp); // magic
            dummy = fgets(buf, sizeof(buf), fp); // comment

            while (tmptime) {
                int new_frame_a, type, key;
                long chunk, chunkptype, last_keyframe = 0;
                long long pos, len;
                char tag[4];
                double ms = 0.0;
                flag = 0;

                for (; fgets(buf, sizeof(buf), fp); line_count++) {
                    // TAG TYPE CHUNK CHUNK/TYPE POS LEN KEY MS
                    if (sscanf(buf, "%s %d %ld %ld %lld %lld %d %lf",
                               tag, &type, &chunk, &chunkptype, &pos, &len, &key, &ms)) {
                        if (type != 1)
                            continue;
                        if (key)
                            last_keyframe = chunkptype;
                        if (chunkptype == tmptime->stf) {
                            int lenf = tmptime->etf - tmptime->stf;
                            new_frame_a = chunkptype - last_keyframe;

                            // If we are doing pass-through, we cannot skip frames, but only start
                            // passthrough on a keyframe boundary. At least, we respect the
                            // last frame the user whishes.
                            if (vob->pass_flag & TC_VIDEO) {
                                new_frame_a = 0;
                                lenf += (chunkptype - last_keyframe);
                            }

                            tmptime->stf = session->frame_a = new_frame_a;
                            tmptime->etf = session->frame_b = new_frame_a + lenf;
                            tmptime->vob_offset = last_keyframe;
                            flag = 1;
                            line_count++;
                            break;
                        }
                    }
                }
                tmptime = tmptime->next;
            }
        }
        fclose(fp);

        if (!flag) {
            //frame not found
            tc_warn("%s: frame %d out of range (%d frames found)",
                    nav_seek_file, session->frame_a, line_count);
            tc_error("invalid option parameter for -c / --nav_seek");
        }
    }
}

/*************************************************************************/

static void handle_keep_asr(vob_t *vob)
{
    int clip, zoomto;
    double asr_out = (double)vob->ex_v_width/(double)vob->ex_v_height;
    double asr_in  = (double)vob->im_v_width/(double)vob->im_v_height;
    double delta   = 0.01;
    double asr_cor = 1.0;


    if (vob->im_asr) {
        switch (vob->im_asr) {
          case 1:
            asr_cor = (1.0);
            break;
          case 2:
            asr_cor = (4.0/3.0);
            break;
          case 3:
            asr_cor = (16.0/9.0);
            break;
          case 4:
            asr_cor = (2.21);
            break;
        }
    }

    if (!vob->zoom_flag)
        tc_error ("keep_asr only works with -Z");

    if (asr_in-delta < asr_out && asr_out < asr_in+delta)
        tc_error ("Aspect ratios are too similar, don't use --keep_asr ");

    if (asr_in > asr_out) {
        /* adjust height */
        int clipV = (vob->im_clip_top +vob->im_clip_bottom);
        int clipH = (vob->im_clip_left+vob->im_clip_right);
        int clip1 = 0;
        int clip2 = 0;

        zoomto = (int)((double)(vob->ex_v_width) /
                   ( ((double)(vob->im_v_width -clipH) / (vob->im_v_width/asr_cor/vob->im_v_height) )/
                    (double)(vob->im_v_height-clipV))+.5);
        clip = vob->ex_v_height - zoomto;
        if (zoomto % 2 != 0)
            (clip>0?zoomto--:zoomto++); // XXX
        clip = vob->ex_v_height - zoomto;
        clip /= 2;
        clip1 = clip2 = clip;

        if (clip & 1) {
            clip1--;
            clip2++;
        }
        ex_clip = TC_TRUE;
        vob->ex_clip_top = -clip1;
        vob->ex_clip_bottom = -clip2;

        vob->zoom_height = zoomto;
    } else {
        /* adjust width */
        int clipV = (vob->im_clip_top +vob->im_clip_bottom);
        int clipH = (vob->im_clip_left+vob->im_clip_right);
        int clip1 = 0;
        int clip2 = 0;
        zoomto = (int)((double)vob->ex_v_height * (
                  ( ((double)(vob->im_v_width-clipH)) / (vob->im_v_width/asr_cor/vob->im_v_height) ) /
                    (double)(vob->im_v_height-clipV)) +.5);

        clip = vob->ex_v_width - zoomto;

        if (zoomto % 2 != 0)
            (clip>0?zoomto--:zoomto++); // XXX
        clip = vob->ex_v_width - zoomto;
        clip /= 2;
        clip1 = clip2 = clip;

        if (clip & 1) {
            clip1--;
            clip2++;
        }
        ex_clip = TC_TRUE;
        vob->ex_clip_left = -clip1;
        vob->ex_clip_right = -clip2;

        vob->zoom_width = zoomto;
    }

    /* sanity checks at last -- DON'T REMOVE THEM! */
    if (vob->ex_v_height - vob->ex_clip_top - vob->ex_clip_bottom <= 0)
        tc_error("invalid top/bottom clip parameter calculated from --keep_asr");

    if (vob->ex_v_width - vob->ex_clip_left - vob->ex_clip_right <= 0)
        tc_error("invalid left/right clip parameter calculated from --keep_asr");

    if (verbose >= TC_INFO)
        tc_log_info(PACKAGE, "V: %-16s | yes (%d,%d,%d,%d)", "keep aspect",
                    vob->ex_clip_top, vob->ex_clip_left,
                    vob->ex_clip_bottom, vob->ex_clip_right);
}

/*************************************************************************/

static void setup_input_sources(vob_t *vob)
{
    if (vob->video_in_file == NULL && vob->audio_in_file == NULL)
        tc_error("no input sources available");
    if (vob->audio_in_file == NULL)
        vob->audio_in_file = vob->video_in_file;

    /* 
     * let's try happily both sources independently.
     * At least one will succeed, if we're here.
     */
    vob->video_in_files = tc_glob_open(vob->video_in_file, 0);
    if (vob->video_in_files) {
        /* we always have at least one source */
        tc_next_video_in_file(vob);
    }
    if (!validate_source_path(vob->video_in_file)) {
        tc_error("invalid input video file: %s", vob->video_in_file);
    }

    vob->audio_in_files = tc_glob_open(vob->audio_in_file, 0);
    if (vob->audio_in_files) {
        /* we always have at least one source */
        tc_next_audio_in_file(vob);
    }
    if (!validate_source_path(vob->audio_in_file)) {
        tc_error("invalid input audio file: %s", vob->audio_in_file);
    }
}

static void teardown_input_sources(vob_t *vob)
{
    if (vob->video_in_files) {
        tc_glob_close(vob->video_in_files);
        vob->video_in_files = NULL;
    }
    if (vob->audio_in_files) {
        tc_glob_close(vob->audio_in_files);
        vob->audio_in_files = NULL;
    }
}

/*************************************************************************/

/* support macros */

#define CLIP_CHECK(MODE, NAME, OPTION) do { \
    /* force to even for YUV mode */ \
    if (vob->im_v_codec == TC_CODEC_YUV420P || vob->im_v_codec == TC_CODEC_YUV422P) { \
        if (vob->MODE ## _left % 2 != 0) { \
            tc_warn("left/right %s must be even in YUV/YUV422 mode", NAME); \
            vob->MODE ## _left--; \
        } \
        if (vob->MODE ## _right % 2 != 0) { \
            tc_warn("left/right %s must be even in YUV/YUV422 mode", NAME); \
            vob->MODE ## _right--; \
        } \
        if (vob->im_v_codec == TC_CODEC_YUV420P && vob->MODE ## _top % 2 != 0) { \
            tc_warn("top/bottom %s must be even in YUV mode", NAME); \
            vob->MODE ## _top--; \
        } \
        if (vob->im_v_codec == TC_CODEC_YUV420P && vob->MODE ## _bottom % 2 != 0) { \
            tc_warn("top/bottom %s must be even in YUV mode", NAME); \
            vob->MODE ## _bottom--; \
        } \
    } \
    /* check against import parameter, this is pre processing! */ \
    if (vob->ex_v_height - vob->MODE ## _top - vob->MODE ## _bottom <= 0 \
     || vob->ex_v_height - vob->MODE ## _top - vob->MODE ## _bottom > TC_MAX_V_FRAME_HEIGHT) \
        tc_error("invalid top/bottom clip parameter for option %s", OPTION); \
    \
    if (vob->ex_v_width - vob->MODE ## _left - vob->MODE ## _right <= 0 \
     || vob->ex_v_width - vob->MODE ## _left - vob->MODE ## _right > TC_MAX_V_FRAME_WIDTH) \
        tc_error("invalid left/right clip parameter for option %s", OPTION); \
    \
    vob->ex_v_height -= (vob->MODE ## _top + vob->MODE ## _bottom); \
    vob->ex_v_width  -= (vob->MODE ## _left + vob->MODE ## _right); \
} while (0)



#define SHUTDOWN_MARK(STAGE) do { \
    tc_debug(TC_DEBUG_CLEANUP, "shutdown: %s", (STAGE)); \
} while (0)


/*************************************************************************/

/* common support data */

typedef struct ratio_t { 
    int t, b; 
} ratio_t;

static const ratio_t asrs[] = { 
    {   1,   1 }, {   1,   1 }, {   4,   3 }, {  16,   9 },
    { 221, 100 }, { 250, 100 }, { 125, 100 }
};

static const char *demuxer_desc[] = {
    "sync AV at PTS start - demuxer disabled",
    "sync AV at initial MPEG sequence",
    "initial MPEG sequence / enforce frame rate",
    "sync AV at initial PTS",
    "initial PTS / enforce frame rate",
    "sync AV by adjusting frames",
};

static const char *deinterlace_desc[] = {
    "disabled", /* never used */
    "interpolate scanlines (fast)",
    "handled by encoder (if available)",
    "zoom to full frame (slow)",
    "drop field / half height (fast)",
    "interpolate scanlines / blend frames",
};

 static const char *antialias_desc[] = {
    "disabled", /* never used */
    "de-interlace effects only",
    "resize effects only",
    "process full frame (slow)"
};


/**
 * main:  transcode main routine.  Performs initialization, parses command
 * line options, and calls the transcoding routines.
 *
 * Parameters:
 *     argc: Command line argument count.
 *     argv: Command line argument vector.
 * Return value:
 *     Zero on success, nonzero on error (exit code).
 */

int main(int argc, char *argv[])
{
    sigset_t sigs_to_block;
    const char *psubase = NULL;
    double fch, asr;
    int ret, leap_bytes1, leap_bytes2;
    struct fc_time *tstart = NULL;
    const TCExportInfo *info = NULL;
    TCFrameSpecs specs;

    /* ------------------------------------------------------------
     *
     *  (I) set transcode defaults:
     *
     * ------------------------------------------------------------ */

    // create global vob structure
    vob = new_vob();
    if (!vob) {
        tc_error("job data initialization failed");
    }

    session = new_session(vob);
    if (!session) {
        tc_error("session data initialization failed");
    }

    // prepare for signal catching
    sigemptyset(&sigs_to_block);
    sigaddset(&sigs_to_block, SIGINT);
    sigaddset(&sigs_to_block, SIGTERM);
    // enabling this breaks the import_vob module.
    //sigaddset(&sigs_to_block, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &sigs_to_block, NULL);

    // start of the signal handler thread is delayed later
    
    // close all threads at exit
    atexit(stop_event_thread);

    /* ------------------------------------------------------------
     *
     * (II) parse command line
     *
     * ------------------------------------------------------------ */

    /* 
     * A *FEW* special options that deserve separate treatment.
     * PLEASE keep VERY LOW the number of this special cases.
     */
    libtc_init(&argc, &argv);

    ret = tc_export_profile_setup_from_cmdline(&argc, &argv);
    if (ret < 0)
        tc_error("failed to setup export profile");

    info = tc_export_profile_load_all();
    tc_export_profile_to_job(info, vob);
 
    if (!parse_cmdline(argc, argv, vob, session))
        exit(EXIT_FAILURE);

    setup_input_sources(vob);

    if (session->progress_meter < 0) {
        // if we have verbosity disabled, default to no progress meter.
        if (verbose) {
            session->progress_meter = 1;
        } else {
            session->progress_meter = 0;
        }
    }

    if (session->psu_mode) {
        if (vob->video_out_file == NULL)
            tc_error("please specify output file name for psu mode");
        if (!strchr(vob->video_out_file, '%') && !no_split) {
            char *pc = tc_malloc(PATH_MAX);
            char *suffix = strrchr(vob->video_out_file, '.');
            if (suffix) {
                *suffix = '\0';
            } else {
                suffix = "";
            }
            tc_snprintf(pc, PATH_MAX, "%s-psu%%02d%s",
                        vob->video_out_file, suffix);
            psubase = pc;
        } else {
            psubase = vob->video_out_file;
        }
    }

    // user doesn't want to start at all;-(
    if (tc_interrupted())
        goto summary;

    // display program version
    if (verbose)
        version();

    if (session->niceness) {
        if (nice(session->niceness) < 0) {
            tc_warn("setting nice to %d failed", session->niceness);
        }
    }

    /* ------------------------------------------------------------
     *
     * (III) auto probe properties of input stream
     *
     * ------------------------------------------------------------ */

    if (auto_probe) {
        // interface to "tcprobe"
        int result = probe_source(vob->video_in_file, vob->audio_in_file, seek_range,
                                  preset_flag, vob);
        if (verbose) {
            tc_log_info(PACKAGE, "V: %-16s | %s (%s)", "auto-probing",
                        (vob->video_in_file != NULL) ?vob->video_in_file :"N/A",
                        result ? "OK" : "FAILED");
            tc_log_info(PACKAGE, "V: %-16s | %s in %s (module=%s)",
                        "import format",
                        tc_codec_to_comment(vob->v_codec_flag),
                        tc_format_to_comment(vob->v_format_flag),
                        session->no_vin_codec == 0 ? session->im_vid_mod : vob->vmod_probed);
            tc_log_info(PACKAGE, "A: %-16s | %s (%s)", "auto-probing",
                        (vob->audio_in_file != NULL) ?vob->audio_in_file :"N/A",
                        result ? "OK" : "FAILED");
            tc_log_info(PACKAGE, "A: %-16s | %s in %s (module=%s)",
                        "import format",
                        tc_codec_to_comment(vob->a_codec_flag),
                        tc_format_to_comment(vob->a_format_flag),
                        session->no_ain_codec==0 ? session->im_aud_mod : vob->amod_probed);
        }
    }

    if (vob->vmod_probed_xml && strstr(vob->vmod_probed_xml, "xml") != NULL
     && vob->video_in_file) {
        if (!probe_source_xml(vob, PROBE_XML_VIDEO))
            tc_error("failed to probe video XML source");
    }
    if (vob->amod_probed_xml && strstr(vob->amod_probed_xml, "xml") != NULL
     && vob->audio_in_file) {
        if (!probe_source_xml(vob, PROBE_XML_AUDIO))
            tc_error("failed to probe audio XML source");
    }

    /* ------------------------------------------------------------
     *
     * (IV) autosplit stream for cluster processing
     *
     * currently, only VOB streams are supported
     *
     * ------------------------------------------------------------*/

    // set up ttime from -c or default
    if (session->fc_ttime_string) {
        // FIXME: should be in -c handler, but we need to know vob->fps first
        free_fc_time(vob->ttime);
        if (parse_fc_time_string(session->fc_ttime_string, vob->fps, ",",
                                 (verbose>1 ? 1 : 0), &vob->ttime) == -1)
            tc_error("error parsing time specifications");
    } else {
        vob->ttime = new_fc_time();
        vob->ttime->fps = vob->fps;
        vob->ttime->stf = TC_FRAME_FIRST;
        vob->ttime->etf = TC_FRAME_LAST;
        vob->ttime->next = NULL;
    }
    session->frame_a = vob->ttime->stf;
    session->frame_b = vob->ttime->etf;
    vob->ttime->vob_offset = 0;
    tstart = vob->ttime;
    counter_on(); //activate

    // determine -S,-c,-L option parameter for distributed processing
    parse_navigation_file(vob, nav_seek_file);

    if (vob->vob_chunk_max) {
        int this_unit = -1;

        // overwrite tcprobe's unit preset:
        if (preset_flag & TC_PROBE_NO_SEEK)
            this_unit = vob->ps_unit;

        if (split_stream(vob, vob->vob_info_file, this_unit,
                         &session->frame_a, &session->frame_b, 1) < 0)
            tc_error("cluster mode option -W error");
    }

    /* ------------------------------------------------------------
     *
     * some sanity checks for command line parameters
     *
     * ------------------------------------------------------------*/

    // -M
    if (vob->demuxer == -1) {
        vob->demuxer = 1;
    }
    if (verbose >= TC_INFO) {
        tc_log_info(PACKAGE, "V: %-16s | (%i) %s", "AV demux/sync",
                    vob->demuxer, demuxer_desc[vob->demuxer]);
    }

    // -P
    if (vob->pass_flag & TC_VIDEO) {
        vob->im_v_codec = TC_CODEC_RAW;
        vob->ex_v_codec = TC_CODEC_RAW;

        // suggestion:
        if (session->no_v_out_codec)
            session->ex_vid_mod = "raw";
        session->no_v_out_codec = 0;

        if (session->no_a_out_codec)
            session->ex_aud_mod = "raw";
        session->no_a_out_codec = 0;

        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE, "V: %-16s | yes", "pass-through");
    }

    // -x
    if (session->no_vin_codec && vob->video_in_file != NULL && vob->vmod_probed == NULL)
        tc_error("module autoprobe failed, no option -x found");


    //overwrite results of autoprobing if modules are provided
    if (session->no_vin_codec && vob->vmod_probed!=NULL) {
        session->im_vid_mod = (char *)vob->vmod_probed_xml;
        //need to load the correct module if the input file type is xml
    }

    if (session->no_ain_codec && vob->amod_probed!=NULL) {
        session->im_aud_mod = (char *)vob->amod_probed_xml;
        //need to load the correct module if the input file type is xml
    }

    // make zero frame size default for no video
    if (session->im_vid_mod != NULL && strcmp(session->im_vid_mod, "null") == 0) {
        vob->im_v_width = 0;
        vob->im_v_height = 0;
    }

    //initial aspect ratio
    asr = (double) vob->im_v_width/vob->im_v_height;

    // -g

    // import size
    // force to even for YUV mode
    if (vob->im_v_codec == TC_CODEC_YUV420P
     || vob->im_v_codec == TC_CODEC_YUV422P) {
        if (vob->im_v_width % 2 != 0) {
            tc_warn("frame width must be even in YUV/YUV422 mode");
            vob->im_v_width--;
        }
        if (vob->im_v_codec == TC_CODEC_YUV420P && vob->im_v_height % 2 != 0) {
            tc_warn("frame height must be even in YUV mode");
            vob->im_v_height--;
        }
    }
    if (verbose >= TC_INFO) {
        if (vob->im_v_width && vob->im_v_height) {
            tc_log_info(PACKAGE, "V: %-16s | %03dx%03d  %4.2f:1  %s",
                        "import frame", vob->im_v_width, vob->im_v_height,
                        asr, tc_asr_code_describe(vob->im_asr));
        } else {
            tc_log_info(PACKAGE, "V: %-16s | disabled", "import frame");
        }
    }

    // init frame size with cmd line frame size
    vob->ex_v_height = vob->im_v_height;
    vob->ex_v_width  = vob->im_v_width;

    // import bytes per frame (RGB 24bits)
    vob->im_v_size   = vob->im_v_height * vob->im_v_width * BPP/8;
    // export bytes per frame (RGB 24bits)
    vob->ex_v_size   = vob->im_v_size;

    // --PRE_CLIP
    if (pre_im_clip) {
        CLIP_CHECK(pre_im_clip, "pre_clip", "--pre_clip");

        if (verbose >= TC_INFO) {
            tc_log_info(PACKAGE, "V: %-16s | %03dx%03d (%d,%d,%d,%d)",
                       "pre clip frame", vob->ex_v_width, vob->ex_v_height,
                       vob->pre_im_clip_top, vob->pre_im_clip_left,
                       vob->pre_im_clip_bottom, vob->pre_im_clip_right);
        }
    }

    // -j
    if (im_clip) {
        CLIP_CHECK(im_clip, "clip", "-j");

        if (verbose >= TC_INFO) {
            tc_log_info(PACKAGE, "V: %-16s | %03dx%03d", "clip frame (<-)",
                        vob->ex_v_width, vob->ex_v_height);
        }
    }

    // -I
    /* can this really happen? */
    if (vob->deinterlace < 0 || vob->deinterlace > 5) {
        tc_error("invalid parameter for option -I");
    }

    if ((verbose >= TC_INFO) && vob->deinterlace) {
        tc_log_info(PACKAGE,
                    "V: %-16s | (mode=%i) %s",
                    "de-interlace", vob->deinterlace,
                    deinterlace_desc[vob->deinterlace]);
    }

    if (vob->deinterlace == 4)
        vob->ex_v_height /= 2;

    // Calculate the missing w or h based on the ASR
    if (vob->zoom_flag && (vob->zoom_width == 0 || vob->zoom_height == 0)) {
        enum missing_t { NONE, CALC_W, CALC_H, ALL } missing = ALL;
        ratio_t asr = asrs[0];
        float oldr;

        // check if we have at least on width or height
        if (vob->zoom_width == 0 && vob->zoom_height == 0)
            missing = ALL;
        else if (vob->zoom_width == 0 && vob->zoom_height > 0)
            missing = CALC_W;
        else if (vob->zoom_width > 0 && vob->zoom_height == 0)
            missing = CALC_H;
        else if (vob->zoom_width > 0 && vob->zoom_height > 0)
            missing = NONE;

        // try import
        if (vob->im_asr > 0 && vob->im_asr < 5)
            asr = asrs[vob->im_asr];
        // try the export aspectratio
        else if (vob->ex_asr > 0 && vob->ex_asr < 5)
            asr = asrs[vob->ex_asr];

        switch (missing) {
          case ALL:
            tc_error("Neither zoom width nor height set, can't guess anything");
          case CALC_W:
            vob->zoom_width = vob->zoom_height * asr.t; vob->zoom_width /= asr.b;
            break;
          case CALC_H:
            vob->zoom_height = vob->zoom_width * asr.b; vob->zoom_height /= asr.t;
            break;
          case NONE:
          default:
            /* can't happen */
            break;
        }

        // for error printout
        oldr = (float)vob->zoom_width/(float)vob->zoom_height;

        // align
        if (vob->zoom_height % 8 != 0)
            vob->zoom_height += 8-(vob->zoom_height%8);
        if (vob->zoom_width % 8 != 0)
            vob->zoom_width += 8-(vob->zoom_width%8);
        oldr = ((float)vob->zoom_width/(float)vob->zoom_height-oldr)*100.0;
        oldr = oldr<0?-oldr:oldr;

        tc_log_info(PACKAGE, "V: %-16s | %03dx%03d  %4.2f:1 error %.2f%%",
                    "auto resize", vob->zoom_width, vob->zoom_height,
                    (float)vob->zoom_width/(float)vob->zoom_height, oldr);
    }

    // -Z ...,fast
    if (vob->fast_resize) {
        int ret = tc_compute_fast_resize_values(vob, TC_FALSE);
        if (ret == 0) {
            if (vob->hori_resize1 == 0 && vob->vert_resize1 == 0)
                resize1 = TC_FALSE;
            else
                resize1 = TC_TRUE;
            if (vob->hori_resize2 == 0 && vob->vert_resize2 == 0)
                resize2 = TC_FALSE;
            else
                resize2 = TC_TRUE;

            if (verbose >= TC_INFO) {
                tc_log_info(PACKAGE, "V: %-16s | Using -B %d,%d,8 -X %d,%d,8",
                            "fast resize",
                            vob->vert_resize1, vob->hori_resize1,
                            vob->vert_resize2, vob->hori_resize2);
            }
            vob->zoom_flag = TC_FALSE;
        } else {
            if(verbose >= TC_INFO) {
                tc_log_info(PACKAGE,
                            "V: %-16s | requested but can't be used (W or H mod 8 != 0)",
                            "fast resize");
            }
        }
    }

    // -X
    if (resize2) {
        if (vob->resize2_mult % 8 != 0)
            tc_error("resize multiplier for option -X is not a multiple of 8");

        // works only for frame dimension beeing an integral multiple of vob->resize2_mult:
        if (vob->vert_resize2
         && (vob->vert_resize2 * vob->resize2_mult + vob->ex_v_height) % vob->resize2_mult != 0)
            tc_error("invalid frame height for option -X, check also option -j");

        if (vob->hori_resize2
         && (vob->hori_resize2 * vob->resize2_mult + vob->ex_v_width) % vob->resize2_mult != 0)
            tc_error("invalid frame width for option -X, check also option -j");

        vob->ex_v_height += (vob->vert_resize2 * vob->resize2_mult);
        vob->ex_v_width += (vob->hori_resize2 * vob->resize2_mult);

        //check2:

        if (vob->ex_v_height > TC_MAX_V_FRAME_HEIGHT
         || vob->ex_v_width >TC_MAX_V_FRAME_WIDTH)
            tc_error("invalid resize parameter for option -X");

        if (vob->vert_resize2 <0 || vob->hori_resize2 < 0)
            tc_error("invalid resize parameter for option -X");

        // new aspect ratio:
        asr *= (double) vob->ex_v_width * (vob->ex_v_height - vob->vert_resize2*vob->resize2_mult)/
                ((vob->ex_v_width - vob->hori_resize2*vob->resize2_mult) * vob->ex_v_height);

        vob->vert_resize2 *= (vob->resize2_mult/8);
        vob->hori_resize2 *= (vob->resize2_mult/8);

        if (verbose >= TC_INFO && vob->ex_v_height > 0)
            tc_log_info(PACKAGE,
                        "V: %-16s | %03dx%03d  %4.2f:1 (-X)",
                        "new aspect ratio",
                        vob->ex_v_width, vob->ex_v_height, asr);
    }

    // -B
    if (resize1) {
        if (vob->resize1_mult % 8 != 0)
            tc_error("resize multiplier for option -B is not a multiple of 8");

        // works only for frame dimension beeing an integral multiple of vob->resize1_mult:
        if (vob->vert_resize1
         && (vob->ex_v_height - vob->vert_resize1*vob->resize1_mult) % vob->resize1_mult != 0)
            tc_error("invalid frame height for option -B, check also option -j");

        if (vob->hori_resize1
         && (vob->ex_v_width - vob->hori_resize1*vob->resize1_mult) % vob->resize1_mult != 0)
            tc_error("invalid frame width for option -B, check also option -j");

        vob->ex_v_height -= (vob->vert_resize1 * vob->resize1_mult);
        vob->ex_v_width -= (vob->hori_resize1 * vob->resize1_mult);

        //check:
        if (vob->vert_resize1 < 0 || vob->hori_resize1 < 0)
            tc_error("invalid resize parameter for option -B");

        //new aspect ratio:
        asr *= (double) vob->ex_v_width * (vob->ex_v_height + vob->vert_resize1*vob->resize1_mult)/
                       ((vob->ex_v_width + vob->hori_resize1*vob->resize1_mult) * vob->ex_v_height);

        vob->vert_resize1 *= (vob->resize1_mult/8);
        vob->hori_resize1 *= (vob->resize1_mult/8);

        if (verbose >= TC_INFO && vob->ex_v_height > 0)
            tc_log_info(PACKAGE,
                        "V: %-16s | %03dx%03d  %4.2f:1 (-B)",
                        "new aspect ratio",
                        vob->ex_v_width, vob->ex_v_height, asr);
    }

    // -Z
    if (vob->zoom_flag) {
        // new aspect ratio:
        asr *= (double) vob->zoom_width*vob->ex_v_height/(vob->ex_v_width * vob->zoom_height);

        vob->ex_v_width  = vob->zoom_width;
        vob->ex_v_height = vob->zoom_height;

        if (verbose >= TC_INFO && vob->ex_v_height > 0)
            tc_log_info(PACKAGE,
                        "V: %-16s | %03dx%03d  %4.2f:1 (%s)",
                        "zoom",
                        vob->ex_v_width, vob->ex_v_height, asr,
                        tcv_zoom_filter_to_string(vob->zoom_filter));
    }

    // -Y
    if (ex_clip) {
        CLIP_CHECK(ex_clip, "clip", "-Y");

        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE,
                        "V: %-16s | %03dx%03d", "clip frame (->)",
                        vob->ex_v_width, vob->ex_v_height);
    }

    // -r
    if (rescale) {
        vob->ex_v_height /= vob->reduce_h;
        vob->ex_v_width /= vob->reduce_w;

        //new aspect ratio:
        asr *= (double)vob->ex_v_width/vob->ex_v_height*(vob->reduce_h*vob->ex_v_height)/
                (vob->reduce_w*vob->ex_v_width);
        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE,
                        "V: %-16s | %03dx%03d  %4.2f:1 (-r)",
                        "rescale frame",
                        vob->ex_v_width, vob->ex_v_height,asr);

        // sanity check for YUV
        if (vob->im_v_codec == TC_CODEC_YUV420P
         || vob->im_v_codec == TC_CODEC_YUV422P) {
            if (vob->ex_v_width%2 != 0 || (vob->im_v_codec == TC_CODEC_YUV420P && vob->ex_v_height % 2 != 0)) {
                tc_error("rescaled width/height must be even for YUV 4:2:0 mode, try -V rgb24");
            }
        }
    }

    // --keep_asr
    if (vob->keepasr) {
        handle_keep_asr(vob);
    }

    // -z
    if (vob->flip && verbose >= TC_INFO)
        tc_log_info(PACKAGE, "V: %-16s | yes", "flip frame");

    // -l
    if (vob->mirror && verbose >= TC_INFO)
        tc_log_info(PACKAGE, "V: %-16s | yes", "mirror frame");

    // -k
    if (vob->rgbswap && verbose >= TC_INFO)
        tc_log_info(PACKAGE, "V: %-16s | yes", "rgb2bgr");

    // -K
    if (vob->decolor && verbose >= TC_INFO)
        tc_log_info(PACKAGE, "V: %-16s | yes", "b/w reduction");

    // -G
    if (vob->dgamma && verbose >= TC_INFO)
        tc_log_info(PACKAGE, "V: %-16s | %.3f", "gamma correction", vob->gamma);

    // number of bits/pixel
    //
    // Christoph Lampert writes in transcode-users/2002-July/003670.html
    //          B*1000            B*1000*asr
    //  bpp =  --------;   W^2 = ------------
    //          W*H*F             bpp * F
    // If this number is less than 0.15, you will
    // most likely see visual artefacts (e.g. in high motion scenes). If you
    // reach 0.2 or more, the visual quality normally is rather good.
    // For my tests, this corresponded roughly to a fixed quantizer of 4,
    // which is not brilliant, but okay.

    if (vob->divxbitrate > 0 && vob->divxmultipass != 3
      && verbose >= TC_INFO) {
        double div = vob->ex_v_width * vob->ex_v_height * vob->fps;
        double bpp = vob->divxbitrate * 1000;
        const char *judge = "";

        if (div < 1.0)
            bpp = 0.0;
        else
            bpp /= div;

        if (bpp <= 0.0)
            judge = " (unknown)";
        else if (bpp > 0.0  && bpp <= 0.15)
            judge = " (low)";

        tc_log_info(PACKAGE, "V: %-16s | %.3f%s", "bits/pixel", bpp, judge);
    }

    // -C
    if (vob->antialias < 0 || vob->antialias > 3) {
        tc_error("invalid parameter for option -C");
    } else {
        if ((verbose >= TC_INFO) && vob->antialias) {
            tc_log_info(PACKAGE,
                        "V: %-16s | (mode=%d|%.2f|%.2f) %s",
                        "anti-alias",
                        vob->antialias, vob->aa_weight, vob->aa_bias,
                        antialias_desc[vob->antialias]);
        }
    }

    // --post_clip
    if (post_ex_clip) {
        CLIP_CHECK(post_ex_clip, "post_clip", "--post_clip");

        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE,
                        "V: %-16s | %03dx%03d",
                        "post clip frame",
                        vob->ex_v_width, vob->ex_v_height);
    }


    // -W
    if (vob->vob_percentage) {
        if (vob->vob_chunk < 0 || vob->vob_chunk < 0)
            tc_error("invalid parameter for option -W");
    } else {
        if (vob->vob_chunk < 0 || vob->vob_chunk > vob->vob_chunk_max + 1)
            tc_error("invalid parameter for option -W");
    }

    // -f

    if (verbose >= TC_INFO)
        tc_log_info(PACKAGE, "V: %-16s | %.3f,%d",
                    "decoding fps,frc",
                    vob->fps, vob->im_frc);

    // -R
    if (vob->divxmultipass && verbose >= TC_INFO) {
        switch (vob->divxmultipass) {
          case 1:
            tc_log_info(PACKAGE,
                        "V: %-16s | (mode=%d) %s %s",
                        "multi-pass",
                        vob->divxmultipass,
                        "writing data (pass 1) to",
                        vob->divxlogfile);
            break;
          case 2:
            tc_log_info(PACKAGE,
                        "V: %-16s | (mode=%d) %s %s",
                        "multi-pass",
                        vob->divxmultipass,
                        "reading data (pass2) from",
                        vob->divxlogfile);
            break;
          case 3:
            if (vob->divxbitrate > VMAXQUANTIZER)
                vob->divxbitrate = VQUANTIZER;
            tc_log_info(PACKAGE,
                        "V: %-16s | (mode=%d) %s (quant=%d)",
                        "single-pass",
                        vob->divxmultipass,
                        "constant quantizer/quality",
                        vob->divxbitrate);
            break;
        }
    }

    // export frame size final check
    if (vob->ex_v_height < 0 || vob->ex_v_width < 0) {
        tc_warn("invalid export frame combination %dx%d", vob->ex_v_width, vob->ex_v_height);
        tc_error("invalid frame processing requested");
    }

    // -V
    if (vob->im_v_codec == TC_CODEC_YUV420P) {
        vob->ex_v_size = (3*vob->ex_v_height * vob->ex_v_width)>>1;
        vob->im_v_size = (3*vob->im_v_height * vob->im_v_width)>>1;
        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE,
                        "V: %-16s | YUV420 (4:2:0) aka I420",
                        "video format");
    } else if (vob->im_v_codec == TC_CODEC_YUV422P) {
        vob->ex_v_size = (2*vob->ex_v_height * vob->ex_v_width);
        vob->im_v_size = (2*vob->im_v_height * vob->im_v_width);
        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE,
                        "V: %-16s | YUV422 (4:2:2) planar",
                        "video format");
     } else if (vob->im_v_codec == TC_CODEC_UYVY) {
        vob->ex_v_size = (2*vob->ex_v_height * vob->ex_v_width);
        vob->im_v_size = (2*vob->im_v_height * vob->im_v_width);
        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE,
                        "V: %-16s | UYVY (4:2:2) packed",
                        "video format");
    } else {
        vob->ex_v_size = vob->ex_v_height * vob->ex_v_width * BPP/8;
        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE,
                        "V: %-16s | RGB24",
                        "video format");
    }

    // -n
    if (session->no_ain_codec == 1 && vob->has_audio == 0
     && vob->a_codec_flag == TC_CODEC_AC3) {
        if (vob->amod_probed == NULL || strcmp(vob->amod_probed,"null") == 0) {
            if (verbose >= TC_DEBUG)
                tc_log_warn(PACKAGE,
                            "problems detecting audio format - using 'null' module");
            vob->a_codec_flag = 0;
        }
    }

    if (preset_flag & TC_PROBE_NO_TRACK) {
        //tracks specified by user
    } else {
        if (!vob->has_audio_track && vob->has_audio) {
            tc_warn("requested audio track %d not found - using 'null' module", vob->a_track);
            vob->a_codec_flag = 0;
        }
    }

    //audio import disabled
    if (vob->a_codec_flag == 0) {
        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE, "A: %-16s | disabled", "import");
        session->im_aud_mod = "null";
    } else {
        //audio format, if probed sucessfully
        if (verbose >= TC_INFO) {
            if (vob->a_stream_bitrate)
                tc_log_info(PACKAGE,
                            "A: %-16s | 0x%-5lx %-12s [%4d,%2d,%1d] %4d kbps",
                            "import format",
                            vob->a_codec_flag,
                            tc_codec_to_comment(vob->a_codec_flag),
                            vob->a_rate, vob->a_bits, vob->a_chan,
                            vob->a_stream_bitrate);
            else
                tc_log_info(PACKAGE,
                            "A: %-16s | 0x%-5lx %-12s [%4d,%2d,%1d]",
                            "import format",
                            vob->a_codec_flag,
                            tc_codec_to_comment(vob->a_codec_flag),
                            vob->a_rate, vob->a_bits, vob->a_chan);
        }
    }

    if (vob->im_a_codec == TC_CODEC_PCM && vob->a_chan > 2 && !(vob->pass_flag & TC_AUDIO)) {
        // Input is more than 2 channels (i.e. 5.1 AC3) but PCM internal
        // representation can't handle that, adjust the channel count to reflect
        // what modules will actually have presented to them.
        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE,
                        "A: %-16s | %d channels -> %d channels",
                        "downmix", vob->a_chan, 2);
        vob->a_chan = 2;
    }

    // calc export bitrate
    switch (vob->ex_a_codec) {
      case 0x1: // PCM
        vob->mp3bitrate = ((vob->mp3frequency > 0) ?vob->mp3frequency :vob->a_rate) *
                           ((vob->dm_bits > 0) ?vob->dm_bits :vob->a_bits) *
                            ((vob->dm_chan > 0) ?vob->dm_chan :vob->a_chan) / 1000;
        break;
      case 0x2000: // PCM
        if (vob->im_a_codec == TC_CODEC_AC3) {
            vob->mp3bitrate = vob->a_stream_bitrate;
        }
        break;
    }

    if (verbose >= TC_INFO) {
        if (vob->pass_flag & TC_AUDIO)
            tc_log_info(PACKAGE,
                        "A: %-16s | 0x%-5x %-12s [%4d,%2d,%1d] %4d kbps",
                        "export format",
                        vob->im_a_codec,
                        tc_codec_to_comment(vob->im_a_codec),
                        vob->a_rate, vob->a_bits, vob->a_chan,
                        vob->a_stream_bitrate);
        else
            tc_log_info(PACKAGE,
                        "A: %-16s | 0x%-5x %-12s [%4d,%2d,%1d] %4d kbps",
                        "export format",
                        vob->ex_a_codec,
                        tc_codec_to_comment(vob->ex_a_codec),
                         ((vob->mp3frequency > 0) ?vob->mp3frequency :vob->a_rate),
                         ((vob->dm_bits > 0) ?vob->dm_bits :vob->a_bits),
                         ((vob->dm_chan > 0) ?vob->dm_chan :vob->a_chan),
                        vob->mp3bitrate);
        tc_log_info(PACKAGE, "V: %-16s | %s%s", "export format",
                    tc_codec_to_string(vob->ex_v_codec),
                    (vob->ex_v_codec == 0) ?" (module dependant)" :"");
    }

    // Do not run out of audio-data
    // import_ac3 now correctly probes the channels of the ac3 stream
    // (previous versions always returned "2"). This breakes transcode
    // when doing -A --tibit
    if (vob->im_a_codec == TC_CODEC_AC3)
        vob->a_chan = vob->a_chan > 2 ?2 :vob->a_chan;

    // -f and --export_fps/export_frc
    //
    // set import/export frc/fps
    if (vob->im_frc == 0)
        tc_frc_code_from_value(&vob->im_frc, vob->fps);

    // ex_fps given, but not ex_frc
    if (vob->ex_frc == 0 && (vob->ex_fps != 0.0))
        tc_frc_code_from_value(&vob->ex_frc, vob->ex_fps);

    if (vob->ex_frc == 0 && vob->im_frc != 0)
        vob->ex_frc = vob->im_frc;

    // ex_frc always overwrites ex_fps
    if (vob->ex_frc > 0)
        tc_frc_code_to_value(vob->ex_frc, &vob->ex_fps);

    if (vob->im_frc <= 0 && vob->ex_frc <= 0 && vob->ex_fps == 0)
        vob->ex_fps = vob->fps;

    if (vob->im_frc == -1)
        vob->im_frc = 0;
    if (vob->ex_frc == -1)
        vob->ex_frc = 0;

    // --export_fps
    if(verbose >= TC_INFO)
        tc_log_info(PACKAGE,
                    "V: %-16s | %.3f,%d",
                    "encoding fps,frc",
                    vob->ex_fps, vob->ex_frc);


    // --a52_demux
    if ((vob->a52_mode & TC_A52_DEMUX) && (verbose >= TC_INFO))
        tc_log_info(PACKAGE,
                    "A: %-16s | %s", "A52 demuxing",
                    "(yes) 3 front, 2 rear, 1 LFE (5.1)");

    //audio language, if probed sucessfully
    if(vob->lang_code > 0 && (verbose >= TC_INFO))
        tc_log_info(PACKAGE,
                    "A: %-16s | %c%c",
                    "language",
                    vob->lang_code >> 8, vob->lang_code & 0xff);

    // recalculate audio bytes per frame since video frames per second
    // may have changed

    // samples per audio frame
    fch = vob->a_rate/vob->ex_fps;

    // bytes per audio frame
    vob->im_a_size = (int)(fch * (vob->a_bits/8) * vob->a_chan);
    vob->im_a_size =  (vob->im_a_size >> 2) << 2;

    // rest:
    fch *= (vob->a_bits/8) * vob->a_chan;

    leap_bytes1 = TC_LEAP_FRAME * (fch - vob->im_a_size);
    leap_bytes2 = - leap_bytes1 + TC_LEAP_FRAME * (vob->a_bits/8) * vob->a_chan;
    leap_bytes1 = (leap_bytes1 >> 2) << 2;
    leap_bytes2 = (leap_bytes2 >> 2) << 2;

    if(leap_bytes1<leap_bytes2) {
        vob->a_leap_bytes = leap_bytes1;
    } else {
        vob->a_leap_bytes = -leap_bytes2;
        vob->im_a_size += (vob->a_bits/8) * vob->a_chan;
    }

    // final size in bytes
    vob->ex_a_size = vob->im_a_size;

    if (verbose >= TC_INFO)
        tc_log_info(PACKAGE,
                    "A: %-16s | %d (%.6f)",
                    "bytes per frame", vob->im_a_size, fch);

    if(no_audio_adjust) {
        vob->a_leap_bytes=0;

        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE, "A: %-16s | disabled", "adjustment");

    } else
        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE,
                        "A: %-16s | %d@%d", "adjustment",
                        vob->a_leap_bytes, vob->a_leap_frame);

    // -s

    if (vob->volume > 0 && vob->a_chan != 2) {
        //tc_error("option -s not yet implemented for mono streams");
    }

    if (vob->volume > 0 && (verbose >= TC_INFO))
        tc_log_info(PACKAGE,
                    "A: %-16s | %5.3f",
                    "rescale stream", vob->volume);

    // -D
    if (vob->sync_ms >= (int) (1000.0/vob->ex_fps)
      || vob->sync_ms <= - (int) (1000.0/vob->ex_fps)) {
        vob->sync     = (int) (vob->sync_ms/1000.0*vob->ex_fps);
        vob->sync_ms -= vob->sync * (int) (1000.0/vob->ex_fps);
    }

    if ((vob->sync || vob->sync_ms) && (verbose >= TC_INFO))
        tc_log_info(PACKAGE,
                    "A: %-16s | %d ms [ %d (A) | %d ms ]",
                    "AV shift",
                    vob->sync * (int) (1000.0/vob->ex_fps) + vob->sync_ms,
                    vob->sync, vob->sync_ms);

    // -d
    if (vob->pcmswap)
        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE, "A: %-16s | yes", "swap bytes");

    // -E

    //set export parameter to input parameter, if no re-sampling is requested
    if (vob->dm_chan == 0)
        vob->dm_chan = vob->a_chan;
    if (vob->dm_bits == 0)
        vob->dm_bits = vob->a_bits;

    // -P
    if (vob->pass_flag & TC_AUDIO) {
        vob->im_a_codec = TC_CODEC_RAW;
        vob->ex_a_codec = TC_CODEC_RAW;
        //suggestion:
        if (session->no_a_out_codec)
            session->ex_aud_mod = "raw";
        session->no_a_out_codec = 0;

        if (verbose >= TC_INFO)
            tc_log_info(PACKAGE, "A: %-16s | yes", "pass-through");
    }

    if (verbose >= TC_INFO)
        tc_log_info(PACKAGE, "H: worker threads   | %i (%i hardware)",
                    session->max_frame_threads, session->hw_threads);

    // --accel
    session->acceleration &= ac_cpuinfo();
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    if (verbose >= TC_INFO)
        tc_log_info(PACKAGE, "H: IA32/AMD64 accel | %s ",
                    ac_flagstotext(session->acceleration));
#endif

    ac_init(session->acceleration);
    tc_ext_init();

    // more checks with warnings
    if (verbose >= TC_INFO) {
        // -o
        if (vob->video_out_file == NULL && session->core_mode == TC_MODE_DEFAULT) {
            vob->video_out_file = TC_DEFAULT_OUT_FILE;
            tc_warn("no option -o found, encoded frames send to \"%s\"",
                    vob->video_out_file);
        }

        if (vob->im_v_codec == TC_CODEC_YUV420P
         && (vob->im_clip_left % 2 != 0 || vob->im_clip_right % 2
         || vob->im_clip_top % 2 != 0 || vob->im_clip_bottom % 2 != 0))
            tc_warn ("Odd import clipping paramter(s) detected, may cause distortion");

        if (vob->im_v_codec == TC_CODEC_YUV420P
         && (vob->ex_clip_left % 2 != 0 || vob->ex_clip_right % 2
         || vob->ex_clip_top % 2 != 0 || vob->ex_clip_bottom % 2 != 0))
            tc_warn ("Odd export clipping paramter(s) detected, may cause distortion");
    }

    // -u
    if (session->buffer_delay_dec == -1) //adjust core parameter
        session->buffer_delay_dec = (vob->pass_flag & TC_VIDEO || session->ex_vid_mod==NULL || strcmp(session->ex_vid_mod, "null") == 0)
                                ?TC_DELAY_MIN :TC_DELAY_MAX;

    if (session->buffer_delay_enc == -1) //adjust core parameter
        session->buffer_delay_enc = (vob->pass_flag & TC_VIDEO || session->ex_vid_mod==NULL || strcmp(session->ex_vid_mod, "null") == 0)
                                ?TC_DELAY_MIN :TC_DELAY_MAX;

    if (verbose >= TC_DEBUG)
        tc_log_msg(PACKAGE, "encoder delay = decode=%d encode=%d usec",
                   session->buffer_delay_dec, session->buffer_delay_enc);

    /* -------------------------------------------------------------
     *
     * OK, so far, now start the support threads, setup buffers, ...
     *
     * ------------------------------------------------------------- */

    //this will speed up in pass-through mode
    if(vob->pass_flag && !(preset_flag & TC_PROBE_NO_BUFFER))
        session->max_frame_buffers = 50;

    if (vob->fps >= vob->ex_fps) {
        /* worst case -> lesser fps (more audio samples for second) */
        specs.frc = vob->im_frc;
    } else {
        specs.frc = vob->ex_frc;
    }
    specs.width  = TC_MAX(vob->im_v_width, vob->ex_v_width);
    specs.height = TC_MAX(vob->im_v_height, vob->ex_v_height);
    specs.format = vob->im_v_codec;

    /* XXX: explain me up */
    specs.rate     = TC_MAX(vob->a_rate, vob->mp3frequency);
    specs.channels = TC_MAX(vob->a_chan, vob->dm_chan);
    specs.bits     = TC_MAX(vob->a_bits, vob->dm_bits);

    tc_framebuffer_set_specs(&specs);

    if (verbose >= TC_INFO) {
        tc_log_info(PACKAGE, "V: video buffer     | %i @ %ix%i [0x%x]",
                    session->max_frame_buffers, specs.width, specs.height, specs.format);
        tc_log_info(PACKAGE, "A: audio buffer     | %i @ %ix%ix%i",
                    session->max_frame_buffers, specs.rate, specs.channels, specs.bits);
    }

#ifdef STATBUFFER
    // allocate buffer
    if (verbose >= TC_DEBUG)
        tc_log_msg(PACKAGE, "allocating %d framebuffers (static)",
                   session->max_frame_buffers);

    if (vframe_alloc(session->max_frame_buffers) < 0)
        tc_error("static framebuffer allocation failed");
    if (aframe_alloc(session->max_frame_buffers) < 0)
        tc_error("static framebuffer allocation failed");

#else
    if(verbose >= TC_DEBUG)
        tc_log_msg(PACKAGE, "%d framebuffers (dynamic) requested",
                   session->max_frame_buffers);
#endif

    // load import/export modules and filters plugins
    if (transcode_init(session, tc_framebuffer_get_specs()) != TC_OK)
        tc_error("plug-in initialization failed");

    // start socket stuff
    if (socket_file)
        if (!tc_socket_init(socket_file))
            tc_error("failed to initialize socket handler");
    
    // now we start the signal handler thread
    if (pthread_create(&event_thread_id, NULL, event_thread, &sigs_to_block) != 0)
        tc_error("failed to start signal handler thread");


    // start frame processing threads
    tc_frame_threads_init(vob,
                          session->max_frame_threads,
                          session->max_frame_threads);


    /* ------------------------------------------------------------
     * transcoder core modes
     * ------------------------------------------------------------*/

    switch (session->core_mode) {
      case TC_MODE_DEFAULT:
        transcode_mode_default(session);
        break;

      case TC_MODE_PSU:
        transcode_mode_psu(session, psubase);
        break;

      case TC_MODE_DIRECTORY:
        transcode_mode_directory(session);
        break;

      case TC_MODE_DVD_CHAPTER:
        transcode_mode_dvd(session);
        break;

      case TC_MODE_DEBUG:
        /* FIXME: get rid of this? */
        tc_log_msg(PACKAGE, "debug \"core\" mode");
        break;

      default:
        //should not get here:
        tc_error("internal error");
    }

    /* ------------------------------------------------------------
     * shutdown transcode, all cores modes end up here, core modes
     * must take care of proper import/export API shutdown.
     *
     * 1) stop and cancel frame processing threads
     * 2) unload all external modules
     * 3) cancel internal signal/server thread
     * ------------------------------------------------------------*/

    // turn counter off
    SHUTDOWN_MARK("counter");
    counter_off();

    // stop and cancel frame processing threads
    SHUTDOWN_MARK("frame threads");
    tc_frame_threads_close();

    // unload all external modules
    SHUTDOWN_MARK("unload modules");
    transcode_fini(session);

    // cancel no longer used internal signal handler threads
    if (event_thread_id) {
        SHUTDOWN_MARK("cancel signal");
        stop_event_thread();
        event_thread_id = (pthread_t)0;
    }

    // shut down control socket, if active
    SHUTDOWN_MARK("control socket");
    tc_socket_fini();

    // all done
    SHUTDOWN_MARK("completed");

 summary:
    // print a summary
    if ((verbose >= TC_INFO) && vob->clip_count)
        tc_log_info(PACKAGE, "clipped %d audio samples",
                    vob->clip_count/2);

    if (verbose >= TC_INFO) {
        long drop = - tc_get_frames_dropped();

        tc_log_info(PACKAGE, "encoded %ld frames (%ld dropped, %ld cloned),"
                            " clip length %6.2f s",
                    (long)tc_get_frames_encoded(), drop,
                    (long)tc_get_frames_cloned(),
                    tc_get_frames_encoded()/vob->ex_fps);
    }

#ifdef STATBUFFER
    // free buffers
    vframe_free();
    aframe_free();
    if(verbose >= TC_DEBUG)
        tc_log_msg(PACKAGE, "buffer released");
#endif

    teardown_input_sources(vob);

    if (vob)
        tc_free(vob);

    //exit at last
    if (tc_interrupted())
        return 127;
    return 0;
}

// this Code below here _never_ gets called.
// it is just there to trick the linker to not remove
// unneeded object files from a .a file.

#include "libtcutil/static_tcutil.h"
#include "libtcutil/static_tctimer.h"

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
