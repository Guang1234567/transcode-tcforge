/*
 * cmdline_def.h -- transcode command line option definitions
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

/* This file contains definitions of all command-line options supported by
 * transcode.  The options are defined using macros which evaluate to
 * different things depending on which of the following symbols is defined
 * (exactly one must be defined before including this file):
 *
 * TC_OPTIONS_TO_ENUM
 *     Output a constant name for each long option, for use in defining an
 *     enum for the "val" field in "struct option".
 *
 * TC_OPTIONS_TO_STRUCT_OPTION
 *     Output a "struct option" definition for each option, for use in
 *     initializing a long-option array.
 *
 * TC_OPTIONS_TO_SHORTOPTS
 *     Output code to create a string of the form "x" or "x:" for each
 *     option which has a short option equivalent, for use in creating the
 *     short-option list passed to getopt().  The resulting string pointer
 *     is assigned to the variable named by the TC_OPTIONS_TO_SHORTOPTS
 *     symbol.
 *
 * TC_OPTIONS_TO_CODE
 *     Output code to process each option, for use in a switch statement.
 *
 * TC_OPTIONS_TO_OPTWIDTH
 *     Output code to compute the maximum width of any long option name
 *     (including the leading "--"), storing the result in the variable
 *     named by the TC_OPTIONS_TO_OPTWIDTH symbol, which must have been
 *     initialized to zero.
 *
 * TC_OPTIONS_TO_HELP
 *     Output a printf() statement to print the help text for each option.
 *     Also output printf() statements for option group headers (defined
 *     with the TC_HEADER macro).  The variable named by the
 *     TC_OPTIONS_TO_HELP symbol is used as the option name field width
 *     (presumed to have been computed with TC_OPTIONS_TO_OPTWIDTH).
 *
 * If none of the above are defined, this file simply declares the
 * parse_cmdline() prototype.
 *
 * Note that we explicitly do not use a #ifdef/#endif pair to avoid
 * multiple inclusion of this file, because it is included multiple times
 * by transcode.c, each time with a different one of the above symbols
 * defined.
 */

/*************************************************************************/

/* Check what we've been included for, and define macros appropriately.
 * The macros defined are:
 *
 * TC_OPTION(name, shortopt, argname, helptext, code)
 *     Defines an option "--name" (name should _not_ be quoted).  The name
 *     must be a valid C identifier, except that it may begin with a digit.
 *     * "shortopt" should be the character constant for the option's
 *       option's short form (e.g. 'x' if the short form is "-x"), or
 *       zero if the option has no short form.
 *     * "argname" should be a string naming the option's argument (for
 *       display in help text) if the option takes an argument, zero
 *       (0, not the string "0") if it does not.
 *     * "helptext" should be the option's help text, as a string.
 *     * "code" should be the code to process the option (which will be in
 *       its own block, so local variables are allowed--however, a comma in
 *       variable declarations will be interpreted as a macro argument
 *       delimiter, so each variable must go in its own statement; see the
 *       -x code for an example).
 *
 * TC_HEADER(name)
 *     Defines an option group header, which will be printed as part of the
 *     help message.
 *
 * _TCO_INIT
 * _TCO_FINI
 *     Local macros containing any appropriate header and trailer code for
 *     the requested output.  Defined only when necessary.
 */

#ifdef TC_OPTION
# error TC_OPTION is already defined!  Check for symbol name conflicts.
#endif
#ifdef TC_HEADER
# error TC_HEADER is already defined!  Check for symbol name conflicts.
#endif
#ifdef _TCO_INIT
# error _TCO_INIT is already defined!  Check for symbol name conflicts.
#endif
#ifdef _TCO_FINI
# error _TCO_FINI is already defined!  Check for symbol name conflicts.
#endif

#ifdef TC_OPTIONS_TO_ENUM
# ifdef TC_OPTION
#  error More than one TC_OPTIONS symbol defined!
# endif
/* Define the option value to either the character value of the short form
 * or a unique enum constant, while not disturbing the enum sequence */
# define TC_OPTION(name,shortopt,argname,helptext,code)   \
    OPT_TMP1_##name,                                      \
    OPT_##name = shortopt + (!shortopt * OPT_TMP1_##name),\
    OPT_TMP2_##name = OPT_TMP1_##name,
# define TC_HEADER(name)  /* nothing */
#endif

#ifdef TC_OPTIONS_TO_STRUCT_OPTION
# ifdef TC_OPTION
#  error More than one TC_OPTIONS symbol defined!
# endif
# define TC_OPTION(name,shortopt,argname,helptext,code) \
    { #name, (argname) ? required_argument : no_argument, NULL, OPT_##name },
# define TC_HEADER(name)  /* nothing */
# define _TCO_FINI {0,0,0,0}
#endif

#ifdef TC_OPTIONS_TO_SHORTOPTS
# ifdef TC_OPTION
#  error More than one TC_OPTIONS symbol defined!
# endif
# define _TCO_INIT {           \
    static char shortbuf[513]; \
    char *ptr = shortbuf;      \
    *shortbuf = 0;
# define TC_OPTION(name,shortopt,argname,helptext,code)   \
    if (shortopt && ptr-shortbuf < sizeof(shortbuf)-3) {  \
         *ptr++ = shortopt;                               \
         if (argname)                                     \
             *ptr++ = ':';                                \
    }
# define TC_HEADER(name)  /* nothing */
# define _TCO_FINI                      \
    *ptr = 0;                           \
    TC_OPTIONS_TO_SHORTOPTS = shortbuf; \
}
#endif

#ifdef TC_OPTIONS_TO_CODE
# ifdef TC_OPTION
#  error More than one TC_OPTIONS symbol defined!
# endif
# define TC_OPTION(name,shortopt,argname,helptext,code)   \
    case OPT_##name: { code; break; }
# define TC_HEADER(name)  /* nothing */
#endif

#ifdef TC_OPTIONS_TO_OPTWIDTH
# ifdef TC_OPTION
#  error More than one TC_OPTIONS symbol defined!
# endif
# define TC_OPTION(name,shortopt,argname,helptext,code) { \
    int len = (sizeof(#name)-1)+2;                        \
    if (argname)                                          \
        len += (sizeof(argname)-1)+1;                     \
    if (len > TC_OPTIONS_TO_OPTWIDTH)                     \
        TC_OPTIONS_TO_OPTWIDTH = len;                     \
}
# define TC_HEADER(name)  /* nothing */
#endif

#ifdef TC_OPTIONS_TO_HELP
# ifdef TC_OPTION
#  error More than one TC_OPTIONS symbol defined!
# endif
# define TC_OPTION(name,shortopt,argname,helptext,code)   \
    print_option_help(#name, shortopt, argname, helptext, TC_OPTIONS_TO_HELP);
# define TC_HEADER(name) \
    printf("\n  ======== %s ========\n\n", name);
#endif

#ifndef TC_OPTION
# define TC_OPTION(name,shortopt,argname,helptext,code)  /* nothing */
# define TC_HEADER(name)  /* nothing */
/* The parsing routine: */
extern int parse_cmdline(int argc, char **argv, vob_t *vob);
#endif


/* Output header code, if any. */
#ifdef _TCO_INIT
_TCO_INIT
#endif

/*************************************************************************/

/* The actual option definitions. */
TC_OPTION(help,               'h', 0,
                "print this usage message and exit",
                usage();
                return 0;
)
TC_OPTION(version,            'v', 0,
                "print version and exit",
                version();
                return 0;
)
TC_OPTION(verbose,            'q', "level",
                "verbosity (0=quiet,1=info,2=debug) [1]",
                verbose = strtol(optarg, &optarg, 10);
                if (*optarg) {
                    tc_error("Invalid argument for -q/--verbose");
                    goto short_usage;
                }
                if (verbose)  // ensure TC_INFO is always set if not silent
                    verbose |= TC_INFO;
                vob->verbose = verbose;
)

/********/ TC_HEADER("Input, output, and control files") /********/

TC_OPTION(input,              'i', "file",
#ifdef HAVE_LIBDVDREAD
                "input file/directory/device/mountpoint name",
#else
                "input file/directory name",
#endif
                vob->video_in_file = optarg;
)

TC_OPTION(multi_input,     0,   0,
                "enable EXPERIMENTAL multiple input mode (see manpage)",
                session->core_mode = TC_MODE_DIRECTORY;
)
TC_OPTION(output,             'o', "file",
                "output file name",
                vob->video_out_file = optarg;
)
TC_OPTION(split_size,            0,   "size",
                "split output file after \"size\" MB [off]",
                session->split_size = strtol(optarg, &optarg, 10);
                if (*optarg) {
                    tc_error("Invalid argument for --split_size");
                    goto short_usage;
                }
)
TC_OPTION(avi_comments,       0,   "file",
                "read AVI header comments from file [off]",
                vob->avi_comment_fd = xio_open(optarg, O_RDONLY);
                if (vob->avi_comment_fd == -1) {
                    tc_error("Cannot open comment file \"%s\"", optarg);
                    goto short_usage;
                }
)
TC_OPTION(split_time,         't', "frames",
                "split output file after n frames [off]",
                session->split_time = strtol(optarg, &optarg, 10);
                if (*optarg) {
                    tc_error("Invalid argument for -t/--split_time");
                    goto short_usage;
                }
)
TC_OPTION(audio_input,        'p', "file",
                "read audio stream from separate file [off]",
                vob->audio_in_file = optarg;
)
TC_OPTION(audio_output,       'm', "file",
                "write audio stream to separate file [off]",
                if (*optarg == '-') {
                    tc_error("Missing argument for -m/--audio_output");
                    goto short_usage;
                }
                vob->audio_out_file = optarg;
                vob->audio_file_flag = 1;
)
TC_OPTION(nav_seek,           0,   "file",
                "use VOB navigation file [off]",
                if (*optarg == '-') {
                    tc_error("Missing argument for --nav_seek");
                    goto short_usage;
                }
                vob->nav_seek_file = optarg;
                nav_seek_file = optarg;
)
TC_OPTION(socket,             0,   "file",
                "socket file for run-time control [off]",
                if (*optarg == '-') {
                    tc_error("Missing argument for --socket");
                    goto short_usage;
                }
                socket_file = optarg;
)
TC_OPTION(write_pid,          0,   "file",
                "write pid of transcode process to \"file\" [off]",
                FILE *f;
                if (*optarg == '-') {
                    tc_error("Missing argument for --write_pid");
                    goto short_usage;
                }
                f = fopen(optarg, "w");
                if (f) {
                    fprintf(f, "%d\n", session->tc_pid);
                    fclose(f);
                }
)
TC_OPTION(config_dir,         0,   "dir",
                "assume config files are in this dir [off]",
                if (*optarg == '-') {
                    tc_error("Missing argument for --config_dir");
                    goto short_usage;
                }
                tc_config_set_dir(optarg);
)

/********/ TC_HEADER("Input stream selection") /********/

TC_OPTION(extract_track,      'a', "a[,v]",
                "extract audio[,video] track [0,0]",
                if (sscanf(optarg, "%d,%d", &vob->a_track, &vob->v_track) < 1
                 || vob->a_track < 0
                 || vob->v_track < 0
                ) {
                    tc_error("Invalid argument for -a/--extract_track");
                    goto short_usage;
                }
                preset_flag |= TC_PROBE_NO_TRACK;
)
TC_OPTION(frames,             'c', "f1-f2[,f3-f4...]",
                "encode only given range (frames or HH:MM:SS),"
                " f2,f4,... are *not* encoded [all]",
                if (*optarg == '-') {
                    tc_error("Missing argument for -c/--frames");
                    goto short_usage;
                }
                session->fc_ttime_string = optarg;
)
TC_OPTION(frame_interval,     0,   "N",
                "select only every Nth frame to be exported [1]",
                vob->frame_interval = strtol(optarg, &optarg, 0);
                if (*optarg || vob->frame_interval < 1) {
                    tc_error("Invalid argument for --frame_interval");
                    goto short_usage;
                }
)
TC_OPTION(title,              'T', "t[,c[-d][,a]]",
                "select DVD title[,chapters[,angle]] [1,all,1]",
                if (sscanf(optarg, "%d,%d-%d,%d", &vob->dvd_title,
                           &vob->dvd_chapter1, &vob->dvd_chapter2,
                           &vob->dvd_angle) >= 3
                ) {
                    /* Chapter range given */
                } else if (sscanf(optarg, "%d,%d,%d", &vob->dvd_title,
                                  &vob->dvd_chapter1, &vob->dvd_angle) >= 1
                ) {
                    /* Single (or no) chapter given */
                    vob->dvd_chapter2 = -1;  /* indicate single chapter */
                } else {
                    tc_error("Invalid argument for -T/--title");
                    goto short_usage;
                }
                if (vob->dvd_title < 1) {
                    tc_error("Invalid title for -T/--title");
                    goto short_usage;
                }
                if (vob->dvd_chapter1 != -1) {
                    if (vob->dvd_chapter1 < 1
                        || (vob->dvd_chapter2 != -1
                            && vob->dvd_chapter2 < vob->dvd_chapter1)
                    ) {
                        tc_error("Invalid chapter(s) for -T/--title");
                        goto short_usage;
                    }
                }
                if (vob->dvd_angle < 1) {
                    tc_error("Invalid angle for -T/--title");
                    goto short_usage;
                }
)
TC_OPTION(psu,                'S', "unit[,s1-s2]",
                "process program stream unit[,s1-s2] sequences [0,all]",
                if (sscanf(optarg, "%d,%d-%d", &vob->ps_unit,
                           &vob->ps_seq1, &vob->ps_seq2) < 0
                 || vob->ps_unit < 0
                 || vob->ps_seq1 < 0
                 || vob->ps_seq2 < 0
                 || vob->ps_seq1 > vob->ps_seq2
                ) {
                    tc_error("Invalid argument for -S/--psu");
                    goto short_usage;
                }
                preset_flag |= TC_PROBE_NO_SEEK;
)
TC_OPTION(vob_seek,           'L', "N",
                "seek to VOB stream offset Nx2kB [0]",
                vob->vob_offset = strtol(optarg, &optarg, 10);
                if (*optarg || vob->vob_offset < 0) {
                    tc_error("Invalid argument for -L/--vob_seek");
                    goto short_usage;
                }
)
TC_OPTION(ts_pid,             0,   "0xNN",
                "transport video stream pid [0]",
                vob->ts_pid1 = strtol(optarg, &optarg, 16);
                if (*optarg) {
                    tc_error("Invalid argument for --ts_pid");
                    goto short_usage;
                }
                vob->ts_pid2 = vob->ts_pid1;
)

/********/ TC_HEADER("Input stream format options") /********/

TC_OPTION(probe,              'H', "n",
                "auto-probe n MB of source (0=off) [1]",
                seek_range = strtol(optarg, &optarg, 10);
                if (*optarg || seek_range < 0) {
                    tc_error("Invalid argument for -H/--probe");
                    goto short_usage;
                }
                if (seek_range == 0)
                    auto_probe = 0;
)
TC_OPTION(mplayer_probe,      0,   0,
                "use (external) mplayer to probe source [off]",
                preset_flag |= TC_PROBE_NO_BUILTIN;
)
TC_OPTION(import_with,        'x', "vmod[,amod]",
                "video[,audio] import modules [null]",
                /* Careful here!  "static char vbuf[1001], abuf[1001]" will
                 * be treated as two separate macro arguments by the
                 * preprocessor, so we have to declare each variable in a
                 * separate statement. */
                static char vbuf[1001];
                static char abuf[1001];
                char quote;
                char *s;
                int n;
                /* Scan the string ourselves, rather than using scanf(),
                 * so we can handle internal quotes properly for -x mplayer */
                if (!*optarg) {
                    tc_error("Invalid argument for -x/--import_with");
                    goto short_usage;
                }
                *vbuf = *abuf = 0;
                quote = 0;
                n = 0;
                s = vbuf;
                while (*optarg) {
                    if (*optarg == quote) {
                        quote = 0;
                    } else if (!quote && (*optarg == '"' || *optarg == '\'')) {
                        quote = *optarg;
                    } else if (!quote && *optarg == ',') {
                        if (s == vbuf) {
                            s = abuf;
                            n = 0;
                        } else {
                            tc_error("Invalid argument for -x/--import_with");
                            goto short_usage;
                        }
                    } else {
                        if (n < (s==vbuf ? sizeof(vbuf) : sizeof(abuf))-1) {
                            s[n++] = *optarg;
                            s[n] = 0;
                        }
                    }
                    optarg++;
                }
                if (quote) {
                    tc_error("Invalid argument for -x/--import_with"
                             " (unbalanced quotes)");
                }
                s[n] = 0;
                n = (s==vbuf ? 1 : 2);
                session->im_vid_mod = vbuf;
                // FIXME: vin -> v_in to match no_v_out_codec (same w/audio)
                session->no_vin_codec = 0;
                if ((s = strchr(session->im_vid_mod, '=')) != NULL) {
                    *s++ = 0;
                    if (!*s) {
                        tc_error("Invalid option string for video import"
                                 " module");
                        goto short_usage;
                    }
                    vob->im_v_string = s;
                }
                if (n >= 2) {
                    session->im_aud_mod = abuf;
                    session->no_ain_codec = 0;
                    if ((s = strchr(session->im_aud_mod, '=')) != NULL) {
                        *s++ = 0;
                        if (!*s) {
                            tc_error("Invalid option string for audio import"
                                     " module");
                            goto short_usage;
                        }
                        vob->im_a_string = s;
                    }
                } else {
                    session->im_aud_mod = session->im_vid_mod;
                }
                /* "auto" checks have to come here, to catch "auto=..." */
                if (strcmp(session->im_vid_mod, "auto") == 0) {
                    session->im_vid_mod = NULL;
                    session->no_vin_codec = 1;
                }
                if (strcmp(session->im_aud_mod, "auto") == 0) {
                    session->im_aud_mod = NULL;
                    session->no_ain_codec = 1;
                }
)
TC_OPTION(frame_size,         'g', "WxH",
                "video frame size [720x576]",
                if (sscanf(optarg, "%dx%d", &vob->im_v_width,
                           &vob->im_v_height) != 2
                 || vob->im_v_width <= 0
                 || vob->im_v_height <= 0
                ) {
                    tc_error("Invalid argument for -g/--frame_size");
                    goto short_usage;
                }
                if (vob->im_v_width  > TC_MAX_V_FRAME_WIDTH
                 || vob->im_v_height > TC_MAX_V_FRAME_HEIGHT
                ) {
                    tc_error("Video frame size out of range (max %dx%d)",
                             TC_MAX_V_FRAME_WIDTH, TC_MAX_V_FRAME_HEIGHT);
                    goto short_usage;
                }
                preset_flag |= TC_PROBE_NO_FRAMESIZE;
)
TC_OPTION(import_asr,         0,   "C",
                "set import display aspect ratio code C [auto]",
                 vob->im_asr = strtol(optarg, &optarg, 10);
                 if (*optarg || vob->im_asr < 0) {
                     tc_error("Invalid argument for --import_asr");
                     goto short_usage;
                 }
                 preset_flag |= TC_PROBE_NO_IMASR;
)
TC_OPTION(import_fps,         'f', "rate[,frc]",
                "input video frame rate[,frc] [25.000,0]",
                int n = sscanf(optarg, "%lf,%d", &vob->fps, &vob->im_frc);
                if (n == 2) {
                    if (vob->im_frc < 0 || vob->im_frc > 15) {
                        tc_error("invalid frame rate code for option -f");
                        goto short_usage;
                    }
                    tc_frc_code_to_value(vob->im_frc, &vob->fps);
                } else {
                    if (n < 1 || vob->fps < MIN_FPS) {
                        tc_error("invalid frame rate for option -f");
                        goto short_usage;
                    }
                }
                preset_flag |= TC_PROBE_NO_FPS;
)
TC_OPTION(hard_fps,           0,   0,
                "disable smooth dropping (for variable fps clips) [enabled]",
                vob->hard_fps_flag = TC_TRUE;
)
TC_OPTION(import_afmt,        'e', "r[,b[,c]]",
                "import audio sample format [48000,16,2]",
                int n = sscanf(optarg, "%d,%d,%d", &vob->a_rate,
                               &vob->a_bits, &vob->a_chan);
                switch (n) {
                  case 3:
                    if (vob->a_chan != 0
                     && vob->a_chan != 1
                     && vob->a_chan != 2
                     && vob->a_chan != 6
                    ) {
                        tc_error("Invalid channels argument for"
                                 " -e/--import_afmt");
                        goto short_usage;
                    }
                    preset_flag |= TC_PROBE_NO_CHAN;
                    /* fall through */
                  case 2:
                    if (vob->a_bits != 8 && vob->a_bits != 16) {
                        tc_error("Invalid bits argument for"
                                 " -e/--import_afmt");
                        goto short_usage;
                    }
                    preset_flag |= TC_PROBE_NO_BITS;
                    /* fall through */
                  case 1:
                    if (vob->a_rate <= 0 || vob->a_rate > RATE) {
                        tc_error("Invalid rate argument for"
                                 " -e/--import_afmt");
                        goto short_usage;
                    }
                    preset_flag |= TC_PROBE_NO_RATE;
                    break;
                  default:
                    tc_error("Invalid argument for -e/--import_afmt");
                    break;
                }
)
TC_OPTION(import_codec,       'n', "0xNN",
                "import audio codec ID [0x2000]",
                vob->a_codec_flag = strtol(optarg, &optarg, 16);
                if (*optarg) {
                    tc_error("Invalid argument for -n/--import_format");
                    goto short_usage;
                }
                preset_flag |= TC_PROBE_NO_ACODEC;
)
TC_OPTION(no_audio_adjust,    0,   0,
                "disable audio frame size adjustment [enabled]",
                no_audio_adjust = TC_TRUE;
)

/********/ TC_HEADER("Output stream format options") /********/

TC_OPTION(export_prof,        0,   "profile",
                "export profile name [none]",
                if (*optarg == '-') {
                    tc_error("Missing argument for --export_prof");
                    goto short_usage;
                }
                vob->ex_prof_name = optarg;
)
TC_OPTION(export_with,        'y', "module-string",
                "export modules",
                static char **ex_mod_args = NULL;
                size_t i = 0;
                size_t num = 0;
                char *s = NULL;
                if (ex_mod_args) { /* avoid memleak with multiple -y */
                    tc_strfreev(ex_mod_args);
                }
                ex_mod_args = tc_strsplit(optarg, ',', &num);
                if (num == 0) {
                    tc_error("Invalid argument for -y/--export_with");
                    goto short_usage;
                }
                for (i = 0; i < num; i++) {
                    if (!strncmp(ex_mod_args[i], "A=", 2)) {
                        session->ex_aud_mod = ex_mod_args[i] + 2;
                        session->no_a_out_codec = 0;
                        if ((s = strchr(session->ex_aud_mod, '=')) != NULL) {
                            *s++ = 0;
                            if (!*s) {
                                tc_error("Invalid option string for audio encoder"
                                         " module");
                                goto short_usage;
                            }
                            vob->ex_a_string = s;
                        }
                    }
                    if (!strncmp(ex_mod_args[i], "V=", 2)) {
                        session->ex_vid_mod = ex_mod_args[i] + 2;
                        session->no_v_out_codec = 0;
                        vob->export_attributes |= TC_EXPORT_ATTRIBUTE_VMODULE;
                        if ((s = strchr(session->ex_vid_mod, '=')) != NULL) {
                            *s++ = 0;
                            if (!*s) {
                                tc_error("Invalid option string for video encoder"
                                         " module");
                                goto short_usage;
                            }
                            vob->ex_v_string = s;
                        }
                    }
                    if (!strncmp(ex_mod_args[i], "M=", 2)) {
                        session->ex_mplex_mod = ex_mod_args[i] + 2;
                        if ((s = strchr(session->ex_mplex_mod, '=')) != NULL) {
                            *s++ = 0;
                            if (!*s) {
                                tc_error("Invalid option string for multiplexor");
                                goto short_usage;
                            }
                            vob->ex_m_string = s;
                        }
                    }
                    if (!strncmp(ex_mod_args[i], "X=", 2)) {
                        session->ex_mplex_mod_aux = ex_mod_args[i] + 2;
                        if ((s = strchr(session->ex_mplex_mod_aux, '=')) != NULL) {
                            *s++ = 0;
                            if (!*s) {
                                tc_error("Invalid option string for auxiliary multiplexor");
                                goto short_usage;
                            }
                            vob->ex_mx_string = s;
                        }
                    }
                }
)
TC_OPTION(export_param,       'F', "string",
                "encoder parameter strings [module dependent]",
                char *s;
                if ((s = strchr(optarg, ',')) != NULL) {
                    char *s2;
                    *s = 0;
                    vob->ex_a_fcc = s+1;
                    if ((s2 = strchr(vob->ex_a_fcc,',')) != NULL) {
                        *s2 = 0;
                        vob->ex_profile_name = s2+1;
                    }
                }
                vob->ex_v_fcc = optarg;
                vob->export_attributes |= TC_EXPORT_ATTRIBUTE_VCODEC;
)
TC_OPTION(export_codec,       'N', "format-string",
                "export codecs",
                size_t i = 0;
                size_t num = 0;
                char **pieces = tc_strsplit(optarg, ',', &num);
                if (num < 1 || num > 2) {
                    tc_error("Invalid argument for -N/--export_format");
                    goto short_usage;
                }
                for (i = 0; i < num; i++) {
                    if (!strncmp(pieces[i], "A=", 2)) {
                        vob->ex_a_codec = tc_codec_from_string(pieces[i] + 2);
                        vob->export_attributes |= TC_EXPORT_ATTRIBUTE_ACODEC;
                    }
                    if (!strncmp(pieces[i], "V=", 2)) {
                        vob->ex_v_codec = tc_codec_from_string(pieces[i] + 2);
                        vob->export_attributes |= TC_EXPORT_ATTRIBUTE_VCODEC;
                    }
                }
                tc_strfreev(pieces);
                if (((vob->export_attributes & TC_EXPORT_ATTRIBUTE_VCODEC)
                     && vob->ex_v_codec == TC_CODEC_ERROR)
                 || ((vob->export_attributes & TC_EXPORT_ATTRIBUTE_ACODEC)
                     && vob->ex_a_codec == TC_CODEC_ERROR)
                ) {
                    tc_error("unknown A/V format for -N/--export_format");
                    goto short_usage;
                }
)
TC_OPTION(multipass,          'R', "N[,vf[,af]]",
                "enable multi-pass encoding (0-3) [0,divx4.log,pcm.log]",
                static char vlogfile[1001] = "divx4.log";
                static char alogfile[1001] = "pcm.log";
                if (sscanf(optarg, "%d,%1000[^,],%1000[^,]",
                           &vob->divxmultipass, vlogfile, alogfile) < 1
                 || vob->divxmultipass < 0 || vob->divxmultipass > 3
                 || !*vlogfile
                 || !*alogfile
                ) {
                    tc_error("Invalid argument for -R/--multipass");
                    goto short_usage;
                }
                vob->divxlogfile = vlogfile;
                vob->audiologfile = alogfile;
)
TC_OPTION(vbitrate,           'w', "r[,k[,c]]",
                "encoder bitrate[,keyframes[,crispness]] [1800,250,100]",
                float ratefact = 1.0f;
                int n = sscanf(optarg, "%f,%d,%d", &ratefact,
                               &vob->divxkeyframes, &vob->divxcrispness);
                switch (n) {
                  case 3:
                    if (vob->divxcrispness < 0 || vob->divxcrispness > 100) {
                        tc_error("Invalid crispness argument for"
                                 " -w/--vbitrate");
                        goto short_usage;
                    }
                  case 2:
                    vob->export_attributes |= TC_EXPORT_ATTRIBUTE_GOP;
                  case 1:
                    if (vob->divxbitrate <= 0) {
                        tc_error("Invalid bitrate argument for"
                                 " -w/--vbitrate");
                        goto short_usage;
                    }
                    vob->divxbitrate = (int)ratefact;
                    vob->m2v_requant =      ratefact;
                    vob->export_attributes |= TC_EXPORT_ATTRIBUTE_VBITRATE;
                    break;
                  default:
                    tc_error("Invalid argument for -w/--vbitrate");
                    goto short_usage;
                }
)
TC_OPTION(video_max_bitrate,  0,   "r",
                "maximum bitrate when encoding variable bitrate MPEG-2"
                " streams [same as -w]",
                vob->video_max_bitrate = strtol(optarg, &optarg, 10);
                if (*optarg || vob->video_max_bitrate < 0) {
                    tc_error("Invalid argument for --video_max_bitrate");
                    goto short_usage;
                }
)
TC_OPTION(export_fps,         0,   "f[,c]",
                "output video frame rate[,code] [as input]",
                int n = sscanf(optarg, "%lf,%d", &vob->ex_fps, &vob->ex_frc);
                if (n < 1 || n > 2) {
                    tc_error("Invalid argument for --export_fps");
                    goto short_usage;
                }
                vob->export_attributes |= TC_EXPORT_ATTRIBUTE_FPS;
                if (n == 2) {
                    if (vob->ex_frc < 0 || vob->ex_frc > 15) {
                        tc_error("Invalid frc value for --export_fps");
                        goto short_usage;
                    }
                    vob->export_attributes |= TC_EXPORT_ATTRIBUTE_FRC;
                    tc_frc_code_to_value(vob->ex_frc, &vob->ex_fps);
                } else {
                    if (vob->ex_fps < MIN_FPS) {
                        tc_error("Invalid fps value for --export_fps");
                        goto short_usage;
                    }
                    vob->ex_frc = 0;
                }
)
TC_OPTION(export_frc,         0,   "C",
                "set export frame rate code C independently of actual"
                " frame rate [derived from export FPS]",
                vob->ex_frc = strtol(optarg, &optarg, 10);
                if (*optarg || vob->ex_frc < 0 || vob->ex_frc > 15) {
                    tc_error("Invalid frc value for --export_frc");
                    goto short_usage;
                }
                vob->export_attributes |= TC_EXPORT_ATTRIBUTE_FRC;
)
TC_OPTION(export_asr,         0,   "C",
                "set export display aspect ratio code C [as input]",
                vob->ex_asr = strtol(optarg, &optarg, 10);
                if (*optarg || vob->ex_asr < 0 || vob->ex_asr > 4) {
                    tc_error("Invalid argument for --export_asr");
                    goto short_usage;
                }
                vob->export_attributes |= TC_EXPORT_ATTRIBUTE_ASR;
)
TC_OPTION(export_par,         0,   "{C | N,D}",
                "set export pixel aspect ratio [auto]",
                int n = sscanf(optarg, "%d,%d", &vob->ex_par_width,
                               &vob->ex_par_height);
                if (n == 1) {
                    /* Only one argument: PAR code */
                    vob->ex_par = vob->ex_par_width;
                    if (vob->ex_par < 0 || vob->ex_par > 5) {
                        tc_error("--export_par must be between 0 and 5");
                        goto short_usage;
                    }
                    tc_par_code_to_ratio(vob->ex_par,
                                         &vob->ex_par_width,
                                         &vob->ex_par_height);
                } else if (n == 2) {
                    /* Two arguments: use nonstandard PAR */
                    vob->ex_par = 0;
                    if (vob->ex_par_width <= 0 || vob->ex_par_height <= 0) {
                        tc_error("bad PAR values for --export_par:"
                                 " %d/%d not [>0]/[>0]",
                                 vob->ex_par_width, vob->ex_par_height);
                        goto short_usage;
                    }
                    /* correct common misbehaviour */
                    if (vob->ex_par_width == 1 && vob->ex_par_height == 1) {
                        vob->ex_par = 1;
                        tc_info("given PAR values of 1/1, reset PAR code"
                                " to 1");
                    }
                } else {
                    /* Bad number of arguments (<1 || >2) */
                    tc_error("Invalid argument for --export_par");
                    goto short_usage;
                }
                vob->export_attributes |= TC_EXPORT_ATTRIBUTE_PAR;
)
TC_OPTION(encode_fields,      0,   "C",
                "enable field-based encoding if supported [off]\n"
                "C can be t (top-first), b (bottom-first),\n"
                "         p (progressive), u (unknown)",
                switch (*optarg) {
                  case 't':
                    vob->encode_fields = TC_ENCODE_FIELDS_TOP_FIRST;    break;
                  case 'b':
                    vob->encode_fields = TC_ENCODE_FIELDS_BOTTOM_FIRST; break;
                  case 'p':
                    vob->encode_fields = TC_ENCODE_FIELDS_PROGRESSIVE;  break;
                  case 'u':
                    vob->encode_fields = TC_ENCODE_FIELDS_UNKNOWN;      break;
                  default:
                    tc_error("Invalid argument for --encode_fields");
                    goto short_usage;
                }
                vob->export_attributes |= TC_EXPORT_ATTRIBUTE_FIELDS;
)
TC_OPTION(pulldown,           0,   0,
                "set MPEG 3:2 pulldown flags on export [off]",
                vob->pulldown = TC_TRUE;
)
TC_OPTION(abitrate,           'b', "r[,v[,q[,m]]]",
                "audio encoder bitrate kBits/s[,vbr[,quality[,mode]]]"
                " [128,0,5,0]",
                if (sscanf(optarg, "%d,%d,%f,%d", &vob->mp3bitrate,
                           &vob->a_vbr, &vob->mp3quality, &vob->mp3mode) < 1
                 || vob->mp3bitrate < 0
                 || vob->a_vbr < 0
                 || vob->mp3quality < -1.00001
                 || vob->mp3mode < 0
                ) {
                    tc_error("Invalid argument for -b/--abitrate");
                    goto short_usage;
                }
                vob->export_attributes |= TC_EXPORT_ATTRIBUTE_ABITRATE;
)
TC_OPTION(export_afmt,        'E', "r[,b[,c]]",
                "audio output samplerate, bits, channels [as input]",
                int n = sscanf(optarg, "%d,%d,%d", &vob->mp3frequency,
                               &vob->dm_bits, &vob->dm_chan);
                switch (n) {
                  case 3:
                    if (vob->dm_chan < 0 || vob->dm_chan > 6) {
                        tc_error("Invalid channels argument for"
                                 " -E/--export_afmt");
                        goto short_usage;
                    }
                    vob->export_attributes |= TC_EXPORT_ATTRIBUTE_ACHANS;
                    /* fall through */
                  case 2:
                    if (vob->dm_bits != 0
                     && vob->dm_bits != 8
                     && vob->dm_bits != 16
                     && vob->dm_bits != 24
                    ) {
                        tc_error("Invalid bits argument for -E/--export_afmt");
                        goto short_usage;
                    }
                    vob->export_attributes |= TC_EXPORT_ATTRIBUTE_ABITS;
                    /* fall through */
                  case 1:
                    if (vob->mp3frequency < 0) {
                        tc_error("Invalid rate argument for -E/--export_afmt");
                        goto short_usage;
                    }
                    vob->export_attributes |= TC_EXPORT_ATTRIBUTE_ARATE;
                    break;
                  default:
                    tc_error("Invalid argument for -E/--export_afmt");
                    break;
                }
)
TC_OPTION(quantizers,         0,   "min,max",
                "min/max quantizer, for MPEG-like codecs [2,31]",
                if (sscanf(optarg, "%d,%d", &vob->min_quantizer,
                           &vob->max_quantizer) != 2
                 || vob->min_quantizer < 1 || vob->min_quantizer > 31
                 || vob->max_quantizer < 1 || vob->max_quantizer > 31
                ) {
                    tc_error("Invalid argument for --quantizers");
                    goto short_usage;
                }
)
TC_OPTION(encoder_noflush,           'O', 0,
                "avoid to flush buffer(s) on encoder stop [enabled]",
                vob->encoder_flush = TC_FALSE;
)

/********/ TC_HEADER("Video processing options") /********/

TC_OPTION(pre_clip,           0,   "t[,l[,b[,r]]]",
                "select initial frame region by clipping [off]",
                int n = sscanf(optarg, "%d,%d,%d,%d",
                               &vob->pre_im_clip_top,
                               &vob->pre_im_clip_left,
                               &vob->pre_im_clip_bottom,
                               &vob->pre_im_clip_right);
                if (n < 1) {
                    tc_error("Invalid argument for --pre_clip");
                    goto short_usage;
                }
                pre_im_clip = TC_TRUE;
                // Symmetrical clipping for only 1-3 arguments
                if (n < 2)
                    vob->pre_im_clip_left   = 0;
                if (n < 3)
                    vob->pre_im_clip_bottom = vob->pre_im_clip_top;
                if (n < 4)
                    vob->pre_im_clip_right  = vob->pre_im_clip_left;
)
TC_OPTION(im_clip,            'j', "t[,l[,b[,r]]]",
                "clip or add frame border before filters [off]",
                int n = sscanf(optarg, "%d,%d,%d,%d",
                               &vob->im_clip_top,
                               &vob->im_clip_left,
                               &vob->im_clip_bottom,
                               &vob->im_clip_right);
                if (n < 1) {
                    tc_error("Invalid argument for -j/--im_clip");
                    goto short_usage;
                }
                im_clip = TC_TRUE;
                // Symmetrical clipping for only 1-3 arguments
                if (n < 2)
                    vob->im_clip_left   = 0;
                if (n < 3)
                    vob->im_clip_bottom = vob->im_clip_top;
                if (n < 4)
                    vob->im_clip_right  = vob->im_clip_left;
)
TC_OPTION(deinterlace,        'I', "mode",
                "deinterlace video using given mode (1-5) [off]",
                vob->deinterlace = strtol(optarg, &optarg, 10);
                if (*optarg || vob->deinterlace < 1 || vob->deinterlace > 5) {
                    tc_error("Invalid argument for -I/--deinterlace");
                    goto short_usage;
                }
)
TC_OPTION(expand,             'X', "n[,m[,M]]",
                "expand to height+n*M rows, width+m*M columns [0,0,32]",
                vob->hori_resize2 = 0;
                if (sscanf(optarg, "%d,%d,%d", &vob->vert_resize2,
                           &vob->hori_resize2, &vob->resize2_mult) < 1
                ) {
                    tc_error("Invalid argument for -X/--expand");
                    goto short_usage;
                }
                if (vob->resize2_mult != 8
                 && vob->resize2_mult != 16
                 && vob->resize2_mult != 32
                ) {
                    tc_error("Invalid multiplier for -X/--expand (must be"
                             " 8, 16, or 32)");
                    goto short_usage;
                }
                resize2 = TC_TRUE;
)
TC_OPTION(shrink,             'B', "n[,m[,M]]",
                "shrink to height-n*M rows, width-m*M columns [0,0,32]",
                vob->hori_resize1 = 0;
                if (sscanf(optarg, "%d,%d,%d", &vob->vert_resize1,
                           &vob->hori_resize1, &vob->resize1_mult) < 1
                ) {
                    tc_error("Invalid argument for -X/--expand");
                    goto short_usage;
                }
                if (vob->resize1_mult != 8
                 && vob->resize1_mult != 16
                 && vob->resize1_mult != 32
                ) {
                    tc_error("Invalid multiplier for -B/--shrink (must be"
                             " 8, 16, or 32)");
                    goto short_usage;
                }
                resize1 = TC_TRUE;
)
TC_OPTION(zoom,               'Z', "[W]x[H][,mode]",
                "resize to W columns, H rows w/filtering [off]",
                char *s = optarg;
                if (isdigit(*s)) {
                    vob->zoom_width = strtol(s, &s, 10);
                    if (vob->zoom_width > TC_MAX_V_FRAME_WIDTH) {
                        tc_error("Invalid width for -Z/--zoom (maximum %d)",
                                 TC_MAX_V_FRAME_WIDTH);
                        goto short_usage;
                    }
                } else {
                    vob->zoom_width = 0;
                }
                if (*s++ != 'x') {
                    tc_error("Invalid argument for -Z/--zoom");
                    goto short_usage;
                }
                if (isdigit(*s)) {
                    vob->zoom_height = strtol(s, &s, 10);
                    if (vob->zoom_height > TC_MAX_V_FRAME_HEIGHT) {
                        tc_error("Invalid height for -Z/--zoom (maximum %d)",
                                 TC_MAX_V_FRAME_HEIGHT);
                        goto short_usage;
                    }
                } else {
                    vob->zoom_height = 0;
                }
                vob->zoom_flag = TC_TRUE;
                if (*s == ',') {
                    s++;
                    if (strncmp(s, "fast", strlen(s)) == 0)
                        vob->fast_resize = TC_TRUE;
                    else if (strncmp(s, "interlaced", strlen(s)) == 0)
                        vob->zoom_interlaced = TC_TRUE;
                }
)
TC_OPTION(zoom_filter,        0,   "filter",
                "use given filter for -Z resizing [Lanczos3]",
                vob->zoom_filter = tcv_zoom_filter_from_string(optarg);
		if (vob->zoom_filter == TCV_ZOOM_NULL) {
                    tc_error("invalid argument for --zoom_filter\n"
                             "filter must be one of:\n"
                             "   bell box b_spline hermite lanczos3"
                             " mitchell triangle cubic_keys4 sinc8");
                    goto short_usage;
                }
)
TC_OPTION(ex_clip,            'Y', "t[,l[,b[,r]]]",
                "clip or add frame border after filters [off]",
                int n = sscanf(optarg, "%d,%d,%d,%d",
                               &vob->ex_clip_top,
                               &vob->ex_clip_left,
                               &vob->ex_clip_bottom,
                               &vob->ex_clip_right);
                if (n < 1) {
                    tc_error("Invalid argument for -Y/--ex_clip");
                    goto short_usage;
                }
                ex_clip = TC_TRUE;
                // Symmetrical clipping for only 1-3 arguments
                if (n < 2)
                    vob->ex_clip_left   = 0;
                if (n < 3)
                    vob->ex_clip_bottom = vob->ex_clip_top;
                if (n < 4)
                    vob->ex_clip_right  = vob->ex_clip_left;
)
TC_OPTION(reduce,             'r', "n[,m]",
                "reduce video height/width by n[,m] [off]",
                int n = sscanf(optarg, "%d,%d", &vob->reduce_h,&vob->reduce_w);
                if (n == 1)
                    vob->reduce_w = vob->reduce_h;
                if (n < 1 || vob->reduce_h <= 0 || vob->reduce_w <= 0) {
                    tc_error("Invalid argument for -r/--reduce");
                    goto short_usage;
                }
	        rescale = TC_TRUE;
)
TC_OPTION(flip,               'z', 0,
                "flip video frame upside down [off]",
                vob->flip = TC_TRUE;
)
TC_OPTION(mirror,             'l', 0,
                "mirror video frame [off]",
                vob->mirror = TC_TRUE;
)
TC_OPTION(swap_colors,        'k', 0,
                "swap red/blue (Cb/Cr) in video frame [off]",
                vob->rgbswap = TC_TRUE;
)
TC_OPTION(grayscale,          'K', 0,
                "enable grayscale mode [off]",
                vob->decolor = TC_TRUE;
)
TC_OPTION(gamma,              'G', "val",
                "gamma correction (0.0-10.0) [off]",
                vob->gamma = strtod(optarg, &optarg);
                if (*optarg || vob->gamma < 0) {
                    tc_error("Invalid argument for -G/--gamma");
                    goto short_usage;
                }
                vob->dgamma = TC_TRUE;
)
TC_OPTION(antialias,          'C', "mode",
                "enable anti-aliasing mode (1-3) [off]",
                vob->antialias = strtol(optarg, &optarg, 10);
                if (*optarg || vob->antialias < 1 || vob->antialias > 3) {
                    tc_error("Invalid argument for -C/--antialias");
                    goto short_usage;
                }
)
TC_OPTION(antialias_para,     0,   "w,b",
                "center pixel weight, xy-bias [0.333,0.500]",
                if (sscanf(optarg, "%lf,%lf",
                               &vob->aa_weight, &vob->aa_bias) != 2) {
                    tc_error("Invalid argument for --antialias_para");
                    goto short_usage;
                }
                if (vob->aa_weight < 0.0 || vob->aa_weight > 1.0) {
                    tc_error("Invalid weight for --antlalias_para"
                             " (0.0 <= w <= 1.0)");
                    goto short_usage;
                }
                if (vob->aa_bias < 0.0 || vob->aa_bias > 1.0) {
                    tc_error("Invalid bias for --antlalias_para"
                             " (0.0 <= b <= 1.0)");
                    goto short_usage;
                }
)
TC_OPTION(post_clip,          0,   "t[,l[,b[,r]]]",
                "select final frame region by clipping [off]",
                int n = sscanf(optarg, "%d,%d,%d,%d",
                               &vob->post_ex_clip_top,
                               &vob->post_ex_clip_left,
                               &vob->post_ex_clip_bottom,
                               &vob->post_ex_clip_right);
                if (n < 1) {
                    tc_error("Invalid argument for --post_clip");
                    goto short_usage;
                }
                post_ex_clip = TC_TRUE;
                // Symmetrical clipping for only 1-3 arguments
                if (n < 2)
                    vob->post_ex_clip_left   = 0;
                if (n < 3)
                    vob->post_ex_clip_bottom = vob->post_ex_clip_top;
                if (n < 4)
                    vob->post_ex_clip_right  = vob->post_ex_clip_left;
)
TC_OPTION(video_format,       'V', "fmt",
                "select internal video format [yuv420p]\n"
                "one of: yuv420p, yuv422p, rgb24",
                if (strcmp(optarg, "yuv420p") == 0) {
                    tc_info("yuv420p is already the default for -V");
                    /* anyway... */
                    vob->im_v_codec = TC_CODEC_YUV420P;
                } else if (strcmp(optarg, "yuv422p") == 0) {
                    vob->im_v_codec = TC_CODEC_YUV422P;
                } else if (strcmp(optarg, "rgb24") == 0) {
                    vob->im_v_codec = TC_CODEC_RGB24;
                } else {
                    tc_error("bad argument for -V/--video_format, should"
                             " be one of: yuv420p (default), yuv422p, rgb24");
                    goto short_usage;
                }
)

/********/ TC_HEADER("Audio processing options") /********/

TC_OPTION(audio_swap,         'd', 0,
                "swap bytes in audio stream [off]",
                vob->pcmswap = TC_TRUE;
)
TC_OPTION(audio_scale,        's', "g[,c,f,r]",
                "scale volume by gain[,center,front,rear] [1,1,1,1]",
                vob->ac3_gain[0] = 1.0;
                vob->ac3_gain[1] = 1.0;
                vob->ac3_gain[2] = 1.0;
                if (sscanf(optarg, "%lf,%lf,%lf,%lf", &vob->volume,
                           &vob->ac3_gain[0], &vob->ac3_gain[1],
                           &vob->ac3_gain[2]) < 1
                 || vob->volume < 0
                ) {
                    tc_error("Invalid argument for -s/--audio_scale");
                    goto short_usage;
                }
)
TC_OPTION(audio_use_ac3,      'A', 0,
                "use AC3 as internal audio codec [off]",
                vob->im_a_codec = TC_CODEC_AC3;
)

/********/ TC_HEADER("Other processing options") /********/

TC_OPTION(filter,             'J', "f1[,f2...]",
                "apply external audio/video filters [none]",
                static int size_plugstr = 0;
                int newlen;
                if (*optarg == '-') {
                    tc_error("Missing argument for -J/--filter");
                    goto short_usage;
                }
                newlen = size_plugstr + strlen(optarg) + 1;  // \0
                if (size_plugstr) // it's an append...
                    newlen++; // ... so add the and ',' separator
                session->plugins_string = tc_realloc(session->plugins_string, newlen);
                if (!session->plugins_string)
                    return 0;
                snprintf(session->plugins_string + size_plugstr,
                         newlen - size_plugstr,
                         "%s%s", size_plugstr ? "," : "", optarg);
                size_plugstr = newlen - 1;
                // cut the \0 for the next append (if any)
)
TC_OPTION(quality,            'Q', "enc[,dec]",
                "encoding[,decoding] quality (0=fastest-5=best) [5,5]",
                if (sscanf(optarg,"%d,%d",&vob->divxquality,&vob->quality) < 1
                 || vob->divxquality < 0
                 || vob->quality < 0
                ) {
                    tc_error("Invalid argument for -Q/--quality");
                    goto short_usage;
                }
)
TC_OPTION(passthrough,        'P', "flag",
                "pass-through flag (0=off|1=V|2=A|3=A+V) [0]",
                vob->pass_flag = strtol(optarg, &optarg, 10);
                if (*optarg || vob->pass_flag < 0 || vob->pass_flag > 3) {
                    tc_error("Invalid argument for -P/--passthrough");
                    goto short_usage;
                }
)
TC_OPTION(sync_frame,         'D', "N",
                "sync video start with audio frame num [0]",
                vob->sync = strtol(optarg, &optarg, 10);
                if (*optarg) {
                    tc_error("Invalid argument for -D/--sync_frame");
                    goto short_usage;
                }
                session->sync_seconds = vob->sync;
                preset_flag |= TC_PROBE_NO_AVSHIFT;
)
TC_OPTION(av_fine_ms,         0,   "time",
                "AV fine-tuning shift in millisecs [autodetect]",
                vob->sync_ms = strtol(optarg, &optarg, 10);
                if (*optarg) {
                    tc_error("Invalid argument for --av_sync_ms");
                    goto short_usage;
                }
                preset_flag |= TC_PROBE_NO_AV_FINE;
)
TC_OPTION(demuxer_sync,     'M',   "N",
                "demuxer PES AV sync mode\n"
                "(0=off|1=PTS only|2=full) [1]",
                vob->demuxer = strtol(optarg, &optarg, 10);
                if (*optarg || vob->demuxer < 0 || vob->demuxer > 5) {
                    tc_error("Invalid argument for -M/--demuxer_sync");
                    goto short_usage;
                }
                preset_flag |= TC_PROBE_NO_DEMUX;
)

/********/ TC_HEADER("Codec-specific options") /********/

TC_OPTION(dv_yv12_mode,       0,   0,
                "(libdv) force YV12 mode for PAL\n"
                "Use this option if transcode autodetection fails,"
                " with DV video.",
                vob->dv_yuy2_mode = TC_FALSE;
)
TC_OPTION(dv_yuy2_mode,       0,   0,
                "(libdv) use YUY2 mode for PAL [YV12]\n"
                "If you experience crashes decoding DV video,"
                " try this option.",
                vob->dv_yuy2_mode = TC_TRUE;
)
TC_OPTION(a52_demux,          0,   0,
                "(liba52) demux AC3/A52 to separate channels [off]",
                vob->a52_mode |= TC_A52_DEMUX;
)
TC_OPTION(a52_drc_off,        0,   0,
                "(liba52) disable dynamic range compression [enabled]",
                vob->a52_mode |= TC_A52_DRC_OFF;
)
TC_OPTION(a52_dolby_off,      0,   0,
                "(liba52) disable Dolby surround [enabled]",
                vob->a52_mode |= TC_A52_DOLBY_OFF;
)

/********/ TC_HEADER("Cluster/PSU/chapter mode processing") /********/

TC_OPTION(autosplit,          'W', "n,m[,file]",
                "autosplit VOB and process part n of m [off]",
                static char vob_logfile[1001] = "";
                if (sscanf(optarg, "%d,%d,%1000[^,]", &vob->vob_chunk,
                           &vob->vob_chunk_max, vob_logfile) < 2
                 || vob->vob_chunk < 0
                 || vob->vob_chunk_max <= 0
                 || vob->vob_chunk >= vob->vob_chunk_max + 1
                ) {
                    tc_error("Invalid parameter for -W/--autosplit");
                    goto short_usage;
                }
                if (*vob_logfile)
                    vob->vob_info_file = vob_logfile;
                session->cluster_mode = TC_TRUE;
)
TC_OPTION(cluster_percentage, 0,   0,
                "use percentage mode for cluster encoding [off]",
                vob->vob_percentage = TC_TRUE;
)
TC_OPTION(cluster_chunks,     0,   "a-b",
                "process chunk range instead of selected chunk [off]",
                if (sscanf(optarg,"%d-%d",
                           &vob->vob_chunk_num1, &vob->vob_chunk_num2) != 2
                 || vob->vob_chunk_num1 < 0
                 || vob->vob_chunk_num2 <= 0
                 || vob->vob_chunk_num1 >= vob->vob_chunk_num2
                ) {
                    tc_error("invalid parameter for --cluster_chunks");
                    goto short_usage;
                }
)
TC_OPTION(psu_mode,           0,   0,
                "process VOB in PSU, -o is a filemask incl. %d [off]",
                session->psu_mode     = TC_TRUE;
                session->core_mode    = TC_MODE_PSU;
                session->cluster_mode = TC_TRUE;
)
TC_OPTION(psu_chunks,         0,   "a-b",
                "process only units a-b for PSU mode [all]",
                if (sscanf(optarg, "%d-%d,%d",
                           &vob->vob_psu_num1, &vob->vob_psu_num2,
                           &session->psu_frame_threshold) < 2
                 || vob->vob_psu_num1 < 0
                 || vob->vob_psu_num2 <= 0
                 || vob->vob_psu_num1 >= vob->vob_psu_num2
                ) {
                    tc_error("Invalid parameter for --psu_chunks");
                    goto short_usage;
                }
)
TC_OPTION(no_split,           0,   0,
                "encode to single file in chapter/psu mode [off]",
                no_split = TC_TRUE;
)
TC_OPTION(chapter_mode,       'U', "base",
                "process DVD in chapter mode to base-ch%02d.avi [off]",
                if (*optarg == '-') {
                    tc_error("Missing argument for -U/--base");
                    goto short_usage;
                }
                chbase = optarg;
                session->core_mode = TC_MODE_DVD_CHAPTER;
)

/********/ TC_HEADER("Synchronization options") /********/

TC_OPTION(resync_interval,            0,   "N",
                "check for A/V (re)synchronization every N frames [0]",
		vob->resync_frame_interval = strtol(optarg, &optarg, 10);
                if (*optarg || vob->resync_frame_interval < 0) {
                    tc_error("Invalid argument for --resync_interval");
                    goto short_usage;
                }
)
TC_OPTION(resync_margin,            0,   "N",
                "set maximum A/V drift to N frames  before to trigger (re)synchronization [1]",
		vob->resync_frame_margin = strtol(optarg, &optarg, 10);
                if (*optarg || vob->resync_frame_margin < 0) {
                    tc_error("Invalid argument for --resync_margin");
                    goto short_usage;
                }
)

/********/ TC_HEADER("Miscellaneous options") /********/

#ifdef TC_OPTIONS_TO_HELP
/* produce ONLY help messages since this option require special tratment */
TC_OPTION(no_log_color,      0,  0,
                "disable colors in log messages [use colors]",
                ; /* nothing */
)
#endif /* TC_OPTIONS_TO_HELP */

TC_OPTION(buffers,            'u', "N",
                "use N framebuffers for AV processing [10]",
                if (sscanf(optarg, "%d,%d,%d", &session->max_frame_buffers,
                           &session->buffer_delay_dec,
                           &session->buffer_delay_enc) < 1
                 || session->max_frame_buffers < 0
                ) {
                    tc_error("Invalid argument for -u/--buffers");
                    goto short_usage;
                }
                preset_flag |= TC_PROBE_NO_BUFFER;
)
TC_OPTION(threads,            0,   "N",
                "use N threads for AV processing [1]",
                session->max_frame_threads = strtol(optarg, &optarg, 10);
                if (*optarg
                 || session->max_frame_threads < 0
                 || session->max_frame_threads > TC_FRAME_THREADS_MAX
                ) {
                    tc_error("Invalid argument for --threads");
                    goto short_usage;
                }
)
TC_OPTION(progress_meter,     0,   "N",
                "select type of progress meter [1]",
                session->progress_meter = strtol(optarg, &optarg, 0);
                if (*optarg || session->progress_meter < 0) {
                    tc_error("Invalid argument for --progress_meter");
                    goto short_usage;
                }
)
TC_OPTION(progress_rate,      0,   "N",
                "print progress every N frames [1]",
                session->progress_rate = strtol(optarg, &optarg, 0);
                if (*optarg || session->progress_rate <= 0) {
                    tc_error("Invalid argument for --progress_rate");
                    goto short_usage;
                }
)
TC_OPTION(nice,               0,   "N",
                "set niceness to N [off]",
                session->niceness = strtol(optarg, &optarg, 0);
                if (*optarg) {
                    tc_error("Invalid argument for --nice");
                    goto short_usage;
                }
)
TC_OPTION(accel,              0,   "type[,type...]",
                "override CPU acceleration flags (for debugging)",
#if defined(ARCH_X86) || defined(ARCH_X86_64)
                int parsed = ac_parseflags(optarg, &(session->acceleration));
                if (!parsed) {		
                    tc_error("bad --accel type, valid types: C %s",
                             ac_flagstotext(AC_ALL));			
                    goto short_usage;
                }
#else
                /* Not supported--leave a statement in so the macro doesn't
                 * complain about a missing argument */
                break;
#endif
)
#if 0
TC_OPTION(debug,              0,   0,
                "enable debugging mode [disabled]",
                session->core_mode = TC_MODE_DEBUG;
)
#endif

/*************************************************************************/

/* Output trailer code, if any. */
#ifdef _TCO_FINI
_TCO_FINI
#endif

/* Undefine the macros we defined above. */

#undef TC_OPTION
#undef TC_HEADER
#undef _TCO_INIT
#undef _TCO_FINI

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
