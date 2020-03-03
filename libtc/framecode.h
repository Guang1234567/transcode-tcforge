/*
 * framecode.h -- framecode list handling include file
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef LIBTC_FRAMECODE_H
#define LIBTC_FRAMECODE_H

/*************************************************************************/

/* A single framecode range structure.  Start times are considered
 * inclusive, while end times are considered exclusive; thus a range with
 * stf==0 and etf==10 contains 10 frames, not 11 frames. */

struct fc_time {
    struct fc_time *next;

    double fps;                 /* Frames per second */
    unsigned int stepf;         /* Step value (process every stepf'th frame) */
    unsigned int vob_offset;    /* For transcode -L (should be removed) */

    unsigned int sh;            /* Start time: hour */
    unsigned int sm;            /* Start time: minute */
    unsigned int ss;            /* Start time: second */
    unsigned int sf;            /* Start time: frame within second */
    unsigned int stf;           /* Start time: frame index */

    unsigned int eh;            /* End time: hour */
    unsigned int em;            /* End time: minute */
    unsigned int es;            /* End time: second */
    unsigned int ef;            /* End time: frame within second */
    unsigned int etf;           /* End time: frame index */
};

/*************************************************************************/

/* Functions for handling fc_time structures. */


/* Allocate a new, zeroed fc_time structure. */
struct fc_time *new_fc_time(void);

/* Free a list of allocated fc_time structures. */
void free_fc_time(struct fc_time *list);

/* Set fields of an fc_time structure from frame indices. */
void set_fc_time(struct fc_time *range, int start, int end);

/* Return whether a list of fc_time structures contains a given frame index. */
int fc_time_contains(const struct fc_time *list, unsigned int frame);

/* Parse a string into a list of fc_time structures. */
struct fc_time *new_fc_time_from_string(const char *string,
                                        const char *separator,
                                        double fps, int verbose);


/* Compatibility macros */

#define parse_fc_time_string(str,fps,sep,verb,list) \
    ((*(list) = new_fc_time_from_string((str), (sep), (fps), (verb))) \
        != NULL ? 0 : -1)
#define fc_frame_in_time(list,frame) fc_time_contains((list), (frame))

// only for avisplit
#define fc_set_start_time(range,n) set_fc_time((range), (n), -1)

/*************************************************************************/

#endif  /* LIBTC_FRAMECODE_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
