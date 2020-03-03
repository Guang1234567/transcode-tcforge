/*
 *  import_def.h
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *
 *  This file is part of transcode, a video stream  processing tool
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

#ifndef _IMPORT_DEF_H
#define _IMPORT_DEF_H


#ifndef MOD_PRE
#error MOD_PRE not defined!
#endif


#define r2(i, a, b) import_ ## a ## b
#define r1(a, b) r2(import_, a, b)
#define RENAME(a, b) r1(a, b)

#define MOD_name static int RENAME(MOD_PRE, _name) (transfer_t *param)
#define MOD_open static int RENAME(MOD_PRE, _open) (transfer_t *param, vob_t *vob)
#define MOD_decode static int RENAME(MOD_PRE, _decode) (transfer_t *param, vob_t *vob)
#define MOD_close  static int RENAME(MOD_PRE, _close) (transfer_t *param)


//extern int verbose_flag;
//extern int capability_flag;

/* ------------------------------------------------------------
 *
 * codec id string
 *
 * ------------------------------------------------------------*/

MOD_name
{
    static int display=0;

    verbose_flag = param->flag;

    // print module version only once
    if(verbose_flag && (display++ == 0)) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CODEC);

    // return module capability flag
    param->flag = capability_flag;

    return(0);
}

MOD_open;
MOD_decode;
MOD_close;

/* ------------------------------------------------------------
 *
 * interface
 *
 * ------------------------------------------------------------*/

int tc_import(int opt, void *para1, void *para2)
{

  switch(opt)
  {

  case TC_IMPORT_NAME:

      return RENAME(MOD_PRE, _name)((transfer_t *) para1);

  case TC_IMPORT_OPEN:

      return RENAME(MOD_PRE, _open)((transfer_t *) para1, (vob_t *) para2);

  case TC_IMPORT_DECODE:

      return RENAME(MOD_PRE, _decode)((transfer_t *) para1, (vob_t *) para2);

  case TC_IMPORT_CLOSE:

      return RENAME(MOD_PRE, _close)((transfer_t *) para1);

  default:
      return(TC_IMPORT_UNKNOWN);
  }
}

#endif
