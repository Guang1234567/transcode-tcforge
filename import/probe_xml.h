/*
 *  probe_xml.h
 *
 *  Copyright (C) Marzio Malanchini - March 2003
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


#include "ioxml.h"

int f_build_xml_tree(info_t *ipipe,audiovideo_t *p_audiovideo,ProbeInfo *p_first_audio,ProbeInfo *p_first_video,long *s_tot_frames_audio, long *s_tot_frames_video);
