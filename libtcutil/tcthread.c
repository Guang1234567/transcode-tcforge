/*
 * tcthread.c -- simple thread abstraction for transcode.
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

#include "common.h"
#include "strutils.h"
#include "logging.h"
#include "tcthread.h"



/*************************************************************************/
// FIXME: doc
static void *tc_thread_wrapper(void *arg)
{
    TCThread *th = arg;
    TCThreadData *td = &(th->data);

    tc_debug(TC_DEBUG_THREADS,
             "(%s) thread start", td->name);

    th->retvalue = th->body(td, th->arg);

    tc_debug(TC_DEBUG_THREADS,
             "(%s) thread end retvalue=%i",
             td->name, th->retvalue);

    return NULL;
}

int tc_thread_init(TCThread *th, const char *name)
{
    int ret = TC_ERROR;

    if (th && name) {
        strlcpy(th->data.name, name, sizeof(th->data.name));
        ret = TC_OK;
    }

    return ret;
}

int tc_thread_start(TCThread *th, TCThreadBodyFn body, void *arg)
{
    int ret = TC_ERROR, err = 0;
    if (th && body) {
        th->body     = body;
        th->arg      = arg;
        th->retvalue = 0;

        tc_mutex_init(&(th->lock));

        err = pthread_create(&(th->tid), NULL, tc_thread_wrapper, th);
        ret = TC_OK;
    }
    return ret;
}

int tc_thread_wait(TCThread *th, int *th_ret)
{
    int ret = TC_ERROR;
    if (th) {
        tc_debug(TC_DEBUG_THREADS,
                 "(%s) waiting for thread: (%s)",
                 __FILE__, th->data.name);

        pthread_join(th->tid, NULL);

        tc_debug(TC_DEBUG_THREADS,
                 "(%s) thread joined: (%s)",
                 __FILE__, th->data.name);

        if (th_ret) {
            *th_ret = th->retvalue;
        }
        ret = TC_OK;
    }
    return ret;
}

/*************************************************************************/

int tc_mutex_init(TCMutex *m)
{
    return pthread_mutex_init(&(m->m), NULL);
}

int tc_mutex_lock(TCMutex *m)
{
    return pthread_mutex_lock(&(m->m));
}

int tc_mutex_unlock(TCMutex *m)
{
    return pthread_mutex_unlock(&(m->m));
}

/*************************************************************************/

int tc_condition_init(TCCondition *c)
{
    return pthread_cond_init(&(c->c), NULL);
}

int tc_condition_wait(TCCondition *c, TCMutex *m)
{
    return pthread_cond_wait(&(c->c), &(m->m));
}

int tc_condition_signal(TCCondition *c)
{
    return pthread_cond_signal(&(c->c));
}

int tc_condition_broadcast(TCCondition *c)
{
    return pthread_cond_broadcast(&(c->c));
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

