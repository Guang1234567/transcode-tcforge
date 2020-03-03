/*
 * multiplexor.h -- interface for the multiplexor in transcode
 * (C) 2009-2010 Francesco Romani <fromani at gmail dot com>
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

#ifndef MULTIPLEXOR_H
#define MULTIPLEXOR_H

#include "libtcmodule/tcmodule-core.h"
#include "libtcmodule/tcmodule-registry.h"
#include "libtc/tcframes.h"
#include "tccore/job.h"

/*************************************************************************
 * MULTITHREADING WARNING:                                               *
 * It *NOT SAFE* to call functions declared on this header from          *
 * different threads. See comments below.                                *
 *************************************************************************/

/*************************************************************************
 * new-style output rotation support.
 * This couple of functions
 *      tc_multiplexor_limit_frames
 *      tc_multiplexor_limit_megabytes
 *
 * Allow the client code to automatically split output into chunks by
 * specifying a maxmimum size, resp. in frames OR in megabytes, for each
 * output chunk.
 *
 * Those functions MUST BE used BEFORE to call first tc_multiplexor_open(),
 * otherwise will fall into unspecifed behaviour.
 * It's important to note that client code CAN call multiple times
 * (even if it is usually pointless ;) ) tc_multiplexor_limit*,
 * but only one limit can be used, so the last limit set will be used.
 */

typedef struct tcrotatecontext_ TCRotateContext;

typedef struct tcmultiplexor_ TCMultiplexor;
struct tcmultiplexor_ {
    TCJob           	*job;
    TCFactory       	factory;

    uint32_t            processed;

    int                 has_aux;

    TCModule        	mux_main;
    TCModule        	mux_aux;

    TCRotateContext 	*rotor;
    TCRotateContext 	*rotor_aux;

    TCModuleExtraData	*vid_xdata;
    TCModuleExtraData 	*aud_xdata;

    int (*open)(TCMultiplexor *mux);
    int (*close)(TCMultiplexor *mux);
    int (*write)(TCMultiplexor *mux, int can_rotate,
                 TCFrameVideo *vframe, TCFrameAudio *aframe);
};



/*
 * tc_multiplexor_limit_frames:
 *     rotate output file(s) every given amount of encoded frames.
 *
 * Parameters:
 *        mux: pointer to the multiplexor to setup.
 *     frames: maximum of frames that every output chunk should contain.
 * Return value:
 *     None.
 * Preconditions:
 *     Multiplexor succesfully initialized
 *     (tc_multiplexor_init returned TC_OK).
 */
void tc_multiplexor_limit_frames(TCMultiplexor *mux, uint32_t frames);

/*
 * tc_multiplexor_limit_megabytes:
 *     rotate output file(s) after a given amount of data was encoded.
 *
 * Parameters:
 *           mux: pointer to the multiplexor to setup.
 *     megabytes: maximum size that every output chunk should have.
 * Return value:
 *     None.
 * Preconditions:
 *     Multiplexor succesfully initialized
 *     (tc_multiplexor_init returned TC_OK).
 */
void tc_multiplexor_limit_megabytes(TCMultiplexor *mux, uint32_t megabytes);


/*************************************************************************/

/* DOCME */

int tc_multiplexor_init(TCMultiplexor *mux, TCJob *job, TCFactory factory);

int tc_multiplexor_fini(TCMultiplexor *mux);

uint32_t tc_multiplexor_processed(TCMultiplexor *mux);

int tc_multiplexor_setup(TCMultiplexor *mux,
                         const char *mux_mod_name,
                         const char *mux_mod_name_aux);

int tc_multiplexor_shutdown(TCMultiplexor *mux);

int tc_multiplexor_open(TCMultiplexor *mux,
                        const char *sink_name,
                        const char *sink_name_aux,
                        TCModuleExtraData *vid_xdata,
                        TCModuleExtraData *aud_xdata);

int tc_multiplexor_close(TCMultiplexor *mux);

/* write and rotate if needed */
int tc_multiplexor_export(TCMultiplexor *mux,
                          TCFrameVideo *vframe, TCFrameAudio *aframe);

/* just write, never rotate */
int tc_multiplexor_write(TCMultiplexor *mux,
                         TCFrameVideo *vframe, TCFrameAudio *aframe);

/*************************************************************************/

#endif /* MULTIPLEXOR_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */

