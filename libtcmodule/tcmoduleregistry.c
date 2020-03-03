/*
 * tcmoduleregistry.c -- module information registry (implementation).
 * (C) 2009-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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

#include "tccore/tc_defaults.h"

#include "libtc/mediainfo.h"
#include "libtcutil/tcutil.h"

#include "tcmodule-data.h"
#include "tcmodule-info.h"
#include "tcmodule-registry.h"

#define REGISTRY_MAX_ENTRIES    16
#define REGISTRY_CONFIG_FILE    "modules.cfg"

/*************************************************************************/

typedef struct formatmodules_ FormatModules;
struct formatmodules_ {
    const char  *name;
    char        *demuxer;
    char        *decoder;
    char        *encoder;
    char        *muxer;
};


struct tcregistry_ {
    TCFactory       factory;
    int             verbose;
    const char      *reg_path;

    FormatModules   fmt_mods[REGISTRY_MAX_ENTRIES];
    int             fmt_last;
};

#define RETURN_IF_NULL(ptr, msg, errval) do { \
    if (!ptr) { \
        tc_log_error(__FILE__, msg); \
        return (errval); \
    } \
} while (0)


#define RETURN_IF_INVALID_STRING(str, msg, errval) do { \
    if (!str || !strlen(str)) { \
        tc_log_error(__FILE__, msg); \
        return (errval); \
    } \
} while (0)


#define RETURN_IF_INVALID_QUIET(val, errval) do { \
    if (!(val)) { \
        return (errval); \
    } \
} while (0)

static void fmt_mods_init(FormatModules *fm)
{
    if (fm) {
        fm->name    = NULL;
        fm->demuxer = NULL;
        fm->decoder = NULL;
        fm->encoder = NULL;
        fm->muxer   = NULL;
    }
}

#define FREE_IF_SET(PPTR) do { \
    if (*(PPTR)) { \
        tc_free(*(PPTR)); \
        *(PPTR) = NULL; \
    } \
} while (0);

static void fmt_mods_fini(FormatModules *fm)
{
    FREE_IF_SET(((char**)&(fm->name))); /* fixme: that's too ugly */
    FREE_IF_SET(&(fm->demuxer));
    FREE_IF_SET(&(fm->decoder));
    FREE_IF_SET(&(fm->encoder));
    FREE_IF_SET(&(fm->muxer));
}

/* yes, this sucks. Badly. */
static const char *fmt_mods_for_class(FormatModules *fm,
                                      const char *modclass)
{
    const char *modname = NULL;

    if (modclass != NULL) {
        if (!strcmp(modclass, "demultiplex")
          || !strcmp(modclass, "demux")) {
            modname = fm->demuxer;
        } else if (!strcmp(modclass, "decode")) {
            modname = fm->decoder;
        } else if (!strcmp(modclass, "encode")) {
            modname = fm->encoder;
        } else if (!strcmp(modclass, "multiplex")
          || !strcmp(modclass, "mplex")) {
            modname = fm->muxer;
        }
    }

    return modname;
}


const char *tc_module_registry_default_path(void)
{
    return REGISTRY_PATH;
}

TCRegistry tc_new_module_registry(TCFactory factory,
                                  const char *regpath, int verbose)
{
    int i;
    TCRegistry registry = NULL;
    RETURN_IF_INVALID_QUIET(factory, NULL);

    registry = tc_zalloc(sizeof(struct tcregistry_));
    RETURN_IF_INVALID_QUIET(registry, NULL);

    if (regpath) {
        registry->reg_path = regpath;
    } else {
        registry->reg_path = REGISTRY_PATH;
    }
    registry->verbose  = verbose;
    registry->factory  = factory; /* soft reference */

    tc_log_debug(TC_DEBUG_MODULES, __FILE__,
                 "verbose=%i registry path='%s'",
                 registry->verbose, registry->reg_path);

    for (i = 0; i < REGISTRY_MAX_ENTRIES; i++) {
        fmt_mods_init(&(registry->fmt_mods[i]));
    }
    registry->fmt_last = 0;

    return registry;
}

int tc_del_module_registry(TCRegistry registry)
{
    int i;
    RETURN_IF_INVALID_QUIET(registry, 1);

    for (i = 0; i < registry->fmt_last; i++) {
        fmt_mods_fini(&(registry->fmt_mods[i]));
    }
    tc_free(registry);
    return TC_OK;
}

static const char *lookup_by_name(TCRegistry registry,
                                  const char *modclass,
                                  const char *fmtname)
{
    const char *modname = NULL;
    int i = 0, done = TC_FALSE;

    for (i = 0; !done && i < registry->fmt_last; i++) {
        if (!strcmp(fmtname, registry->fmt_mods[i].name)) {
            modname = fmt_mods_for_class(&(registry->fmt_mods[i]),
                                         modclass);
            done = TC_TRUE;
        }
    }
    return modname;
}

static FormatModules *fmt_mods_get_for_format(TCRegistry registry,
                                              const char *fmtname)
{
    FormatModules *fm = &(registry->fmt_mods[registry->fmt_last]);
    const char *dirs[] = { ".", registry->reg_path, NULL };
    TCConfigEntry registry_conf[] = { 
        { "demuxer", &(fm->demuxer), TCCONF_TYPE_STRING, 0, 0, 0 },
        { "decoder", &(fm->decoder), TCCONF_TYPE_STRING, 0, 0, 0 },
        { "encoder", &(fm->encoder), TCCONF_TYPE_STRING, 0, 0, 0 },
        { "muxer",   &(fm->muxer),   TCCONF_TYPE_STRING, 0, 0, 0 },
        { NULL, NULL, 0, 0, 0, 0 }
    };
    int ret = tc_config_read_file(dirs, REGISTRY_CONFIG_FILE,
                                  fmtname, registry_conf, __FILE__);
    if (!ret) {
        tc_log_debug(TC_DEBUG_MODULES, __FILE__,
                     "missing an entry for '%s' into registry file",
                     fmtname);
    } else {
        tc_log_debug(TC_DEBUG_MODULES, __FILE__,
                     "found an entry for '%s' into registry file",
                     fmtname);

        fm->name = tc_strdup(fmtname);
        if (!fm->name) {
            ret = 0;
        } else {
            /* everything succeeded, so we can claim the item */
            registry->fmt_last++;
        }
    }
    return (ret) ?fm :NULL;
}

const char *tc_get_module_name_for_format(TCRegistry registry,
                                          const char *modclass,
                                          const char *fmtname)
{
    const char *modname = NULL;
    const char *where = "N/A";

    RETURN_IF_INVALID_STRING(modclass, "empty module class", NULL);
    RETURN_IF_INVALID_STRING(fmtname, "empty format name", NULL);
    RETURN_IF_NULL(registry, "invalid registry reference", NULL);

    tc_log_debug(TC_DEBUG_MODULES, __FILE__,
                 "searching modules for class '%s', format '%s'",
                 modclass, fmtname);

    where = "cache";
    modname = lookup_by_name(registry, modclass, fmtname);

    if (!modname) {
        if (registry->fmt_last < REGISTRY_MAX_ENTRIES) {
            FormatModules *fm = fmt_mods_get_for_format(registry, fmtname);
            if (fm) {
                where = "registry file";
                modname = fmt_mods_for_class(fm, modclass);
            }
        } else {
            tc_log_debug(TC_DEBUG_MODULES, __FILE__,
                         "module registry full (please file a bug report)");
        }
    }
    if (modname) {
        tc_log_debug(TC_DEBUG_MODULES, __FILE__,
                     "found in %s, module='%s'",
                     where, modname);
    }
    return modname;
}


#define MOD_NAME_LIST_SEP   ','

TCModule tc_new_module_from_names(TCFactory factory,
                                  const char *modclass,
                                  const char *modnames,
                                  int media)
{
    TCModule mod = NULL;
    size_t i = 0, num = 0;
    char **names = NULL;
    
    RETURN_IF_INVALID_STRING(modclass, "empty module class", NULL);
    RETURN_IF_INVALID_STRING(modnames, "empty module name set", NULL);
    RETURN_IF_NULL(factory, "invalid factory reference", NULL);
    
    names = tc_strsplit(modnames, MOD_NAME_LIST_SEP, &num);
    if (!names) {
        tc_log_debug(TC_DEBUG_MODULES, __FILE__,
                     "error splitting the name sequence '%s'",
                     modnames);
    } else {
        for (i = 0; !mod && names[i]; i++) {
            tc_log_debug(TC_DEBUG_MODULES, __FILE__,
                         "loading from names: '%s'",
                         names[i]);

            mod = tc_new_module(factory, modclass, names[i], media);
        }

        tc_strfreev(names);
    }

    return mod;
}

#ifdef SOMEDAY_THOSE_WILL_BE_USEFUL

TCModule tc_new_module_for_format(TCRegistry registry,
                                  const char *modclass,
                                  const char *format,
                                  int media)
{
    FormatModules *fm = NULL;
    TCModule mod = NULL;

    RETURN_IF_INVALID_STRING(modclass, "empty module class", NULL);
    RETURN_IF_INVALID_STRING(modclass, "empty format name", NULL);
    RETURN_IF_NULL(registry, "invalid registry reference", NULL);

    fm = fmt_mods_get_for_format(registry, format);
    if (fm) {
        const char *modnames = fmt_mods_for_class(fm, modclass);
        if (modnames) {
            mod = tc_new_module_from_names(registry->factory, modclass,
                                           modnames, media);
        } else {
            tc_log_warn(__FILE__,
                        "no module in registry for class=%s format=%s",
                        modclass, format);
        }
    }

    return mod;
}

TCModule tc_new_module_most_fit(TCRegistry registry,
                                const char *modclass,
                                const char *fmtname, const char *modname,
                                int media)
{
    TCModule mod = NULL;
    
    RETURN_IF_INVALID_STRING(modclass, "empty module class", NULL);
    RETURN_IF_NULL(registry, "invalid registry reference", NULL);

    if (modname) {
        mod = tc_new_module(registry->factory, modclass, modname, media);
    } else if (fmtname) {
        mod = tc_new_module_for_format(registry, modclass, fmtname, media);
    } else {
        tc_log_warn(__FILE__, "missing both format name and module name");
    }
    return mod;
}

#endif /* SOMEDAY_THOSE_WILL_BE_USEFUL */

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
