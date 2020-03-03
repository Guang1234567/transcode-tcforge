/*
 * filter.c -- audio/video filter handling
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "transcode.h"
#include "filter.h"

// temp defines during module system switchover
//#define SUPPORT_NMS     // support NMS modules?
#define SUPPORT_CLASSIC // support classic modules?

#ifdef SUPPORT_CLASSIC
/* For dlopening old-style modules */
# ifdef HAVE_DLFCN_H
#  include <dlfcn.h>
# else
#  ifdef OS_DARWIN
#   include "libdldarwin/dlfcn.h"
#  endif
# endif
#endif  // SUPPORT_CLASSIC

/*************************************************************************/

/* Data for a single filter instance.  An ID value of 0 indicates that no
 * filter is present. */

typedef struct FilterInstance_ {
    char name[MAX_FILTER_NAME_LEN+1]; // Filter name
    int id;                     // Unique ID value for this filter instance
    int enabled;                // Nonzero if filter is inabled
#ifdef SUPPORT_CLASSIC
    void *handle;               // DLL handle for old-style modules
    TCFilterOldEntryFunc entry; // Module entry point for old-style modules
#endif
#ifdef SUPPORT_NMS
#error please add field(s) needed for NMS
#endif
} FilterInstance;


/* Flag: are we initialized? */
static int initialized = 0;

/* Filter instance table. */
static FilterInstance filters[MAX_FILTERS];


/* Macro to check that tc_filter_init() has been called, and abort the
 * function otherwise.  Pass the appropriate return value (nothing for a
 * void function) as the macro parameter. */
#define CHECK_INITIALIZED(...) do {                                     \
    if (!initialized) {                                                 \
        tc_log_warn(__FILE__, "%s() called before initialization!",     \
                    __FUNCTION__);                                      \
        return __VA_ARGS__;                                             \
    }                                                                   \
} while (0)

/*************************************************************************/

/**
 * id_to_index:  Local helper function to convert a filter ID value to a
 * filters[] index.  Outputs an error message with tc_log_warn() on error.
 *
 * Parameters:
 *       id: Filter ID.
 *     func: Calling function's name (__FUNCTION__).
 * Return value:
 *     filters[] index corresponding to given ID, or -1 on error.
 */

static int id_to_index(int id, const char *func)
{
    int i;

    if (id <= 0) {
        tc_log_warn(__FILE__, "Bad filter ID %d passed to %s()", id, func);
        return -1;
    }
    for (i = 0; i < MAX_FILTERS; i++) {
        if (filters[i].id == id)
            break;
    }
    if (i >= MAX_FILTERS) {
        tc_log_warn(__FILE__, "Filter ID %d does not exist in %s()", id, func);
        return -1;
    }
    return i;
}

/*************************************************************************/
/*************************************************************************/

/* tc_filter_init:  Initialize the filter subsystem.  Must be called before
 * any other filter functions.
 *
 * Parameters:
 *     None.
 * Return value:
 *     Nonzero on success, zero on failure.
 */

int tc_filter_init(void)
{
    int i;

    if (initialized) {
        tc_log_warn(__FILE__, "tc_filter_init() called twice!");
        return 1;
    }
    for (i = 0; i < MAX_FILTERS; i++)
        filters[i].id = 0;
    initialized = 1;
    return 1;
}

/*************************************************************************/

/* tc_filter_fini:  Close down the filter subsystem.  If the filter system
 * has not yet been initialized, do nothing.  After calling this function,
 * no filter functions (other than tc_filter_init()) may be called.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 */

void tc_filter_fini(void)
{
    int i;

    if (!initialized)
        return;

    for (i = 0; i < MAX_FILTERS; i++) {
        if (filters[i].id != 0)
            tc_filter_remove(filters[i].id);
    }

    initialized = 0;
}

/*************************************************************************/

/**
 * tc_filter_process:  Sends the given frame to all enabled filters for
 * processing.
 *
 * Parameters:
 *     frame: Frame to process.
 * Return value:
 *     None.
 * Prerequisites:
 *     frame->tag is set to an appropriate value
 */

void tc_filter_process(frame_list_t *frame)
{
    int last_id;

    CHECK_INITIALIZED();
    if (!frame) {
        tc_log_warn(__FILE__, "tc_filter_process: frame is NULL!");
        return;
    }

    /* The order of the filters is given by their ID values--however, this
     * does not necessarily match the order in the filters[] array.  We
     * keep track of the last ID processed (starting at 0, lower than any
     * valid ID), then each time through the loop, search for the lowest ID
     * greater than that value, which will be the next filter to process.
     * The loop ends when no enabled filter has an ID greater than the last
     * ID processed. */

    last_id = 0;
    for (;;) {
        int next_filter = -1, i;

        for (i = 0; i < MAX_FILTERS; i++) {
            if (filters[i].id <= last_id || !filters[i].enabled)
                continue;
            if (next_filter < 0 || filters[i].id < filters[next_filter].id)
                next_filter = i;
        }
        if (next_filter < 0)
            break;
        last_id = filters[next_filter].id;

#ifdef SUPPORT_NMS
# error please write NMS support code
#endif

#ifdef SUPPORT_CLASSIC
        if (!filters[next_filter].entry) {
            tc_log_warn(__FILE__, "Filter %s (%d) missing entry function"
                        " (bug?), disabling", filters[i].name, last_id);
            filters[next_filter].enabled = 0;
            continue;
        }
        frame->filter_id = last_id;
        filters[next_filter].entry(frame, NULL);
#endif
    }  // for (;;)
}

/*************************************************************************/

/**
 * tc_filter_add:  Adds the given filter at the end of the filter chain,
 * and initializes it using the given option string.
 *
 * Parameters:
 *        name: Name of filter to add.
 *     options: Options to pass to filter (if NULL, no options are passed).
 * Return value:
 *     Filter ID (nonzero) on success, zero on failure.
 */

int tc_filter_add(const char *name, const char *options)
{
    int i, id;

    CHECK_INITIALIZED(0);
    if (!name) {
        tc_log_warn(__FILE__, "tc_filter_add: name is NULL!");
        return 0;
    } else if (!*name) {
        tc_log_warn(__FILE__, "tc_filter_add: name is empty!");
        return 0;
    } else if (strlen(name) > MAX_FILTER_NAME_LEN) {
        tc_log_warn(__FILE__, "tc_filter_add: name \"%s\" is too long!"
                    " (max %d chars)", name, MAX_FILTER_NAME_LEN);
        return 0;
    }

    /* Find the largest ID value currently in use, and use the next value */
    id = 0;
    for (i = 0; i < MAX_FILTERS; i++) {
        if (filters[i].id > id)
            id = filters[i].id;
    }
    id++;
    if (id <= 0) {  // wraparound check
        tc_log_warn(__FILE__, "tc_filter_add: out of filter IDs, restart %s",
                    PACKAGE);
        return 0;
    }

    /* Find the first available filter table entry, returning an error if
     * none is found, and initialize the entry */
    for (i = 0; i < MAX_FILTERS; i++) {
        if (!filters[i].id)
            break;
    }
    if (i >= MAX_FILTERS) {
        tc_log_warn(__FILE__, "tc_filter_add: no free filter slots! (max %d)",
                    MAX_FILTERS);
        return 0;
    }
    strlcpy(filters[i].name, name, sizeof(filters[i].name));
    filters[i].enabled = 0;

#ifdef SUPPORT_NMS
# error please write NMS support code
#endif

#ifdef SUPPORT_CLASSIC
    {
        char path[1000];
        frame_list_t dummy_frame;

        /* Load the module and look up the tc_filter() address */
        if (tc_snprintf(path, sizeof(path), "%s/filter_%s.so",
                        tc_get_vob()->mod_path, name) < 0) {
            tc_log_error(__FILE__, "tc_filter_add: path buffer overflow");
            return 0;
        }
        filters[i].handle = dlopen(path, RTLD_NOW);
        if (!filters[i].handle) {
            const char *error = dlerror();
            if (!error)
                error = "Unknown error";
            tc_log_warn(PACKAGE, "Unable to load filter %s: %s", name, error);
            return 0;
        }
        filters[i].entry = dlsym(filters[i].handle, "tc_filter");
        if (!filters[i].entry) {
            const char *error = dlerror();
            if (!error)
                error = "Unknown error (corrupt module?)";
            tc_log_warn(PACKAGE, "Unable to initialize filter %s: %s",
                        name, error);
            dlclose(filters[i].handle);
            return 0;
        }
        filters[i].id = id;  /* loaded, at least */
        if (verbose >= TC_DEBUG)
            tc_log_msg(__FILE__, "tc_filter_add: module %s loaded", path);

        /* Call tc_filter() to initialize the module */
        dummy_frame.filter_id = id;
        dummy_frame.tag = TC_FILTER_INIT;
        /* Maximum size of a single frame, video or audio */
        dummy_frame.size = 0;
        /* XXX: it seems never used, so 0 should be safe -- FR */
        if (filters[i].entry(&dummy_frame, (char *)options) < 0) {
            tc_warn("Initialization of filter %s failed, skipping.", name);
            tc_filter_remove(id);
        }
        if (verbose >= TC_DEBUG)
            tc_log_msg(__FILE__, "tc_filter_add: filter %s successfully"
                       " initialized", name);
    }
#endif  // SUPPORT_CLASSIC

    /* Module was successfully loaded and initialized, so enable it */
    filters[i].enabled = 1;
    return 1;
}

/*************************************************************************/

/**
 * tc_filter_find:  Return the ID for the named filter.
 *
 * Parameters:
 *     name: Name of filter to find.
 * Return value:
 *     Filter ID (nonzero) on success, zero on error or if given filter is
 *     not loaded.
 */

int tc_filter_find(const char *name)
{
    int i;

    CHECK_INITIALIZED(0);
    for (i = 0; i < MAX_FILTERS; i++) {
        if (strcmp(filters[i].name, name) == 0)
            return filters[i].id;
    }
    return 0;
}

/*************************************************************************/

/**
 * tc_filter_remove:  Remove the given filter.
 *
 * Parameters:
 *     id: ID of filter to remove.
 * Return value:
 *     None.
 */

void tc_filter_remove(int id)
{
    int i;

    CHECK_INITIALIZED();
    if ((i = id_to_index(id, __FUNCTION__)) < 0)
        return;

#ifdef SUPPORT_NMS
# error please write NMS support code
#endif

#ifdef SUPPORT_CLASSIC
    if (filters[i].handle) {
        if (filters[i].entry) {
            frame_list_t ptr;
            ptr.tag = TC_FILTER_CLOSE;
            ptr.filter_id = filters[i].id;
            filters[i].entry(&ptr, NULL);
        } else {
            tc_log_warn(__FILE__, "Filter %s (%d) missing entry function"
                        " (bug?)", filters[i].name, id);
        }
        dlclose(filters[i].handle);
        filters[i].handle = NULL;
        filters[i].entry = NULL;
    }
#endif

    memset(filters[i].name, 0, sizeof(filters[i].name));
    filters[i].id = 0;
    filters[i].enabled = 0;
}

/*************************************************************************/

/**
 * tc_filter_enable:  Enable the given filter.
 *
 * Parameters:
 *     id: ID of filter to enable.
 * Return value:
 *     Nonzero on success, zero on error.
 */

int tc_filter_enable(int id)
{
    int i;

    CHECK_INITIALIZED(0);
    i = id_to_index(id, __FUNCTION__);
    if (i < 0)
        return 0;
    filters[i].enabled = 1;
    return 1;
}

/*************************************************************************/

/**
 * tc_filter_disable:  Disable the given filter.
 *
 * Parameters:
 *     id: ID of filter to enable.
 * Return value:
 *     Nonzero on success, zero on error.
 */

int tc_filter_disable(int id)
{
    int i;

    CHECK_INITIALIZED(0);
    i = id_to_index(id, __FUNCTION__);
    if (i < 0)
        return 0;
    filters[i].enabled = 0;
    return 1;
}

/*************************************************************************/

/**
 * tc_filter_configure:  Configure the given filter.
 *
 * Parameters:
 *          id: ID of filter to configure.
 *     options: Option string for filter.
 * Return value:
 *     Nonzero on success, zero on error.
 */

int tc_filter_configure(int id, const char *options)
{
    int i;

    CHECK_INITIALIZED(0);
    i = id_to_index(id, __FUNCTION__);
    if (i < 0)
        return 0;

#ifdef SUPPORT_NMS
# error please write NMS support code
#endif

#ifdef SUPPORT_CLASSIC
    {
        frame_list_t dummy_frame;

        if (!filters[i].entry) {
            tc_log_warn(__FILE__, "Filter %s (%d) missing entry function"
                        " (bug?), disabling", filters[i].name, id);
            filters[i].enabled = 0;
            return 0;
        }
        /* Old filter API does a close before reconfiguring */
        dummy_frame.filter_id = id;
        dummy_frame.tag = TC_FILTER_CLOSE;
        filters[i].entry(&dummy_frame, NULL);
        dummy_frame.filter_id = id;
        dummy_frame.tag = TC_FILTER_INIT;
        dummy_frame.size = 0;
        /* XXX: it seems never used, so 0 should be safe -- FR */
        if (filters[i].entry(&dummy_frame, (char *)options) < 0) {
            tc_log_warn(PACKAGE, "Reconfiguration of filter %s failed,"
                        " disabling.", filters[i].name);
            filters[i].enabled = 0;
            return 0;
        }
        return 1;
    }
#endif
}

/*************************************************************************/

/**
 * tc_filter_get_conf:  Return current configuration information for the
 * given option on the given filter.  If `option' is NULL, returns a
 * summary of configuration information in an undefined format.
 *
 * Parameters:
 *         id: ID of filter to retrieve configuration information for.
 *     option: Name of option to retrieve information for, or NULL.
 * Return value:
 *     A pointer to an unmodifiable string containing the result, or NULL
 *     on error.
 */

const char *tc_filter_get_conf(int id, const char *option)
{
    int i;

    CHECK_INITIALIZED(NULL);
    i = id_to_index(id, __FUNCTION__);
    if (i < 0)
        return 0;

#ifdef SUPPORT_NMS
# error please write NMS support code
#endif

#ifdef SUPPORT_CLASSIC
    {
        frame_list_t dummy_frame;
        static char buf[PATH_MAX];

        memset(buf, 0, sizeof(buf));
        dummy_frame.filter_id = id;
        dummy_frame.tag = TC_FILTER_GET_CONFIG;
        if (filters[i].entry) {
            if (filters[i].entry(&dummy_frame, buf) == 0)
                return buf;
        } else {
            tc_log_warn(__FILE__, "Filter %s (%d) missing entry function"
                        " (bug?), disabling", filters[i].name, id);
            filters[i].enabled = 0;
        }
        return NULL;
    }
#endif
}

/*************************************************************************/

/**
 * tc_filter_list:  Return a list of filters according to the given
 * parameter.  The list is comma-space separated, with each name enclosed
 * in double quotes; filters are listed in the same order they are applied.
 *
 * Parameters:
 *     what: Selects what kind of modules to list (TC_FILTER_LIST_*).
 * Return value:
 *     A pointer to an unmodifiable string containing the result.
 */

const char *tc_filter_list(enum tc_filter_list_enum what)
{
    static char buf[MAX_FILTERS * (MAX_FILTER_NAME_LEN+4)];
    int last_id;

    *buf = 0;
    CHECK_INITIALIZED(buf);

    /* Use the same logic as in tc_filter_process() to retrieve the filters
     * in ID order. */
    last_id = 0;
    for (;;) {
        int next_filter = -1, i;

        for (i = 0; i < MAX_FILTERS; i++) {
            if (filters[i].id <= last_id)
                continue;
            if (what == TC_FILTER_LIST_ENABLED && !filters[i].enabled)
                continue;
            if (what == TC_FILTER_LIST_DISABLED && filters[i].enabled)
                continue;
            if (next_filter < 0 || filters[i].id < filters[next_filter].id)
                next_filter = i;
        }
        if (next_filter < 0)
            break;
        last_id = filters[next_filter].id;
        tc_snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%s\"%s\"",
                    last_id==0 ? "" : ", ", filters[next_filter].name);
    }
    return buf;
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
