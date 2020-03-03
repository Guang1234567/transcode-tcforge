/*
 * tc_avcodec.h -- transcode's support macros and tools for easier
 *                libavcodec/libavformat/libavutil usage
 * (C) 2007-2010 - Francesco Romani <fromani at gmail dot com>
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


#ifndef TC_AVCODEC_H
#define TC_AVCODEC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>

#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>


/*************************************************************************/


/*
 *  libavcodec locking goodies. It's preferred and encouraged  to use
 *  macros below, but accessing libavcodec mutex will work too.
 */

void tc_lock_libavcodec(void);
void tc_unlock_libavcodec(void);

/* backward compatibility */
#define TC_LOCK_LIBAVCODEC	tc_lock_libavcodec()
#define TC_UNLOCK_LIBAVCODEC	tc_unlock_libavcodec()


#define TC_INIT_LIBAVCODEC do { \
    tc_lock_libavcodec();   \
    avcodec_init();         \
    avcodec_register_all(); \
    tc_unlock_libavcodec(); \
} while (0)

/* FIXME: not sure that locks are needed */
#define TC_INIT_LIBAVFORMAT do { \
    tc_lock_libavcodec();   \
    av_register_all();      \
    tc_unlock_libavcodec(); \
} while (0)

#endif  /* TC_AVCODEC_H */

