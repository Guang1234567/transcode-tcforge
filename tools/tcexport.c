/*
 * tcexport.c -- standalone encoder frontend for transcode
 * (C) 2006-2010 - Francesco Romani <fromani at gmail dot com>
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



#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "src/probe.h"
#include "src/transcode.h"
#include "src/framebuffer.h"
#include "src/filter.h"

#include "libtcmodule/tcmodule-core.h"
#include "libtcmodule/tcmodule-registry.h"
#include "libtcutil/cfgfile.h"
#include "libtcext/tc_ext.h"
#include "libtc/libtc.h"
#include "libtc/tccodecs.h"
#include "libtc/tcframes.h"
#include "libtc/mediainfo.h"

#include "libtcexport/export.h"
#include "libtcexport/export_profile.h"

#include "rawsource.h"
#include "tcstub.h"

#define EXE "tcexport"

enum {
    STATUS_DONE = -1, /* used internally */
    STATUS_OK = 0,
    STATUS_BAD_PARAM,
    STATUS_IO_ERROR,
    STATUS_NO_MODULE,
    STATUS_MODULE_ERROR,
    STATUS_PROBE_FAILED,
    /* ... */
    STATUS_INTERNAL_ERROR = 64, /* must be the last one */
};

#define VIDEO_LOG_FILE       "mpeg4.log"
#define AUDIO_LOG_FILE       "pcm.log"

#define VIDEO_CODEC          "yuv420p"
#define AUDIO_CODEC          "pcm"

#define RANGE_STR_SEP        ","

enum {
    LOG_FILE_NAME_LEN = 32,
    MOD_BUF_NAME_LEN  = 64
};

typedef  struct tcencconf_ TCEncConf;
struct tcencconf_ {
    int         dry_run; /* flag */
    TCJob       *job;

    char        *video_codec;
    char        *audio_codec;

    char        vlogfile[LOG_FILE_NAME_LEN];
    char        alogfile[LOG_FILE_NAME_LEN];

    const char  *video_mod;
    const char  *audio_mod;
    const char  *mplex_mod;
    const char  *mplex_mod_aux;
    char        **mod_args; /* to avoid memleaks */

    char        *range_str;
};


void version(void)
{
    printf("%s v%s (C) 2006-2010 Transcode Team\n",
           EXE, VERSION);
}

static void usage(void)
{
    version();
    printf("Usage: %s [options]\n", EXE);
    printf("    -d verbosity      Verbosity mode [1 == TC_INFO]\n");
    printf("    -D                dry run, only loads module (used"
           " for testing)\n");
    printf("    -m path           Use PATH as module path\n");
    printf("    -c f1-f2[,f3-f4]  encode only f1-f2[,f3-f4]"
           " (frames or HH:MM:SS) [all]\n");
    printf("    -b b[,v[,q[,m]]]  audio encoder bitrate kBits/s"
           "[,vbr[,quality[,mode]]] [%i,%i,%i,%i]\n",
           ABITRATE, AVBR, AQUALITY, AMODE);
    printf("    -i file           video input file name\n");
    printf("    -p file           audio input file name\n");
    printf("    -o file           output file (base)name\n");
    printf("    -P profile        select export profile."
           " if you want to use more than one profile,\n"
           "                      provide a comma separated list.\n");
    printf("    -N V=v,A=a        Video,Audio output format (any order)"
           " (encoder) [%s,%s]\n", VIDEO_CODEC, AUDIO_CODEC);
    printf("    -y V=v,A=a,M=m    Video,Audio,Multiplexor export"
           " modules (any order) [%s,%s,%s]\n", TC_DEFAULT_EXPORT_VIDEO,
           TC_DEFAULT_EXPORT_AUDIO, TC_DEFAULT_EXPORT_MPLEX);
    printf("    -w b[,k[,c]]      encoder"
           " bitrate[,keyframes[,crispness]] [%d,%d,%d]\n",
            VBITRATE, VKEYFRAMES, VCRISPNESS);
    printf("    -R n[,f1[,f2]]    enable multi-pass encoding"
           " (0-3) [%d,mpeg4.log,pcm.log]\n", VMULTIPASS);
}

static void config_init(TCEncConf *conf, TCJob *job)
{
    conf->dry_run   = TC_FALSE;
    conf->job       = job;

    conf->range_str = NULL;

    strlcpy(conf->vlogfile, VIDEO_LOG_FILE, sizeof(conf->vlogfile));
    strlcpy(conf->alogfile, AUDIO_LOG_FILE, sizeof(conf->alogfile));

    conf->video_mod     = NULL;
    conf->audio_mod     = NULL;
    conf->mplex_mod     = NULL;
    conf->mplex_mod_aux = NULL;

    conf->mod_args      = NULL;
}

/* split up module string (=options) to module name */
static char *setup_mod_string(const char *mod)
{
    size_t modlen = strlen(mod);
    char *sep = strchr(mod, '=');
    char *opts = NULL;

    if (modlen > 0 && sep != NULL) {
        size_t optslen;

        opts = sep + 1;
        optslen = strlen(opts);

        if (!optslen) {
            opts = NULL; /* no options or bad options given */
        }
        *sep = '\0'; /* mark end of module name */
    }
    return opts;
}

static void setup_codecs(TCJob *job, char **args)
{
    int i = 0;

    for (i = 0; args[i]; i++) {
        if (!strncmp(args[i], "A=", 2)) {
            job->ex_a_codec = tc_codec_from_string(args[i] + 2);
            job->export_attributes |= TC_EXPORT_ATTRIBUTE_ACODEC;
        }
        if (!strncmp(args[i], "V=", 2)) {
            job->ex_v_codec = tc_codec_from_string(args[i] + 2);
            job->export_attributes |= TC_EXPORT_ATTRIBUTE_VCODEC;
        }
    }
}


static void setup_user_mods(TCEncConf *conf, TCJob *job, char **args)
{
    int i = 0;

    for (i = 0; args[i]; i++) {
        if (!strncmp(args[i], "A=", 2)) {
            conf->audio_mod = args[i] + 2;
            job->ex_a_string = setup_mod_string(conf->audio_mod);
        }
        if (!strncmp(args[i], "V=", 2)) {
            conf->video_mod = args[i] + 2;
            job->ex_v_string = setup_mod_string(conf->video_mod);
        }
        if (!strncmp(args[i], "M=", 2)) {
            conf->mplex_mod = args[i] + 2;
            job->ex_m_string = setup_mod_string(conf->mplex_mod);
        }
    }
    return;
}

/* basic sanity check */
#define VALIDATE_OPTION \
        if (optarg[0] == '-') { \
            usage(); \
            return STATUS_BAD_PARAM; \
        }

static int parse_options(int argc, char** argv, TCEncConf *conf)
{
    int ch, n;
    size_t num = 0;
    char **pieces = NULL;
    TCJob *job = conf->job;

    if (argc == 1) {
        usage();
        return STATUS_BAD_PARAM;
    }

    tc_ext_init();
    libtc_init(&argc, &argv);

    job->mod_path = tc_module_default_path();
    job->reg_path = tc_module_registry_default_path();

    while (1) {
        ch = getopt(argc, argv, "b:c:Dd:hi:m:N:o:p:R:y:w:v?");
        if (ch == -1) {
            break;
        }

        switch (ch) {
          case 'D':
            conf->dry_run = TC_TRUE;
            break;
          case 'd':
            VALIDATE_OPTION;
            job->verbose = atoi(optarg);
            break;
          case 'c':
            VALIDATE_OPTION;
            conf->range_str = optarg;
            break;
          case 'b':
            VALIDATE_OPTION;
            n = sscanf(optarg, "%i,%i,%f,%i",
                       &job->mp3bitrate, &job->a_vbr, &job->mp3quality,
                       &job->mp3mode);
            if (n < 0
              || job->mp3bitrate < 0
              || job->a_vbr < 0
              || job->mp3quality < -1.00001
              || job->mp3mode < 0) {
                tc_log_error(EXE, "invalid parameter for -b");
                return STATUS_BAD_PARAM;
            }
            break;
          case 'i':
            VALIDATE_OPTION;
            job->video_in_file = optarg;
            break;
          case 'm':
            VALIDATE_OPTION;
            job->mod_path = optarg;
            break;
          case 'N':
            VALIDATE_OPTION;
            pieces = tc_strsplit(optarg, ',', &num);
            if (num != 2) {
                tc_log_error(EXE, "invalid parameter for option -N"
                                  " (you must specify ALL parameters)");
                return STATUS_BAD_PARAM;
            }

            setup_codecs(job, pieces);

            tc_strfreev(pieces);

            if (job->ex_v_codec == TC_CODEC_ERROR
             || job->ex_a_codec == TC_CODEC_ERROR) {
                tc_log_error(EXE, "unknown A/V format");
                return STATUS_BAD_PARAM;
            }
            break;
          case 'p':
            VALIDATE_OPTION;
            job->audio_in_file = optarg;
            break;
          case 'R':
            VALIDATE_OPTION;
            n = sscanf(optarg,"%d,%64[^,],%64s",
                       &job->divxmultipass, conf->vlogfile, conf->alogfile);

            if (n == 3) {
                job->audiologfile = conf->alogfile;
                job->divxlogfile = conf->vlogfile;
            } else if (n == 2) {
                job->divxlogfile = conf->vlogfile;
            } else if (n != 1) {
                tc_log_error(EXE, "invalid parameter for option -R");
                return STATUS_BAD_PARAM;
            }

            if (job->divxmultipass < 0 || job->divxmultipass > 3) {
                tc_log_error(EXE, "invalid multi-pass in option -R");
                return STATUS_BAD_PARAM;
            }
            break;
          case 'o':
            VALIDATE_OPTION;
            job->video_out_file = optarg;
            break;
          case 'w':
            VALIDATE_OPTION;
            sscanf(optarg,"%d,%d,%d",
                   &job->divxbitrate, &job->divxkeyframes,
                   &job->divxcrispness);

            if (job->divxcrispness < 0 || job->divxcrispness > 100
              || job->divxbitrate <= 0 || job->divxkeyframes < 0) {
                tc_log_error(EXE, "invalid parameter for option -w");
                return STATUS_BAD_PARAM;
            }
            break;
          case 'y':
            VALIDATE_OPTION;

            if (conf->mod_args) {
                tc_strfreev(conf->mod_args);
            }

            conf->mod_args = tc_strsplit(optarg, ',', &num);
            if (num == 0) {
                tc_log_error(EXE, "invalid parameter for option -y"
                                  " (you must specify at least one parameter)");
                return STATUS_BAD_PARAM;
            }

            setup_user_mods(conf, job, conf->mod_args);

            break;
          case 'v':
            version();
            return STATUS_DONE;
          case '?': /* fallthrough */
          case 'h': /* fallthrough */
          default:
            usage();
            return STATUS_BAD_PARAM;
        }
    }
    return STATUS_OK;
}

static void setup_im_size(TCJob *job)
{
    double fch;
    int leap_bytes1, leap_bytes2;

    /* update job structure */
    /* assert(YUV420P source) */
    job->im_v_size = (3 * job->im_v_width * job->im_v_height) / 2;
    /* borrowed from transcode.c */
    /* samples per audio frame */
    // fch = job->a_rate/job->ex_fps;
    /* 
     * XXX I still have to understand why we
     * doing like this in transcode.c, so I'll simplify things here
     */
    fch = job->a_rate/job->fps;
    /* bytes per audio frame */
    job->im_a_size = (int)(fch * (job->a_bits/8) * job->a_chan);
    job->im_a_size =  (job->im_a_size>>2)<<2;

    fch *= (job->a_bits/8) * job->a_chan;

    leap_bytes1 = TC_LEAP_FRAME * (fch - job->im_a_size);
    leap_bytes2 = - leap_bytes1 + TC_LEAP_FRAME * (job->a_bits/8) * job->a_chan;
    leap_bytes1 = (leap_bytes1 >>2)<<2;
    leap_bytes2 = (leap_bytes2 >>2)<<2;

    if (leap_bytes1 < leap_bytes2) {
    	job->a_leap_bytes = leap_bytes1;
    } else {
	    job->a_leap_bytes = -leap_bytes2;
    	job->im_a_size += (job->a_bits/8) * job->a_chan;
    }
}

static void setup_ex_params(TCJob *job)
{
    /* common */
    job->ex_fps       = job->fps;
    job->ex_frc       = job->im_frc;
    /* video */
    job->ex_v_width   = job->im_v_width;
    job->ex_v_height  = job->im_v_height;
    job->ex_v_size    = job->im_v_size;
    /* audio */
    job->ex_a_size    = job->im_a_size;
    /* a_rate already correctly setup */
    job->mp3frequency = job->a_rate;
    job->dm_bits      = job->a_bits;
    job->dm_chan      = job->a_chan;
}

static int setup_ranges(TCEncConf *conf)
{
    TCJob *job = conf->job;
    int ret = 0;

    if (conf->range_str != NULL) {
        ret = parse_fc_time_string(conf->range_str, job->fps,
                                   RANGE_STR_SEP, job->verbose,
                                   &job->ttime);
    } else {
        job->ttime = new_fc_time();
        if (job->ttime == NULL) {
            ret = -1;
        } else {
            job->ttime->stf = TC_FRAME_FIRST;
            job->ttime->etf = TC_FRAME_LAST;
            job->ttime->vob_offset = 0;
            job->ttime->next = NULL;
        }
    }
    return ret;
}

static int setup_modnames(TCEncConf *conf, TCJob *job, TCRegistry registry)
{
    const char *fmtname = NULL;

    if (!conf->video_mod) {
        fmtname = tc_codec_to_string(job->ex_v_codec);
        conf->video_mod = tc_get_module_name_for_format(registry,
                                                        "encode",
                                                        fmtname);
    }
    if (!conf->video_mod) {
        tc_log_error(EXE, "unable to find the video encoder module"
                          " and none specified");
        return TC_ERROR;
    }

    if (!conf->audio_mod) {
        fmtname = tc_codec_to_string(job->ex_a_codec);
        conf->audio_mod = tc_get_module_name_for_format(registry,
                                                        "encode",
                                                        fmtname);
    }
    if (!conf->audio_mod) {
        tc_log_error(EXE, "unable to find the audio encoder module"
                          " and none specified");
        return TC_ERROR;
    }

    if (!conf->mplex_mod) {
        /* try by outfile extension */
        fmtname = strrchr(job->video_out_file, '.');
        if (fmtname) {
            conf->mplex_mod = tc_get_module_name_for_format(registry,
                                                            "multiplex",
                                                            fmtname);
        }
    }
    if (!conf->mplex_mod) {
        tc_log_error(EXE, "unable to find the multiplexor module"
                          " and none specified");
        return TC_ERROR;
    }

    /* double muxer not (yet?) supported */
    conf->mplex_mod_aux = NULL;
    return TC_OK;
}

#define MOD_OPTS(opts) (((opts) != NULL) ?((opts)) :"none")
static void print_summary(TCEncConf *conf, int verbose)
{
    TCJob *job = conf->job;

    version();
    if (verbose >= TC_INFO) {
        tc_log_info(EXE, "M: %-16s | %s", "destination",
                    job->video_out_file);
        tc_log_info(EXE, "E: %-16s | %i,%i kbps", "bitrate(A,V)",
                    job->divxbitrate, job->mp3bitrate);
        tc_log_info(EXE, "E: %-16s | %s,%s", "logfile (A,V)",
                    job->divxlogfile, job->audiologfile);
        tc_log_info(EXE, "V: %-16s | %s (options=%s)", "encoder",
                    conf->video_mod, MOD_OPTS(job->ex_v_string));
        tc_log_info(EXE, "A: %-16s | %s (options=%s)", "encoder",
                    conf->audio_mod, MOD_OPTS(job->ex_a_string));
        tc_log_info(EXE, "M: %-16s | %s (options=%s)", "format",
                    conf->mplex_mod, MOD_OPTS(job->ex_m_string));
        tc_log_info(EXE, "M: %-16s | %.3f", "fps", job->fps);
        tc_log_info(EXE, "V: %-16s | %ix%i", "picture size",
                    job->im_v_width, job->im_v_height);
        tc_log_info(EXE, "V: %-16s | %i", "bytes per frame",
                    job->im_v_size);
        tc_log_info(EXE, "V: %-16s | %i", "pass", job->divxmultipass);
        tc_log_info(EXE, "A: %-16s | %i,%i,%i", "rate,chans,bits",
                    job->a_rate, job->a_chan, job->a_bits);
        tc_log_info(EXE, "A: %-16s | %i", "bytes per frame",
                    job->im_a_size);
        tc_log_info(EXE, "A: %-16s | %i@%i", "adjustement",
                         job->a_leap_bytes, job->a_leap_frame);
    }
}
#undef MOD_OPTS

/************************************************************************/
/************************************************************************/

#define EXIT_IF(cond, msg, status) \
    if((cond)) { \
        tc_log_error(EXE, msg); \
        return status; \
    }


int main(int argc, char *argv[])
{
    int ret = 0, status = STATUS_OK;
    double samples = 0;
    /* needed by some modules */
    TCFactory factory = NULL;
    TCRegistry registry = NULL;
    const TCExportInfo *info = NULL;
    TCFrameSource *framesource = NULL;
    TCEncConf config;
    TCVHandle tcv_handle = tcv_init();
    TCJob *job = tc_get_vob();

    /* reset some fields */
    job->audiologfile = AUDIO_LOG_FILE;
    job->divxlogfile  = VIDEO_LOG_FILE;

    ac_init(AC_ALL);
    config_init(&config, job);

    filter[0].id = 0; /* to make gcc happy */

    /* we want to modify real argc/argv pair */
    ret = tc_export_profile_setup_from_cmdline(&argc, &argv);
    if (ret < 0) {
        /* error, so bail out */
        return STATUS_BAD_PARAM;
    }

    info = tc_export_profile_load_all();
    tc_export_profile_to_job(info, job);

    ret = parse_options(argc, argv, &config);
    if (ret != STATUS_OK) {
        return (ret == STATUS_DONE) ?STATUS_OK :ret;
    }
    if (job->ex_v_codec == TC_CODEC_ERROR
     || job->ex_a_codec == TC_CODEC_ERROR) {
        tc_log_error(EXE, "bad export codec/format (use -N)");
        return STATUS_BAD_PARAM;
    }
    verbose = job->verbose;
    ret = probe_source(job->video_in_file, job->audio_in_file,
                       1, 0, job);
    if (!ret) {
        return STATUS_PROBE_FAILED;
    }

    samples = TC_AUDIO_SAMPLES_IN_FRAME(job->a_rate, job->ex_fps);
    job->im_a_size = tc_audio_frame_size(samples, job->a_chan, job->a_bits,
                                         &job->a_leap_bytes);
    job->im_v_size = tc_video_frame_size(job->im_v_width, job->im_v_height,
                                         job->im_v_codec);
    setup_im_size(job);
    setup_ex_params(job);
    ret = setup_ranges(&config);
    if (ret != 0) {
        tc_log_error(EXE, "error using -c option."
                          " Recheck your frame ranges!");
        return STATUS_BAD_PARAM;
    }

    factory = tc_new_module_factory(job->mod_path, job->verbose);
    EXIT_IF(!factory, "can't setup module factory", STATUS_MODULE_ERROR);
    registry = tc_new_module_registry(factory, job->reg_path, verbose);
    EXIT_IF(!registry, "can't setup module registry", STATUS_MODULE_ERROR);

    /* open the A/V source */
    framesource = tc_rawsource_open(job);
    EXIT_IF(framesource == NULL, "can't get rawsource handle", STATUS_IO_ERROR);
    ret = tc_rawsource_num_sources();
    EXIT_IF(ret != 2, "can't open both input sources", STATUS_IO_ERROR);

    ret = tc_export_new(job, factory,
                        tc_runcontrol_get_instance(),
                        tc_framebuffer_get_specs());
    EXIT_IF(ret != 0, "can't setup export subsystem", STATUS_MODULE_ERROR);

    tc_export_config(verbose, 1, 0);

    ret = setup_modnames(&config, job, registry);
    EXIT_IF(ret != TC_OK, "can't setup export modules", STATUS_MODULE_ERROR);

    print_summary(&config, verbose);

    ret = tc_export_setup(config.audio_mod, config.video_mod,
                          config.mplex_mod, config.mplex_mod_aux);
    EXIT_IF(ret != 0, "can't setup export modules", STATUS_MODULE_ERROR);

    if (!config.dry_run) {
        struct fc_time *tstart = NULL;

        ret = tc_export_init();
        EXIT_IF(ret != 0, "can't initialize encoder", STATUS_INTERNAL_ERROR);

        ret = tc_export_open();
        EXIT_IF(ret != 0, "can't open encoder files", STATUS_IO_ERROR);

        /* ok, now we can do the real (ranged) encoding */
        for (tstart = job->ttime; tstart != NULL; tstart = tstart->next) {
            tc_export_loop(framesource, tstart->stf, tstart->etf);
            printf("\n"); /* dont' mess (too much) the counter output */
        }

        ret = tc_export_close();
        ret = tc_export_stop();

    }

    tc_export_shutdown();
    tc_export_del();

    ret = tc_rawsource_close();
    ret = tc_del_module_factory(factory);
    ret = tc_del_module_registry(registry);
    tcv_free(tcv_handle);
    free_fc_time(job->ttime);
    tc_export_profile_cleanup();

    if(verbose >= TC_INFO) {
        long encoded =   tc_get_frames_encoded();
        long dropped = - tc_get_frames_dropped();
    	long cloned  =   tc_get_frames_cloned();

        tc_log_info(EXE, "encoded %ld frames (%ld dropped, %ld cloned),"
                         " clip length %6.2f s",
                    encoded, dropped, cloned, encoded/job->fps);
    }
    return status;
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
