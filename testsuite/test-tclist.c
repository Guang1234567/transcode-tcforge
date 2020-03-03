/*
 * test-tclist.c -- testsuite for TCList* family; 
 *                   everyone feel free to add more tests and improve
 *                   existing ones.
 * (C) 2008-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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
#include "libtcutil/tclist.h"


/*************************************************************************/

#define TC_TEST_BEGIN(NAME, CACHED) \
static int tclist_ ## NAME ## _test(void) \
{ \
    const char *TC_TEST_name = # NAME ; \
    const char *TC_TEST_errmsg = ""; \
    int TC_TEST_step = -1; \
    \
    TCList L; \
    \
    tc_log_info(__FILE__, "running test: [%s]", # NAME); \
    if (tc_list_init(&L, (CACHED)) == TC_OK) {


#define TC_TEST_END \
        if (tc_list_fini(&L) != TC_OK) { \
            return 1; \
        } \
        return 0; \
    } \
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


#define TC_RUN_TEST(NAME) \
    errors += tclist_ ## NAME ## _test()


/*************************************************************************/

enum {
    UNCACHED = 0,
    CACHED   = 1
};

TC_TEST_BEGIN(U_just_init, UNCACHED)
    TC_TEST_IS_TRUE(tc_list_size(&L) == 0);
TC_TEST_END

TC_TEST_BEGIN(U_append, UNCACHED)
    long num = 42;
    TC_TEST_IS_TRUE(tc_list_append(&L, &num) == TC_OK);
    TC_TEST_IS_TRUE(tc_list_size(&L) == 1);
TC_TEST_END

TC_TEST_BEGIN(U_append_get, UNCACHED)
    long num = 42, *res;
    TC_TEST_IS_TRUE(tc_list_append(&L, &num) == TC_OK);
    TC_TEST_IS_TRUE(tc_list_size(&L) == 1);
    res = tc_list_get(&L, 0);
    TC_TEST_IS_TRUE(&num == res);
TC_TEST_END

TC_TEST_BEGIN(U_prepend_get, UNCACHED)
    long num = 42, *res;
    TC_TEST_IS_TRUE(tc_list_prepend(&L, &num) == TC_OK);
    TC_TEST_IS_TRUE(tc_list_size(&L) == 1);
    res = tc_list_get(&L, 0);
    TC_TEST_IS_TRUE(&num == res);
TC_TEST_END

TC_TEST_BEGIN(U_appendN_get, UNCACHED)
    long *res, nums[] = { 23, 42, 18, 75, 73, 99, 14, 29 };
    int i = 0, len = sizeof(nums)/sizeof(nums[0]);
    for (i = 0; i < len; i++) {
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(tc_list_append(&L, &(nums[i])) == TC_OK);
    }
    TC_TEST_UNSET_STEP;
    TC_TEST_IS_TRUE(tc_list_size(&L) == len);
    res = tc_list_get(&L, 0);
    TC_TEST_IS_TRUE(&(nums[0]) == res);
TC_TEST_END

TC_TEST_BEGIN(U_prependN_get, UNCACHED)
    long *res, nums[] = { 23, 42, 18, 75, 73, 99, 14, 29 };
    int i = 0, len = sizeof(nums)/sizeof(nums[0]);
    for (i = 0; i < len; i++) {
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(tc_list_prepend(&L, &(nums[i])) == TC_OK);
    }
    TC_TEST_UNSET_STEP;
    TC_TEST_IS_TRUE(tc_list_size(&L) == len);
    res = tc_list_get(&L, 0);
    TC_TEST_IS_TRUE(&(nums[len-1]) == res);
TC_TEST_END

TC_TEST_BEGIN(U_appendN_getN, UNCACHED)
    long *res, nums[] = { 23, 42, 18, 75, 73, 99, 14, 29 };
    int i = 0, len = sizeof(nums)/sizeof(nums[0]);
    for (i = 0; i < len; i++) {
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(tc_list_append(&L, &(nums[i])) == TC_OK);
    }
    TC_TEST_UNSET_STEP;
    TC_TEST_IS_TRUE(tc_list_size(&L) == len);
    for (i = 0; i < len; i++) {
        res = tc_list_get(&L, i);
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(&(nums[i]) == res);
    }
TC_TEST_END

TC_TEST_BEGIN(U_prependN_getN, UNCACHED)
    long *res, nums[] = { 23, 42, 18, 75, 73, 99, 14, 29 };
    int i = 0, len = sizeof(nums)/sizeof(nums[0]);
    for (i = 0; i < len; i++) {
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(tc_list_prepend(&L, &(nums[i])) == TC_OK);
    }
    TC_TEST_UNSET_STEP;
    TC_TEST_IS_TRUE(tc_list_size(&L) == len);
    for (i = 0; i < len; i++) {
        res = tc_list_get(&L, i);
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(&(nums[len-1-i]) == res);
    }
TC_TEST_END

TC_TEST_BEGIN(U_appendN_getN_Rev, UNCACHED)
    long *res, nums[] = { 23, 42, 18, 75, 73, 99, 14, 29 };
    int i = 0, len = sizeof(nums)/sizeof(nums[0]);
    for (i = 0; i < len; i++) {
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(tc_list_append(&L, &(nums[i])) == TC_OK);
    }
    TC_TEST_UNSET_STEP;
    TC_TEST_IS_TRUE(tc_list_size(&L) == len);
    for (i = 0; i < len; i++) {
        res = tc_list_get(&L, -1-i);
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(&(nums[len-1-i]) == res);
    }
TC_TEST_END


TC_TEST_BEGIN(U_prependN_getN_Rev, UNCACHED)
    long *res, nums[] = { 23, 42, 18, 75, 73, 99, 14, 29 };
    int i = 0, len = sizeof(nums)/sizeof(nums[0]);
    for (i = 0; i < len; i++) {
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(tc_list_prepend(&L, &(nums[i])) == TC_OK);
    }
    TC_TEST_UNSET_STEP;
    TC_TEST_IS_TRUE(tc_list_size(&L) == len);
    for (i = 0; i < len; i++) {
        res = tc_list_get(&L, -1-i);
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(&(nums[i]) == res);
    }
TC_TEST_END

TC_TEST_BEGIN(U_appendN_popN_First, UNCACHED)
    long *res, nums[] = { 23, 42, 18, 75, 73, 99, 14, 29 };
    int i = 0, len = sizeof(nums)/sizeof(nums[0]);
    for (i = 0; i < len; i++) {
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(tc_list_append(&L, &(nums[i])) == TC_OK);
    }
    TC_TEST_UNSET_STEP;
    i = 0;
    TC_TEST_IS_TRUE(len == tc_list_size(&L));
    while (tc_list_size(&L)) {
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE((len - i) == tc_list_size(&L));
        res = tc_list_pop(&L, 0);
        TC_TEST_IS_TRUE(&(nums[i]) == res);
        i++;
    }
    TC_TEST_UNSET_STEP;
    TC_TEST_IS_TRUE(0 == tc_list_size(&L));
TC_TEST_END

TC_TEST_BEGIN(U_appendN_popN_Last, UNCACHED)
    long *res, nums[] = { 23, 42, 18, 75, 73, 99, 14, 29 };
    int i = 0, len = sizeof(nums)/sizeof(nums[0]);
    for (i = 0; i < len; i++) {
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE(tc_list_append(&L, &(nums[i])) == TC_OK);
    }
    TC_TEST_UNSET_STEP;
    i = 0;
    TC_TEST_IS_TRUE(len == tc_list_size(&L));
    while (tc_list_size(&L)) {
        TC_TEST_SET_STEP(i);
        TC_TEST_IS_TRUE((len - i) == tc_list_size(&L));
        res = tc_list_pop(&L, -1);
        TC_TEST_IS_TRUE(&(nums[len-1-i]) == res);
        i++;
    }
    TC_TEST_UNSET_STEP;
    TC_TEST_IS_TRUE(0 == tc_list_size(&L));
TC_TEST_END




/*************************************************************************/

static int test_list_all(void)
{
    int errors = 0;


    TC_RUN_TEST(U_just_init);
    TC_RUN_TEST(U_append);
    TC_RUN_TEST(U_append_get);
    TC_RUN_TEST(U_prepend_get);
    TC_RUN_TEST(U_appendN_get);
    TC_RUN_TEST(U_prependN_get);
    TC_RUN_TEST(U_appendN_getN);
    TC_RUN_TEST(U_prependN_getN);
    TC_RUN_TEST(U_appendN_getN_Rev);
    TC_RUN_TEST(U_prependN_getN_Rev);
    TC_RUN_TEST(U_appendN_popN_First);
    TC_RUN_TEST(U_appendN_popN_Last);

    return errors;
}

int main(int argc, char *argv[])
{
    int errors = 0;
    
    libtc_init(&argc, &argv);
    
    errors = test_list_all();

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
