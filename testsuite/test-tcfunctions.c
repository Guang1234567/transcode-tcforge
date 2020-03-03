/*
 * test-tcfunctions -- generic testsuite for libtc code
 *                     everyone feel free to add more tests and improve
 *                     existing ones.
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

#include "libtc/libtc.h"

/*************************************************************************/

#define TC_TEST_BEGIN(NAME) \
static int tcregistry_ ## NAME ## _test(void) \
{ \
    const char *TC_TEST_name = # NAME ; \
    const char *TC_TEST_errmsg = ""; \
    int TC_TEST_step = -1; \
    \
    if (tc_log_info(__FILE__, "running test: [%s]", TC_TEST_name) == 0) { \
        /* hack to begin a block */ \

#define TC_TEST_END \
        return 0; \
    } /* end the block! */ \
TC_TEST_failure: \
    if (TC_TEST_step != -1) { \
        tc_log_warn(__FILE__, "FAILED test [%s] at step %i", TC_TEST_name, TC_TEST_step); \
    } \
    tc_log_warn(__FILE__, "FAILED test [%s] NOT verified: %s", TC_TEST_name, TC_TEST_errmsg); \
    return 1; \
}

#define TC_TEST_SET_STEP(STEP) do { \
    TC_TEST_step = (STEP); \
} while (0)

#define TC_TEST_UNSET_STEP do { \
    TC_TEST_step = -1; \
} while (0)

#define TC_TEST_IS_TRUE(EXPR) do { \
    int err = (EXPR); \
    if (!err) { \
        TC_TEST_errmsg = # EXPR ; \
        goto TC_TEST_failure; \
    } \
} while (0)

#define TC_TEST_IS_TRUE2(EXPR, MSG) do { \
    int err = (EXPR); \
    if (!err) { \
        TC_TEST_errmsg = (MSG); \
        goto TC_TEST_failure; \
    } \
} while (0)


#define TC_RUN_TEST(NAME) \
    errors += tcregistry_ ## NAME ## _test()


/*************************************************************************/

TC_TEST_BEGIN(hwthreads)
    char buf[TC_BUF_MIN] = { '\0' };
    int ret, nth_tc = 0, nth_sys = 0;
    FILE *f = popen("grep processor /proc/cpuinfo | wc -l", "r");

    TC_TEST_IS_TRUE2(f != NULL, "popen");
    fgets(buf, sizeof(buf), f);
    tc_strstrip(buf);
    nth_sys = atoi(buf);
    pclose(f);

    TC_TEST_IS_TRUE(nth_sys > 0);

    ret = tc_sys_get_hw_threads(&nth_tc);
tc_log_info(__FILE__, "nth_sys=%i nth_tc=%i", nth_sys, nth_tc);

    TC_TEST_IS_TRUE(ret == TC_OK);
    TC_TEST_IS_TRUE(nth_tc > 0);
    TC_TEST_IS_TRUE(nth_tc == nth_sys);
TC_TEST_END

/*************************************************************************/

static int test_tcfunctions_all(void)
{
    int errors = 0;

    TC_RUN_TEST(hwthreads);

    return errors;
}

int main(int argc, char *argv[])
{
    int errors = 0;
    
    libtc_init(&argc, &argv);
    
    errors = test_tcfunctions_all();

    putchar('\n');
    tc_log_info(__FILE__, "test summary: %i error%s (%s)",
                errors,
                (errors > 1) ?"s" :"",
                (errors > 0) ?"FAILED" :"PASSED");
    return (errors > 0) ?1 :0;
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

/*************************************************************************/
