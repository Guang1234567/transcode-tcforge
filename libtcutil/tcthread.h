/*
 * tcthread.h -- simple thread abstraction for transcode.
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


#ifndef TCTHREAD_H
#define TCTHREAD_H

#include "config.h"

#include <pthread.h>
#include <stdint.h>


/*
 * Quick Summary:
 * DOCME
 *
 */

enum {
    TC_THREAD_NAME_LEN = 16
};


typedef struct tcthreaddata_ TCThreadData;
struct tcthreaddata_ {
    char name[TC_THREAD_NAME_LEN];
};

typedef int (*TCThreadBodyFn)(TCThreadData *td, void *datum);

typedef struct tcmutex_ TCMutex;
struct tcmutex_ {
    pthread_mutex_t m;
};

typedef struct tccondition_ TCCondition;
struct tccondition_ {
    pthread_cond_t c;
};

typedef struct tcthread_ TCThread;
struct tcthread_ {
    pthread_t       tid;

    TCThreadData    data;
    TCThreadBodyFn  body;
    void            *arg;

    TCMutex         lock;
    int             retvalue;
};

/*
 * TCThread API in a nutshell:
 *
 *
 */

int tc_thread_init(TCThread *th, const char *name);
int tc_thread_start(TCThread *th, TCThreadBodyFn body, void *arg);
int tc_thread_wait(TCThread *th, int *th_ret);

int tc_mutex_init(TCMutex *m);
int tc_mutex_lock(TCMutex *m);
int tc_mutex_unlock(TCMutex *m);

int tc_condition_init(TCCondition *c);
int tc_condition_wait(TCCondition *c, TCMutex *m);
int tc_condition_signal(TCCondition *c);
int tc_condition_broadcast(TCCondition *c);



#endif /* TCTHREAD_H */
