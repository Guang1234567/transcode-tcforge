/*
 * test-tcglob.c -- testsuite for TCDirList* family; 
 *                  everyone feel free to add more tests and improve
 *                  existing ones.
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


#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "libtc/libtc.h"
#include "libtcutil/tcglob.h"


int main(int argc, char *argv[])
{
    TCGlob *g = NULL;
    const char *pc = NULL;

    libtc_init(&argc, &argv);

    if (argc != 2) {
        tc_error("usage: %s pattern_to_glob", argv[0]);
    }

    g = tc_glob_open(argv[1], 0);
    if (!g) {
        tc_error("glob open error");
    }
    while ((pc = tc_glob_next(g)) != NULL) {
        puts(pc);
    }
    tc_glob_close(g);

    return 0;
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
