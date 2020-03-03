/*
 * tcmodule.c -- transcode module system, take two.
 * (C) 2005-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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


#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
# ifdef OS_DARWIN
#  include "libdldarwin/dlfcn.h"
# endif
#endif

#include <string.h>

#include "libtc/tccodecs.h"
#include "libtcutil/tcutil.h"
#include "tccore/tc_defaults.h"

#include "tcmodule-data.h"
#include "tcmodule-core.h"
#include "tcmodule-plugin.h"


#define TC_FACTORY_MAX_HANDLERS     (32)
#define MOD_TYPE_MAX_LEN            (TC_BUF_MIN * 2)

#define tc_module_init(module, features) \
    (module)->klass->init(&((module)->instance), features)
#define tc_module_fini(module) \
    (module)->klass->fini(&((module)->instance))

/* module entry point */
typedef const TCModuleClass* (*TCModuleEntry)(void);

typedef enum {
    TC_DESCRIPTOR_FREE = 0,     /* free to use */
    TC_DESCRIPTOR_CREATED,      /* reserved, but not yet registered */
    TC_DESCRIPTOR_DONE,         /* ok, all donw and ready to run */
} TCHandleStatus;

typedef struct tcmoduledescriptor_ TCModuleDescriptor;
struct tcmoduledescriptor_ {
    const char      *type;      /* packed class + name using make_modtype below */
    void            *so_handle; /* used by dl*() stuff */
    TCHandleStatus  status;
    TCModuleInfo    info;

    /* main copy of module class data.
     * all instance pointers will refer to this. */
    TCModuleClass   klass;

    int             ref_count;  /* how many instances are floating around? */
};

struct tcfactory_ {
    const char          *mod_path;   /* base directory for plugin search */
    int                 verbose;

    TCModuleDescriptor  descriptors[TC_FACTORY_MAX_HANDLERS];
    int                 descriptor_count;

    int                 instance_count;
};

/*************************************************************************
 * API 3.x (transcode 1.2.0)                                             *
 *                                                                       *
 * Now the default class is turned upside down.                          *
 * The module are now supposed to `inherit' from this default class.     *
 * This means that the modules *MUST* override the mandatory, checked    *
 * method below, which will always fail complaining loudly when invoked, *
 * and should/could override only the methods that actually implement.   *
 *                                                                       *
 * This change was made because with the API3 additions the number of    *
 * methods grewsignificantly, and it is pointless to force a bunch of    *
 * modules to provide empty stubs for required, while optional, methods. *
 * Moreover, the module code is now simpler and leaner.                  *
 *************************************************************************/

#define METHOD_CHECK(self, method_name) do { \
    if (self != NULL) { \
        tc_log_warn(self->type, "critical: module doesn't provide" \
                                " %s method", method_name); \
    } else { \
        tc_log_error(__FILE__, "critical: %s method missing AND bad" \
                               " instance pointer", method_name); \
    } \
} while (0)

static int void_init(TCModuleInstance *self, uint32_t features)
{
    METHOD_CHECK(self, "initialization");
    return TC_ERROR;
}

static int void_fini(TCModuleInstance *self)
{
    METHOD_CHECK(self, "finalization");
    return TC_ERROR;
}

static int void_configure(TCModuleInstance *self,
                          const char *options, vob_t *vob,
                          TCModuleExtraData *xdata[])
{
    METHOD_CHECK(self, "configuration");
    return TC_ERROR;
}

static int void_stop(TCModuleInstance *self)
{
    METHOD_CHECK(self, "stopping");
    return TC_ERROR;
}

static int void_inspect(TCModuleInstance *self,
                         const char *param, const char **value)
{
    METHOD_CHECK(self, "inspect");
    return TC_ERROR;
}

#undef METHOD_CHECK

/*************************************************************************/

#define MOD_FEATURES TC_MODULE_FEATURE_NONE
#define MOD_FLAGS TC_MODULE_FLAG_NONE
#define MOD_VERSION "0.0.0"
#define MOD_NAME "void"
#define MOD_CAP "internal void module"
#define MOD_DESCRIPTION "internal void module"


static int void_open(TCModuleInstance *self,
                     const char *filename,
                     TCModuleExtraData *xdata[])
{
    TC_MODULE_SELF_CHECK(self,     "open");
    TC_MODULE_SELF_CHECK(filename, "open");

    return TC_OK;
}

static int void_close(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "close");

    return TC_OK;
}

static int void_encode_video(TCModuleInstance *self,
                             TCFrameVideo *inframe,
                             TCFrameVideo *outframe)
{
    TC_MODULE_SELF_CHECK(self,     "encode_video");
    TC_MODULE_SELF_CHECK(inframe,  "encode_video");
    TC_MODULE_SELF_CHECK(outframe, "encode_video");

    outframe->video_len = 0;
    return TC_OK;
}

static int void_encode_audio(TCModuleInstance *self,
                             TCFrameAudio *inframe,
                             TCFrameAudio *outframe)
{
    TC_MODULE_SELF_CHECK(self,     "encode_audio");
    TC_MODULE_SELF_CHECK(inframe,  "encode_audio");
    TC_MODULE_SELF_CHECK(outframe, "encode_audio");

    outframe->audio_len = 0;
    return TC_OK;
}

static int void_decode_video(TCModuleInstance *self,
                             TCFrameVideo *inframe,
                             TCFrameVideo *outframe)
{
    TC_MODULE_SELF_CHECK(self,     "decode_video");
    TC_MODULE_SELF_CHECK(inframe,  "decode_video");
    TC_MODULE_SELF_CHECK(outframe, "decode_video");

    outframe->video_len = 0;
    return TC_OK;
}

static int void_decode_audio(TCModuleInstance *self,
                             TCFrameAudio *inframe,
                             TCFrameAudio *outframe)
{
    TC_MODULE_SELF_CHECK(self,     "decode_audio");
    TC_MODULE_SELF_CHECK(inframe,  "decode_audio");
    TC_MODULE_SELF_CHECK(outframe, "decode_audio");

    outframe->audio_len = 0;
    return TC_OK;
}

static int void_flush_video(TCModuleInstance *self,
                            TCFrameVideo *frame,
                            int *frame_returned)
{
    TC_MODULE_SELF_CHECK(self,  "flush_video");
    TC_MODULE_SELF_CHECK(frame, "flush_video");
    TC_MODULE_SELF_CHECK(frame_returned, "flush_video");

    *frame_returned = 0;
    return TC_OK;
}

static int void_flush_audio(TCModuleInstance *self,
                            TCFrameAudio *frame,
                            int *frame_returned)
{
    TC_MODULE_SELF_CHECK(self,  "flush_audio");
    TC_MODULE_SELF_CHECK(frame, "flush_audio");
    TC_MODULE_SELF_CHECK(frame_returned, "flush_audio");

    *frame_returned = 0;
    return TC_OK;
}

static int void_filter_video(TCModuleInstance *self,
                             TCFrameVideo *frame)
{
    TC_MODULE_SELF_CHECK(self,  "filter_video");
    TC_MODULE_SELF_CHECK(frame, "filter_video");
    return TC_OK;
}

static int void_filter_audio(TCModuleInstance *self,
                             TCFrameAudio *frame)
{
    TC_MODULE_SELF_CHECK(self,  "filter_audio");
    TC_MODULE_SELF_CHECK(frame, "filter_audio");
    return TC_OK;
}

static int void_write_video(TCModuleInstance *self,
                            TCFrameVideo *frame)
{
    TC_MODULE_SELF_CHECK(self,  "write_video");
    TC_MODULE_SELF_CHECK(frame, "write_video");
    return 0;
}

static int void_write_audio(TCModuleInstance *self,
                            TCFrameAudio *frame)
{
    TC_MODULE_SELF_CHECK(self,  "write_audio");
    TC_MODULE_SELF_CHECK(frame, "write_audio");
    return 0;
}


static int void_read_video(TCModuleInstance *self,
                           TCFrameVideo *frame)
{
    TC_MODULE_SELF_CHECK(self,  "read_video");
    TC_MODULE_SELF_CHECK(frame, "read_video");
    return 0;
}

static int void_read_audio(TCModuleInstance *self,
                           TCFrameAudio *frame)
{
    TC_MODULE_SELF_CHECK(self,  "read_audio");
    TC_MODULE_SELF_CHECK(frame, "read_audio");
    return 0;
}


/*************************************************************************/

static const TCCodecID void_codecs_video_in[] = { 
    TC_CODEC_ANY, TC_CODEC_ERROR 
};
static const TCCodecID void_codecs_video_out[] = { 
    TC_CODEC_ANY, TC_CODEC_ERROR 
};
static const TCCodecID void_codecs_audio_in[] = { 
    TC_CODEC_ANY, TC_CODEC_ERROR 
};
static const TCCodecID void_codecs_audio_out[] = { 
    TC_CODEC_ANY, TC_CODEC_ERROR 
};
static const TCFormatID void_formats_in[] = { 
    TC_FORMAT_ANY, TC_FORMAT_ERROR 
};
static const TCFormatID void_formats_out[] = { 
    TC_FORMAT_ANY, TC_FORMAT_ERROR 
};

TC_MODULE_INFO(void);

static const TCModuleClass void_class = {
    TC_MODULE_CLASS_HEAD(void),
    .id           = 0,  /* has to be zero */

    .init         = void_init,
    .fini         = void_fini,
    .configure    = void_configure,
    .inspect      = void_inspect,
    .stop         = void_stop,

    .open         = void_open,
    .close        = void_close,

    .encode_audio = void_encode_audio,
    .encode_video = void_encode_video,
    .decode_audio = void_decode_audio,
    .decode_video = void_decode_video,
    .filter_audio = void_filter_audio,
    .filter_video = void_filter_video,

    .flush_video  = void_flush_video,
    .flush_audio  = void_flush_audio,

    .read_video   = void_read_video,
    .read_audio   = void_read_audio,

    .write_video  = void_write_video,
    .write_audio  = void_write_audio,
};

/*************************************************************************
 * private helpers                                                       *
 *************************************************************************/

/*
 * translate_modclass:
 *     validate a module class name, represented by a given string.
 *
 * Parameters:
 *     modclass: a class nome to validate.
 * Return Value:
 *     TC_MOFULE_FEATURE_* correspondent.
 */
static uint32_t translate_modclass(const char *modclass)
{
    uint32_t ret = TC_MODULE_FEATURE_NONE;

    if (modclass != NULL) {
        if (!strcmp(modclass, "filter")) {
            ret = TC_MODULE_FEATURE_FILTER;
        } else if (!strcmp(modclass, "demultiplex")
          || !strcmp(modclass, "demux")) {
            ret = TC_MODULE_FEATURE_DEMULTIPLEX;
        } else if (!strcmp(modclass, "decode")) {
            ret = TC_MODULE_FEATURE_DECODE;
        } else if (!strcmp(modclass, "encode")) {
            ret = TC_MODULE_FEATURE_ENCODE;
        } else if (!strcmp(modclass, "multiplex")
          || !strcmp(modclass, "mplex")) {
            ret = TC_MODULE_FEATURE_MULTIPLEX;
        }
    }
    return ret;
}

/* FIXME: writeme */
static uint32_t translate_media(int media)
{
    uint32_t ret = 0;

    if (media & TC_VIDEO) {
        ret |= TC_MODULE_FEATURE_VIDEO;
    }
    if (media & TC_AUDIO) {
        ret |= TC_MODULE_FEATURE_AUDIO;
    }
    if (media & TC_EXTRA) {
        ret |= TC_MODULE_FEATURE_EXTRA;
    }

    return ret;
}

/*
 * TCModuleDescriptorIter:
 *     generic iterator function on factory descriptors.
 *     In some different contexts, a iterator can be applied on all module
 *     descriptors in a given factory. Specific iterator functions can do
 *     arbitrary actions on descriptor data.
 *     See below to get some usage examples.
 *
 * Parameters:
 *         desc: pointer to a TCModuleDescriptor.
 *     userdata: opaque pointer to function-specific data.
 * Return Value:
 *     0  -> keep on going
 *     <0 -> stop iteration and return code verbatim
 *     >0 -> stop iteration and return current iteration index
 * Side effects:
 *     Arbitrary, defined by specific function.
 * Preconditions:
 *     given factory (but isn't guaranteed that also descriptors are) already
 *     initialized and contains valid data.
 */
typedef int (*TCModuleDescriptorIter)(TCModuleDescriptor *desc, void *userdata);

/*
 * tc_foreach_descriptor:
 *     apply given iterator with given data to all internal descriptors,
 *     *both used and unused*.
 *
 * Parameters:
 *      factory: factory instance to use
 *     iterator: iterator to apply at factory descriptors
 *     userdata: opaque data to pass to iterator along with each descriptor
 *        index: pointer to an integer. If not NULL, will be filled
 *               with index of last descriptor elaborated
 * Return Value:
 *     return code of the last execution of iterator.
 * Side effects:
 *     None (see specific descriptor for this).
 * Postconditions:
 *     If return value is 0, given iteratr wass applied to *all*
 *     descriptors in factory.
 */
static int tc_foreach_descriptor(TCFactory factory,
                                 TCModuleDescriptorIter iterator,
                                 void *userdata,
                                 int *index)
{
    int ret, i = 0;

    if (!factory || !iterator) {
        return -1;
    }

    for (i = 0; i < TC_FACTORY_MAX_HANDLERS; i++) {
        ret = iterator(&(factory->descriptors[i]), userdata);
        if (ret != 0) {
            break;
        }
    }
    /* iteration stopped, so we mark the interruption point */
    if (ret != 0 && index != NULL) {
        *index = i;
    }
    return ret;
}

/*
 * descriptor_something: some iterator functions
 */

/*
 * descriptor_match_modtype:
 *     verify the match for a given descriptor and a given module type.
 *
 * Parameters:
 *         desc: descriptor to verify
 *     modtype_: module type to look for.
 * Return Value:
 *     1  if given descriptor has given module type,
 *     0  succesfull.
 *     -1 if a given parameter is bogus.
 */
static int descriptor_match_modtype(TCModuleDescriptor *desc,
                                    void *modtype_)
{
    char *modtype = modtype_;
    if (!desc || !modtype) {
        return -1;
    }
    if (desc->status == TC_DESCRIPTOR_DONE
      && desc->type != NULL
      && (strcmp(desc->type, modtype) == 0)) {
        /* found it! */
        return 1;
    }
    return 0;
}

/*
 * descriptor_is_free:
 *     verify the match for a given descriptor is an unitialized one.
 *
 * Parameters:
 *       desc: descriptor to verify
 *     unused: dummy parameter to achieve API conformancy.
 * Return Value:
 *     1  if given descriptor is a free one (uninitialized),
 *     0  succesfull.
 *     -1 if a given parameter is bogus.
 */
static int descriptor_is_free(TCModuleDescriptor *desc, void *unused)
{
    if (!desc) {
        return -1;
    }
    if (desc->status == TC_DESCRIPTOR_FREE) {
        return 1;
    }
    return 0;
}

/*
 * descriptor_init:
 *     initialize a plugin descriptor with valid defaults.
 *
 * Parameters:
 *       desc: descriptor to initialize.
 *     unused: dummy parameter to achieve API conformancy.
 * Return Value:
 *     0  succesfull.
 *     -1 if a given parameter is bogus.
 */
static int descriptor_init(TCModuleDescriptor *desc, void *unused)
{
    if (!desc) {
        return -1;
    }

    desc->status = TC_DESCRIPTOR_FREE;
    memcpy(&(desc->info), &void_info, sizeof(TCModuleInfo));
    desc->klass.info = &(desc->info);
    desc->type = NULL;
    desc->so_handle = NULL;
    desc->ref_count = 0;

    return 0;
}

/*
 * descriptor_fini:
 *     finalize a plugin descriptor, releasing all acquired
 *     resources.
 *
 * Parameters:
 *       desc: descriptor to finalize.
 *     unused: dummy parameter to achieve API conformancy.
 * Return Value:
 *     1  if given descriptor has still some live instances around,
 *     0  succesfull.
 *     -1 if a given parameter is bogus.
 * Side effects:
 *     A plugin will be released and unloaded (via dlclose()).
 */
static int descriptor_fini(TCModuleDescriptor *desc, void *unused)
{
    if (!desc) {
        return -1;
    }

    /* can't finalize an descriptor with some living instances still around */
    if (desc->ref_count > 0) {
        return 1;
    }

    if (desc->status == TC_DESCRIPTOR_DONE) {
        if (desc->type != NULL) {
            tc_free((void*)desc->type);  /* avoid const warning */
            desc->type = NULL;
        }
        if (desc->so_handle != NULL) {
            dlclose(desc->so_handle);
            desc->so_handle = NULL;
        }
        desc->status = TC_DESCRIPTOR_FREE;
    }
    return 0;
}

/* just a thin wrapper to adapt API */
static int find_by_modtype(TCFactory factory, const char *modtype)
{
    int ret, id;
    ret = tc_foreach_descriptor(factory, descriptor_match_modtype,
                                (void*)modtype, &id);
    /* ret >= 1 -> found something */
    return (ret >= 1) ?id : -1;
}

/* just a thin wrapper to adapt API */
static int find_first_free_descriptor(TCFactory factory)
{
    int ret, id;
    ret = tc_foreach_descriptor(factory, descriptor_is_free, NULL, &id);
    /* ret >= 1 -> found something */
    return (ret >= 1) ?id : -1;
}

/* Yeah, is that simple. Yet. ;) */
static void make_modtype(char *buf, size_t bufsize,
                         const char *modclass, const char *modname)
{
    tc_snprintf(buf, bufsize, "%s:%s", modclass, modname);
}

/*
 * tc_module_class_copy:
 *     copy a module class into another one. Can perform
 *     a soft (reference) copy or a hard (full) one.
 *     Only non-null function pointer to plugin operations
 *     will be copied.
 *     soft copy: make the two classes points to same real data.
 *     hard copy: make two independent copies duplicating the data.
 *
 * Parameters:
 *         klass: source class to be copied.
 *        nklass: class destionation of copy.
 *     soft_copy: boolean flag: if !0 do a soft copy,
 *                do an hard one otherwise.
 * Return Value:
 *     0  successfull
 *     -1 given (at least) a bad TCModuleClass reference
 *     1  not enough memory to perform a full copy
 * Postconditions:
 *     destination class is a copy of source class.
 */

#define COPY_IF_NOT_NULL(field) do { \
    if (klass->field != NULL) { \
        nklass->field = klass->field; \
    } \
} while (0)

static int tc_module_class_copy(const TCModuleClass *klass,
                                TCModuleClass *nklass)
{
    if (!klass || !nklass) {
        /* 'impossible' condition */
        tc_log_error(__FILE__, "bad module class reference for setup: %s%s",
                                (!klass) ?"plugin class" :"",
                                (!nklass) ?"core class" :"");
        return -1;
    }

    if (!klass->init || !klass->fini || !klass->configure || !klass->stop
     || !klass->inspect) {
        /* should'nt happen */
        tc_log_error(__FILE__, "can't setup a module class without "
                               "one or more mandatory methods");
        return -1;
    }

    /* register only method really provided by given class */
    nklass->init      = klass->init;
    nklass->fini      = klass->fini;
    nklass->configure = klass->configure;
    nklass->stop      = klass->stop;
    nklass->inspect   = klass->inspect;

    nklass->info      = klass->info;

    COPY_IF_NOT_NULL(open);
    COPY_IF_NOT_NULL(close);

    COPY_IF_NOT_NULL(encode_audio);
    COPY_IF_NOT_NULL(encode_video);
    COPY_IF_NOT_NULL(decode_audio);
    COPY_IF_NOT_NULL(decode_video);
    COPY_IF_NOT_NULL(filter_audio);
    COPY_IF_NOT_NULL(filter_video);

    COPY_IF_NOT_NULL(flush_video);
    COPY_IF_NOT_NULL(flush_audio);

    COPY_IF_NOT_NULL(read_video);
    COPY_IF_NOT_NULL(read_audio);

    COPY_IF_NOT_NULL(write_video);
    COPY_IF_NOT_NULL(write_audio);

    return 0;
}

#undef COPY_IF_NOT_NULL

/*************************************************************************
 * module versioning helpers                                             *
 *************************************************************************/

struct tcmodver {
    int reserved; /* future expansion */
    int major;
    int minor;
    int micro;
};

static void expand_version(uint32_t version, struct tcmodver *modver)
{
    modver->reserved = (version & 0xFF000000) >> 24;
    modver->major    = (version & 0x00FF0000) >> 16;
    modver->minor    = (version & 0x0000FF00) >>  8;
    modver->micro    = (version & 0x000000FF);
}

/*
 * tc_module_version_matches:
 *     check compatibilty between the transcode core version and
 *     a supplied module version.
 *     Only a major version mismatch gives incompatibility (...yet).
 *
 * Parameters:
 *     modversion: the module version being checked.
 * Return Value:
 *     0  if module is incompatible with transcode core
 *     !0 otherwise.
 * Side Effects:
 *     in case of incompatibilty (of any degree), messages are
 *     tc_log()'d out.
 */
static int tc_module_version_matches(uint32_t modversion)
{
    struct tcmodver ver_core, ver_mod;

    expand_version(TC_MODULE_VERSION, &ver_core);
    expand_version(modversion,        &ver_mod);

    if (ver_core.reserved != ver_mod.reserved) {
        tc_log_error(__FILE__, "internal version error");
        return 0;
    }

    /* different major versions are a no-no */
    if (ver_core.major != ver_mod.major) {
        tc_log_error(__FILE__, "incompatible module version "
                               "(core=%i.%i.%i|module=%i.%i.%i)",
                               ver_core.major, ver_core.minor, ver_core.micro,
                               ver_mod.major, ver_mod.minor, ver_mod.micro);
        return 0;
    }
    /* if you use different minor version, you've to know that */
    if (ver_core.minor != ver_mod.minor) {
        tc_log_error(__FILE__, "old module version "
                               "(core=%i.%i.%i|module=%i.%i.%i)",
                               ver_core.major, ver_core.minor, ver_core.micro,
                               ver_mod.major, ver_mod.minor, ver_mod.micro);
        /* still compatible! */
    }
    /* different micro version are ok'd silently */
    return 1;
}


/*************************************************************************
 * main private helpers: _load and _unload                               *
 *************************************************************************/

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

#define TC_LOG_DEBUG(fp, level, format, ...) do { \
    if ((fp)->verbose >= level) { \
        tc_log_info(__FILE__, format, __VA_ARGS__); \
    } \
} while (0)


/*
 * tc_load_module:
 *     load in a given factory a plugin needed for a given module.
 *     please note that here 'plugin' and 'module' terms are used
 *     interchangeably since a given module name from a given module
 *     class usually (almost always, even if such constraint doesn't
 *     exist) originates from a plugin with same class and same name.
 *
 *     In other words, doesn't exist (yet, nor is planned) a plugin
 *     that can generate more than one module and/or more than one
 *     module class
 *
 * Parameters:
 *      factory: module factory to loads module in
 *     modclass: class of plugin to load
 *      modname: name of plugin to load
 * Return Value:
 *     >= 0 identifier (slot) of newly loaded plugin
 *     -1   error occcurred (and notified via tc_log*())
 * Side effects:
 *     a plugin (.so) is loaded into process
 * Preconditions:
 *     none.
 * Postconditions:
 *     none
 */
static int tc_load_module(TCFactory factory,
                          const char *modclass, const char *modname)
{
    int id = -1, ret = -1;
    char full_modpath[PATH_MAX];
    char modtype[MOD_TYPE_MAX_LEN];
    TCModuleEntry modentry = NULL;
    TCModuleDescriptor *desc = NULL;
    const TCModuleClass *nclass;

    /* 'impossible' conditions */
    RETURN_IF_INVALID_STRING(modclass, "empty module class", -1);
    RETURN_IF_INVALID_STRING(modname, "empty module name", -1);
    
    make_modtype(modtype, PATH_MAX, modclass, modname);
    tc_snprintf(full_modpath, PATH_MAX, "%s/%s_%s.so",
                factory->mod_path, modclass, modname);

    id = find_first_free_descriptor(factory);
    if (id == -1) {
        /* should'nt happen */
        tc_log_error(__FILE__, "already loaded the maximum number "
                               "of modules (%i)", TC_FACTORY_MAX_HANDLERS);
        return -1;
    }
    TC_LOG_DEBUG(factory, TC_DEBUG, "using slot %i for plugin '%s'",
                 id, modtype);
    desc = &(factory->descriptors[id]);
    desc->ref_count = 0;

    desc->so_handle = dlopen(full_modpath, RTLD_GLOBAL | RTLD_NOW);
    if (!desc->so_handle) {
        TC_LOG_DEBUG(factory, TC_INFO, "can't load module '%s'; reason: %s",
                     modtype, dlerror());
        goto failed_dlopen;
    }
    desc->type = tc_strdup(modtype);
    if (!desc->type) {
        goto failed_strdup;
    }
    desc->status = TC_DESCRIPTOR_CREATED;

    /* soft copy is enough here, since information will be overwritten */
    tc_module_class_copy(&void_class, &(desc->klass));

    modentry = dlsym(desc->so_handle, "tc_plugin_setup");
    if (!modentry) {
        TC_LOG_DEBUG(factory, TC_INFO, "module '%s' doesn't have new style"
                     " entry point", modtype);
        goto failed_setup;
    }
    nclass = modentry();

    if (!tc_module_version_matches(nclass->version)) {
        /* reason already tc_log'd out */
        goto failed_setup;
    }

    ret = tc_module_class_copy(nclass, &(desc->klass));

    if (ret !=  0) {
        /* should'nt happen */
        tc_log_error(__FILE__, "failed class registration for module '%s'",
                               modtype);
        goto failed_setup;
    }

    desc->klass.id = id; /* enforce class/descriptor id */
    desc->status = TC_DESCRIPTOR_DONE;
    factory->descriptor_count++;

    return id;

failed_setup:
    desc->status = TC_DESCRIPTOR_FREE;
    tc_free((void*)desc->type);  /* avoid const warning */
failed_strdup:
    dlclose(desc->so_handle);
failed_dlopen:
    return -1;
}

#define CHECK_VALID_ID(id, where) do { \
    if (id < 0 || id > TC_FACTORY_MAX_HANDLERS) { \
        if (factory->verbose >= TC_DEBUG) { \
            tc_log_error(__FILE__, "%s: invalid id (%i)", where, id); \
        } \
        return -1; \
    } \
} while (0)

/*
 * tc_unload_module:
 *     unload a given (by id) plugin from given factory.
 *     This means that module belonging to such plugin is no longer
 *     avalaible from given factory, unless, of course, reloading such
 *     plugin.
 *
 * Parameters:
 *     factory: a module factory
 *          id: id of plugin to unload
 * Return Value:
 *     TC_OK      plugin unloaded correctly
 *     TC_ERROR   error occcurred (and notified via tc_log*())
 * Side effects:
 *     a plugin (.so) is UNloaded from process
 * Preconditions:
 *     reference count for given plugin is zero.
 *     This means that no modules instances created by such plugin are
 *     still active.
 * Postconditions:
 *     none
 */
static int tc_unload_module(TCFactory factory, int id)
{
    int ret = 0;
    TCModuleDescriptor *desc = NULL;

    CHECK_VALID_ID(id, "tc_unload_module");
    desc = &(factory->descriptors[id]);

    if (desc->ref_count > 0) {
        TC_LOG_DEBUG(factory, TC_DEBUG, "can't unload a module with active"
                     " ref_count (id=%i, ref_count=%i)",
                     desc->klass.id, desc->ref_count);
        return TC_ERROR;
    }

    ret = descriptor_fini(desc, NULL);
    if (ret == 0) {
        factory->descriptor_count--;
        return TC_OK;
    }
    return TC_ERROR;
}

/*************************************************************************
 * implementation of exported functions                                  *
 *************************************************************************/

TCFactory tc_new_module_factory(const char *modpath, int verbose)
{
    TCFactory factory = NULL;
    RETURN_IF_INVALID_STRING(modpath, "empty module path", NULL);

    factory = tc_zalloc(sizeof(struct tcfactory_));
    RETURN_IF_INVALID_QUIET(factory, NULL);

    factory->mod_path = modpath;
    factory->verbose = verbose;
    factory->descriptor_count = 0;
    factory->instance_count = 0;

    tc_foreach_descriptor(factory, descriptor_init, NULL, NULL);

    return factory;
}

int tc_del_module_factory(TCFactory factory)
{
    RETURN_IF_INVALID_QUIET(factory, 1);

    tc_foreach_descriptor(factory, descriptor_fini, NULL, NULL);

    if (factory->descriptor_count > 0) {
        /* should'nt happpen */
        tc_log_warn(__FILE__, "left out %i module descriptors",
                              factory->descriptor_count);
        return TC_ERROR;
    }

    tc_free(factory);
    return TC_OK;
}

TCModule tc_new_module(TCFactory factory,
                       const char *modclass, const char *modname,
                       int media)
{
    char modtype[MOD_TYPE_MAX_LEN];
    uint32_t flags = translate_modclass(modclass);
    int id = -1, ret;
    TCModule module = NULL;

    RETURN_IF_INVALID_QUIET(factory, NULL);
    if (flags == TC_MODULE_FEATURE_NONE) {
        TC_LOG_DEBUG(factory, TC_INFO, "unknown module class '%s'",
                      modclass);
        return NULL;
    }

    make_modtype(modtype, MOD_TYPE_MAX_LEN, modclass, modname);
    TC_LOG_DEBUG(factory, TC_DEBUG, "trying to load '%s'", modtype);
    id = find_by_modtype(factory, modtype);
    if (id == -1) {
        /* module type not already known */
        TC_LOG_DEBUG(factory, TC_STATS, "plugin not found for '%s',"
                     " loading...", modtype);
        id = tc_load_module(factory, modclass, modname);
        if (id == -1) {
            /* load failed, give up */
            return NULL;
        }
    }
    TC_LOG_DEBUG(factory, TC_DEBUG, "module descriptor found: id %i", id);

    module = tc_zalloc(sizeof(struct tcmodule_));
    module->instance.type = factory->descriptors[id].type;
    module->instance.id = factory->instance_count + 1;
    module->klass = &(factory->descriptors[id].klass);

    ret = tc_module_init(module, flags|translate_media(media));
    if (ret != 0) {
        TC_LOG_DEBUG(factory, TC_DEBUG, "initialization of '%s' failed"
                     " (code=%i)", modtype, ret);
        tc_free(module);
        return NULL;
    }

    factory->descriptors[id].ref_count++;
    factory->instance_count++;
    TC_LOG_DEBUG(factory, TC_DEBUG, "module created: type='%s'"
                 " instance id=(%i)", module->instance.type,
                 module->instance.id);
    TC_LOG_DEBUG(factory, TC_STATS, "descriptor ref_count=(%i) instances"
                 " so far=(%i)", factory->descriptors[id].ref_count,
                 factory->instance_count);

    return module;
}

int tc_del_module(TCFactory factory, TCModule module)
{
    int ret = 0, id = -1;

    RETURN_IF_INVALID_QUIET(factory, 1);
    RETURN_IF_INVALID_QUIET(module, -1);
    
    id = module->klass->id;
    CHECK_VALID_ID(id, "tc_del_module");

    ret = tc_module_fini(module);
    if (ret != 0) {
        TC_LOG_DEBUG(factory, TC_DEBUG, "finalization of '%s' failed"
                     " (code=%i)", module->instance.type, ret);
        return ret;
    }
    tc_free(module);

    factory->instance_count--;
    factory->descriptors[id].ref_count--;
    if (factory->descriptors[id].ref_count == 0) {
        ret = tc_unload_module(factory, id);
    }
    return ret;
}

const char *tc_module_default_path(void)
{
    return MODULE_PATH;
}

/*************************************************************************
 * Debug helpers.                                                        *
 *************************************************************************/

int tc_plugin_count(const TCFactory factory)
{
    RETURN_IF_INVALID_QUIET(factory, -1);
    return factory->descriptor_count;
}

int tc_instance_count(const TCFactory factory)
{
    RETURN_IF_INVALID_QUIET(factory, -1);
    return factory->instance_count;
}


#include <assert.h>

int tc_compare_modules(const TCModule amod, const TCModule bmod)
{
    assert(amod != NULL && bmod != NULL);

    if ((amod == bmod) || (amod->instance.id == bmod->instance.id)) {
        return 1;
    }

    if (strcmp(amod->instance.type, bmod->instance.type) == 0) {
        /* some internal sanity checks.
         * assert()s are used here because those conditions
         * WILL NOT *NEVER* BE FALSE!
         * otherwise something _*really*_ evil is going on
         */
        assert(amod->klass != NULL && bmod->klass != NULL);
        assert(amod->klass == bmod->klass);
        assert(amod->klass->id == bmod->klass->id);
        assert(amod->klass->info == bmod->klass->info);
        /* we should check method pointers as well? */
        return 0;
    }
    return -1;
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
