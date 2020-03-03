/*
 * cmdline.c -- parse transcode command line
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "transcode.h"
#include "decoder.h"
#include "probe.h"
#include "libtc/libtc.h"
#include "libtc/ratiocodes.h"
#include "libtc/tccodecs.h"
#include "libtcutil/xio.h"
#include "libtcutil/cfgfile.h"

#include "cmdline.h"

#include <ctype.h>
#ifdef HAVE_GETOPT_LONG_ONLY
#include <getopt.h>
#else
#include "libtc/getopt.h"
#endif


/* Global variables from transcode.c that should eventually go away. */
char
    *nav_seek_file=NULL, *socket_file=NULL,
    *chbase=NULL, //*dirbase=NULL,
    base[TC_BUF_MIN];
int preset_flag=0, auto_probe=1, seek_range=1;
int no_audio_adjust=TC_FALSE, no_split=TC_FALSE;


/*************************************************************************/

/* Utility routines used by option processing. */


/**
 * print_option_help:  Print a help line for a given option.
 *
 * Parameters:
 *         name: Option name (long name).
 *     shortopt: Character used for the short form of the option, 0 if none.
 *      argname: String describing the option's argument, NULL if none.
 *     helptext: Help text for the option.  May include newlines (\n).
 *     optwidth: Number of columns to use for the long name and argument.
 * Return value:
 *     None.
 */

#define MAX_LINELEN   79  /* Maximum line length */
#define MAX_OPTWIDTH  35  /* Maximum space to allocate to options */

static void print_option_help(const char *name, char shortopt,
                              const char *argname, const char *helptext,
                              int optwidth)
{
    int helpmax;
    char optbuf[MAX_LINELEN+1];
    const char *s;

    if (optwidth > MAX_OPTWIDTH)
        optwidth = MAX_OPTWIDTH;
    snprintf(optbuf, sizeof(optbuf), "--%s%s%s",
             name,
             argname ? " " : "",
             argname ? argname : "");
    printf("  %c%c%c%-*s  ",
           shortopt ? '-' : ' ',
           shortopt ? shortopt : ' ',
           shortopt ? '/' : ' ',
           optwidth, optbuf);
    if (strlen(optbuf) > optwidth) {
        /* If the option overflowed the given width, skip to the next line */
        printf("\n%-*s", 5 + optwidth + 2, "");
    }
    /* Break help text into lines at whitespace or \n */
    helpmax = MAX_LINELEN - 5 - optwidth - 2;
    s = helptext;
    s += strspn(s, " \t");
    do {
        const char *t, *next;
        t = s + helpmax;
        if (t > s + strlen(s))
            t = s + strlen(s);
        if (t > s + strcspn(s, "\n"))
            t = s + strcspn(s, "\n");
        /* Don't try to break text with no whitespace */
        if (s + strcspn(s, " \t") < t) {
            while (t > s+1 && *t && !isspace(*t))
                t--;
        }
        if (*t == '\n') {
            /* Preserve whitespace after a newline */
            next = t + 1;
        } else {
            next = t + strspn(t, " \t\n");
        }
        /* Only print indent if there's more text */
        printf("%.*s\n%-*s",
               (int)(t-s), s,
               *next ? optwidth+7+3 : 0, "");
        s = next;
        /* Indent subsequent lines 3 spaces (the +3 above is also for this) */
        helpmax = 79 - 5 - optwidth - 2 - 3;
    } while (*s);
}

/*************************************************************************/

/* The actual option definitions are located in cmdline_def.h using macros;
 * see that file for details. */

enum {
    DUMMY = 0x100,
#define TC_OPTIONS_TO_ENUM
#include "cmdline_def.h"
#undef TC_OPTIONS_TO_ENUM
};

static struct option tc_options[] = {
#define TC_OPTIONS_TO_STRUCT_OPTION
#include "cmdline_def.h"
#undef TC_OPTIONS_TO_STRUCT_OPTION
};


/**
 * usage:  Print a command-line help message.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 */

static void usage(void)
{
    int optwidth = 0;

#define TC_OPTIONS_TO_OPTWIDTH optwidth
#include "cmdline_def.h"
#undef TC_OPTIONS_TO_OPTWIDTH

    version();
    printf("\n");
    printf("Usage: transcode [options...]\n");
    printf("\n");
    printf("Options:\n");
#define TC_OPTIONS_TO_HELP optwidth
#include "cmdline_def.h"
#undef TC_OPTIONS_TO_HELP
    printf("\n");
    printf("use tcmodinfo to discover module properties and configurable options.\n");
}


/**
 * parse_cmdline:  Parse all options on the transcode command line, storing
 * appropriate values in the global "vob" data structure.
 *
 * Parameters:
 *     argc: Command line argument count.
 *     argv: Command line argument vector.
 *      vob: Global data structure.
 * Return value:
 *     Nonzero on success, zero on error.
 */

int parse_cmdline(int argc, char **argv, vob_t *vob,
                  TCSession *session)
{
    const char *shortopts;
    int option;

#define TC_OPTIONS_TO_SHORTOPTS shortopts
#include "cmdline_def.h"
#undef TC_OPTIONS_TO_SHORTOPTS

    while (-1 != (option = getopt_long_only(argc, argv, shortopts,
                                            tc_options, NULL))
    ) {
        switch (option) {
#define TC_OPTIONS_TO_CODE
#include "cmdline_def.h"
#undef TC_OPTIONS_TO_CODE
          default:
          short_usage:  /* error-handling label */
            fprintf(stderr, "'transcode -h | more' shows a list of available"
                            " command line options.\n");
            return 0;
        }
    }

    if (optind == 1)
        goto short_usage;

#ifndef __APPLE__
    if (optind < argc) {
        int n;
        tc_warn("unused command line argument detected (%d/%d)", optind, argc);
        for (n = optind; n < argc; n++)
            tc_warn("argc[%d]=%s (unused)", n, argv[n]);
    }
#endif

    return 1;
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
