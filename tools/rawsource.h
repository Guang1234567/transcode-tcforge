/*
 * rawsource.h -- (almost) raw source reader interface for encoder
 *                expect WAV audio and YUV4MPEG2 video
 * (C) 2006-2010 - Francesco Romani <fromani at gmail dot com>
 *
 * This file is part of transcode, a video stream processing tool.
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef _FILE_SOURCE_H
#define _FILE_SOURCE_H

#include "tccore/frame.h"
#include "tccore/job.h"

TCFrameSource *tc_rawsource_open(TCJob *job);
int tc_rawsource_num_sources(void);
int tc_rawsource_close(void);

#endif /* _FILE_SOURCE_H */
