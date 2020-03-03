/*
 *  logging.h - transcode logging infrastructure (interface)
 *  Written by Thomas Oestreich, Francesco Romani, Andrew Church, and others
 *
 *  This file is part of transcode, a video stream processing tool.
 *  transcode is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  transcode is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>
#include <stdio.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Rationale: User messages VS debug messages.
 * WRITEME
 */


/*************************************************************************/

#define TC_LOG_COLOR_ENV_VAR    "TRANSCODE_LOG_NO_COLOR"
#define TC_LOG_COLOR_OPTION     "--log_no_color"


/* how much messages do you want to see? */
typedef enum tcverboselevel_ TCVerboseLevel;
enum tcverboselevel_ {
    TC_QUIET =  0,
    TC_INFO,
    TC_DEBUG,
    TC_STATS
};

/* which messages are that? */
typedef enum tclogtype_ TCLogType;
enum tclogtype_ {
    TC_LOG_ERR   = 0, /* critical error condition */
    TC_LOG_WARN,      /* non-critical error condition */
    TC_LOG_INFO,      /* informative highlighted message */
    TC_LOG_MSG,       /* regular message */
    TC_LOG_MARK       /* verbatim, don't add anything */
};

/* how to present the messages */
typedef enum tclogtarget_ TCLogTarget;
enum tclogtarget_ {
    TC_LOG_TARGET_INVALID = 0,  /* the usual `error/unknown' value */
    TC_LOG_TARGET_CONSOLE = 1,  /* default */
    TC_LOG_TARGET_USEREXT = 127 /* use this as base for extra methods */
};

/* those are flags, actually */
typedef enum tcdebugsource_ TCDebugSource;
enum tcdebugsource_ {
    TC_DEBUG_CLEANUP = (1UL << 0), /* thread shutdown. Not easy as it seems. */
    TC_DEBUG_FLIST   = (1UL << 1), /* multithreaded framebuffer. */
    TC_DEBUG_SYNC    = (1UL << 2), /* the synchronization engine. */
    TC_DEBUG_COUNTER = (1UL << 3), /* counter code. (can be tricky too...) */
    TC_DEBUG_PRIVATE = (1UL << 4), /* generic internal behaviour */
    TC_DEBUG_THREADS = (1UL << 5), /* filter handling and import threads. */
    TC_DEBUG_WATCH   = (1UL << 6), /* legacy, currently. */
    TC_DEBUG_MODULES = (1UL << 7)  /* module system */
};

typedef struct tclogcontext_ TCLogContext;
struct tclogcontext_ {
    TCDebugSource debug_src;

    TCVerboseLevel verbose;
    int use_colors; /* flag */
    FILE *f;

    int log_count;
    int flush_thres;

    void *priv;

    int (*send)(TCLogContext *ctx, TCLogType level,
		const char *tag, const char *fmt, va_list ap);
    int (*close)(TCLogContext *ctx);
};

/*************************************************************************/

/*
 * tc_log_init:
 *    initialize the logging subsystem. You MUST call this function
 *    before to use any logging function, or you'll get an undefined
 *    behaviour.
 *
 * Parameters:
 *    N/A
 * Return Value:
 *    TC_OK on success,
 *    TC_ERROR on error.
 * Side effects:
 *    Environment variable TC_DEBUG is searched and parsed
 * Postconditions:
 *    Is now safe to open a log target.
 */ 
int tc_log_init(void);

/*
 * tc_log_fini:
 *    finalize the logging subsystem, by releasing any acquired resource.
 *
 * Parameters:
 *    N/A
 * Return Value:
 *    TC_OK on success,
 *    TC_ERROR on error.
 * Postconditions:
 *    NO logging function ca be used until tc_log_init is called again.
 */
int tc_log_fini(void);

/*************************************************************************/

/*
 * TCLogMethodOpen:
 *    hook function for log open methods.
 *    the tc_log_open function takes care to generic initialization step,
 *    by setting sane defaults into the Log Context and setting up the
 *    message filter. The it calls the open method corresponding to the
 *    chosen target and let this method to complete the initialization.
 *
 * Parameters:
 *    ctx: TCLogContext holding the partially initialized data.
 *   argc: pointer to argc (as in main function).
 *   argv: pointer to argv (as in main function).
 * Return Value:
 *    TC_OK if successful,
 *    TC_ERROR otherwise.
 */
typedef int (*TCLogMethodOpen)(TCLogContext *ctx, int *argc, char ***argv);

/*
 * tc_log_register_method:
 *    register a new Log Method open function (see above) and bind it
 *    to a given value identifying a target. The client code can use
 *    the TC_LOG_USEREXT value as base for new target identifiers.
 *    Even if recommended, new values must not be consecutive between
 *    each other.
 *    There is a maximum of methods which can registered at any time.
 *    Duplicate targets are not forbidden; however, the first target
 *    is always used. (this means you cannot override predefined methods). 
 *
 * Parameters:
 *    target: new value to be associated with open method
 *      open: method to be used for open the new target.
 * Return Value:
 *    TC_OK if succesfull, or
 *    TC_ERROR on error (reached the maximum number of targets)
 */
int tc_log_register_method(TCLogTarget target, TCLogMethodOpen open);

/*
 * tc_log_open:
 *    open a log target.
 *    There it can be just ONE open target at time.
 *    You MUST open the log target before to actually use tc_log or
 *    tc_log_debug, otherwise an undefine behaviour bust be expected.
 *    This function also set the messages filter.
 *    Log messages can be filtered (effectively sent to target or
 *    silently dropped) depending on their level, which is in turn
 *    inferred by their category
 *
 * Parameters:
 *     target: an existing log target (predefined or added via
 *             tc_log_register_method function).
 *    verbose: maximum category of messages which should be effectively
 *             logged. Messages sent to logger having an higher
 *             (so lesser priority) level are silently dropped.
 *       argc: pointer to argc (as in main function).
 *       argv: pointer to argv (as in main function).
 *
 * Return Value:
 *    TC_OK if succesfull,
 *    TC_ERROR otherwise.
 */
int tc_log_open(TCLogTarget target, TCVerboseLevel verbose,
                int *argc, char ***argv);

/*
 * tc_log_close:
 *    close the log target, relasing any acquired resource.
 *
 * Parameters:
 *    N/A
 * Return Value:
 *    TC_OK if succesfull,
 *    TC_ERROR otherwise.
 */ 
int tc_log_close(void);

/*
 * tc_log:
 *     log arbitrary user-oriented messages according
 *     to a printf-like format chosen by the caller.
 *     See note above about the difference between user messages
 *     and debug messages.
 *
 * Parameters:
 *      type: category of the logging message being submitted.
 *            the message type implies it's priority. Depending
 *            from the target filtering (setup with tc_log_open)
 *            some messages can be not actually sent to target.
 *       tag: header of message, to identify subsystem originating
 *            the message. It's suggested to use __FILE__ as
 *            fallback default tag.
 *       fmt: printf-like format string. You must provide enough
 *            further arguments to fullfill format string, doing
 *            otherwise will cause an undefined behaviour, most
 *            likely a crash.
 * Return Value:
 *      0 if message succesfully logged.
 *      1 message was NOT written at all.
 *     -1 if message was written truncated.
 *        (message too large and buffer allocation failed).
 * Side effects:
 *     this function store final message in an intermediate string
 *     before to log it to destination. If such intermediate string
 *     is wider than a given amount (TC_BUF_MIN * 2 at moment
 *     of writing), tc_log needs to dinamically allocate some memory.
 *     This allocation can fail, and as result log message will be
 *     truncated to fit in avalaible static buffer.
 */
int tc_log(TCLogType type, const char *tag, const char *fmt, ...)
#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((format(printf,3,4)))
#endif
;

/* 
 * When to use tc_log*() stuff?
 *
 * tc_log() family should be used for non-output status messages, like
 * the ones coming from the various modules and components of transcode.
 * For actual output use printf() (or fprintf(), etc.) as appropriate.
 * (yes, this means that transcode prints a lot of status and a very
 * few output messages).
 */

/* compatibility macros */
#define tc_error(format, args...) do { \
    tc_log(TC_LOG_ERR, PACKAGE, format , ## args); \
    exit(1); \
} while(0)
#define tc_info(format, args...) \
    tc_log(TC_LOG_INFO, PACKAGE, format , ## args)
#define tc_warn(format, args...) \
    tc_log(TC_LOG_WARN, PACKAGE, format , ## args)

/* macro goodies */
#define tc_log_error(tag, format, args...) \
    tc_log(TC_LOG_ERR, tag, format , ## args)
#define tc_log_info(tag, format, args...) \
    tc_log(TC_LOG_INFO, tag, format , ## args)
#define tc_log_warn(tag, format, args...) \
    tc_log(TC_LOG_WARN, tag, format , ## args)
#define tc_log_msg(tag, format, args...) \
    tc_log(TC_LOG_MSG, tag, format , ## args)

#define tc_log_perror(tag, string) do {                            \
    const char *__s = (string);  /* watch out for side effects */  \
    tc_log_error(tag, "%s%s%s", __s ? __s : "",                    \
                 (__s && *__s) ? ": " : "",  strerror(errno));     \
} while (0)


/*
 * tc_log_debug:
 *     Log arbitrary debug messages according
 *     to a printf-like format chosen by the caller.
 *     See note above about the difference between user messages
 *     and debug messages.
 *     Debug messages are always sent using minimum format.
 *
 * Parameters:
 *       src: debug group of the logging message being submitted.
 *            A debug group roughly covers a subsystem.
 *            However, often groups spans through multiple subsystems.
 *            Depending on the overall settings, messages originating
 *            debug groups can be actually logged or suppressed.
 *       tag: header of message, to identify subsystem originating
 *            the message. It's suggested to use __FILE__ as
 *            fallback default tag.
 *       fmt: printf-like format string. You must provide enough
 *            further arguments to fullfill format string, doing
 *            otherwise will cause an undefined behaviour, most
 *            likely a crash.
 * Return Value:
 *      0 if message succesfully logged.
 *      1 message was NOT written at all.
 *     -1 if message was written truncated.
 *        (message too large and buffer allocation failed).
 * Side effects:
 *     this function store final message in an intermediate string
 *     before to log it to destination. If such intermediate string
 *     is wider than a given amount (TC_BUF_MIN * 2 at moment
 *     of writing), tc_log needs to dinamically allocate some memory.
 *     This allocation can fail, and as result log message will be
 *     truncated to fit in avalaible static buffer.
 */
int tc_log_debug(TCDebugSource src, const char *tag, const char *fmt, ...)
#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((format(printf,3,4)))
#endif
;

#define tc_debug(src, fmt, args...) \
    tc_log_debug(src, __FILE__, fmt, ## args)

#ifdef __cplusplus
}
#endif

#endif  /* LOGGING_H */
