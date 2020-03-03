/*
 * import_xvid.c -- dummy module to direct users to ffmpeg module
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#define MOD_NAME    "import_xvid.so"
#define MOD_VERSION "v0.1.0 (2006-03-29)"
#define MOD_CODEC   "(video) none/obsolete"

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB | TC_CAP_YUV | TC_CAP_VID;

#define MOD_PRE xvid
#include "import_def.h"

MOD_open
{
    tc_log_error(MOD_NAME, "**************** NOTICE ****************");
    tc_log_error(MOD_NAME, "This module is obsolete.  Please use the");
    tc_log_error(MOD_NAME, "ffmpeg module (-x ffmpeg) for XviD video.");
    return TC_IMPORT_ERROR;
}

MOD_decode
{
    return TC_IMPORT_ERROR;
}

MOD_close
{
    return TC_IMPORT_ERROR;
}

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
