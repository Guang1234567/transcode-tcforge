/*
 * cmdline.h -- header for transcode command line parser
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef _CMDLINE_H
#define _CMDLINE_H

/*************************************************************************/

/* The parsing routine: */
extern int parse_cmdline(int argc, char **argv, vob_t *vob,
                         TCSession *session);

/* Global variables from transcode.c that should eventually go away. */
extern char *nav_seek_file, *socket_file, *chbase, //*dirbase,
            base[TC_BUF_MIN];
extern int preset_flag, auto_probe, seek_range;
extern int no_audio_adjust, no_split;

/*************************************************************************/

#endif  /* _CMDLINE_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
