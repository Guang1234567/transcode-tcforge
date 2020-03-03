/*
 * cfgfile.c -- routines to handle external configuration files
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#define _ISOC99_SOURCE  /* needed by glibc to declare strtof() */

#include "common.h"
#include "logging.h"
#include "memutils.h"
#include "strutils.h"
#include "cfgfile.h"

#include <unistd.h>
#include <errno.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>



static char *config_dir = NULL;

static int parse_line(const char *buf, TCConfigEntry *conf, const char *tag,
                      const char *filename, int line);
static void parse_line_error(const char *buf, const char *filename, int line,
                             const char *tag, const char *format, ...)
#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((format(printf,5,6)))
#endif
;

/*************************************************************************/
/* helpers macros and functions */

#define CLEANUP_LINE(line) do { \
    /* skip comments, if any */ \
    char *s = strchr((line), '#'); \
    if (s) { \
        *s = 0; \
    } \
    /* Remove leading and trailing spaces, if any */ \
    tc_strstrip((line)); \
} while (0)

/**
 * FIXME
 */
static void print_error(const char *name, const char *tag)
{
    if (errno == EEXIST) {
        tc_log_warn(tag, "Configuration file %s does not exist!",
                    name);
    } else if (errno == EACCES) {
        tc_log_warn(tag, "Configuration file %s cannot be read!",
                    name);
    } else {
        tc_log_warn(tag, "Error opening configuration file %s: %s",
                    name, strerror(errno));
    }
}

/**
 * fopen_verbose:  Opens a configuration file in read only mode, printing
 * meaningful error messages in case of failure.
 *
 * Parameters:
 *    name: Name of configuration file to open.
 *     tag: Tag to use in log messages.
 * Return value:
 *     Read-only FILE pointer to configuration file, NULL if failed.
 */

static FILE *fopen_verbose(const char *name, const char *tag)
{
    FILE *f = fopen(name, "r");
    if (!f) {
        print_error(name, tag);
    }
    return f;
}

/**
 * lookup_section:  Move FILE pointer to begining of data belonging to
 * given section.
 *
 * Parameters:
 *         f: Already open FILE pointer to configuration file.
 *   section: Name of section to lookup.
 *     tag: Tag to use in log messages.
 * Return value:
 *      number of lines skipped (>= 0) if succesfull.
 *      < 0 if error occurs.
 */
static int lookup_section(FILE *f, const char *section, const char *tag)
{
    char expect[TC_BUF_MAX], buf[TC_BUF_MAX];
    int line = 0;

    tc_snprintf(expect, sizeof(expect), "[%s]", section);
    do {
        if (!fgets(buf, sizeof(buf), f)) {
            tc_log_warn(tag, "Section [%s] not found in configuration"
                             " file!", section);
            return -1;
        }
        line++;
        CLEANUP_LINE(buf);
    } while (strcmp(buf, expect) != 0);
    return line;
}

/**
 * FIXME
 */
static FILE *fopen_fallback(const char **dirs, const char *filename,
                            char *path_buf, size_t len,
                            const char *tag)
{
    FILE *f = NULL;
    int i;

    if (dirs) {
        for (i = 0; !f && dirs[i]; i++) {
            tc_snprintf(path_buf, len,
                        "%s/%s", dirs[i], filename);
            f = fopen(path_buf, "r");
        }
    }
    /* the global is now a last-resort fallback */
    if (!f && config_dir) {
        tc_snprintf(path_buf, len,
                    "%s/%s", config_dir, filename);
        f = fopen_verbose(path_buf, tag);
    }
    if (!f) {
        print_error(filename, tag);
    }
    return f;
}

/*************************************************************************/

/**
 * tc_config_set_dir:  Sets the directory in which configuration files are
 * searched for.
 *
 * Parameters:
 *     dir: Directory to search for configuration files in.  If NULL, the
 *          current directory is used.
 * Return value:
 *     None.
 */

void tc_config_set_dir(const char *dir)
{
    tc_free(config_dir);
    config_dir = dir ? tc_strdup(dir) : NULL;
 }


/**
 * tc_config_read_file:  Reads in configuration information from an external
 * file.
 *
 * Parameters:
 *          dir: XXX
 *     filename: Name of the configuration file to read.
 *      section: Section to read within the file, or NULL to read the
 *               entire file regardless of sections.
 *         conf: Array of configuration entries.
 *          tag: Tag to use in log messages.
 * Return value:
 *     Nonzero on success, zero on failure.
 */

int tc_config_read_file(const char **dirs,
                        const char *filename, const char *section,
                        TCConfigEntry *conf,
                        const char *tag)
{
    char buf[TC_BUF_MAX], path_buf[PATH_MAX+1];
    FILE *f = NULL;
    int line = 0;

    /* Sanity checks */
    if (!tag)
        tag = __FILE__;
    if (!filename || !conf) {
        tc_log_error(tag, "tc_config_read_file(): %s == NULL!",
                     !filename ? "filename" : !conf ? "conf" : "???");
        return 0;
    }

    f = fopen_fallback(dirs, filename, path_buf, PATH_MAX, tag);
    if (!f) {
        return 0;
    }

    if (section) {
        line = lookup_section(f, section, tag);
        if (line == -1) {
            /* error */
            fclose(f);
            return 0;
        }
    }

    /* Read in the configuration values (up to the end of the section, if
     * a section name was given) */
    while (fgets(buf, sizeof(buf), f)) {
        line++;
        CLEANUP_LINE(buf);

        /* Ignore empty lines and comment lines */
        if (!*buf || *buf == '#')
            continue;

        /* If it's a section name, this is the end of the current section */
        if (*buf == '[') {
            if (section)
                break;
            else
                continue;
        }

        /* Pass it on to the parser */
        parse_line(buf, conf, tag, path_buf, line);
    }

    fclose(f);
    return 1;
}

/*************************************************************************/

/**
 * tc_config_read_line:  Processes a string as if it were a line read
 * from a configuration file.  The string must have all leading and
 * trailing whitespace stripped.
 *
 * Parameters:
 *     string: String to process.
 *       conf: Array of configuration entries.
 *        tag: Tag to use in log messages.
 * Return value:
 *     Nonzero if the string was successfully processed, else zero.
 */

int tc_config_read_line(const char *string, TCConfigEntry *conf,
                        const char *tag)
{
    if (!tag)
        tag = __FILE__;
    if (!string || !conf) {
        tc_log_error(tag, "tc_config_read_line(): %s == NULL!",
                     !string ? "string" : !conf ? "conf" : "???");
        return 0;
    }
    return parse_line(string, conf, tag, NULL, 0);
}

/*************************************************************************/

/**
 * tc_config_print:  Prints the given array of configuration data.
 *
 * Parameters:
 *     conf: Array of configuration data.
 *      tag: Tag to use in log messages.
 * Return value:
 *     None.
 */

void tc_config_print(const TCConfigEntry *conf, const char *tag)
{
    /* Sanity checks */
    if (!tag)
        tag = __FILE__;
    if (!conf) {
        tc_log_error(tag, "tc_config_print(): conf == NULL!");
        return;
    }

    while (conf->name) {
        char buf[TC_BUF_MAX];
        switch (conf->type) {
          case TCCONF_TYPE_FLAG:
            tc_snprintf(buf, sizeof(buf), "%d", *((int *)conf->ptr) ? 1 : 0);
            break;
          case TCCONF_TYPE_INT:
            tc_snprintf(buf, sizeof(buf), "%d", *((int *)conf->ptr));
            break;
          case TCCONF_TYPE_FLOAT:
            tc_snprintf(buf, sizeof(buf), "%f", *((float *)conf->ptr));
            break;
          case TCCONF_TYPE_STRING:
            tc_snprintf(buf, sizeof(buf), "%s", *((char **)conf->ptr));
            break;
        }
        tc_log_info(tag, "%s = %s", conf->name, buf);
        conf++;
    }
}

/*************************************************************************/
/*************************************************************************/

/**
 * parse_line:  Internal routine to parse a single line of a configuration
 * file and set the appropriate variable.
 *
 * Parameters:
 *          buf: Line to process.  Leading and trailing whitespace
 *               (including newlines) are assumed to have been stripped.
 *         conf: Array of configuration entries.
 *          tag: Tag to use in log messages.
 *     filename: Name of file being processed, or NULL if none.
 *         line: Current line number in file.
 * Return value:
 *     Nonzero if the buffer was successfully parsed, else 0.
 * Preconditions:
 *     buf != NULL
 *     conf != NULL
 *     tag != NULL
 */

static int parse_line(const char *buf, TCConfigEntry *conf, const char *tag,
                      const char *filename, int line)
{
    char workbuf[TC_BUF_MAX];
    char *name, *value, *s;

    /* Make a working copy of the string */
    if (strlcpy(workbuf, buf, sizeof(workbuf)) >= sizeof(workbuf)) {
        parse_line_error(buf, filename, line, tag,
                         "Buffer overflow while parsing configuration data");
        return 0;
    }

    /* Split string into name and value */
    name = workbuf;
    s = strchr(workbuf, '=');
    if (s) {
        value = s+1;
        while (s > workbuf && isspace(s[-1]))
            s--;
        *s = 0;
        while (isspace(*value))
            value++;
    } else {
        value = NULL;
    }
    if (!*name) {
        parse_line_error(buf, filename, line, tag,
                         "Syntax error in option (missing variable name)");
        return 0;
    } else if (value && !*value) {
        parse_line_error(buf, filename, line, tag,
                         "Syntax error in option (missing value)");
        return 0;
    }

    /* Look for a matching configuration entry */
    while (conf->name) {
        if (strcmp(conf->name, name) == 0)
            break;
        conf++;
    }
    if (!conf->name) {
        parse_line_error(buf, filename, line, tag,
                         "Unknown configuration variable `%s'", name);
        return 0;
    }
    /* Make sure non-flag entries have values */
    if (conf->type != TCCONF_TYPE_FLAG && !value) {
        parse_line_error(buf, filename, line, tag,
                         "Syntax error in option (missing value)");
        return 0;
    }

    /* Set the value appropriately */

    switch (conf->type) {
      case TCCONF_TYPE_FLAG:
        if (!value
         || strcmp(value,"1"   ) == 0
         || strcmp(value,"yes" ) == 0
         || strcmp(value,"on " ) == 0
         || strcmp(value,"true") == 0
        ) {
            *((int *)(conf->ptr)) = (int)conf->max;
        } else if (strcmp(value,"0"    ) == 0
                || strcmp(value,"no"   ) == 0
                || strcmp(value,"off"  ) == 0
                || strcmp(value,"false") == 0
        ) {
            *((int *)(conf->ptr)) = 0;
        } else {
            parse_line_error(buf, filename, line, tag,
                             "Value for variable `%s' must be either 1 or 0",
                             name);
            return 0;
        }
        break;

      case TCCONF_TYPE_INT: {
        long lvalue;
        errno = 0;
        lvalue = strtol(value, &value, 0);
        if (*value) {
            parse_line_error(buf, filename, line, tag,
                             "Value for variable `%s' must be an integer",
                             name);
            return 0;
        } else if (errno == ERANGE
#if LONG_MIN < INT_MIN
                || lvalue < INT_MIN
#endif
#if LONG_MAX < INT_MAX
                || lvalue > INT_MAX
#endif
                || ((conf->flags & TCCONF_FLAG_MIN) && lvalue < conf->min)
                || ((conf->flags & TCCONF_FLAG_MAX) && lvalue > conf->max)
        ) {
            parse_line_error(buf, filename, line, tag,
                             "Value for variable `%s' is out of range", name);
            return 0;
        } else {
            *((int *)(conf->ptr)) = (int)lvalue;
        }
        break;
      }

      case TCCONF_TYPE_FLOAT: {
        float fvalue;
        errno = 0;
#ifdef HAVE_STRTOF
        fvalue = strtof(value, &value);
#else
        fvalue = (float)strtod(value, &value);
#endif
        if (*value) {
            parse_line_error(buf, filename, line, tag,
                             "Value for variable `%s' must be a number", name);
            return 0;
        } else if (errno == ERANGE
                || ((conf->flags & TCCONF_FLAG_MIN) && fvalue < conf->min)
                || ((conf->flags & TCCONF_FLAG_MAX) && fvalue > conf->max)
        ) {
            parse_line_error(buf, filename, line, tag,
                             "Value for variable `%s' is out of range", name);
            return 0;
        } else {
            *((float *)(conf->ptr)) = fvalue;
        }
        break;
      }

      case TCCONF_TYPE_STRING: {
        char *newval = tc_strdup(value);
        if (!newval) {
            parse_line_error(buf, filename, line, tag,
                             "Out of memory setting variable `%s'", name);
            return 0;
        } else {
            *((char **)(conf->ptr)) = newval;
        }
        break;
      }

      default:
        parse_line_error(buf, filename, line, tag,
                         "Unknown type %d for variable `%s' (bug?)",
                         conf->type, name);
        return 0;

    } /* switch (conf->type) */

    return 1;
}

/*************************************************************************/

/**
 * parse_line_error:  Helper routine for parse_line() to print an error
 * message, formatted appropriately depending on whether a filename was
 * given or not.
 *
 * Parameters:
 *          buf: String that caused the error.
 *     filename: Name of file being processed, or NULL if none.
 *         line: Current line number in file.
 *          tag: Tag to use in log message.
 *       format: Format string for message.
 * Return value:
 *     None.
 * Preconditions:
 *     buf != NULL
 *     tag != NULL
 *     format != NULL
 */

static void parse_line_error(const char *buf, const char *filename, int line,
                             const char *tag, const char *format, ...)
{
    char msgbuf[TC_BUF_MAX];
    va_list args;

    va_start(args, format);
    tc_vsnprintf(msgbuf, sizeof(msgbuf), format, args);
    va_end(args);
    if (filename) {
        tc_log_warn(tag, "%s:%d: %s", filename, line, msgbuf);
    } else {
        tc_log_warn(tag, "\"%s\": %s", buf, msgbuf);
    }
}

/*************************************************************************/

/**
 * tc_config_list_read_file:  Read a list section from given configuration
 * file and return the corresponding data list.
 *
 * Parameters:
 *     filename: Name of file being processed.
 *      section: Name of the section to be read.
 *          tag: Tag to use in log message.
 * Return value:
 *     A pointer to a valid configuration list structure if succesfull,
 *     NULL otherwise.
 */

TCList *tc_config_list_read_file(const char **dirs,
                                 const char *filename, const char *section,
                                 const char *tag)
{
    char buf[TC_BUF_MAX], path_buf[PATH_MAX+1];
    TCList *list = tc_malloc(sizeof(TCList));
    FILE *f = NULL;
    int line = 0;

    /* Sanity checks */
    if (!tag)
        tag = __FILE__;
    if (!filename) {
        tc_log_error(tag, "tc_config_list_read_file(): missing filename");
        return 0;
    }
    if (!section) {
        tc_log_error(tag, "tc_config_list_read_file(): missing section");
        return 0;
    }
    if (!list) {
        tc_log_error(tag, "tc_config_list_read_file(): unable to allocate list");
    }
    tc_list_init(list, 0);

    f = fopen_fallback(dirs, filename, path_buf, PATH_MAX, tag);
    if (!f) {
        return NULL;
    }

    line = lookup_section(f, section, tag);
    if (line == -1) {
        /* error */
        fclose(f);
        return NULL;
    }

    /* Read in the configuration values (up to the end of the section, if
     * a section name was given) */
    while (fgets(buf, sizeof(buf), f)) {
        line++;
        CLEANUP_LINE(buf);

        /* Ignore empty lines and comment lines */
        if (!*buf || *buf == '#')
            continue;

        /* If it's a section name, this is the end of the current section */
        if (*buf == '[') {
            break;
        } else {
            int err  = tc_list_append_dup(list, buf, strlen(buf) + 1);
            if (err) {
                tc_log_error(tag, "out of memory at line %i", line);
                tc_config_list_free(list, 0);
                list = NULL;
            }
        }
    }

    fclose(f);
    return list;
}

/**
 * tc_config_list_free:  Dispose a configuration list created by
 * tc_config_list_read_file.
 *
 * Parameters:
 *     list: Head of configuration list to free.
 *  refonly: If !0, DO NOT free data pointed to list;
 *           If 0, free list itslef AND data as well.
 *           Recap: use 0 to free everything, use !0 if you
 *           do want to preserve data for some reasons.
 * Return value:
 *     None.
 */
void tc_config_list_free(TCList *list, int refonly)
{
    tc_list_del(list, !refonly);
}

static int elem_printer(TCListItem *item, void *tag)
{
    tc_log_info(tag, "%s", (const char*)item->data);
    return 0;
}
    

/**
 * tc_config_list_print:  Prints the given configuration list.
 *
 * Parameters:
 *     list: Configuration list to be printed.
 *  section: Name of section on which configuration list belongs.
 *      tag: Tag to use in log messages.
 * Return value:
 *     None.
 */
void tc_config_list_print(const TCList *list,
                          const char *section, const char *tag)
{
    /* Sanity checks */
    if (!tag)
        tag = __FILE__;
    if (!section) {
        tc_log_error(tag, "tc_config_list_print(): section == NULL!");
        return;
    }
    if (!list) {
        tc_log_error(tag, "tc_config_list_print(): list == NULL!");
        return;
    }

    tc_log_info(tag, "[%s]", section);
    tc_list_foreach((TCList*)list, elem_printer, (char*)tag); /* ugh */
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

