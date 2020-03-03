/*
 *  import_null.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
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

#define MOD_NAME    "import_null.so"
#define MOD_VERSION "v0.2.0 (2002-01-19)"
#define MOD_CODEC   "(video) null | (audio) null"

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = -1;

#define MOD_PRE null
#include "import_def.h"


/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{

  if(param->flag == TC_AUDIO) {

    param->fd = NULL;
    return(0);
  }

  if(param->flag == TC_VIDEO) {

    param->fd = NULL;
    return(0);
  }

  return(TC_IMPORT_ERROR);
}


/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

MOD_decode
{
  if(param->flag == TC_AUDIO) {
    memset(param->buffer, 0, param->size);
    return(0);
  }

  if(param->flag == TC_VIDEO) {
    memset(param->buffer, 0, param->size);
    return(0);
  }

  return(TC_IMPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
  if(param->flag == TC_AUDIO) {
    return(0);
  }

  if(param->flag == TC_VIDEO) {
    return(0);
  }

  return(TC_IMPORT_ERROR);
}



