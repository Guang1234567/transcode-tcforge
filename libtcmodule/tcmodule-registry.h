/*
 * tcmodule-registry.h -- module information registry.
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


#ifndef TCMODULEREGISTRY_H
#define TCMODULEREGISTRY_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "tcmodule-core.h"
#include "tcmodule-data.h"

#include <stdint.h>

/*************************************************************************/

/* TCRregistry data type (aka: the module registry)
 * 
 * A TCRegistry contains the bindings between codec/file formats and
 * the module that implements them.
 * Using the registry, the client code can automatically find (or load)
 * the best module for a given format.
 * Multiple preferences (aka: fallback chains) are possible for a given
 * format, so the registry can keep on trying more than one single
 * module before to give up.
 *
 * The module registry itself is a configuration file. The transcode
 * distribution ships a default, fully-functional one.
 *
 * To learn about the syntax of this file, see the comments included.
 */
typedef struct tcregistry_ *TCRegistry;


/*
 * tc_module_default_path:
 *       provides the libtcmodule default, compiled-in
 *       search path for the module registry.
 *
 * Parameters:
 *       None.
 *
 * Return Value:
 *       The compiled-in default registry search path as constant string.
 *       There is no need to free it.
 */
const char *tc_module_registry_default_path(void);


/*
 * tc_new_module_registry:
 *      initialize a module registry. This function will acquire all
 *      needed resources and set all things appropriately to make the
 *      registry ready to find the appropriate module type.
 *
 * Parameters:
 *    factory:
 *        a TCFactory already initialized and ready.
 *    regpath:
 *        registry file base directory. The TCRegistry will look
 *        for the registry file into this directory.
 *        Note that this must be a single directory.
 *    verbose:
 *        verbosiness level of registry. Control the quantity
 *        of informative messates to print out.
 *        Should be one of TC_INFO, TC_DEBUG... value.
 *
 * Return Value:
 *     A valid TCRegistry if initialization was done
 *     succesfully, NULL otherwise. In latter case, a informative
 *     message is sent through tc_log*().
 *
 * Preconditions:
 *     regpath NOT NULL; giving a NULL parameter will cause a
 *     graceful failure.
 */
TCRegistry tc_new_module_registry(TCFactory factory,
                                  const char *regpath, int verbose);

/*
 * tc_del_module_registry:
 *     finalize a module registry. Frees all the resources aquired so far.
 *
 * Parameters:
 *     registry: a TCRegistry to finalize.
 *
 * Return Value:
 *     TC_OK    if succesfull.
 *     TC_ERROR an error occurred (notified via tc_log*).
 *
 * Preconditions:
 *     The given registry was already initialized. Trying to finalize a
 *     non-initialized registry will cause an undefined behaviour.
 *
 * Postconditions:
 *     all resources acquired by factory are released; no modules are
 *     loaded or avalaible, nor module instances are still floating around.
 */
int tc_del_module_registry(TCRegistry registry);

/*
 * tc_get_module_name_for_format:
 *     scans the module registry and return the module name *set* for
 *     a given format and for a given module class.
 *     (see also: tc_new_module_from_names)
 *
 * Parameters:
 *     registry:
 *         an already initilialized TCRegistry.
 *     modclass:
 *         the class (decoder, encoder, multiplexor, demultiplexor) of the
 *         requested module (set).
 *     fmtname:
 *         the name of the format that the module (set) must support.
 *
 * Return Value:
 *     NULL if no modules are configured for the requested class and format.
 *     A module set otherwise.
 *     A module set is a comma separated list of module names, as a constant
 *     string. The caller must NOT free it.
 */
const char *tc_get_module_name_for_format(TCRegistry registry,
                                          const char *modclass,
                                          const char *fmtname);

/*
 * tc_new_module_from_names:
 *       like tc_new_module (see tcmodule-core.h) but iterates on a module
 *       set (see tc_get_module_name_for_format) until finds the first valid
 *       module. A module is valid when it is loaded succesfully.
 *
 * Parameters:
 *       factory: use this factory instance to build the new module.
 *      modclass: class of module requested (filter, encoding,
 *                demultiplexing...).
 *      modnames: a module set.
 *         media: media type for new module (TC_VIDEO, TC_AUDIO, TC_EXTRA)
 *
 * Return value:
 *      NULL: if *all* the module given in the set failed to load.
 *      Otherwise, returns the first valid handle to a new module instance.
 *
 * Side effects:
 *      a plugin can be loaded (except for errors!) implicitely.
 *
 * Preconditions:
 *      the given registry was already intialized.
 *
 * Postconditions:
 *       if succeeded, module ready to use by client code.
 *
 */
TCModule tc_new_module_from_names(TCFactory factory,
                                  const char *modclass,
                                  const char *modnames,
                                  int media);

/*************************************************************************/

#endif  /* TCMODULEREGISTRY_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
