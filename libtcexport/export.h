/*
 *  export.h -- the transcode export layer. Again.
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Updated and partially rewritten by
 *  Francesco Romani - 2006-2010
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

#include "tccore/job.h"
#include "tccore/runcontrol.h"
#include "libtc/tcframes.h"
#include "libtcmodule/tcmodule-core.h"


#ifndef EXPORT_H
#define EXPORT_H

/* DOCME */

/*
 * MULTITHREADING NOTE:
 * It is *GUARANTEED SAFE* to call the following functions 
 * from different threads.
 */
/*************************************************************************/

/*
 * tc_get_frames_{dropped,skipped,encoded,cloned,skipped_cloned}:
 *     get the current value of a frame counter.
 *
 * Parameters:
 *     None
 * Return Value:
 *     the current value of requested counter
 */
uint32_t tc_get_frames_dropped(void);
uint32_t tc_get_frames_skipped(void);
uint32_t tc_get_frames_encoded(void);
uint32_t tc_get_frames_cloned(void);
uint32_t tc_get_frames_skipped_cloned(void);

/*
 * tc_update_frames_{dropped,skipped,encoded,cloned}:
 *     update the current value of a frame counter of a given value.
 *
 * Parameters:
 *     val: value to be added to the current value of requested counter.
 *     This parameter is usually just '1' (one)
 * Return Value:
 *     None
 */
void tc_update_frames_dropped(uint32_t val);
void tc_update_frames_skipped(uint32_t val);
void tc_update_frames_encoded(uint32_t val);
void tc_update_frames_cloned(uint32_t val);

/**
 * tc_export_{audio,video}_notify:
 *      notify encoder that a new {audio,video} frame is ready
 *      to be encoded.
 *      You NEED to call those functions to properly syncronize encoder
 *      and avoid deadlocks.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      None.
 */
void tc_export_audio_notify(void);
void tc_export_video_notify(void);

/*************************************************************************/

/* it's a singleton, so we recycle the new/del pair... improperly */
int tc_export_new(TCJob *vob, TCFactory factory,
                  TCRunControl *run_control,
		  const TCFrameSpecs *specs);

int tc_export_del(void);

int tc_export_setup(const char *a_mod, const char *v_mod,
                    const char *m_mod, const char *m_mod_aux);

void tc_export_shutdown(void);

/*************************************************************************/
/* optionally-called configuration functions                             */


int tc_export_config(int verbose, int progress_meter, int cluster_mode);

void tc_export_rotation_limit_frames(int frames);

void tc_export_rotation_limit_megabytes(int megabytes);


/*************************************************************************/

int tc_export_init(void);

int tc_export_open(void);

void tc_export_loop(TCFrameSource *fs, int frame_first, int frame_last);

int tc_export_frames(int frame_id,
                     TCFrameVideo *vframe, TCFrameAudio *aframe);

int tc_export_flush(void);

int tc_export_stop(void);

int tc_export_close(void);


/*************************************************************************/

#endif /* EXPORT_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
