/*
 *  dl_loader.c
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

#include "transcode.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
# ifdef OS_DARWIN
#  include "libdldarwin/dlfcn.h"
# endif
#endif

#include "dl_loader.h"

const char *mod_path = MODULE_PATH;

char module[TC_BUF_MAX];

int (*TCV_export)(int opt, void *para1, void *para2);
int (*TCA_export)(int opt, void *para1, void *para2);
int (*TCV_import)(int opt, void *para1, void *para2);
int (*TCA_import)(int opt, void *para1, void *para2);

static void watch_export_module(const char *s, int opt, transfer_t *para)
{
    tc_debug(TC_DEBUG_MODULES,
             "module=%s [option=%02d, flag=%d]", s, opt, ((para==NULL)? -1:para->flag));
}

static void watch_import_module(const char *s, int opt, transfer_t *para)
{
    tc_debug(TC_DEBUG_MODULES,
             "module=%s [option=%02d, flag=%d]", s, opt, ((para==NULL)? -1:para->flag));
}

int tcv_export(int opt, void *para1, void *para2)
{
    int ret;

    watch_export_module("tcv_export", opt, (transfer_t*) para1);

    ret = TCV_export(opt, para1, para2);

    if (ret == TC_EXPORT_ERROR && (verbose >= TC_DEBUG))
        tc_log_msg(__FILE__, "video export module error");

    if (ret == TC_EXPORT_UNKNOWN && (verbose >= TC_DEBUG))
        tc_log_msg(__FILE__, "option %d unsupported by video export module", opt);

    return ret;
}

int tca_export(int opt, void *para1, void *para2)
{
    int ret;

    watch_export_module("tca_export", opt, (transfer_t*) para1);

    ret = TCA_export(opt, para1, para2);

    if (ret == TC_EXPORT_ERROR && (verbose >= TC_DEBUG))
        tc_log_msg(__FILE__, "audio export module error");

    if (ret == TC_EXPORT_UNKNOWN && (verbose >= TC_DEBUG))
        tc_log_msg(__FILE__, "option %d unsupported by audio export module", opt);

    return(ret);
}

int tcv_import(int opt, void *para1, void *para2)
{
    int ret;

    watch_import_module("tcv_import", opt, (transfer_t*) para1);

    ret = TCV_import(opt, para1, para2);

    if (ret == TC_IMPORT_ERROR && (verbose >= TC_DEBUG))
        tc_log_msg(__FILE__, "video import module error");

    if (ret == TC_IMPORT_UNKNOWN && (verbose >= TC_DEBUG))
        tc_log_msg(__FILE__, "option %d unsupported by video import module", opt);

    return(ret);
}

int tca_import(int opt, void *para1, void *para2)
{
    int ret;

    watch_import_module("tca_import", opt, (transfer_t*) para1);

    ret = TCA_import(opt, para1, para2);

    if (ret == TC_IMPORT_ERROR && (verbose >= TC_DEBUG))
        tc_log_msg(__FILE__, "audio import module error");

    if (ret == TC_IMPORT_UNKNOWN && (verbose >= TC_DEBUG))
        tc_log_msg(__FILE__, "option %d unsupported by audio import module", opt);

    return(ret);
}


void *load_module(const char *mod_name, int mode)
{
    const char *error;
    void *handle;

    if (mode & TC_EXPORT) {
        tc_snprintf(module, sizeof(module), "%s/export_%s.so", ((mod_path==NULL)? MODULE_PATH:mod_path), mod_name);

        tc_debug(TC_DEBUG_MODULES,
                 "loading %s export module %s",
                 ((mode & TC_VIDEO)? "video": "audio"), module);

        handle = dlopen(module, RTLD_GLOBAL| RTLD_LAZY);
        if (!handle) {
            error = dlerror();
            tc_warn("%s", error);
            tc_warn("(%s) loading \"%s\" failed", __FILE__, module);
            return(NULL);
        }

        if (mode & TC_VIDEO) {
            TCV_export = dlsym(handle, "tc_export");
            error = dlerror();
            if (error != NULL)  {
                tc_warn("%s", error);
                return(NULL);
            }
        }

        if (mode & TC_AUDIO) {
            TCA_export = dlsym(handle, "tc_export");
            error = dlerror();
            if (error != NULL)  {
                tc_warn("%s", error);
                return(NULL);
            }
        }

        return(handle);
    }


    if (mode & TC_IMPORT) {
        tc_snprintf(module, sizeof(module), "%s/import_%s.so", ((mod_path==NULL)? MODULE_PATH:mod_path), mod_name);

        tc_debug(TC_DEBUG_MODULES,
                 "loading %s import module %s",
                 ((mode & TC_VIDEO)? "video": "audio"), module);

        handle = dlopen(module, RTLD_GLOBAL| RTLD_LAZY);
        if (!handle) {
            error = dlerror();
            tc_warn("%s", error);
            return(NULL);
        }

        if (mode & TC_VIDEO) {
            TCV_import = dlsym(handle, "tc_import");
            error = dlerror();
            if (error != NULL)  {
                tc_warn("%s", error);
                return(NULL);
            }
        }

        if(mode & TC_AUDIO) {
            TCA_import = dlsym(handle, "tc_import");
            error = dlerror();
            if (error != NULL)  {
                tc_warn("%s", error);
                return(NULL);
            }
        }

        return(handle);
    }

    // wrong mode?
    return(NULL);
}

void unload_module(void *handle)
{
    if (dlclose(handle) != 0) {
        perror("unloading module");
    }
    handle=NULL;
}

