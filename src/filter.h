/*
 * filter.h -- audio/video filter include file
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef FILTER_H
#define FILTER_H

#include "framebuffer.h"

/*************************************************************************/

/* Maximum number of filter instances that can be loaded. */
#define MAX_FILTERS		16

/* Maximum length of a filter name, in bytes. */
#define MAX_FILTER_NAME_LEN	32

/* Parameters to tc_filter_list(). */
enum tc_filter_list_enum {
    TC_FILTER_LIST_LOADED,
    TC_FILTER_LIST_ENABLED,
    TC_FILTER_LIST_DISABLED,
};


/* Filter interface functions. */
extern int tc_filter_init(void);
extern void tc_filter_fini(void);
extern void tc_filter_process(frame_list_t *frame);
extern int tc_filter_add(const char *name, const char *options);
extern int tc_filter_find(const char *name);
extern void tc_filter_remove(int id);
extern int tc_filter_enable(int id);
extern int tc_filter_disable(int id);
extern int tc_filter_configure(int id, const char *options);
extern const char *tc_filter_get_conf(int id, const char *option);
extern const char *tc_filter_list(enum tc_filter_list_enum what);

/* Type of the exported module entry point for the old module system, and a
 * prototype for tc_filter() for those modules. */
typedef int (*TCFilterOldEntryFunc)(void *ptr, char *options);
extern int tc_filter(frame_list_t *ptr, char *options);

/*************************************************************************/

#endif  /* FILTER_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
