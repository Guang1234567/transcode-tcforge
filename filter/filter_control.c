/*
 *  filter_control
 *
 *  Copyright (C) Tilmann Bitterberg - June 2002
 *
 *  This file is part of transcode, a video stream processing tool
 *
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

#define MOD_NAME    "filter_control.so"
#define MOD_VERSION "v0.0.1 (2003-11-29)"
#define MOD_CAP     "apply a filter control list"
#define MOD_AUTHOR  "Tilmann Bitterberg"

#include "src/transcode.h"
#include "src/filter.h"
#include "src/socket.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include <ctype.h>


#if 0
#ifndef M_BUF_SIZE
#define M_BUF_SIZE 8192
#endif

typedef struct cmd_t {
    char *cmd;
    int (*action)(char *);
} cmd_t;

static struct cmd_t actions[]={
    {"preview",   tc_socket_preview  },
    {"parameter", tc_socket_parameter},
    {"list",      tc_socket_list     },
    {"config",    tc_socket_config   },
    {"disable",   tc_socket_disable  },
    {"enable",    tc_socket_enable   },
    {"load",      tc_socket_load     },
    {NULL,        NULL               }
};

static int init_done = 0;

typedef struct ctrl_t{
    char *file;
    FILE *f;
    char *ofile;
    FILE *of;
} ctrl_t;

typedef struct flist_t {
    unsigned int frame;
    char *line;
    struct cmd_t *cmd;
    struct flist_t *next;
} flist_t;

#define mmalloc(str,type) \
   do {  \
       str = (type *)malloc(sizeof(type)); \
       if (!str) { \
           tc_log_error(MOD_NAME, "(%s:%d) No Memory for %s", __FILE__, __LINE__, #str); \
           return -1; \
       } \
       memset(str, 0, sizeof(type)); \
   } while (0)


static int parse_input_list (ctrl_t *ctrl, flist_t **flist_tofill);
#endif

int tc_filter(frame_list_t *ptr, char *options)
{
#if 0
    static ctrl_t  *ctrl  = NULL;
    static flist_t *flist = NULL;
    static flist_t *first = NULL;

    if (ptr->tag & TC_AUDIO) return 0;

    if (ptr->tag & TC_FILTER_GET_CONFIG) {
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "Tilmann Bitterberg", "VRYE", "1");
      optstr_param (options, "file", "read commands to apply from file.", "%s", "");
      optstr_param (options, "ofile", "write output of commands to output file", "%s", "/dev/null");

      return 0;
    }

    if (ptr->tag & TC_FILTER_INIT && !init_done) {

	char *file = NULL;
	int ret;

	if (!options) {
	    tc_log_error(MOD_NAME, "This filter makes no sense without options");
	    goto init_e_out;
	}

	mmalloc(ctrl, ctrl_t);

	file = malloc (M_BUF_SIZE);

	memset (file, 0, M_BUF_SIZE);
	optstr_get (options, "file", "%[^:]", file);

	if (strlen(file)>0) {
	    ctrl->file = tc_strdup(file);
	} else {
	    tc_log_error(MOD_NAME, "The \"file\" option is mandatory");
	    goto init_e_out;
	}
	if (NULL == (ctrl->f = fopen(ctrl->file, "r"))) {
	    tc_log_error(MOD_NAME, "Cannot open \"%s\": %s", ctrl->file,
			 strerror(errno));
	    goto init_e_out;
	}

	memset (file, 0, M_BUF_SIZE);
	optstr_get (options, "ofile", "%[^:]", file);

	if (strlen(file)>0) {
	    ctrl->ofile = tc_strdup(file);
	    if (NULL == (ctrl->of = fopen(ctrl->ofile, "w"))) {
		tc_log_error(MOD_NAME, "Cannot open \"%s\": %s", ctrl->ofile,
			     strerror(errno));
		goto init_e_out;
	    }
	}

	ret = parse_input_list (ctrl, &flist);

	if (ret < 0) {
	    tc_log_error(MOD_NAME, "An error occurred parsing the command file");
	    return -1;
	}

	if (flist == NULL) {
	    tc_log_error(MOD_NAME, "WTF? Nothing to do");
	    return -1;
	}

	first = flist;

	for (first = flist; flist->next; flist = flist->next) {
	    //tc_log_msg(MOD_NAME, "Frame %u -> %s", flist->frame, flist->line);
	}

	// sort the list?
	flist = first;

	init_done++;
	free(file);
	return 0;

init_e_out:
	if (ctrl && ctrl->ofile) free (ctrl->ofile); ctrl->ofile = NULL;
	if (ctrl && ctrl->file) free (ctrl->file); ctrl->file = NULL;
	if (ctrl) free(ctrl); ctrl = NULL;
	if (file) free(file); file = NULL;
	return -1;
    } // INIT


    if(ptr->tag & TC_FILTER_CLOSE) {

	flist = first;
	while (flist) {
	    first = flist->next;
	    free(flist->line);
	    free(flist);
	    flist = first;
	}
	if (!ctrl) return 0;

	if (ctrl->f) fclose (ctrl->f);
	if (ctrl->of) fclose (ctrl->of);
	if (ctrl->ofile) free(ctrl->ofile);
	if (ctrl->file) free(ctrl->file);
	free (ctrl);
	ctrl = NULL;

    } // CLOSE



    if(ptr->tag & TC_PRE_S_PROCESS) {

	int ret;
	char buf[M_BUF_SIZE];

	flist = first;

	if (!flist) {
	    tc_log_msg(MOD_NAME, "No more actions");
	    return 0;
	}

	// there may be more actions per frame
	while (ptr->id == flist->frame) {

	    strlcpy (buf, flist->line, sizeof(buf));
	    ret = flist->cmd->action(buf);

	    if (buf[strlen(buf)-1] == '\n') {
		if (ctrl->of) {
		    fprintf(ctrl->of, "** Result at frame %d of \"%s\":\n", ptr->id, flist->line);
		    fprintf(ctrl->of, "%s", buf);
		}
	    }

	    //if (verbose & TC_DEBUG)
	    tc_log_msg(MOD_NAME, "Executed at %d \"%s\"", ptr->id, flist->line);

	    first = flist->next;
	    free (flist->line);
	    free (flist);
	    flist = first;
	    if (flist == NULL) break;

	}

	return 0;

    } // PROCESS

    return 0;
#else  // 0
    tc_log_error(MOD_NAME, "This filter is currently disabled.");
    tc_log_error(MOD_NAME, "Please contact transcode-devel@exit1.org");
    tc_log_error(MOD_NAME, "if you need it.");
    return -1;
#endif  // 0
}

#if 0
#define skipws(a) while (a && *a && isspace(*a++))

static int parse_input_list (ctrl_t *ctrl, flist_t **flist_tofill)
{
    char *buf;
    int count = 1;
    flist_t *first;
    flist_t *flist;
    char *fnum, *action, *line;

    fnum = action = line = NULL;

    buf = malloc (M_BUF_SIZE);
    memset (buf, 0, M_BUF_SIZE);

    mmalloc (flist, flist_t);

    first = flist;

    while ( fgets(buf, M_BUF_SIZE, ctrl->f) ) {

	cmd_t *cmd = &actions[0];

	buf[strlen(buf)-1] = '\0';
	line = buf;
	skipws(line);

	if (!line) {
	    tc_log_error(MOD_NAME, "Syntax error at line %d -- empty?", count);
	    return -2;
	}
	line--;
	fnum = line;

	if (*fnum == '#' || strlen(line)<2) {
	    count++;
	    continue;
	}

	action = strchr(line, ' ');
	if (!action) {
	    tc_log_error(MOD_NAME, "Syntax error at line %d", count);
	    return -2;
	}

	skipws(action);
	if (!action) {
	    tc_log_error(MOD_NAME, "Syntax error at line %d", count);
	    return -2;
	}
	action--;

	// fnum points to the frame number
	// action points to the requested function name

	// look up the requested action in the table
	while (cmd->cmd) {
	    if (strncasecmp(cmd->cmd, action, 4) == 0) break;
	    cmd++;
	}

	if (cmd->cmd == NULL) {
	    tc_log_warn(MOD_NAME, "Warning at line %d: unknown command (%s) found -- ignored", count, action);
	    count++;
	    continue;
	}

	flist->frame = strtol(fnum, (char **)NULL, 10);
	flist->line = tc_strdup (action);
	flist->cmd = cmd;

	mmalloc (flist->next, flist_t);

	flist = flist->next;

	count++;
    }

    count--;
    tc_log_info(MOD_NAME, "Found %d lines", count);

    *flist_tofill = first;

    return 0;
}
#endif  // 0
