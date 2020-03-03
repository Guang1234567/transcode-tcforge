/*
 * framebuffer.c -- audio/video frame ringbuffers, reloaded. Again.
 * (C) 2005-2010 - Francesco Romani <fromani -at- gmail -dot- com>
 * Based on code
 * (C) 2001-2006 - Thomas Oestreich.
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

#include "libtcutil/tcthread.h"

#include "tccore/tc_defaults.h"
#include "tccore/runcontrol.h"
#include "transcode.h"
#include "framebuffer.h"

#include "libtc/tcframes.h"
#include "libtc/ratiocodes.h"

/* unit testing needs this */
#ifndef FBUF_TEST
#define STATIC  static
#else
#define STATIC 
#endif

/* layer ids for debugging/human consumption, from inner to outer */
#define FPOOL_NAME  "framepool"
#define FRING_NAME  "framering"
#define FRBUF_NAME  "framebuffer"
#define MOD_NAME    FRBUF_NAME

#define PTHREAD_ID  ((unsigned)pthread_self())

/*
 * Summary:
 * This code acts as generic ringbuffer implementation, with
 * specializations for main (audio and video) ringbufffers
 * in order to cope legacy constraints from 1.0.x series.
 * It replaces former src/{audio,video}_buffer.c in (hopefully!)
 * a more generic, clean, maintanable, compact way.
 * 
 * Please note that there is *still* some other ringbuffer
 * scatthered through codebase (subtitle buffer,d emux buffers,
 * possibly more). They will be merged lately or will be dropped
 * or reworked.
 */

/*************************************************************************/
/* frame processing stages. `Locked' stage is now ignored.               */
/*************************************************************************/

#define TC_FRAME_STAGE_ID(ST)   ((ST) + 1)
#define TC_FRAME_STAGE_ST(ID)   ((ID) - 1)
#define TC_FRAME_STAGE_NUM      (TC_FRAME_STAGE_ID(TC_FRAME_READY) + 1)

struct stage {
    TCFrameStatus   status;
    const char      *name;
    int             broadcast;
};

static const struct stage frame_stages[] = {
    { TC_FRAME_NULL,    "null",     TC_FALSE },
    { TC_FRAME_EMPTY,   "empty",    TC_FALSE },
    { TC_FRAME_WAIT,    "wait",     TC_TRUE  },
    { TC_FRAME_LOCKED,  "locked",   TC_TRUE  }, /* legacy */
    { TC_FRAME_READY,   "ready",    TC_FALSE },
};

STATIC const char *frame_status_name(TCFrameStatus S)
{
    int i = TC_FRAME_STAGE_ID(S);
    return frame_stages[i].name;
}

/*************************************************************************/
/* frame spec(ification)s. How big those framebuffer should be?          */
/*************************************************************************/

/* 
 * Specs used internally. I don't export this structure directly
 * because I want to be free to change it if needed
 */
static TCFrameSpecs tc_specs = {
    /* Largest supported values, to ensure the buffer is always big enough
     * (see FIXME in tc_framebuffer_set_specs()) */
    .frc      = 3,  // PAL, why not
    .width    = TC_MAX_V_FRAME_WIDTH,
    .height   = TC_MAX_V_FRAME_HEIGHT,
    .format   = TC_CODEC_RGB24,
    .rate     = RATE,
    .channels = CHANNELS,
    .bits     = BITS,
    .samples  = 48000.0,
};

const TCFrameSpecs *tc_framebuffer_get_specs(void)
{
    return &tc_specs;
}

/* 
 * we compute (ahead of time) samples value for later usage.
 */
void tc_framebuffer_set_specs(const TCFrameSpecs *specs)
{
    /* silently ignore NULL specs */
    if (specs != NULL) {
        double fps;

        /* raw copy first */
        ac_memcpy(&tc_specs, specs, sizeof(TCFrameSpecs));

        /* restore width/height/bpp
         * (FIXME: temp until we have a way to know the max size that will
         *         be used through the decode/process/encode chain; without
         *         this, -V yuv420p -y raw -F rgb (e.g.) crashes with a
         *         buffer overrun)
         */
        tc_specs.width  = TC_MAX_V_FRAME_WIDTH;
        tc_specs.height = TC_MAX_V_FRAME_HEIGHT;
        tc_specs.format = TC_CODEC_RGB24;

        /* then deduct missing parameters */
        if (tc_frc_code_to_value(tc_specs.frc, &fps) == TC_NULL_MATCH) {
            fps = 1.0; /* sane, very worst case value */
        }
/*        tc_specs.samples = (double)tc_specs.rate/fps; */
        tc_specs.samples = (double)tc_specs.rate;
        /* 
         * FIXME
         * ok, so we use a MUCH larger buffer (big enough to store 1 *second*
         * of raw audio, not 1 *frame*) than needed for reasons similar as 
         * seen for above video.
         * Most notably, this helps in keeping buffers large enough to be
         * suitable for encoder flush (see encode_lame.c first).
         */
    }
}

/*************************************************************************/
/* Frame allocation/disposal helpers; those are effectively just thin    */
/* wrappers around libtc facilities. They are just interface adapters.   */
/*************************************************************************/

#define TCFRAMEPTR_IS_NULL(tcf)    (tcf.generic == NULL)

static TCFramePtr tc_video_alloc(const TCFrameSpecs *specs)
{
    TCFramePtr frame;
    /* NOTE: The temporary frame buffer is _required_ (hence TC_FALSE)
     *       if any video transformations (-j, -Z, etc.) are used! */
    frame.video = tc_new_video_frame(specs->width, specs->height,
                                      specs->format, TC_FALSE);
    return frame;
}

static TCFramePtr tc_audio_alloc(const TCFrameSpecs *specs)
{
    TCFramePtr frame;
    frame.audio = tc_new_audio_frame(specs->samples, specs->channels,
                                     specs->bits);
    return frame;
}

static void tc_video_free(TCFramePtr frame)
{
    tc_del_video_frame(frame.video);
}

static void tc_audio_free(TCFramePtr frame)
{
    tc_del_audio_frame(frame.audio);
}

vframe_list_t *vframe_alloc_single(void)
{
    /* NOTE: The temporary frame buffer is _required_ (hence TC_FALSE)
     *       if any video transformations (-j, -Z, etc.) are used! */
    return tc_new_video_frame(tc_specs.width, tc_specs.height,
                              tc_specs.format, TC_FALSE);
}

aframe_list_t *aframe_alloc_single(void)
{
    return tc_new_audio_frame(tc_specs.samples, tc_specs.channels,
                              tc_specs.bits);
}

/*************************************************************************/

#ifndef FBUF_TEST
typedef struct tcframequeue_ TCFrameQueue;
#endif
struct tcframequeue_ {
    TCFramePtr  *frames;
    int         size;
    int         num;
    int         first;
    int         last;

    int         priority;
    TCFramePtr  (*get)(TCFrameQueue *Q);
    int         (*put)(TCFrameQueue *Q, TCFramePtr ptr);
};

STATIC void tc_frame_queue_dump_status(TCFrameQueue *Q, const char *tag)
{
    int i = 0;
    tc_log_msg(FPOOL_NAME, "(%s|queue|%s) size=%i num=%i first=%i last=%i",
               tag, (Q->priority) ?"HEAP" :"FIFO",
               Q->size, Q->num, Q->first, Q->last);

    for (i = 0; i < Q->size; i++) {
        frame_list_t *ptr = (i < Q->num) ?Q->frames[i].generic :NULL;
        /* small trick to avoid heap noise */

        tc_log_msg(FPOOL_NAME,
                   "(%s|queue) #%i ptr=%p (id=%i|bufid=%i|status=%s)",
                   tag, i, ptr,
                   (ptr) ?ptr->id    :-1,
                   (ptr) ?ptr->bufid :-1,
                   (ptr) ?frame_status_name(ptr->status) :"unknown");
    }
}

STATIC void tc_frame_queue_del(TCFrameQueue *Q)
{
    tc_free(Q);
}

STATIC int tc_frame_queue_empty(TCFrameQueue *Q)
{
    return (Q->num == 0) ?TC_TRUE :TC_FALSE;
}

STATIC int tc_frame_queue_size(TCFrameQueue *Q)
{
    return Q->num;
}

STATIC TCFramePtr tc_frame_queue_get(TCFrameQueue *Q)
{
    return Q->get(Q);
}

STATIC int tc_frame_queue_put(TCFrameQueue *Q, TCFramePtr ptr)
{
    return Q->put(Q, ptr);
}


static TCFramePtr fifo_get(TCFrameQueue *Q)
{
    TCFramePtr ptr = { .generic = NULL };
    if (Q->num > 0) {
        ptr = Q->frames[Q->first];
        Q->first = (Q->first + 1) % Q->size;
        Q->num--;
    }
    return ptr;
}

static int fifo_put(TCFrameQueue *Q, TCFramePtr ptr)
{
    int ret = 0;
    if (Q->num < Q->size) {
        Q->frames[Q->last] = ptr;
        Q->last = (Q->last + 1) % Q->size;
        Q->num++;
        ret = 1;
    }
    return ret;
}

/*
 * heap auxiliary functions work into the Key domain (K)
 * while heap main functions (Queue hierarchy) work into the
 * Position domain (P).
 * Of course Queue data is in P too.
 */

#define KEY(J)          ((J)+1)
#define POS(K)          ((K)-1)
#define PARENT(K)       ( (K) / 2)
#define LEFT_SON(K)     ( (K) * 2)
#define RIGHT_SON(K)    (((K) * 2) + 1)

#define FRAME_ID(Q, J)  ((Q)->frames[(J)].generic->id)
#define FRAME_SWAP(Q, Ja, Jb) do {         \
    TCFramePtr tmp = (Q)->frames[(Ja)];    \
    (Q)->frames[(Ja)] = (Q)->frames[(Jb)]; \
    (Q)->frames[(Jb)] = tmp;               \
} while (0);

#ifdef FBUF_TEST
int is_heap(TCFrameQueue *Q, int debug)
{
    int k, t, good = 1, N = KEY(Q->num - 1);

    if (debug) {
        tc_log_info("* is_heap", "N=%i Q->num=%i", N, Q->num);
        tc_frame_queue_dump_status(Q, "is_heap");
    }

    for (k = N; k > 1; k--) {
        if (debug) {
            tc_log_info("is_heap", "> k=%i(%i)", k, POS(k));
        }
        for (t = k; good && (t > 1 && PARENT(t) >= 1); t = PARENT(t)) {
            int P    = POS(t);
            int PP   = POS(PARENT(t));
            int pid  = FRAME_ID(Q, P);
            int ppid = FRAME_ID(Q, PP);

            if (pid < ppid) {
                good = 0;
            }
            if (debug || !good) {
                tc_log_info((good) ?"is_heap" :"HEAP_VIOLATION",
                            ">> t=%i(%i) parent=%i(%i) pid=%i ppid=%i",
                            t, P, PARENT(t), PP, pid, ppid);
                if (!good) {
                    tc_frame_queue_dump_status(Q, "HEAP_VIOLATION");
                }
            }
 
        }
    }
    return good;
}
#endif

static int pick_son(TCFrameQueue *Q, int k)
{
    int N = KEY(Q->num), L = LEFT_SON(k), R = RIGHT_SON(k);
    int ret = L;

    if ((R < N) && (FRAME_ID(Q, POS(R)) < FRAME_ID(Q, POS(L)))) {
        /* right son has to exist */
        ret = R;
    }
    return ret;
}

static void heap_down(TCFrameQueue *Q, int k)
{
    int S, N = KEY(Q->num);
    while (k < N) {
        int J = POS(k);
        S = POS(pick_son(Q, k));
        if ((S < N) && (FRAME_ID(Q, J) > FRAME_ID(Q, S))) {
            FRAME_SWAP(Q, J, S);
        } else {
            break;
        }
        k = KEY(S);
    }
}

static void heap_up(TCFrameQueue *Q, int k)
{
    for (; k > 1; k /= 2) {
        int J = POS(k), P = POS(PARENT(k));
        if (FRAME_ID(Q, J) < FRAME_ID(Q, P)) {
            FRAME_SWAP(Q, J, P);
        }
    }
}


static TCFramePtr heap_get(TCFrameQueue *Q)
{
    TCFramePtr ptr = { .generic = NULL };
    if (Q->num > 0) {
        ptr = Q->frames[0];
        Q->num--; /* must overwrite the last one */
        Q->frames[0] =  Q->frames[Q->num];
        /* *** */
        heap_down(Q, KEY(0));
    }
    return ptr;
}

static int heap_put(TCFrameQueue *Q, TCFramePtr ptr)
{
    int ret = 0;
    if (Q->num < Q->size) {
        int last = Q->num;
        Q->frames[last] = ptr;
        Q->num++;
        ret = 1;
        /* *** */
        heap_up(Q, KEY(last));
    }
    return ret;
}

STATIC TCFrameQueue *tc_frame_queue_new(int size, int priority)
{
    TCFrameQueue *Q = NULL;
    uint8_t *mem = NULL;

    mem = tc_zalloc(sizeof(TCFrameQueue) + (sizeof(TCFramePtr) * size));
    if (mem) {
        Q           = (TCFrameQueue *)mem;
        Q->frames   = (TCFramePtr *)(mem + sizeof(TCFrameQueue));
        Q->size     = size;
        Q->priority = priority;
        if (priority) {
            Q->get       = heap_get;
            Q->put       = heap_put;
        } else {
            Q->get       = fifo_get;
            Q->put       = fifo_put;
        }
    }
    return Q;
}

/*************************************************************************/

#ifndef FBUF_TEST
typedef struct tcframepool_ TCFramePool;
#endif
struct tcframepool_ {
    const char   *ptag;      /* given from ringbuffer */
    const char   *tag;

    TCMutex      lock;
    TCCondition  empty;
    int          waiting;    /* how many thread blocked here? */

    TCFrameQueue *queue;
};

STATIC int tc_frame_pool_init(TCFramePool *P, int size, int priority,
                              const char *tag, const char *ptag)
{
    int ret = TC_ERROR;
    if (P) {
        tc_mutex_init(&P->lock);
        tc_condition_init(&P->empty);

        P->ptag     = (ptag) ?ptag :"unknown";
        P->tag      = (tag)  ?tag  :"unknown";
        P->waiting  = 0;
        P->queue     = tc_frame_queue_new(size, priority);
        if (P->queue) {
            ret = TC_OK;
        }
    }
    return ret;
}

STATIC int tc_frame_pool_fini(TCFramePool *P)
{
    if (P && P->queue) {
        tc_frame_queue_del(P->queue);
        P->queue = NULL;
    }
    return TC_OK;
}

STATIC void tc_frame_pool_dump_status(TCFramePool *P)
{
    tc_log_msg(FPOOL_NAME, "(%s|%s) waiting=%i fifo status:",
               P->ptag, P->tag, P->waiting);
    tc_frame_queue_dump_status(P->queue, P->tag);
}

STATIC void tc_frame_pool_put_frame(TCFramePool *P, TCFramePtr ptr)
{
    int wakeup = 0;
    tc_mutex_lock(&P->lock);
    wakeup = tc_frame_queue_put(P->queue, ptr);

    tc_debug(TC_DEBUG_FLIST,
             "(%s|put_frame|%s|%s|0x%X) wakeup=%i waiting=%i",
             FPOOL_NAME,
             P->tag, P->ptag, PTHREAD_ID, wakeup, P->waiting);

    if (P->waiting && wakeup) {
        tc_condition_signal(&P->empty);
    }

    tc_mutex_unlock(&P->lock);
}

STATIC TCFramePtr tc_frame_pool_get_frame(TCFramePool *P)
{
    int interrupted = TC_FALSE;

    TCFramePtr ptr = { .generic = NULL };
    tc_mutex_lock(&P->lock);

    tc_debug(TC_DEBUG_FLIST,
             "(%s|get_frame|%s|%s|0x%X) requesting frame",
             FPOOL_NAME,
             P->tag, P->ptag, PTHREAD_ID);

    P->waiting++;
    while (!interrupted && tc_frame_queue_empty(P->queue)) {
        tc_debug(TC_DEBUG_THREADS,
                 "(%s|get_frame|%s|%s|0x%X) blocking (no frames in pool)",
                 FPOOL_NAME,
                 P->tag, P->ptag, PTHREAD_ID);

        tc_condition_wait(&P->empty, &P->lock);

        tc_debug(TC_DEBUG_FLIST,
                 "(%s|get_frame|%s|%s|0x%X) UNblocking",
                 FPOOL_NAME,
                 P->tag, P->ptag, PTHREAD_ID);

        interrupted = !tc_running();
    }
    P->waiting--;

    if (!interrupted) {
        ptr = tc_frame_queue_get(P->queue);
    }

    tc_debug(TC_DEBUG_FLIST,
             "(%s|got_frame|%s|%s|0x%X) frame=%p #%i",
             FPOOL_NAME,
             P->tag, P->ptag, PTHREAD_ID,
             ptr.generic,
             (ptr.generic) ?ptr.generic->bufid :(-1));

    tc_mutex_unlock(&P->lock);
    return ptr;
}

/* to be used ONLY in safe places like init, fini, flush */
STATIC TCFramePtr tc_frame_pool_pull_frame(TCFramePool *P)
{
    return tc_frame_queue_get(P->queue);
}

/* ditto */
STATIC void tc_frame_pool_push_frame(TCFramePool *P, TCFramePtr ptr)
{
    tc_frame_queue_put(P->queue, ptr);
}

STATIC void tc_frame_pool_wakeup(TCFramePool *P, int broadcast)
{
    tc_mutex_lock(&P->lock);
    if (broadcast) {
        tc_condition_broadcast(&P->empty);
    } else {
        tc_condition_signal(&P->empty);
    }
    tc_mutex_unlock(&P->lock);
}

/*************************************************************************
 * Layered, custom allocator/disposer for ringbuffer structures.
 * The idea is to simplify (from ringbuffer viewpoint!) frame
 * allocation/disposal and to make it as much generic as is possible
 * (avoid if()s and so on).
 *************************************************************************/

typedef TCFramePtr (*TCFrameAllocFn)(const TCFrameSpecs *);
typedef void       (*TCFrameFreeFn)(TCFramePtr);

/*************************************************************************/

typedef struct tcframering_ TCFrameRing;
struct tcframering_ {
    const char          *tag;

    TCFramePtr          *frames; /* main frame references */
    int                 size;    /* how many of them? */

    TCFramePool         pools[TC_FRAME_STAGE_NUM];

    const TCFrameSpecs  *specs;  /* what we need here? */
    /* (de)allocation helpers */
    TCFrameAllocFn      alloc;
    TCFrameFreeFn       free;
};

static TCFrameRing tc_audio_ringbuffer;
static TCFrameRing tc_video_ringbuffer;

/*************************************************************************/

static TCFramePool *tc_frame_ring_get_pool(TCFrameRing *rfb,
                                           TCFrameStatus S)
{
    return &(rfb->pools[TC_FRAME_STAGE_ID(S)]);
}

/* sometimes the lock is taken on the upper layer */
static int tc_frame_ring_get_pool_size(TCFrameRing *rfb,
                                       TCFrameStatus S,
                                       int locked)
{
    int size;
    TCFramePool *P = tc_frame_ring_get_pool(rfb, S);
    if (locked) {
        tc_mutex_lock(&P->lock);
    }
    size = tc_frame_queue_size(P->queue);
    if (locked) {
        tc_mutex_unlock(&P->lock);
    }
    return size;
}


static void tc_frame_ring_put_frame(TCFrameRing *rfb,
                                    TCFrameStatus S,
                                    TCFramePtr ptr)
{
    TCFramePool *P = tc_frame_ring_get_pool(rfb, S);
    ptr.generic->status = S;
    tc_frame_pool_put_frame(P, ptr);
}

static TCFramePtr tc_frame_ring_get_frame(TCFrameRing *rfb,
                                          TCFrameStatus S)
{
    TCFramePool *P = tc_frame_ring_get_pool(rfb, S);
    return tc_frame_pool_get_frame(P);
}

static void tc_frame_ring_dump_status(TCFrameRing *rfb,
                                      const char *id)
{
    tc_debug(TC_DEBUG_FLIST,
             "(%s|%s|%s|0x%X) frame status: null=%i empty=%i wait=%i"
             " locked=%i ready=%i",
             FRBUF_NAME, id, rfb->tag, PTHREAD_ID,
             tc_frame_ring_get_pool_size(rfb, TC_FRAME_NULL,   TC_FALSE),
             tc_frame_ring_get_pool_size(rfb, TC_FRAME_EMPTY,  TC_FALSE),
             tc_frame_ring_get_pool_size(rfb, TC_FRAME_WAIT,   TC_FALSE),
             tc_frame_ring_get_pool_size(rfb, TC_FRAME_LOCKED, TC_FALSE), /* legacy */
             tc_frame_ring_get_pool_size(rfb, TC_FRAME_READY,  TC_FALSE));
}


/*************************************************************************/
/* NEW API, yet private                                                  */
/*************************************************************************/

/*
 * tc_frame_ring_init: (NOT thread safe)
 *     initialize a framebuffer ringbuffer by allocating needed
 *     amount of frames using given parameters.
 *
 * Parameters:
 *       rfb: ring framebuffer structure to initialize.
 *     specs: frame specifications to use for allocation.
 *     alloc: frame allocation function to use.
 *      free: frame disposal function to use.
 *      size: size of ringbuffer (number of frame to allocate)
 * Return Value:
 *      > 0: wrong (NULL) parameters
 *        0: succesfull
 *      < 0: allocation failed for one or more framesbuffers/internal error
 */
static int tc_frame_ring_init(TCFrameRing *rfb,
                              const char *tag,
                              const TCFrameSpecs *specs,
                              TCFrameAllocFn alloc,
                              TCFrameFreeFn free,
                              int size)
{
    int i = 0;

    if (rfb == NULL   || specs == NULL || size < 0
     || alloc == NULL || free == NULL) {
        return 1;
    }
    size = (size > 0) ?size :1; /* allocate at least one frame */

    rfb->frames = tc_malloc(size * sizeof(TCFramePtr));
    if (rfb->frames == NULL) {
        return -1;
    }

    rfb->tag   = tag;
    rfb->size  = size;
    rfb->specs = specs;
    rfb->alloc = alloc;
    rfb->free  = free;

    /* first, warm up the pools */
    for (i = 0; i < TC_FRAME_STAGE_NUM; i++) {
        TCFrameStatus S = TC_FRAME_STAGE_ST(i);
        const char *name = frame_status_name(S);

        int err = tc_frame_pool_init(&(rfb->pools[i]), size,
                                     (S == TC_FRAME_READY),
                                     name, tag);
        
        if (err) {
            tc_log_error(FRING_NAME,
                         "(init|%s) failed to init [%s] frame pool", tag, name);
            return err;
        }
    }

    /* then, fillup the `NULL' pool */
    for (i = 0; i < size; i++) {
        rfb->frames[i] = rfb->alloc(rfb->specs);
        if (TCFRAMEPTR_IS_NULL(rfb->frames[i])) {
            tc_debug(TC_DEBUG_FLIST,
                     "(%s|init|%s) failed frame allocation",
                     FRING_NAME, tag);
            return -1;
        }

        rfb->frames[i].generic->bufid = i;
        tc_frame_ring_put_frame(rfb, TC_FRAME_NULL, rfb->frames[i]);

        tc_debug(TC_DEBUG_FLIST, 
                 "(%s|init|%s) frame [%p] allocated at bufid=[%i]",
                 FRING_NAME, tag,
                 rfb->frames[i].generic,
                 rfb->frames[i].generic->bufid);
 
    }
    return 0;
}

/*
 * tc_frame_ring_fini: (NOT thread safe)
 *     finalize a framebuffer ringbuffer by freeing all acquired
 *     resources (framebuffer memory).
 *
 * Parameters:
 *       rfb: ring framebuffer structure to finalize.
 * Return Value:
 *       None.
 */
static void tc_frame_ring_fini(TCFrameRing *rfb)
{
    if (rfb != NULL && rfb->free != NULL) {
        int i = 0;
 
        /* cool down the pools */
        for (i = 0; i < TC_FRAME_STAGE_NUM; i++) {
            const char *name = frame_status_name(TC_FRAME_STAGE_ST(i));
            int err = tc_frame_pool_fini(&(rfb->pools[i]));
        
            if (err) {
                tc_log_error(FRBUF_NAME,
                             "(fini|%s) failed to fini [%s] frame pool",
                             rfb->tag, name);
            }
        }
   
        for (i = 0; i < rfb->size; i++) {
            tc_debug(TC_DEBUG_CLEANUP,
                     "(%s|fini|%s) freeing frame #%i in [%s] status",
                     FRING_NAME, rfb->tag, i,
                     frame_status_name(rfb->frames[i].generic->status));
            rfb->free(rfb->frames[i]);
        }
        tc_free(rfb->frames);
    }
}

/*
 * tc_frame_ring_register_frame:
 *      retrieve and register a framebuffer from a ringbuffer by
 *      attaching an ID to it, setup properly status and updating
 *      internal ringbuffer counters.
 *
 *      That's the function that client code is supposed to use
 *      (maybe wrapped by some thin macros to save status setting troubles).
 *      In general, dont' use retrieve_frame directly, use register_frame
 *      instead.
 *
 * Parameters:
 *         rfb: ring framebuffer to use
 *          id: id to attach to registered framebuffer
 *      status: status of framebuffer to register. This was needed to
 *              make registering process multi-purpose.
 * Return Value:
 *      Always a generic framebuffer pointer. That can be pointing to NULL
 *      if there isn't no more framebuffer avalaible on given ringbuffer;
 *      otherwise, it will point to a valid framebuffer.
 */
static TCFramePtr tc_frame_ring_register_frame(TCFrameRing *rfb,
                                               int id, int status)
{
    TCFramePtr ptr;

    tc_debug(TC_DEBUG_FLIST,
            "(%s|register_frame|%s|0x%X) registering frame id=[%i]",
             FRING_NAME, rfb->tag, PTHREAD_ID, id);

    ptr = tc_frame_ring_get_frame(rfb, TC_FRAME_NULL);

    if (!TCFRAMEPTR_IS_NULL(ptr)) {
        if (status == TC_FRAME_EMPTY) {
            /* reset the frame data */
            ptr.generic->id         = id;
            ptr.generic->tag        = 0;
            ptr.generic->filter_id  = 0;
            ptr.generic->attributes = 0;
            /*ptr.generic->codec      = TC_CODEC_NULL;*/
            ptr.generic->next       = NULL;
            ptr.generic->prev       = NULL;
        }

        ptr.generic->status = status;

        tc_frame_ring_dump_status(rfb, "register_frame");
    }
    return ptr; 
}

/*
 * tc_frame_ring_remove_frame:
 *      De-register and release a given framebuffer;
 *      also updates internal ringbuffer counters.
 *      
 *      That's the function that client code is supposed to use.
 *      In general, don't use release_frame directly, use remove_frame
 *      instead.
 *
 * Parameters:
 *        rfb: ring framebuffer to use.
 *      frame: generic pointer to frambuffer to remove.
 * Return Value:
 *      None.
 */
static void tc_frame_ring_remove_frame(TCFrameRing *rfb,
                                       TCFramePtr frame)
{
    if (rfb != NULL && !TCFRAMEPTR_IS_NULL(frame)) {
        /* release valid pointer to pool */
        tc_frame_ring_put_frame(rfb, TC_FRAME_NULL, frame);

        tc_frame_ring_dump_status(rfb, "remove_frame");
    }
}

static void tc_frame_ring_reinject_frame(TCFrameRing *rfb,
                                         TCFramePtr frame)
{
    if (rfb != NULL && !TCFRAMEPTR_IS_NULL(frame)) {
        /* reinject valid pointer into pool */
        tc_frame_ring_put_frame(rfb, frame.generic->status, frame);

        tc_frame_ring_dump_status(rfb, "reinject_frame");
    }
}


/*
 * tc_frame_ring_flush (NOT thread safe):
 *      unclaim ALL claimed frames on given ringbuffer, making
 *      ringbuffer ready to be used again.
 *
 * Parameters:
 *      rfb: ring framebuffer to use.
 * Return Value:
 *      amount of framebuffer unclaimed by this function.
 */
static int tc_frame_ring_flush(TCFrameRing *rfb)
{
    int i = 0, n = 0;
    TCFramePool *NP = tc_frame_ring_get_pool(rfb, TC_FRAME_NULL);

    for (i = 0; i < rfb->size; i++) {
        TCFrameStatus S = rfb->frames[i].generic->status;

        if (S == TC_FRAME_NULL) {
            /* 99% of times we don't want to see this. */
            tc_debug(TC_DEBUG_CLEANUP,
                     "(%s|flush|%s) frame #%i already free (not flushed)",
                     FRING_NAME, rfb->tag, i);
        } else {
            TCFramePool *P = tc_frame_ring_get_pool(rfb, S);
            TCFramePtr frame;

            tc_debug(TC_DEBUG_CLEANUP,
                     "(%s|flush|%s) flushing frame #%i in [%s] status",
                     FRING_NAME, rfb->tag, i, frame_status_name(S));

            frame = tc_frame_pool_pull_frame(P);
            if (TCFRAMEPTR_IS_NULL(frame)) {
                tc_debug(TC_DEBUG_CLEANUP,
                         "(%s|flush|%s) got NULL while flushing frame #%i",
                         FRING_NAME, rfb->tag, i);
                tc_frame_pool_dump_status(P);
            } else {
                frame.generic->status = TC_FRAME_NULL;
                tc_frame_pool_push_frame(NP, frame);
                n++;
            }
        }
    }

    return n;
}

#define TC_FRAME_STAGE_ALL (-1)

static void tc_frame_ring_wakeup(TCFrameRing *rfb, int stage)
{
    int i = 0;

    for (i = 0; i < TC_FRAME_STAGE_NUM; i++) {
        if (stage == TC_FRAME_STAGE_ALL || stage == i) {
            tc_debug(TC_DEBUG_CLEANUP,
                     "(%s|wakeup|%s|0x%x) waking up pool [%s]",
                     FRING_NAME, rfb->tag, PTHREAD_ID,
                     frame_stages[i].name);

            tc_frame_pool_wakeup(&(rfb->pools[i]), frame_stages[i].broadcast);
        }
    }
}

static void tc_frame_ring_push_next(TCFrameRing *rfb,
                                    TCFramePtr ptr, int status)
{
    tc_debug(TC_DEBUG_FLIST,
             "(%s|push_next|%s|0x%X) frame=[%p] bufid=[%i] [%s] -> [%s]",
             FRBUF_NAME, rfb->tag, PTHREAD_ID,
             ptr.generic,
             ptr.generic->bufid,
             frame_status_name(ptr.generic->status),
             frame_status_name(status));

    tc_frame_ring_put_frame(rfb, status, ptr);
}

static void tc_frame_ring_get_counters(TCFrameRing *rfb,
                                       int *im, int *fl, int *ex)
{
    *im = tc_frame_ring_get_pool_size(rfb, TC_FRAME_EMPTY, TC_FALSE);
    *fl = tc_frame_ring_get_pool_size(rfb, TC_FRAME_WAIT,  TC_FALSE);
    *ex = tc_frame_ring_get_pool_size(rfb, TC_FRAME_READY, TC_FALSE);
}

/*************************************************************************/
/* Backward-compatible API                                               */
/*************************************************************************/

int aframe_alloc(int num)
{
    return tc_frame_ring_init(&tc_audio_ringbuffer,
                              "audio", &tc_specs,
                              tc_audio_alloc, tc_audio_free, num);
}

int vframe_alloc(int num)
{
    return tc_frame_ring_init(&tc_video_ringbuffer,
                              "video", &tc_specs,
                              tc_video_alloc, tc_video_free, num);
}

void aframe_free(void)
{
    tc_frame_ring_fini(&tc_audio_ringbuffer);
}

void vframe_free(void)
{
    tc_frame_ring_fini(&tc_video_ringbuffer);
}


aframe_list_t *aframe_dup(aframe_list_t *f)
{
    TCFramePtr frame;

    if (f == NULL) {
        tc_log_warn(FRBUF_NAME, "aframe_dup: empty frame");
        return NULL;
    }

    frame = tc_frame_ring_register_frame(&tc_audio_ringbuffer,
                                         0, TC_FRAME_WAIT);
    if (!TCFRAMEPTR_IS_NULL(frame)) {
        aframe_copy(frame.audio, f, 1);
        tc_frame_ring_put_frame(&tc_audio_ringbuffer,
                                TC_FRAME_WAIT, frame);
    }
    return frame.audio;
}

vframe_list_t *vframe_dup(vframe_list_t *f)
{
    TCFramePtr frame;

    if (f == NULL) {
        tc_log_warn(FRBUF_NAME, "vframe_dup: empty frame");
        return NULL;
    }

    frame = tc_frame_ring_register_frame(&tc_video_ringbuffer,
                                         0, TC_FRAME_WAIT);
    if (!TCFRAMEPTR_IS_NULL(frame)) {
        vframe_copy(frame.video, f, 1);
        tc_frame_ring_put_frame(&tc_video_ringbuffer,
                                TC_FRAME_WAIT, frame);
    }
    return frame.video;
}

/*************************************************************************/

aframe_list_t *aframe_register(int id)
{
    TCFramePtr frame = tc_frame_ring_register_frame(&tc_audio_ringbuffer,
                                                    id, TC_FRAME_EMPTY);
    return frame.audio;
}

vframe_list_t *vframe_register(int id)
{
    TCFramePtr frame = tc_frame_ring_register_frame(&tc_video_ringbuffer,
                                                    id, TC_FRAME_EMPTY); 
    return frame.video;
}

void aframe_remove(aframe_list_t *ptr)
{
    if (ptr == NULL) {
        tc_log_warn(FRBUF_NAME, "aframe_remove: given NULL frame pointer");
    } else {
        TCFramePtr frame = { .audio = ptr };
        tc_frame_ring_remove_frame(&tc_audio_ringbuffer, frame);
    }
}

void vframe_remove(vframe_list_t *ptr)
{
    if (ptr == NULL) {
        tc_log_warn(FRBUF_NAME, "vframe_remove: given NULL frame pointer");
    } else {
        TCFramePtr frame = { .video = ptr };
        tc_frame_ring_remove_frame(&tc_video_ringbuffer, frame);
    }
}

void aframe_reinject(aframe_list_t *ptr)
{
    if (ptr == NULL) {
        tc_log_warn(FRBUF_NAME, "aframe_reinject: given NULL frame pointer");
    } else {
        TCFramePtr frame = { .audio = ptr };
        tc_frame_ring_reinject_frame(&tc_audio_ringbuffer, frame);
    }
}

void vframe_reinject(vframe_list_t *ptr)
{
    if (ptr == NULL) {
        tc_log_warn(FRBUF_NAME, "vframe_reinject: given NULL frame pointer");
    } else {
        TCFramePtr frame = { .video = ptr };
        tc_frame_ring_reinject_frame(&tc_video_ringbuffer, frame);
    }
}

aframe_list_t *aframe_retrieve(void)
{
    TCFramePtr ptr = tc_frame_ring_get_frame(&tc_audio_ringbuffer, TC_FRAME_READY);
    return ptr.audio;
}

vframe_list_t *vframe_retrieve(void)
{
    TCFramePtr ptr = tc_frame_ring_get_frame(&tc_video_ringbuffer, TC_FRAME_READY);
    return ptr.video;
}

aframe_list_t *aframe_reserve(void)
{
    TCFramePtr ptr = tc_frame_ring_get_frame(&tc_audio_ringbuffer, TC_FRAME_WAIT);
    return ptr.audio;
}

vframe_list_t *vframe_reserve(void)
{
    TCFramePtr ptr = tc_frame_ring_get_frame(&tc_video_ringbuffer, TC_FRAME_WAIT);
    return ptr.video;
}

void aframe_push_next(aframe_list_t *ptr, int status)
{
    if (ptr == NULL) {
        /* a bit more of paranoia */
        tc_log_warn(FRBUF_NAME, "aframe_push_next: given NULL frame pointer");
    } else {
        TCFramePtr frame = { .audio = ptr };

        tc_frame_ring_push_next(&tc_audio_ringbuffer, frame, status);
    }
}

void vframe_push_next(vframe_list_t *ptr, int status)
{
    if (ptr == NULL) {
        /* a bit more of paranoia */
        tc_log_warn(FRBUF_NAME, "vframe_push_next: given NULL frame pointer");
    } else {
        TCFramePtr frame = { .video = ptr };

        tc_frame_ring_push_next(&tc_video_ringbuffer, frame, status);
    }
}

/*************************************************************************/

void aframe_flush(void)
{
    tc_frame_ring_flush(&tc_audio_ringbuffer);
}

void vframe_flush(void)
{
    tc_frame_ring_flush(&tc_video_ringbuffer);
}

void tc_framebuffer_flush(void)
{
    tc_frame_ring_flush(&tc_audio_ringbuffer);
    tc_frame_ring_flush(&tc_video_ringbuffer);
}

/*************************************************************************/

void tc_framebuffer_interrupt_stage(TCFrameStatus S)
{
    int i = TC_FRAME_STAGE_ID(S);
    if (i >= 0 && i < TC_FRAME_STAGE_NUM) {
        tc_frame_ring_wakeup(&tc_audio_ringbuffer, i);
        tc_frame_ring_wakeup(&tc_video_ringbuffer, i);
    } else {
        tc_log_warn(FRBUF_NAME, "interrupt_stage: bad status (%i)", S);
    }
}

void tc_framebuffer_interrupt(void)
{
    tc_frame_ring_wakeup(&tc_audio_ringbuffer, TC_FRAME_STAGE_ALL);
    tc_frame_ring_wakeup(&tc_video_ringbuffer, TC_FRAME_STAGE_ALL);
}

/*************************************************************************/

void aframe_dump_status(void)
{
    tc_frame_ring_dump_status(&tc_audio_ringbuffer,
                              "buffer status");
}

void vframe_dump_status(void)
{
    tc_frame_ring_dump_status(&tc_video_ringbuffer,
                              "buffer status");
}

/*************************************************************************/

int vframe_have_more(void)
{
    return tc_frame_ring_get_pool_size(&tc_video_ringbuffer,
                                       TC_FRAME_EMPTY, TC_TRUE);
}

int aframe_have_more(void)
{
    return tc_frame_ring_get_pool_size(&tc_audio_ringbuffer,
                                       TC_FRAME_EMPTY, TC_TRUE);
}

/*************************************************************************/
/* Frame copying routines                                                */
/*************************************************************************/

void aframe_copy(aframe_list_t *dst, const aframe_list_t *src,
                 int copy_data)
{
    if (!dst || !src) {
        tc_log_warn(__FILE__, "aframe_copy: given NULL frame pointer");
    	return;
    }

    /* copy all common fields with just one move */
    ac_memcpy(dst, src, sizeof(frame_list_t));
    
    if (copy_data) {
        /* really copy video data */
        ac_memcpy(dst->audio_buf, src->audio_buf, dst->audio_size);
    } else {
        /* soft copy, new frame points to old audio data */
        dst->audio_buf = src->audio_buf;
    }
}

void vframe_copy(vframe_list_t *dst, const vframe_list_t *src,
                 int copy_data)
{
    if (!dst || !src) {
        tc_log_warn(__FILE__, "vframe_copy: given NULL frame pointer");
    	return;
    }

    /* copy all common fields with just one move */
    ac_memcpy(dst, src, sizeof(frame_list_t));
    
    dst->deinter_flag = src->deinter_flag;
    dst->free         = src->free;
    /* 
     * we assert that plane pointers *are already properly set*
     * we're focused on copy _content_ here.
     */

    if (copy_data == 1) {
        /* really copy video data */
        ac_memcpy(dst->video_buf,  src->video_buf,  dst->video_size);
    } else {
        /* soft copy, new frame points to old video data */
        dst->video_buf  = src->video_buf;
    }
}

/*************************************************************************/

void vframe_get_counters(int *im, int *fl, int *ex)
{
    tc_frame_ring_get_counters(&tc_video_ringbuffer, im, fl, ex);
}

void aframe_get_counters(int *im, int *fl, int *ex)
{
    tc_frame_ring_get_counters(&tc_audio_ringbuffer, im, fl, ex);
}

void tc_framebuffer_get_counters(int *im, int *fl, int *ex)
{
    int v_im, v_fl, v_ex, a_im, a_fl, a_ex;

    vframe_get_counters(&v_im, &v_fl, &v_ex);
    aframe_get_counters(&a_im, &a_fl, &a_ex);

    *im = v_im + a_im;
    *fl = v_fl + a_fl;
    *ex = v_ex + a_ex;
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

