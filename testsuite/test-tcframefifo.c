/*
 * test-tcframefifo.c -- testsuite for TCFrameFifo code; 
 *                       everyone feel free to add more tests and improve
 *                       existing ones.
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
#include "src/framebuffer.h"

/*************************************************************************/

#define TC_TEST_BEGIN(NAME, SIZE, PRIORITY) \
static int tcframequeue_ ## NAME ## _test(void) \
{ \
    const char *TC_TEST_name = # NAME ; \
    const char *TC_TEST_errmsg = ""; \
    int TC_TEST_step = -1; \
    int TC_TEST_dump = TC_FALSE; \
    \
    TCFrameQueue *Q = NULL; \
    \
    tc_log_info(__FILE__, "running test: [%s]", # NAME); \
    Q = tc_frame_queue_new((SIZE), (PRIORITY)); \
    if (Q) {


#define TC_TEST_END \
        if (TC_TEST_dump) { \
            tc_frame_queue_dump_status(Q, TC_TEST_name); \
        } \
        tc_frame_queue_del(Q); \
        return 0; \
    } \
TC_TEST_failure: \
    if (TC_TEST_step != -1) { \
        tc_log_warn(__FILE__, "FAILED test [%s] at step %i", TC_TEST_name, TC_TEST_step); \
    } \
    tc_log_warn(__FILE__, "FAILED test [%s] NOT verified: %s", TC_TEST_name, TC_TEST_errmsg); \
    if (TC_TEST_dump) { \
        tc_frame_queue_dump_status(Q, TC_TEST_name); \
    } \
    return 1; \
}

#define TC_TEST_SET_DUMP(DUMP) do { \
    TC_TEST_dump = (DUMP); \
} while (0);

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
    errors += tcframequeue_ ## NAME ## _test()

#define TCFRAMEPTR_IS_NULL(ptr) (ptr.generic == NULL)

/*************************************************************************/

enum {
    UNPRIORITY = 0,
    PRIORITY   = 1,
    QUEUESIZE = 10
};

static void init_frames(int num, frame_list_t *frames, TCFramePtr *ptrs)
{
    frame_list_t *cur = NULL;
    int i;

    for (i = 0; i < num; i++) {
        cur = &(frames[i]);
        memset(cur, 0, sizeof(frame_list_t));

        cur->bufid      = i;
        cur->id         = i;
        ptrs[i].generic = cur;
#if 0
        tc_log_info("IF",
                    "(%p) ptr[%i].generic->id=%i",
                    ptrs[i].generic, i, ptrs[i].generic->id);
#endif                    
    }
}

#if 0
static void dump_frames(int num, TCFramePtr *ptrs)
{
    int i;

    for (i = 0; i < num && ptrs[i].generic; i++) {
        tc_log_info("DD",
                    "(%p) ptr[%i].generic->id=%i",
                    ptrs[i].generic, i, ptrs[i].generic->id);
    }
}
#endif


TC_TEST_BEGIN(U_init_empty, QUEUESIZE, UNPRIORITY)
    TC_TEST_IS_TRUE(tc_frame_queue_empty(Q));
    TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == 0);
TC_TEST_END

TC_TEST_BEGIN(U_get1, QUEUESIZE, UNPRIORITY)
    TCFramePtr fp = { .generic = NULL };

    TC_TEST_IS_TRUE(tc_frame_queue_empty(Q));

    fp = tc_frame_queue_get(Q);
    TC_TEST_IS_TRUE(TCFRAMEPTR_IS_NULL(fp));
    TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == 0);
TC_TEST_END


TC_TEST_BEGIN(U_put1, QUEUESIZE, UNPRIORITY)
    frame_list_t frame[1];
    TCFramePtr ptr[1];

    int wakeup = 0;
    init_frames(1, frame, ptr);
    
    wakeup = tc_frame_queue_put(Q, ptr[0]);
    TC_TEST_IS_TRUE(wakeup);
    TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == 1);
TC_TEST_END

TC_TEST_BEGIN(U_put1_get1, QUEUESIZE, UNPRIORITY)
    frame_list_t frame[1];
    TCFramePtr ptr[1];
    TCFramePtr fp = { .generic = NULL };

    int wakeup = 0;
    init_frames(1, frame, ptr);
    
    wakeup = tc_frame_queue_put(Q, ptr[0]);
    TC_TEST_IS_TRUE(wakeup);
    TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == 1);

    fp = tc_frame_queue_get(Q);
    TC_TEST_IS_TRUE(!TCFRAMEPTR_IS_NULL(fp));
    TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == 0);
TC_TEST_END

/*************************************************************************/

TC_TEST_BEGIN(S_init_empty, QUEUESIZE, PRIORITY)
    TC_TEST_IS_TRUE(tc_frame_queue_empty(Q));
    TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == 0);
    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END

TC_TEST_BEGIN(S_get1, QUEUESIZE, PRIORITY)
    TCFramePtr fp = { .generic = NULL };

    TC_TEST_IS_TRUE(tc_frame_queue_empty(Q));
    TC_TEST_IS_TRUE(is_heap(Q, 0));

    fp = tc_frame_queue_get(Q);
    TC_TEST_IS_TRUE(TCFRAMEPTR_IS_NULL(fp));
    TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == 0);

    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END


TC_TEST_BEGIN(S_put1, QUEUESIZE, PRIORITY)
    frame_list_t frame[1];
    TCFramePtr ptr[1];

    int wakeup = 0;
    init_frames(1, frame, ptr);
    
    TC_TEST_IS_TRUE(ptr[0].generic->id == 0);
    wakeup = tc_frame_queue_put(Q, ptr[0]);
    TC_TEST_IS_TRUE(wakeup);
    TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == 1);

    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END

TC_TEST_BEGIN(S_put1_get1, QUEUESIZE, PRIORITY)
    frame_list_t frame[1];
    TCFramePtr ptr[1];
    TCFramePtr fp = { .generic = NULL };

    int wakeup = 0;
    init_frames(1, frame, ptr);
    
    TC_TEST_IS_TRUE(ptr[0].generic->id == 0);
    wakeup = tc_frame_queue_put(Q, ptr[0]);
    TC_TEST_IS_TRUE(wakeup);
    TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == 1);
    TC_TEST_IS_TRUE(is_heap(Q, 0));

    fp = tc_frame_queue_get(Q);
    TC_TEST_IS_TRUE(!TCFRAMEPTR_IS_NULL(fp));
    TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == 0);
    TC_TEST_IS_TRUE(fp.generic->id == 0);
    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END

TC_TEST_BEGIN(S_put4, QUEUESIZE, PRIORITY)
    frame_list_t frame[4];
    TCFramePtr ptr[4];
    int i = 0;

    int wakeup = 0;
    init_frames(4, frame, ptr);

    for (i = 0; i < 4; i++) {
        TC_TEST_IS_TRUE(ptr[i].generic->id == i);
        wakeup = tc_frame_queue_put(Q, ptr[i]);
        TC_TEST_IS_TRUE(wakeup);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (i+1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END

TC_TEST_BEGIN(S_put4_rev, QUEUESIZE, PRIORITY)
    frame_list_t frame[4];
    TCFramePtr ptr[4];
    int i = 0;

    int wakeup = 0;
    init_frames(4, frame, ptr);
    
    for (i = 0; i < 4; i++) {
        wakeup = tc_frame_queue_put(Q, ptr[4-i-1]);
        TC_TEST_IS_TRUE(wakeup);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (i+1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END

TC_TEST_BEGIN(S_putMax, QUEUESIZE, PRIORITY)
    frame_list_t frame[QUEUESIZE];
    TCFramePtr ptr[QUEUESIZE];
    int i = 0;

    int wakeup = 0;
    init_frames(QUEUESIZE, frame, ptr);
    
    for (i = 0; i < QUEUESIZE; i++) {
        wakeup = tc_frame_queue_put(Q, ptr[i]);
        TC_TEST_IS_TRUE(wakeup);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (i+1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END


TC_TEST_BEGIN(S_putMax_rev, QUEUESIZE, PRIORITY)
    frame_list_t frame[QUEUESIZE];
    TCFramePtr ptr[QUEUESIZE];
    int i = 0;

    int wakeup = 0;
    init_frames(QUEUESIZE, frame, ptr);
    
    for (i = 0; i < QUEUESIZE; i++) {
        wakeup = tc_frame_queue_put(Q, ptr[QUEUESIZE-i-1]);
        TC_TEST_IS_TRUE(wakeup);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (i+1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END



TC_TEST_BEGIN(S_put4_get2, QUEUESIZE, PRIORITY)
    frame_list_t frame[4];
    TCFramePtr ptr[4];
    TCFramePtr fp = { .generic = NULL };
    int i = 0;

    int wakeup = 0;
    init_frames(4, frame, ptr);
    
    for (i = 0; i < 4; i++) {
        wakeup = tc_frame_queue_put(Q, ptr[i]);
        TC_TEST_IS_TRUE(wakeup);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (i+1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    for (i = 0; i < 2; i++) {
        fp = tc_frame_queue_get(Q);
        TC_TEST_IS_TRUE(!TCFRAMEPTR_IS_NULL(fp));
        TC_TEST_IS_TRUE(fp.generic->id == i);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (4-i-1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END


TC_TEST_BEGIN(S_put5_get5, QUEUESIZE, PRIORITY)
    frame_list_t frame[5];
    TCFramePtr ptr[5];
    TCFramePtr fp = { .generic = NULL };
    int i = 0;

    int wakeup = 0;
    init_frames(5, frame, ptr);
    
    for (i = 0; i < 5; i++) {
        wakeup = tc_frame_queue_put(Q, ptr[i]);
        TC_TEST_IS_TRUE(wakeup);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (i+1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    for (i = 0; i < 5; i++) {
        fp = tc_frame_queue_get(Q);
        TC_TEST_IS_TRUE(!TCFRAMEPTR_IS_NULL(fp));
        TC_TEST_IS_TRUE(fp.generic->id == i);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (5-i-1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END

TC_TEST_BEGIN(S_put5_get5_rev, QUEUESIZE, PRIORITY)
    frame_list_t frame[5];
    TCFramePtr ptr[5];
    TCFramePtr fp = { .generic = NULL };
    int i = 0;

    int wakeup = 0;
    init_frames(5, frame, ptr);
    
    for (i = 0; i < 5; i++) {
        wakeup = tc_frame_queue_put(Q, ptr[5-i-1]);
        TC_TEST_IS_TRUE(wakeup);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (i+1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }
    
    for (i = 0; i < 5; i++) {
        fp = tc_frame_queue_get(Q);
        TC_TEST_IS_TRUE(!TCFRAMEPTR_IS_NULL(fp));
        TC_TEST_IS_TRUE(fp.generic->id == i);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (5-i-1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }
    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END

TC_TEST_BEGIN(S_putMax_getMax, QUEUESIZE, PRIORITY)
    frame_list_t frame[QUEUESIZE];
    TCFramePtr ptr[QUEUESIZE];
    TCFramePtr fp = { .generic = NULL };
    int i = 0;

    int wakeup = 0;
    init_frames(QUEUESIZE, frame, ptr);
    
    for (i = 0; i < QUEUESIZE; i++) {
        wakeup = tc_frame_queue_put(Q, ptr[i]);
        TC_TEST_IS_TRUE(wakeup);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (i+1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    for (i = 0; i < QUEUESIZE; i++) {
        fp = tc_frame_queue_get(Q);
        TC_TEST_IS_TRUE(!TCFRAMEPTR_IS_NULL(fp));
        TC_TEST_IS_TRUE(fp.generic->id == i);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (QUEUESIZE-i-1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END

TC_TEST_BEGIN(S_putMax_getMax_rev, QUEUESIZE, PRIORITY)
    frame_list_t frame[QUEUESIZE];
    TCFramePtr ptr[QUEUESIZE];
    TCFramePtr fp = { .generic = NULL };
    int i = 0;

    int wakeup = 0;
    init_frames(QUEUESIZE, frame, ptr);
    
    for (i = 0; i < QUEUESIZE; i++) {
        wakeup = tc_frame_queue_put(Q, ptr[QUEUESIZE - i - 1]);
        TC_TEST_IS_TRUE(wakeup);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (i+1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    for (i = 0; i < QUEUESIZE; i++) {
        fp = tc_frame_queue_get(Q);
        TC_TEST_IS_TRUE(!TCFRAMEPTR_IS_NULL(fp));
        TC_TEST_IS_TRUE(fp.generic->id == i);
        TC_TEST_IS_TRUE(tc_frame_queue_size(Q) == (QUEUESIZE-i-1));
        TC_TEST_IS_TRUE(is_heap(Q, 0));
    }

    TC_TEST_IS_TRUE(is_heap(Q, 0));
TC_TEST_END



/*************************************************************************/

static int test_frame_queue_all(void)
{
    int errors = 0;

    TC_RUN_TEST(U_init_empty);
    TC_RUN_TEST(U_get1);
    TC_RUN_TEST(U_put1);
    TC_RUN_TEST(U_put1_get1);

    TC_RUN_TEST(S_init_empty);
    TC_RUN_TEST(S_get1);
    TC_RUN_TEST(S_put1);
    TC_RUN_TEST(S_put1_get1);
    TC_RUN_TEST(S_put4);
    TC_RUN_TEST(S_put4_rev);
    TC_RUN_TEST(S_putMax);
    TC_RUN_TEST(S_putMax_rev);
    TC_RUN_TEST(S_put4_get2);
    TC_RUN_TEST(S_put5_get5);
    TC_RUN_TEST(S_put5_get5_rev);
    TC_RUN_TEST(S_putMax_getMax);
    TC_RUN_TEST(S_putMax_getMax_rev);

    return errors;
}

/* stubs */
int verbose = TC_INFO;
int tc_running(void);
int tc_running(void)
{
    return TC_TRUE;
}

int main(int argc, char *argv[])
{
    int errors = 0;
    
    libtc_init(&argc, &argv);

    if (argc == 2) {
        verbose = atoi(argv[1]);
    }

    errors = test_frame_queue_all();

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
