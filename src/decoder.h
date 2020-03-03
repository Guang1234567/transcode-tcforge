/*
 *  decoder.h -- transcode import layer module, declarations.
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Enhancements and partial rewrite:
 *  (C) Francesco Romani - November 2007.
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

#ifndef DECODER_H
#define DECODER_H

#include "tccore/job.h"

/*
 * tc_import_init (NOT thread safe):
 * prepare import layer for execution, by loading import modules,
 * checking their capabilities against those requested by core,
 * intializing them.
 *
 * After this function terminates succesfully, import threads can be
 * created and import layer can be started.
 *
 * Parameters:
 *          vob: vob structure.
 *        a_mod: name of the module to be used for import audio.
 *        v_mod: name of the module to be used for import video.
 * Return Value:
 *         TC_OK: succesfull.
 *      TC_ERROR: failure. Reason was already tc_log*()ged out.
 * Postconditions:
 *      Import threads can now be created.
 */
int tc_import_init(vob_t *vob, const char *a_mod, const char *v_mod);

/*
 * tc_import_shutdown (NOT thread safe):
 * shutdown import layer after the import threads termination, by
 * freeing resources acquired by import modules and unloading them.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      None.
 * Preconditions:
 *      Import threads are terminated.
 */
void tc_import_shutdown(void);


/*
 * tc_import_open (Thread safe):
 * open both the audio and video streams.
 *
 * Parameters:
 *      vob: vob structure.
 * Return Value:
 *         TC_OK: succesfull.
 *      TC_ERROR: failure. Reason was already tc_log*()ged out.
 * Preconditions:
 *      import modules are loaded and initialized correctly;
 *      tc_import_init was executed succesfully.
 */
int tc_import_open(vob_t *vob);

/*
 * tc_import_close (Thread safe):
 * close both the audio and video streams.
 *
 * Parameters:
 *      None.
 * Return Value:
 *         TC_OK: succesfull.
 *      TC_ERROR: failure. Reason was already tc_log*()ged out.
 * Preconditions:
 *      Import threads are terminated;
 *      tc_import_threads_cancel was executed succesfully.
 */
int tc_import_close(void);

/*
 * tc_import_threads_create (Thread safe):
 * create both audio and video import threads, and automatically,
 * implicitely and immediately starts importing loops and the import
 * layer itself.
 *
 * Parameters:
 *      vob: vob structure.
 * Return Value:
 *      None.
 * Preconditions:
 *      import modules are loaded and initialized correctly;
 *      tc_import_init was executed succesfully.
 *      import streams are been opened correctly;
 *      tc_import_open was executed succesfully.
 */
void tc_import_threads_create(vob_t *vob);

/*
 * tc_import_threads_cancel (Thread safe):
 * destroy both audio and video import threads, and automatically and
 * implicitely stop the whole import layer.
 * It's important to note that this function assume that import loops
 * are already been terminated.
 * This is a blocking function.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      None.
 * Preconditions:
 *      import threads are terminated for any reason
 *      (regular stop, end of stream reached, forced interruption).
 */
void tc_import_threads_cancel(void);


/*
 * tc_import_{,video_,audio_}status (Thread safe):
 * query the status of import layer.
 *
 * Import layer has the responsability to provide raw data for further
 * layers. Since there always is some buffering, isn't sufficient to
 * check if import threads are running or not, we also need to see if
 * there is some buffered data in the frame FIFOs.
 *
 * Parameters:
 *      None.
 * Return Value:
 *      !0: there is some futher data to process.
 *       0: no more data avalaible.
 */
int tc_import_status(void);
int tc_import_audio_status(void);
int tc_import_video_status(void);

/*************************************************************************/

void tc_multi_import_threads_create(vob_t *vob);
void tc_multi_import_threads_cancel(void);

#endif /* DECODER_H */
