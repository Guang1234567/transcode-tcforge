/*
 * tccfgshow.c -- inspect the transcode internal settings and constants.
 * (C) 2009-2010 - Francesco Romani <fromani at gmail dot com>
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


#include "config.h"
#include "tcstub.h"
#include "libtc/mediainfo.h"
#include "libtcmodule/tcmodule-registry.h"
#include "libtcexport/export_profile.h"

#define EXE "tccfgshow"

enum {
    STATUS_OK = 0,
    STATUS_BAD_PARAM,
};

void version(void)
{
    printf("%s (%s v%s) (C) 2009-2010 Transcode Team\n", EXE, PACKAGE, VERSION);
}

static void usage(void)
{
    version();
    tc_log_info(EXE, "Usage: %s [options]", EXE);
    fprintf(stderr, "    -M    Print the compiled-in module path\n");
    fprintf(stderr, "    -P    Print the compiled-in profile path\n");
    fprintf(stderr, "    -R    Print the compiled-in registry path\n");
    fprintf(stderr, "    -F    Print the list of supported formats\n");
    fprintf(stderr, "    -C    Print the list of supported codecs\n");
}

enum {
    SHOW_NONE        =     0,
    SHOW_MOD_PATH    =     1,
    SHOW_PROF_PATH   = (1<<1),
    SHOW_REG_PATH    = (1<<2),
    SHOW_FORMAT_LIST = (1<<3),
    SHOW_CODEC_LIST  = (1<<4)
};

static int show_codec(const TCCodecInfo *info, void *userdata)
{
    if (info->comment) {
        fprintf(userdata, "%16s    %s\n", info->name, info->comment);
    }
    return TC_TRUE;
}

static int show_format(const TCFormatInfo *info, void *userdata)
{
    if (info->comment) {
        fprintf(userdata, "%16s    %s\n", info->name, info->comment);
    }
    return TC_TRUE;
}



int main(int argc, char *argv[])
{
    int ch;
    int status = STATUS_OK;
    int show =  SHOW_NONE;

    if (argc == 1) {
        usage();
        return STATUS_BAD_PARAM;
    }

    libtc_init(&argc, &argv);

    while (1) {
        ch = getopt(argc, argv, "CFMPRhv?");
        if (ch == -1) {
            break;
        }

        switch (ch) {
          case 'C':
            show |= SHOW_CODEC_LIST;
            break;
          case 'F':
            show |= SHOW_FORMAT_LIST;
            break;
          case 'M':
            show |= SHOW_MOD_PATH;
            break;
          case 'P':
            show |= SHOW_PROF_PATH;
            break;
          case 'R':
            show |= SHOW_REG_PATH;
            break;
          case 'v':
            version();
            return STATUS_OK;
          case '?': /* fallthrough */
          case 'h': /* fallthrough */
          default:
            usage();
            return STATUS_OK;
        }
    }

    /* FIXME: should we switch to the *default_path() functions? */
    if (show & SHOW_MOD_PATH) {
        printf("%s\n", tc_module_default_path());
    }
    if (show & SHOW_PROF_PATH) {
        printf("%s\n", tc_export_profile_default_path());
    }
    if (show & SHOW_REG_PATH) {
        printf ("%s\n", tc_module_registry_default_path());
    }
    if (show & SHOW_CODEC_LIST) {
        tc_codec_foreach(show_codec, stdout);
    }
    if (show & SHOW_FORMAT_LIST) {
        tc_format_foreach(show_format, stdout);
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
