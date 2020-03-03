/*
 * cfgfile.h -- include for configuration file handling
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef LIBTC_CFGFILE_H
#define LIBTC_CFGFILE_H

#include "tclist.h"

/*************************************************************************/

/* Configuration entry types */
typedef enum {
    TCCONF_TYPE_FLAG,   /* type int */
    TCCONF_TYPE_INT,    /* type int */
    TCCONF_TYPE_FLOAT,  /* type float */
    TCCONF_TYPE_STRING, /* type char * (memory will be allocated) */
} TCConfigEntryType;


/* Structure of a configuration entry.  An array of these, as passed to the
 * functions below, ends with an entry with name==NULL. */
typedef struct {
    const char *name;   /* Name used in configuration file */
    void *ptr;          /* Pointer to value */
    TCConfigEntryType type;
    int flags;          /* TCCONF_FLAG_* below */
    double min, max;    /* Used when FLAG_MIN or FLAG_MAX are set */
} TCConfigEntry;

#define TCCONF_FLAG_MIN         (1<<0)
#define TCCONF_FLAG_MAX         (1<<1)
#define TCCONF_FLAG_RANGE       (1<<0)

/*************************************************************************/

/* Set the directory used to find configuration files. */
void tc_config_set_dir(const char *dir);

/* Read module configuration data from the given file. */
int tc_config_read_file(const char **dirs,
                        const char *filename, const char *section,
                        TCConfigEntry *conf, const char *tag);

/* Process a string as if it were a line from a configuration file. */
int tc_config_read_line(const char *string, TCConfigEntry *conf,
                        const char *tag);

/* Print module configuration data. */
void tc_config_print(const TCConfigEntry *conf, const char *tag);

/*************************************************************************/

/* Read a List section of a configuration file. Triggered by PVM module. */
TCList *tc_config_list_read_file(const char **dirs,
                                 const char *filename, const char *section,
                                 const char *tag);

/* Print module configuration list for a given section */
void tc_config_list_print(const TCList *list,
			  const char *section, const char *tag);

/* Dispose a configuration list produced by read_config_list */
void tc_config_list_free(TCList *list, int refonly);

/*************************************************************************/

#endif  /* LIBTC_CFGFILE_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
