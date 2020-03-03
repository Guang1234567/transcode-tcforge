/*
 *  decode_a52.c
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

#include "tccore/tcinfo.h"

#include "src/transcode.h"
#include "libtc/libtc.h"

#include "ioaux.h"
#include "tc.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
# ifdef OS_DARWIN
#  include "libdldarwin/dlfcn.h"
# endif
#endif

static char *mod_path=MODULE_PATH;

#define MODULE "a52_decore.so"

// dl stuff
static int (*p_a52_decore)(decode_t *decode);
static void *handle;
static char module[TC_BUF_MAX];

static int a52_do_init(char *path)
{
    const char *error;

    tc_snprintf(module, sizeof(module), "%s/%s", path, MODULE);

    if(verbose & TC_DEBUG)
        tc_log_msg(__FILE__, "loading external module %s", module);

    // try transcode's module directory
    handle = dlopen(module, RTLD_NOW);
    if (!handle) {
        //try the default:
        //      handle = dlopen(MODULE, RTLD_GLOBAL| RTLD_LAZY);
        if (!handle) {
            error = dlerror();
            fputs (error, stderr);
            fputs("\n", stderr);
            return -1;
        }
    }

    p_a52_decore = dlsym(handle, "a52_decore");
    error = dlerror();
    if (error != NULL)  {
        fputs(error, stderr);
        fputs("\n", stderr);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------
 *
 * decoder thread
 *
 * ------------------------------------------------------------*/

void decode_a52(decode_t *decode)
{
    verbose = decode->verbose;

    // load the codec
    if(a52_do_init(mod_path)<0) {
        tc_log_error(__FILE__, "failed to init ATSC A-52 stream decoder");
        import_exit(1);
    }

    p_a52_decore(decode);
    dlclose(handle);
    import_exit(0);
}


