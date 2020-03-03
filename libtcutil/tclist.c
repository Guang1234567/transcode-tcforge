/*
 * tclist.c -- a list for transcode / implementation
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

#include "common.h"
#include "memutils.h"
#include "tclist.h"

#include <string.h>


/*************************************************************************/

enum {
    DIR_BACKWARD = -1,
    DIR_FORWARD  = +1
};

static int free_item(TCListItem *item, void *unused)
{
    tc_free(item);
    return 0;
}

static int free_item_all(TCListItem *item, void *unused)
{
    free(item->data);
    free(item);
    return 0;
}


static TCListItem* next_item(TCListItem *cur, int direction)
{
    TCListItem *ret = NULL;
    if (cur) {
        if (direction == DIR_BACKWARD) {
            ret = cur->prev;
        } else {
            ret = cur->next;
        }
    }
    return ret;
}

static TCListItem *start_item(TCList *L, int direction)
{
    TCListItem *ret = NULL;
    if (L) {
         if (direction == DIR_BACKWARD) {
            ret = L->tail;
        } else {
            ret = L->head;
        }
    }
    return ret;
}

static void del_item(TCList *L, TCListItem *IT)
{
    if (L->use_cache) {
        IT->prev = NULL;
        IT->data = NULL;
        IT->next = L->cache;
        L->cache = IT;
    } else {
        tc_free(IT);
    }
}

static TCListItem *new_item(TCList *L)
{
    TCListItem *IT = NULL;
    if (L->use_cache && L->cache) {
        IT = L->cache;
        L->cache = L->cache->next;
    } else {
        IT = tc_zalloc(sizeof(TCListItem));
    }
    return IT;
}


static int foreach_item(TCListItem *start, int direction,
                        TCListVisitor vis, void *userdata)
{
    int ret = 0;
    TCListItem *cur = NULL, *inc = NULL;

    for (cur = start; cur; cur = inc) {
        inc = next_item(cur, direction);
        ret = vis(cur, userdata);
        if (ret != 0) {
            break;
        }
    }

    return ret;
}

struct find_data {
    int         dir;
    int         cur_idx;
    int         stop_idx;
    TCListItem *ptr;
};

static int elem_finder(TCListItem *item, void *userdata)
{
    struct find_data *FD = userdata;
    int ret = 0;

    FD->ptr = item;

    if (FD->cur_idx != FD->stop_idx) {
       FD->cur_idx += FD->dir;
    } else {
        ret = 1;
    }
    return ret;
}

static TCListItem *find_position(TCList *L, int pos)
{
    TCListItem *ret = NULL;
    if (L) {
        /* common cases first */
        if (pos == 0) {
            ret = L->head;
        } else if (pos == -1) {
            ret = L->tail;
        } else {
            /* if we're here we can't avoid a full scan */
            struct find_data FD = { DIR_FORWARD, 0, 0, NULL };
            if (pos >= 0) {
                FD.dir      = DIR_FORWARD; /* enforce */
                FD.cur_idx  = 0;
                FD.stop_idx = pos;
            } else {
                /* 
                 * we're perfectly fine with negative indexes;
                 * we'll just starting from the end going backwards
                 * with -1 being the last element.
                 */
                FD.dir      = DIR_BACKWARD;
                FD.cur_idx  = L->nelems - 1;
                FD.stop_idx = L->nelems + pos;
            }
            /* we can now catch some over/under-run common cases */
            if (FD.stop_idx > 0 || FD.stop_idx < L->nelems) {
                /* we want something in the middle, so let's run */
                FD.ptr = NULL; /* for safeness */
                foreach_item(start_item(L, FD.dir), FD.dir, elem_finder, &FD);
                ret = FD.ptr; /* cannot fail */
            }
        }
    }
    return ret;
}

static int item_insert_before(TCList *L, int pos, void *data)
{
    int ret = TC_ERROR;
    TCListItem *ref = find_position(L, pos);
    if (ref) {
        TCListItem *ext = new_item(L);
        if (ext) {
            ext->data = data;
            ref->prev->next = ext;
            ext->prev = ref->prev;
            ext->next = ref;
            ref->prev = ext;
            L->nelems++;
            ret = TC_OK;
        }
    }
    return ret;
}

static int item_insert_after(TCList *L, int pos, void *data)
{
    int ret = TC_ERROR;
    TCListItem *ref = find_position(L, pos);
    if (ref) {
        TCListItem *ext = new_item(L);
        if (ext) {
            ext->data = data;
            ref->next->prev = ext;
            ext->next = ref->next;
            ext->prev = ref;
            ref->next = ext;
            L->nelems++;
            ret = TC_OK;
        }
    }
    return ret;
}

/*************************************************************************/

int tc_list_init(TCList *L, int elemcache)
{
    if (L) {
        L->head      = NULL;
        L->tail      = NULL;
        L->nelems    = 0;
        L->cache     = NULL;
        L->use_cache = elemcache;

        return 0;
    }
    return -1;
}

int tc_list_fini(TCList *L)
{
    /* if !use_cache, this will not hurt anyone */
    foreach_item(L->head,  DIR_FORWARD, free_item, NULL);
    foreach_item(L->cache, DIR_FORWARD, free_item, NULL);
    /* now reset to clean status */
    return tc_list_init(L, 0);
}

int tc_list_size(TCList *L)
{
    return (L) ?L->nelems :0;
}

int tc_list_foreach(TCList *L, TCListVisitor vis, void *userdata)
{
    return foreach_item(L->head, DIR_FORWARD, vis, userdata);
}

int tc_list_append(TCList *L, void *data)
{
    int ret = TC_ERROR;
    TCListItem *IT = new_item(L);
 
    if (IT) {
        IT->data = data;
        IT->prev = L->tail;
        if (!L->head) {
            L->head = IT;
        } else {
            /* at least one element */
            L->tail->next = IT;
        }
        L->tail = IT;
        L->nelems++;
 
        ret = TC_OK;
    }
    return ret;
}

int tc_list_prepend(TCList *L, void *data)
{
    int ret = TC_ERROR;
    TCListItem *IT = new_item(L);

    if (IT) {
        IT->data = data;
        IT->next = L->head;
        if (!L->tail) {
            L->tail = IT;
        } else {
            /* at least one element */
            L->head->prev = IT;
        }
        L->head = IT;
        L->nelems++;
 
        ret = TC_OK;
    }
    return ret;
}


int tc_list_insert(TCList *L, int pos, void *data)
{
    int ret = TC_ERROR;
    if (L && data) {
        if (pos == 0) {
            ret = tc_list_prepend(L, data);
        } else if (pos == -1) {
            ret = tc_list_append(L, data);
        } else if (pos > 0) {
            ret = item_insert_before(L, pos, data);
        } else {
            ret = item_insert_after(L, pos, data);
        }
    }
    return ret;
}

void *tc_list_get(TCList *L, int pos)
{
    TCListItem *IT = find_position(L, pos);
    return (IT) ?IT->data :NULL;
}

void *tc_list_pop(TCList *L, int pos)
{
    TCListItem *IT = find_position(L, pos);
    void *data = NULL;
    if (IT) {
        data = IT->data;
        if (L->head == IT) {
            if (IT->next) {
                IT->next->prev = NULL;
            }
            L->head = IT->next;
        } else if (L->tail == IT) {
            if (IT->prev) {
                IT->prev->next = NULL;
            }
            L->tail = IT->prev;
        } else {
            IT->next->prev = IT->prev;
            IT->prev->next = IT->next;
        }
        
        del_item(L, IT);
        L->nelems--;
    }
    return data;
}

/*************************************************************************/

int tc_list_insert_dup(TCList *L, int pos, void *data, size_t size)
{
    int ret = TC_ERROR;
    void *mem = tc_malloc(size);
    if (mem) {
        memcpy(mem, data, size);
        ret = tc_list_insert(L, pos, data);
        if (ret == TC_ERROR) {
            tc_free(mem);
        }
    }
    return ret;
}

int tc_list_fini_cleanup(TCList *L)
{
    /* if !use_cache, this will not hurt anyone */
    foreach_item(L->head,  DIR_FORWARD, free_item_all, NULL);
    foreach_item(L->cache, DIR_FORWARD, free_item_all, NULL);
    /* now reset to clean status */
    return tc_list_init(L, 0);
}

void tc_list_del(TCList *L, int clean)
{
    if (clean) {
        tc_list_fini_cleanup(L);
    }
    tc_free(L);
}

TCList *tc_list_new(int usecache)
{
    TCList *L = tc_malloc(sizeof(TCList));
    if (L) {
        tc_list_init(L, usecache);
    }
    return L;
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
