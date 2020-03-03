/*
 *  dl_loader.h
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *
 *  This file is part of transcode, a video processing tool
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

#ifndef _DL_LOADER_H
#define _DL_LOADER_H

void *load_module(const char *mod, int mode);
void unload_module(void *handle);

// extern int (*TCV_export)(int opt, void *para1, void *para2);
// extern int (*TCA_export)(int opt, void *para1, void *para2);
// extern int (*TCV_import)(int opt, void *para1, void *para2);
// extern int (*TCA_import)(int opt, void *para1, void *para2);

int tcv_export(int opt, void *para1, void *para2);
int tca_export(int opt, void *para1, void *para2);
int tcv_import(int opt, void *para1, void *para2);
int tca_import(int opt, void *para1, void *para2);

#endif
