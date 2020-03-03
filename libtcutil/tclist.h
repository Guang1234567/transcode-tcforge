/*
 * tclist.h -- a list for transcode / interface
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

#ifndef TCLIST_H
#define TCLIST_H

typedef struct tclistitem_ TCListItem;
struct tclistitem_ {
    void        *data;
    TCListItem  *next;
    TCListItem  *prev;
};

typedef struct tclist_ TCList;
struct tclist_ {
    TCListItem  *head;
    TCListItem  *tail;
    TCListItem  *cache; 
    int         use_cache;
    int         nelems;
};

/*
 * LIST INDEXING NOTE:
 * -----------------------------------------------------------------------
 * WRITEME
 */

/*
 * TCListVisitor:
 *     typedef for visitor function.
 *     A visitor function is called on elements of a given list (usually
 *     on each element) giving to it pointers to current element and custom
 *     user data. It is safe to modify the element data inside the visitor
 *     function, even free()ing it. But DO NOT modify the list *item*
 *     inside this function. Use the special crafted upper-level list
 *     functions for that.
 *
 * Parameters:
 *         item: pointer to the list item currently visited.
 *     userdata: pointer to custom, opaque caller-given data.
 * Return Value:
 *      0: success. Iteration continues to the next element, if any.
 *     !0: failure. Iteration stops here.
 */
typedef int (*TCListVisitor)(TCListItem *item, void *userdata);

/*
 * tc_list_init:
 *     intializes a list data structure.
 *     A list can use a deleted element cache. The deleted list elements
 *     (NOT the data pointed by them!) are not really released until the
 *     tc_list_fini() is called, but they are just saved (in the `cache'
 *     field.
 *     Use the cache feature if ALL the following conditions are met:
 *     - you will do a lot of insertion/removals.
 *     - you will stabilize the list size around a given size.
 *     Otherwise using the cache will not hurt, but it will neither help.
 *
 * Parameters:
 *             L: pointer to list to be initialized.
 *     elemcache: treated as boolean, enables or disable the internal cache.
 * Return Value:
 *     TC_OK on success,
 *     TC_ERROR on error.
 */
int tc_list_init(TCList *L, int elemcache);

/*
 * tc_list_fini:
 *     finalizes a list data structure. Frees all resources aquired,
 *     including any cached list element (if any), but *NOT* the data
 *     pointed by list elements.
 *
 * Parameters:
 *     L: pointer to list to be finalized
 * Return Value:
 *     TC_OK on success,
 *     TC_ERROR on error.
 */
int tc_list_fini(TCList *L);

/*
 * tc_list_size:
 *     gives the number of elements present in the list.
 *
 * Parameters:
 *     L: list to be used.
 * Return Value:
 *    -1 on error,
 *    the number of elements otherwise
 */
int tc_list_size(TCList *L);

/*
 * tc_list_foreach:
 *     applies a visitor function to all elements in the given lists,
 *     halting at first visit failed.
 *
 * Parameters:
 *             L: pointer to list to be visited.
 *           vis: visitor function to be applied.
 *      userdata: pointer to opaque data to be passed unchanged to visitor
 *                function at each call.
 * Return Value:
 *     0: if all elements are visited correctly.
 *    !0: the value returned by the first failed call to visitor function.
 */
int tc_list_foreach(TCList *L, TCListVisitor vis, void *userdata);

/*
 * tc_list_{append,prepend}:
 *     append or prepend an element to the list.
 *     The element is added on the {last,first} position of the list.
 *
 * Parameters:
 *        L: pointer to list to be used
 *     data: pointer to data to be appended or prepend.
 *           *PLEASE NOTE* that JUST THE POINTER is copied on the newly-added
 *           element. NO deep copy is performed.
 *           The caller has to allocate memory by itself if it want to
 *           add a copy of the data.
 * Return Value:
 *     TC_OK on success,
 *     TC_ERROR on error.
 */
int tc_list_append(TCList *L, void *data);
int tc_list_prepend(TCList *L, void *data);

/*
 * tc_list_insert:
 *      the newly-inserted elements BECOMES the position `pos' on the list.
 *      Position after the last -> the last.
 *      Position before the first -> the first.
 */
int tc_list_insert(TCList *L, int pos, void *data);

/*
 * tc_list_get:
 *     gives access to the data pointed by the element in the given position.
 *
 * Parameters:
 *       L: list to be accessed.
 *     pos: position of the element on which the data will be returned.
 * Return Value:
 *     NULL on error (requested element doesn't exist)
 *     a pointer to the data belonging to the requested list item.
 */
void *tc_list_get(TCList *L, int pos);

/*
 * tc_list_pop:
 *     removes the element in the given position.
 *
 * Parameters:
 *       L: list to be accessed.
 *     pos: position of the element on which the data will be returned.
 * Return Value:
 *     NULL on error (requested element doesn't exist)
 *     a pointer to the data assigned to the requested list item.
 */
void *tc_list_pop(TCList *L, int pos);

/*************************************************************************/

int tc_list_fini_cleanup(TCList *L);

TCList *tc_list_new(int usecache);
void tc_list_del(TCList *L, int clean);

int tc_list_insert_dup(TCList *L, int pos, void *data, size_t size);
#define tc_list_append_dup(L, DATA, SIZE) tc_list_insert_dup(L, -1, DATA, SIZE)
#define tc_list_prepend_dup(L, DATA, SIZE) tc_list_insert_dup(L, 0,  DATA, SIZE)

#endif /* TCLIST_H */
