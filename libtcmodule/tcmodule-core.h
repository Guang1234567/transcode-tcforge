/*
 * tcmodule-core.h -- transcode module system, take two: core components.
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



/*
 * this header file is intended to be included in components
 * which want to use the new transcode module system, acting like
 * clients respect to a plugin.
 */

#ifndef TCMODULE_CORE_H
#define TCMODULE_CORE_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>

#include "tcmodule-data.h" /* pulls framebuffer.h and transcode.h */

/*
 * this structure will hold all the data needed by client code to use a module:
 * the module operations and the capabilities (given by module class, so shared
 * between all the modules) and the private data.
 */
typedef struct tcmodule_ *TCModule;
struct tcmodule_ {
    const TCModuleClass	*klass;
    /* pointer to class data shared between all instances */

    TCModuleInstance 	instance;
    /* private instance data for each module, embedded here */
};

/*************************************************************************
 * interface helpers, using static inline wrappers                       *
 *************************************************************************/

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_configure(TCModule handle,
                               const char *options, TCJob *vob,
                               TCModuleExtraData *xdata[])
{
    return handle->klass->configure(&(handle->instance), options, vob, xdata);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_stop(TCModule handle)
{
    return handle->klass->stop(&(handle->instance));
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_inspect(TCModule handle,
                             const char *param, const char **value)
{
    return handle->klass->inspect(&(handle->instance), param, value);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_encode_video(TCModule handle,
                                  TCFrameVideo *inframe,
                                  TCFrameVideo *outframe)
{
    return handle->klass->encode_video(&(handle->instance),
                                       inframe, outframe);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_encode_audio(TCModule handle,
                                  TCFrameAudio *inframe,
                                  TCFrameAudio *outframe)
{
    return handle->klass->encode_audio(&(handle->instance),
                                       inframe, outframe);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_decode_video(TCModule handle,
                                  TCFrameVideo *inframe,
                                  TCFrameVideo *outframe)
{
    return handle->klass->decode_video(&(handle->instance),
                                       inframe, outframe);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_decode_audio(TCModule handle,
                                  TCFrameAudio *inframe,
                                  TCFrameAudio *outframe)
{
    return handle->klass->decode_audio(&(handle->instance),
                                       inframe, outframe);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_filter_video(TCModule handle,
                                  TCFrameVideo *frame)
{
    return handle->klass->filter_video(&(handle->instance), frame);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_filter_audio(TCModule handle,
                                  TCFrameAudio *frame)
{
    return handle->klass->filter_audio(&(handle->instance), frame);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_open(TCModule handle, const char *filename,
                          TCModuleExtraData *xdata[])
{
    return handle->klass->open(&(handle->instance), filename, xdata);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_close(TCModule handle)
{
    return handle->klass->close(&(handle->instance));
}


#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_write_video(TCModule handle, TCFrameVideo *frame)
{
    return handle->klass->write_video(&(handle->instance), frame);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_write_audio(TCModule handle, TCFrameAudio *frame)
{
    return handle->klass->write_audio(&(handle->instance), frame);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_read_video(TCModule handle, TCFrameVideo *frame)
{
    return handle->klass->read_video(&(handle->instance), frame);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_read_audio(TCModule handle, TCFrameAudio *frame)
{
    return handle->klass->read_audio(&(handle->instance), frame);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_flush_video(TCModule handle, TCFrameVideo *frame,
				 int *frame_returned)
{
    return handle->klass->flush_video(&(handle->instance), frame,
				      frame_returned);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_flush_audio(TCModule handle, TCFrameAudio *frame,
				 int *frame_returned)
{
    return handle->klass->flush_audio(&(handle->instance), frame,
				      frame_returned);
}


#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static const TCModuleInfo *tc_module_get_info(TCModule handle)
{
    return handle->klass->info;
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static int tc_module_match(int codec, int type,
                           TCModule handle, TCModule other)
{
    return tc_module_info_match(codec, type,
                                handle->klass->info, other->klass->info);
}

#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static void tc_module_show_info(TCModule handle, int verbose)
{
    tc_module_info_log(handle->klass->info, verbose);
}


/* factory data type. */
typedef struct tcfactory_ *TCFactory;

/*************************************************************************
 * factory methods                                                       *
 *************************************************************************/

/*
 * tc_new_module_factory:
 *      initialize a module factory. This function will acquire all
 *      needed resources and set all things appropriately to make the
 *      factory ready for create module instances, loading plugins on
 *      demand if needed.
 *
 * Parameters:
 *    modpath:
 *        module base directory. The factory will look for
 *        transcode plugins to load if needed starting from this
 *        directory.
 *        Note that this must be a single directory.
 *    verbose:
 *        verbosiness level of factory. Control the quantity
 *        of informative messates to print out.
 *        Should be one of TC_INFO, TC_DEBUG... value.
 *
 * Return Value:
 *     A valid TCFactory if initialization was done
 *     succesfully, NULL otherwise. In latter case, a informative
 *     message is sent through tc_log*().
 *
 * Preconditions:
 *     modpath NOT NULL; giving a NULL parameter will cause a
 *     graceful failure.
 *
 * Postconditions:
 *     factory initialized and ready to create TCModules.
 */
TCFactory tc_new_module_factory(const char *modpath, int verbose);

/*
 * tc_del_module_factory:
 *     finalize a module factory. Shutdowns the factory completely,
 *     cleaning up everything and unloading plugins.
 *     PLEASE NOTE: this function _CAN_ fail, notably if a plugin
 *     can't be unloaded cleanly (this usually happens because a plugin
 *     has still some live  instances at finalization time).
 *     ALWAYS check the return value and take opportune countermeasures.
 *     At time of writing, a factory can't (and it's unlikely it will
 *     do) destroy all living instances automatically.
 *
 * Parameters:
 *     factory: factory handle to finalize.
 *
 * Return Value:
 *     TC_OK    if succesfull.
 *     TC_ERROR an error occurred (notified via tc_log*).
 *
 * Preconditions:
 *     The given factory was already initialized. Trying to finalize a
 *     non-initialized factory will cause an undefined behaviour.
 *
 * Postconditions:
 *     all resources acquired by factory are released; no modules are
 *     loaded or avalaible, nor module instances are still floating around.
 */
int tc_del_module_factory(TCFactory factory);

/*
 * tc_new_module:
 *      using given factory, create a new module instance of the given type,
 *      belonging to given class, and initialize it with reasonnable
 *      default values.
 *      This function may load a plugin implicitely to fullfill the request,
 *      since plugins are loaded on demand of client code.
 *      The returned instance pointer must be released using
 *      tc_del_module (see below).
 *      The returned instance is ready to use with above tc_module_* macros,
 *      or in any way you like.
 *
 *      PLEASE NOTE: this function automatically invokes module initialization
 *      method on given module. You should NOT do by yourself.
 *
 * Parameters:
 *       factory: use this factory instance to build the new module.
 *      modclass: class of module requested (filter, encoding,
 *                demultiplexing...).
 *       modname: name of module requested.
 *         media: media type for new module (TC_VIDEO, TC_AUDIO, TC_EXTRA)
 *
 * Return value:
 *      NULL: an error occurred, and notified via tc_log_*()
 *      valid handle to a new module instance otherwise.
 *
 * Side effects:
 *      a plugin can be loaded (except for errors!) implicitely.
 *
 * Preconditions:
 *      the given factory was already intialized.
 *
 * Postconditions:
 *       if succeeded, module ready to use by client code.
 *
 * Examples:
 *      if you want to load the "foobar" plugin, belonging to filter class,
 *      you should use a code like this:
 *
 *      TCModule my_module = tc_new_module("filter", "foobar");
 */
TCModule tc_new_module(TCFactory factory,
                       const char *modclass, const char *modname, int media);

/*
 * tc_del_module:
 *      destroy a module instance using given factory, unloading corrispondent
 *      plugin from factory if needed.
 *      This function release the maximum amount of resources possible
 *      acquired by a given module; since some resources (originating plugin)
 *      are shared between all instances, there is possible that some call
 *      doesn't release all resources. Anyway, si guaranted that all resources
 *      are released when all instances are destroyed.
 *
 *      PLEASE NOTE: this function automatically invokes module finalization
 *      method on given module. You should'nt do by yourself.
 *
 * Parameters:
 *      factory: a factory handle, the same one used to create the module
 *      module: module instance to destroy.
 *
 * Return Value:
 *      TC_OK    if succesfull
 *      TC_ERROR an error occurred (notified via tc_log*).
 *
 * Side effects:
 *      a plugin could be unloaded implicitely.
 *
 * Preconditions:
 *      factory already initialized.
 *      ***GIVEN MODULE WAS CREATED USING GIVEN FACTORY***
 *      to violate this condition will case an undefined behaviour.
 *      At time of writing, factory *CANNOT* detect when this condition
 *      is violated. So be careful.
 *
 *      given module instance was obtained using tc_new_module,
 *      applying this function to a module instances obtained in a
 *      different way causes undefined behaviour, most likely a memory
 *      corruption.
 *
 * Postconditions:
 *      resources belonging to instance are released (see above).
 */
int tc_del_module(TCFactory factory, TCModule module);

/*
 * tc_plugin_count:
 *      get the number of loaded plugins in a given factory.
 *      Used mainly for debug purposes.
 *
 * Parameters:
 *      factory: handle to factory to analyze.
 *
 * Return Value:
 *      the number of plugins loaded at the moment.
 *
 * Preconditions:
 *      Given factory was already initialized.
 *      To apply this function to an unitialized factory will cause
 *      an undefine dbehaviour
 */
int tc_plugin_count(const TCFactory factory);

/*
 * tc_module_count:
 *      get the number of module created and still valid by a given
 *      factory. Used mainly for debug purposes.
 *
 * Parameters:
 *      factory: handle to factory to analyze.
 *
 * Return Value:
 *      the number of module created and not yet destroyed at the moment.
 *
 * Preconditions:
 *      Given factory was already initialized.
 *      To apply this function to an unitialized factory will cause
 *      an undefined behaviour
 */
int tc_instance_count(const TCFactory factory);

/*
 * tc_compare_modules:
 *      compare two module (through it's handler) supposed to be the same
 *      type (class + name). Used mainly for debug purposes.
 *
 *      This function *MUST* SCREW UP BADLY if internal checks
 *      are absoultely clean, so assert are used at this moment.
 *
 * Parameters:
 *      amod: handle to first module instance.
 *      bmod: handle to second module instance
 *
 * Return value:
 *      -1 totally different modules
 *       0 same class (some shared data)
 *      +1 same module instance: the two handles point to same instance
 *
 * Side effects:
 *      client code can be abort()ed.
 *
 * Preconditions:
 *      both module handles must refer to valid modules.
 */
int tc_compare_modules(const TCModule amod, const TCModule bmod);

/*
 * tc_module_default_path:
 *       provides the libtcmodule default, compiled-in
 *       search path for the modules.
 *
 * Parameters:
 *       None.
 *
 * Return Value:
 *       The compiled-in default module search path as constant string.
 *       There is no need to free it.
 */
const char *tc_module_default_path(void);

#endif /* TCMODULE_CORE_H */
