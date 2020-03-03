/*
 *  probe_dvd.c
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

#include "tccore/tcinfo.h"
#include "src/transcode.h"
#include "libtc/libtc.h"
#include "tc.h"
#include "dvd_reader.h"


void probe_dvd(info_t *ipipe)
{

  int max_titles;

  if(dvd_init(ipipe->name, &max_titles, ipipe->verbose)<0) {
    tc_log_error(__FILE__, "failed to open DVD %s", ipipe->name);
    ipipe->error=1;
    return;
  }

  if(dvd_probe(ipipe->dvd_title, ipipe->probe_info)<0) {
    tc_log_error(__FILE__, "failed to probe DVD title information");

    dvd_close();
    ipipe->error=1;
    return;
  }

  dvd_close();

}

