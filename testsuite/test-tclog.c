/*
 * test-tclog.c -- testsuite for tc_*log* family (tc_functions.c);
 *                 everyone feel free to add more tests and improve
 *                 existing ones.
 * (C) 2006-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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

#define _GNU_SOURCE 1

#include "libtc/libtc.h"
#include <stdlib.h>
#include <stdio.h>

#define TC_MSG_BUF_SIZE     (256) /* ripped from libtc/tc_functions.c */
#define HUGE_MSG_SIZE       (TC_MSG_BUF_SIZE * 2)
#define STD_MSG_SIZE        (64)
#define TINY_MSG_SIZE       (4)

int main(int argc, char *argv[])
{
    int i = 0, ret;
    char huge[HUGE_MSG_SIZE] = { '\0' };
    char std[STD_MSG_SIZE] = { '\0' };
    char tiny[TINY_MSG_SIZE] = { '\0' };

    ret = libtc_init(&argc, &argv);
    if (ret != TC_OK) {
        exit(2);
    }

    for (i = 0; i < HUGE_MSG_SIZE - 1; i++) {
        huge[i] = 'H';
    }
    for (i = 0; i < STD_MSG_SIZE - 1; i++) {
        std[i] = 'S';
    }
    for (i = 0; i < TINY_MSG_SIZE - 1; i++) {
        tiny[i] = 'T';
    }

    fprintf(stderr, "round 1: NULL (begin)\n");
    tc_log_msg(NULL, NULL);
    tc_log_info(NULL, NULL);
    tc_log_warn(NULL, NULL);
    tc_log_error(NULL, NULL);
    fprintf(stderr, "round 1: NULL (end)\n");

    fprintf(stderr, "round 2: empty (begin)\n");
    tc_log_msg("", "");
    tc_log_info("", "");
    tc_log_warn("", "");
    tc_log_error("", "");
    fprintf(stderr, "round 2: empty (end)\n");

    fprintf(stderr, "round 3: NULL + empty (begin)\n");
    tc_log_msg("", NULL);
    tc_log_msg(NULL, "");
    tc_log_info("", NULL);
    tc_log_info(NULL, "");
    tc_log_warn("", NULL);
    tc_log_warn(NULL, "");
    tc_log_error("", NULL);
    tc_log_error(NULL, "");
    fprintf(stderr, "round 3: NULL + empty (end)\n");

    fprintf(stderr, "round 9: larger than life (begin)\n");
    tc_log_msg(huge, "%s%s%s%s", huge, huge, huge, huge);
    tc_log_info(huge, "%s%s%s%s", huge, huge, huge, huge);
    tc_log_warn(huge, "%s%s%s%s", huge, huge, huge, huge);
    tc_log_error(huge, "%s%s%s%s", huge, huge, huge, huge);
    fprintf(stderr, "round 9: larger than life (end)\n");
    
    fprintf(stderr, "round 10: multiline (begin)\n");
    tc_log_msg("multiline", "%s:\n"
               "line number 1, nothing particular here\n"
               "line number 2, nothing particular here\n"
               "line number 3, nothing particular here\n"
               "line number 4, nothing particular here\n",
               "multiline");
    tc_log_info("multiline", "%s:\n"
                "line number 1, nothing particular here\n"
                "line number 2, nothing particular here\n"
                "line number 3, nothing particular here\n"
                "line number 4, nothing particular here\n",
                "multiline");
    tc_log_warn("multiline", "%s:\n"
                "line number 1, nothing particular here\n"
                "line number 2, nothing particular here\n"
                "line number 3, nothing particular here\n"
                "line number 4, nothing particular here\n",
                "multiline");
    tc_log_error("multiline", "%s:\n"
                 "line number 1, nothing particular here\n"
                 "line number 2, nothing particular here\n"
                 "line number 3, nothing particular here\n"
                 "line number 4, nothing particular here\n",
                 "multiline");
    fprintf(stderr, "round 10: multiline (end)\n");
    
    return 0;
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "hugeouhugeup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
