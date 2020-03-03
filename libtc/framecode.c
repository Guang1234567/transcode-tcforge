/*
 * framecode.c -- framecode list handling
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>  /* for UINT_MAX and ULONG_MAX */
#include <math.h>

#include "libtc.h"
#include "framecode.h"

/* Internal function prototypes: */
static void normalize_fc_time(struct fc_time *range);
static struct fc_time *parse_one_range(const char *string, double fps,
                                       const char **errmsg_ret,
                                       int *errpos_ret);
static int parse_one_time(const char **strptr, unsigned int *hour_ret,
                          unsigned int *min_ret, unsigned int *sec_ret,
                          unsigned int *frame_ret, const char **errmsg_ret);
static int parse_one_value(const char **strptr, unsigned int *value_ret,
                           const char **errmsg_ret);

/*************************************************************************/
/************************** External interface ***************************/
/*************************************************************************/

/**
 * new_fc_time:  Allocate a new, zeroed fc_time structure.
 *
 * Parameters:
 *     None.
 * Return value:
 *     The allocated fc_time structure, or NULL on failure.
 * Side effects:
 *     Prints an error message if allocation fails.
 */

struct fc_time *new_fc_time(void)
{
    return tc_zalloc(sizeof(struct fc_time));
}

/*************************************************************************/

/**
 * free_fc_time:  Free a list of allocated fc_time structures.
 *
 * Parameters:
 *     list: The list of structures to free.
 * Return value:
 *     None.
 */

void free_fc_time(struct fc_time *list)
{
    while (list) {
        struct fc_time *temp = list->next;
        free(list);
        list = temp;
    }
}

/*************************************************************************/

/**
 * set_fc_time:  Set fields of an fc_time structure from frame indices.
 *
 * Parameters:
 *     range: The fc_time structure to modify.
 *     start: The frame index for the start time, or -1 for no change.
 *       end: The frame index for the end time, or -1 for no change.
 * Return value:
 *     None.
 * Side effects:
 *     Prints an error message if the `range' parameter is invalid (either
 *     the parameter is NULL or it points to a range whose `fps' field is
 *     not a positive value).
 */

void set_fc_time(struct fc_time *range, int start, int end)
{
    if (!range || range->fps <= 0) {
        tc_log_error(__FILE__, "set_fc_time() with invalid range!");
        return;
    }
    if (start >= 0) {
        range->sh = 0;
        range->sm = 0;
        range->ss = 0;
        range->sf = start;
    }
    if (end >= 0) {
        range->eh = 0;
        range->em = 0;
        range->es = 0;
        range->ef = end;
    }
    normalize_fc_time(range);
}

/*************************************************************************/

/**
 * fc_time_contains:  Return whether a list of fc_time structures contains
 * a given frame index.
 *
 * Parameters:
 *      list: List of fc_time structures to check.
 *     frame: Frame index.
 * Return value:
 *     Nonzero if one of the ranges contains the given frame index, else 0.
 */

int fc_time_contains(const struct fc_time *list, unsigned int frame)
{
    while (list) {
        if (frame >= list->stf && frame < list->etf)
            return 1;
        list = list->next;
    }
    return 0;
}

/*************************************************************************/

/**
 * new_fc_time_from_string:  Parse a string into a list of fc_time
 * structures.
 *
 * Parameters:
 *        string: The string to parse.
 *     separator: A string containing separators for distinct ranges
 *                within `string'.
 *           fps: The value to store in each range's `fps' field.
 *       verbose: If positive, each range will be printed as it is parsed.
 *                If negative, error messages will be suppressed.
 * Return value:
 *     The list of fc_time structures on success, NULL on failure.
 * Side effects:
 *     Prints an error message if parsing fails, unless verbose < 0.
 */

struct fc_time *new_fc_time_from_string(const char *string,
                                        const char *separator,
                                        double fps, int verbose)
{
    struct fc_time *list, *tail;
    char rangebuf[101];  /* Buffer to hold a single range for processing */
    const char *s;

    /* Sanity checks first */
    if (!string) {
        if (verbose >= 0) {
            tc_log_error(__FILE__,
                         "new_fc_time_from_string(): string is NULL!");
        }
        return NULL;
    }
    if (!separator) {
        if (verbose >= 0) {
            tc_log_error(__FILE__,
                         "new_fc_time_from_string(): separator is NULL!");
        }
        return NULL;
    }
    if (fps <= 0) {
        if (verbose >= 0) {
            tc_log_error(__FILE__, "new_fc_time_from_string(): fps <= 0!");
        }
        return NULL;
    }

    /* Loop through all ranges in the string */
    list = tail = NULL;
    s = string + strspn(string,separator);
    while (*s) {
        struct fc_time *range;  /* Newly-allocated fc_time structure */
        const char *errmsg;     /* Error message from parse_one_range() */
        int errpos;             /* Position of error within range string */
        const char *t;

        t = s + strcspn(s,separator);
        if (t-s > sizeof(rangebuf)-1) {
            if (verbose >= 0) {
                tc_log_error(__FILE__, "new_fc_time_from_string():"
                             " range string too long! (%u/%u)",
                             (unsigned)(t-s), (unsigned)sizeof(rangebuf)-1);
                /* Print out the string and the location of the error */
                tc_log_error(__FILE__, "%s", string);
                tc_log_error(__FILE__, "%*s", (int)((s-string)+1), "^");
            }
            /* Don't forget to free anything we already parsed */
            free_fc_time(list);
            return NULL;
        }
        memcpy(rangebuf, s, t-s);
        rangebuf[t-s] = 0;
        errmsg = "unknown error";
        errpos = 0;
        range = parse_one_range(rangebuf, fps, &errmsg, &errpos);
        if (!range) {
            if (verbose >= 0) {
                tc_log_error(__FILE__, "Error parsing framecode range: %s",
                             errmsg);
                tc_log_error(__FILE__, "%s", string);
                tc_log_error(__FILE__, "%*s", (int)((s-string+errpos)+1), "^");
            }
            free_fc_time(list);
            return NULL;
        }
        if (verbose > 0) {
            tc_log_info(__FILE__, "Range: %u:%02u:%02u.%u (%u)"
                        " - %u:%02u:%02u.%u (%u)",
                        range->sh, range->sm, range->ss, range->sf,
                        range->stf,
                        range->eh, range->em, range->es, range->ef,
                        range->etf);
        }
        if (!list) {
            list = tail = range;
        } else {
            tail->next = range;
            tail = range;
        }
        s = t + strspn(t,separator);
    }

    /* Parsing completed successfully */
    return list;
}

/*************************************************************************/
/************************** Internal functions ***************************/
/*************************************************************************/

/**
 * normalize_fc_time:  Convert the HH:MM:SS.FF times stored in the given
 * fc_time structure to a normalized form, with MM < 60, SS < 60, and
 * FF < range->fps; also, store the frame indices corresponding to the
 * start and end times in range->stf and range->etf, respectively.
 * Fractional frame numbers are rounded down to the next lowest integer.
 * Used by set_fc_time() and parse_one_range().
 *
 * Parameters:
 *     range: fc_time structure to normalize.
 * Return value:
 *     None.
 * Preconditions:
 *     range != NULL
 *     range->fps > 0
 */

static void normalize_fc_time(struct fc_time *range)
{
    /* Calculate frame index from time parameters (round down) */
    range->stf = floor(((range->sh * 60 + range->sm) * 60 + range->ss)
                       * range->fps)
                 + range->sf;
    /* Calculate total number of seconds */
    range->ss = (unsigned int)floor(range->stf / range->fps);
    /* Calculate frame remainder */
    range->sf = (unsigned int)floor(range->stf - (range->ss * range->fps));
    /* Calculate normalized hours, minutes, and seconds */
    range->sh = range->ss / 3600;
    range->sm = (range->ss/60) % 60;
    range->ss %= 60;

    /* Repeat for end time */
    range->etf = floor(((range->eh * 60 + range->em) * 60 + range->es)
                       * range->fps)
                 + range->ef;
    range->es = (unsigned int)floor(range->etf / range->fps);
    range->ef = (unsigned int)floor(range->etf - (range->es * range->fps));
    range->eh = range->es / 3600;
    range->em = (range->es/60) % 60;
    range->es %= 60;
}

/*************************************************************************/

/**
 * parse_one_range:  Parse a string containing a single framecode range,
 * and return a newly allocated fc_time structure containing the range.
 * Used by new_fc_time_from_string().
 *
 * Parameters:
 *         string: String to parse.
 *            fps: Frames-per-second value to use for the range.
 *     errmsg_ret: Pointer to location to store error message in.  On
 *                 failure, this is filled with a pointer to an error
 *                 message; on success, the value is not modified.
 *     errpos_ret: Pointer to location to store error position in.  On
 *                 failure, this is filled with the offset in characters
 *                 from the start of the string to the position at which
 *                 the error occurred; on success, the value is not
 *                 modified.
 * Return value:
 *     The newly-allocated fc_time structure, or NULL on error.
 * Preconditions:
 *     string != NULL
 *     fps > 0
 *     errmsg_ret != NULL
 *     errpos_ret != NULL
 */

static struct fc_time *parse_one_range(const char *string, double fps,
                                       const char **errmsg_ret,
                                       int *errpos_ret)
{
    struct fc_time *range;    /* New fc_time structure */
    const char *s = string;   /* Current parsing location */

    /* Allocate new (cleared) fc_time and set FPS */
    range = new_fc_time();
    if (!range) {
        *errmsg_ret = "out of memory";
        *errpos_ret = 0;
        return NULL;
    }
    range->fps = fps;
    range->stepf = 1;

    /* Parse start time */
    if (!parse_one_time(&s, &range->sh, &range->sm, &range->ss, &range->sf,
                        errmsg_ret))
        goto error;

    /* Check for and skip intervening hyphen */
    if (*s != '-') {
        *errmsg_ret = "syntax error (expected '-')";
        goto error;
    }
    s++;

    /* Parse end time */
    if (!parse_one_time(&s, &range->eh, &range->em, &range->es, &range->ef,
                        errmsg_ret))
        goto error;

    /* Parse step value, if present */
    if (*s == '/') {
        s++;
        if (!parse_one_value(&s, &range->stepf, errmsg_ret))
            goto error;
    }

    /* Make sure we're at the end of the string */
    if (*s) {
        *errmsg_ret = "garbage at end of range";
        goto error;
    }

    /* Successfully parsed: normalize values and return */
    normalize_fc_time(range);
    return range;

  error:
    *errpos_ret = s - string;
    free_fc_time(range);
    return NULL;
}

/*************************************************************************/

/**
 * parse_one_time:  Parse an [[[HH:]MM:]SS.]FF time specification.
 *
 * Parameters:
 *         strptr: Pointer to the string to be parsed.
 *       hour_ret: Pointer to variable to receive the parsed hour value.
 *                 The stored value is unchanged on error.
 *        min_ret: Pointer to variable to receive the parsed minute value.
 *                 The stored value is unchanged on error.
 *        sec_ret: Pointer to variable to receive the parsed second value.
 *                 The stored value is unchanged on error.
 *      frame_ret: Pointer to variable to receive the parsed frame value.
 *                 The stored value is unchanged on error.
 *     errmsg_ret: As for parse_one_range().
 * Return value:
 *     Nonzero if parsing succeeded, zero on error.
 * Preconditions:
 *     strptr != NULL
 *     *strptr != NULL
 *     hour_ret != NULL
 *     min_ret != NULL
 *     sec_ret != NULL
 *     frame_ret != NULL
 *     errmsg_ret != NULL
 * Side effects:
 *     `*strptr' is advanced to the first character beyond the parsed
 *     string on success; on failure, the stored value points to the
 *     location of the error.
 */

static int parse_one_time(const char **strptr, unsigned int *hour_ret,
                          unsigned int *min_ret, unsigned int *sec_ret,
                          unsigned int *frame_ret, const char **errmsg_ret)
{
    unsigned int hour = 0, min = 0, sec = 0, frame = 0;
    int saw_colon = 0;  /* for deciding whether it's a bare frame count */

    if (!parse_one_value(strptr, &hour, errmsg_ret))
        return 0;
    if (**strptr == ':') {
        saw_colon = 1;
        (*strptr)++;
        if (!parse_one_value(strptr, &min, errmsg_ret))
            return 0;
        if (**strptr == ':') {
            (*strptr)++;
            if (!parse_one_value(strptr, &sec, errmsg_ret))
                return 0;
        } else {
            sec = min;
            min = hour;
            hour = 0;
        }
    } else {
        sec = hour;
        hour = 0;
    }
    if (**strptr == '.') {
        (*strptr)++;
        if (!parse_one_value(strptr, &frame, errmsg_ret))
            return 0;
    } else if (!saw_colon) {
        /* No colon or dot--must be a bare frame count */
        frame = sec;
        sec = 0;
    }

    /* Success */
    *hour_ret = hour;
    *min_ret = min;
    *sec_ret = sec;
    *frame_ret = frame;
    return 1;
}

/*************************************************************************/

/**
 * parse_one_value:  Parse a single base-10 nonnegative integer value from
 * the given string.  Used by parse_one_range().
 *
 * Parameters:
 *         strptr: Pointer to the string to be parsed.
 *      value_ret: Pointer to variable to receive the parsed value.  The
 *                 stored value is unchanged on error.
 *     errmsg_ret: As for parse_one_range().
 * Return value:
 *     Nonzero if parsing succeeded, zero on error.
 * Preconditions:
 *     strptr != NULL
 *     *strptr != NULL
 *     value_ret != NULL
 *     errmsg_ret != NULL
 * Side effects:
 *     `*strptr' is advanced to the first character beyond the parsed
 *     string on success; on failure, the stored value is unchanged.
 */

static int parse_one_value(const char **strptr, unsigned int *value_ret,
                           const char **errmsg_ret)
{
    const char *s;
    unsigned long lvalue;

    errno = 0;
    lvalue = (unsigned int)strtoul(*strptr, (char **)&s, 10);
    if (s == *strptr) {
        *errmsg_ret = "not a valid number";
        return 0;
    } else if (errno == ERANGE
#if ULONG_MAX > UINT_MAX
            || lvalue > UINT_MAX
#endif
    ) {
        *errmsg_ret = "value out of range";
        return 0;
    }
    *strptr = s;
    *value_ret = (unsigned int)lvalue;
    return 1;
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
