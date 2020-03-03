/*
 *  tcstub.h - stub (but with sane values) symbols declatations
 *             for transcode support programs.
 *
 *  Copyright (C) Tilmann Bitterberg - August 2002
 *  updated and partially rewritten by
 *  Copyright (C) Francesco Romani - January 2006
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

#ifndef TC_STUB_H
#define TC_STUB_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
# ifdef OS_DARWIN
#  include "libdldarwin/dlfcn.h"
# endif
#endif

#include "framebuffer.h"
#include "transcode.h"
#include "filter.h"
#include "socket.h"
#include "video_trans.h"

#include "libtcmodule/tcmodule-core.h"
#include "libtcutil/tcutil.h"

#define OPTS_SIZE 8192 //Buffersize
#define NAME_LEN 256

/* FIXME: this should use the routines from filter.c */
struct filter_struct {
  int id;
  int status;
  int unload;
  char *options;
  void *handle;
  char *name;
  int namelen;
  int (*entry)(void *ptr, void *options);
};

extern struct filter_struct filter[MAX_FILTERS];

int load_plugin(const char *path, int id, int verbose);

#endif /* TC_STUB_H */

/* vim: sw=4
 */
