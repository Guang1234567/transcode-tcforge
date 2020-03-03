/*
 * test-tcmodule.c -- testsuite for tcmodule functions; 
 *                    everyone feel free to add more tests and improve
 *                    existing ones.
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
#include "src/transcode.h"
#include "libtc/libtc.h"
#include "libtcutil/tcutil.h"
#include "libtcmodule/tcmodule-core.h"

int verbose = TC_QUIET;
int err;

static vob_t *vob = NULL;

static TCFactory factory;

// dependencies
vob_t *tc_get_vob(void) { return vob; }


void aframe_copy(aframe_list_t *dst, const aframe_list_t *src, int copy_data) { ; }
void vframe_copy(vframe_list_t *dst, const vframe_list_t *src, int copy_data) { ; }

// partial line length: I don't bother with full line length,
// it's just a naif padding
#define ADJUST_TO_COL 60
static void test_result_helper(const char *name, int ret, int expected)
{
    char spaces[ADJUST_TO_COL] = { ' ' };
    size_t slen = strlen(name);
    int i = 0, padspace = ADJUST_TO_COL - slen;

    if (padspace > 0) {
        // do a bit of padding to let the output looks more nice
        for (i = 0; i < padspace; i++) {
            spaces[i] = ' ';
        }
    }

    if (ret != expected) {
        tc_log_error(__FILE__, "'%s'%sFAILED (%i|%i)",
                     name, spaces, ret, expected);
    } else {
        tc_log_info(__FILE__, "'%s'%sOK",
                    name, spaces);
    }
}


static int test_bad_init(const char *modpath)
{
    factory = tc_new_module_factory("", 0);
    err = (factory == NULL) ?-1 :0;

    test_result_helper("bad_init::init", err, -1);
    return 0;
}

static int test_init_fini(const char *modpath)
{
    factory = tc_new_module_factory(modpath, 0);
    err = (factory == NULL) ?-1 :0;

    test_result_helper("init_fini::init", err, 0);
    test_result_helper("init_fini::fini", tc_del_module_factory(factory), 0);
    return 0;
}

static int test_bad_create(const char *modpath)
{
    TCModule module = NULL;
    factory = tc_new_module_factory(modpath, verbose);
    err = (factory == NULL) ?-1 :0;

    test_result_helper("bad_create::init", err, 0);
    module = tc_new_module(factory, "inexistent", "inexistent", 0);
    if (module != NULL) {
        tc_log_error(__FILE__, "loaded inexistent module?!?!");
    }
    test_result_helper("bad_create::fini", tc_del_module_factory(factory), 0);
    return 0;
}

static int test_create(const char *modpath)
{
    TCModule module = NULL;
    factory = tc_new_module_factory(modpath, verbose);
    err = (factory == NULL) ?-1 :0;

    test_result_helper("create::init", err, 0);
    module = tc_new_module(factory, "filter", "null", 0);
    if (module == NULL) {
        tc_log_error(__FILE__, "can't load filter_null");
    } else {
        test_result_helper("create::check",
                            tc_compare_modules(module,
                                                              module),
                            1);
        test_result_helper("create::instances",
                           tc_instance_count(factory),
                           1);
        test_result_helper("create::descriptors",
                           tc_plugin_count(factory),
                           1);
        tc_del_module(factory, module);
    }
    test_result_helper("create::fini", tc_del_module_factory(factory), 0);
    return 0;
}

static int test_double_create(const char *modpath)
{
    TCModule module1 = NULL, module2 = NULL;
    factory = tc_new_module_factory(modpath, verbose);
    err = (factory == NULL) ?-1 :0;

    test_result_helper("double_create::init", err, 0);
    module1 = tc_new_module(factory, "filter", "null", TC_VIDEO);
    if (module1 == NULL) {
        tc_log_error(__FILE__, "can't load filter_null (1)");
    }
    module2 = tc_new_module(factory, "filter", "null", TC_AUDIO);
    if (module2 == NULL) {
        tc_log_error(__FILE__, "can't load filter_null (1)");
    }

    test_result_helper("double_create::check",
                       tc_compare_modules(module1, module2),
                       0);
    test_result_helper("double_create::instances",
                       tc_instance_count(factory),
                       2);
    test_result_helper("double_create::descriptors",
                       tc_plugin_count(factory),
                       1);
    if (module1) {
        tc_del_module(factory, module1);
    }
    if (module2) {
        tc_del_module(factory, module2);
    }
    test_result_helper("double_create::fini", tc_del_module_factory(factory), 0);
    return 0;
}

#define HOW_MUCH_STRESS         (512) // at least 32, 2 to let the things work
static int test_stress_create(const char *modpath)
{
    TCModule module[HOW_MUCH_STRESS];
    int i, equality;
    factory = tc_new_module_factory(modpath, verbose);
    err = (factory == NULL) ?-1 :0;

    test_result_helper("stress_create::init", err, 0);

    for (i = 0; i < HOW_MUCH_STRESS; i++) {
        module[i] = tc_new_module(factory, "filter", "null", TC_VIDEO);
        if (module[i] == NULL) {
            tc_log_error(__FILE__, "can't load filter_null (%i)", i);
            break;
        }
    }

    test_result_helper("stress_create::create", i, HOW_MUCH_STRESS);
    if (HOW_MUCH_STRESS != i) {
        tc_log_error(__FILE__, "halted with i = %i (limit = %i)",
                     i, HOW_MUCH_STRESS);
        return 1;
    }

    // note that we MUST start from 1
    for (i = 1; i < HOW_MUCH_STRESS; i++) {
        equality = tc_compare_modules(module[i-1], module[i]);
        if (equality != 0) {
            tc_log_error(__FILE__, "diversion! %i | %i", i-1, i);
            break;
        }
    }

    test_result_helper("stress_create::check", i, HOW_MUCH_STRESS);
    if (HOW_MUCH_STRESS != i) {
        tc_log_error(__FILE__, "halted with i = %i (limit = %i)",
                     i, HOW_MUCH_STRESS);
        return 1;
    }

    test_result_helper("stress_create::instances",
                       tc_instance_count(factory),
                       HOW_MUCH_STRESS);
    test_result_helper("stress_create::descriptors",
                       tc_plugin_count(factory), 1);


    for (i = 0; i < HOW_MUCH_STRESS; i++) {
        tc_del_module(factory, module[i]);
    }

    test_result_helper("stress_create::instances (postnuke)",
                       tc_instance_count(factory), 0);
    test_result_helper("stress_create::descriptors (postnuke)",
                       tc_plugin_count(factory), 0);


    test_result_helper("stress_create::fini", tc_del_module_factory(factory), 0);

    return 0;
}

static int test_stress_load(const char *modpath)
{
    TCModule module;
    int i, breakage = 0, instances = 0, descriptors = 0;
    factory = tc_new_module_factory(modpath, verbose);
    err = (factory == NULL) ?-1 :0;

    test_result_helper("stress_load::init", err, 0);

    for (i = 0; i < HOW_MUCH_STRESS; i++) {
        module = tc_new_module(factory, "filter", "null", TC_VIDEO);
        if (module == NULL) {
            tc_log_error(__FILE__, "can't load filter_null (%i)", i);
            break;
        }

        instances = tc_instance_count(factory);
        if(instances != 1) {
            tc_log_error(__FILE__, "wrong instance count: %i, expected %i\n",
                         instances, 1);
            breakage = 1;
            break;
        }

        descriptors = tc_plugin_count(factory);
        if(descriptors != 1) {
            tc_log_error(__FILE__, "wrong descriptor count: %i, expected %i\n",
                         descriptors, 1);
            breakage = 1;
            break;
        }

        tc_del_module(factory, module);

        instances = tc_instance_count(factory);
        if(instances != 0) {
            tc_log_error(__FILE__, "wrong instance count (postnuke): %i, expected %i\n",
                         instances, 0);
            breakage = 1;
            break;
        }

        descriptors = tc_plugin_count(factory);
        if(descriptors != 0) {
            tc_log_error(__FILE__, "wrong descriptor count (postnuke): %i, expected %i\n",
                         descriptors, 0);
            breakage = 1;
            break;
        }
    }

    test_result_helper("stress_load::check", breakage, 0);
    test_result_helper("stress_load::fini", tc_del_module_factory(factory), 0);

    return 0;
}

static int test_load_filter_encode(const char *modpath)
{
    TCModule module1 = NULL, module2 = NULL;
    factory = tc_new_module_factory(modpath, verbose);
    err = (factory == NULL) ?-1 :0;

    test_result_helper("load_filter_encode::init", err, 0);
    module1 = tc_new_module(factory, "filter", "null", TC_AUDIO);
    if (module1 == NULL) {
        tc_log_error(__FILE__, "can't load filter_null (1)");
    }
    module2 = tc_new_module(factory, "encode", "null", TC_VIDEO);
    if (module2 == NULL) {
        tc_log_error(__FILE__, "can't load encode_null (1)");
    }

    test_result_helper("load_filter_encode::check",
                       tc_compare_modules(module1, module2),
                       -1);
    test_result_helper("load_filter_encode::instances",
                       tc_instance_count(factory),
                       2);
    test_result_helper("load_filter_encode::descriptors",
                       tc_plugin_count(factory),
                       2);
    if (module1) {
        tc_del_module(factory, module1);
    }
    if (module2) {
        tc_del_module(factory, module2);
    }
    test_result_helper("load_filter_encode::fini", tc_del_module_factory(factory), 0);
    return 0;
}

static int test_load_encode_multiplex(const char *modpath)
{
    TCModule module1 = NULL, module2 = NULL;
    factory = tc_new_module_factory(modpath, verbose);
    err = (factory == NULL) ?-1 :0;

    test_result_helper("load_encode_multiplex::init", err, 0);
    module1 = tc_new_module(factory, "encode", "null", TC_VIDEO);
    if (module1 == NULL) {
        tc_log_error(__FILE__, "can't load encode_null (1)");
    }
    module2 = tc_new_module(factory, "multiplex", "null", TC_VIDEO|TC_AUDIO);
    if (module2 == NULL) {
        tc_log_error(__FILE__, "can't load multiplex_null (1)");
    }

    test_result_helper("load_encode_multiplex::check",
                       tc_compare_modules(module1, module2),
                       -1);
    test_result_helper("load_encode_multiplex::instances",
                       tc_instance_count(factory),
                       2);
    test_result_helper("load_encode_multiplex::descriptors",
                       tc_plugin_count(factory),
                       2);
    if (module1) {
        tc_del_module(factory, module1);
    }
    if (module2) {
        tc_del_module(factory, module2);
    }
    test_result_helper("load_encode_multiplex::fini", tc_del_module_factory(factory), 0);
    return 0;
}

int main(int argc, char* argv[])
{
    if(argc != 2) {
        fprintf(stderr, "usage: %s /module/path\n", argv[0]);
        exit(1);
    }

    vob = tc_zalloc(sizeof(vob_t));

    libtc_init(&argc, &argv);

    putchar('\n');
    test_bad_init(argv[1]);
    putchar('\n');
    test_init_fini(argv[1]);
    putchar('\n');
    test_bad_create(argv[1]);
    putchar('\n');
    test_create(argv[1]);
    putchar('\n');
    test_double_create(argv[1]);
    putchar('\n');
    test_stress_create(argv[1]);
    putchar('\n');
    test_stress_load(argv[1]);
    putchar('\n');
    test_load_filter_encode(argv[1]);
    putchar('\n');
    test_load_encode_multiplex(argv[1]);

    tc_free(vob);

    return 0;
}

#include "libtcutil/static_optstr.h"

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
