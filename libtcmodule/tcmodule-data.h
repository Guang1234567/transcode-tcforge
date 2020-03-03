/*
 * tcmodule-data.h -- transcode module system, take two: data types.
 * (C) 2005-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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


/*
 * this header file contains basic data types declarations for transcode's
 * new module system (1.1.x and later).
 * Should not be included directly, but doing this will not harm anything.
 */
#ifndef TCMODULE_DATA_H
#define TCMODULE_DATA_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>

#include "tcmodule-info.h"

#include "libtcutil/memutils.h"
#include "libtc/tcframes.h"
#include "tccore/job.h"

#define TC_MODULE_VERSION_MAJOR     3
#define TC_MODULE_VERSION_MINOR     2
#define TC_MODULE_VERSION_MICRO     0

#define TC_MAKE_MOD_VERSION(MAJOR, MINOR, MICRO) \
         (((    0UL & 0xFF) << 24) \
         |(((MAJOR) & 0xFF) << 16) \
         |(((MINOR) & 0xFF) <<  8) \
         | ((MICRO) & 0xFF))

#define TC_MODULE_VERSION   \
        TC_MAKE_MOD_VERSION(TC_MODULE_VERSION_MAJOR, \
                            TC_MODULE_VERSION_MINOR, \
                            TC_MODULE_VERSION_MICRO)

#define TC_MODULE_EXTRADATA_SIZE    1024
#define TC_MODULE_EXTRADATA_MAX	    16

/*
 * allowed status transition chart:
 *
 *                     init                 configure
 *  +--------------+ -----> +-----------+ ------------> +--------------+
 *  | module limbo |        | [created] |               | [configured] |
 *  +--------------+ <----- +-----------+ <-----------  +--------------+
 *                    fini  A                stop       |
 *                          |                           |
 *                          |                           |
 *                          |   any specific operation: |
 *                          |       encode_*, filter_*, |
 *                          |            multiplex, ... |
 *                          |                           V
 *                          `-------------- +-----------+
 *                                 stop     | [running] |
 *                                          +-----------+
 *
 */


typedef struct tcmoduleextradata_ TCModuleExtraData;
struct tcmoduleextradata_ {
    int         stream_id; /* container ordering */
    TCCodecID   codec;
    TCMemChunk  extra;
};

/*
 * Data structure private for each instance.
 * This is an almost-opaque structure.
 *
 * The main purpose of this structure is to let each module (class)
 * to have it's private data, totally opaque to loader and to the
 * client code.
 * This structure also keep some accounting informations useful
 * both for module code and for loader. Those informations are
 * a module id, which identifies uniquely a given module instance
 * in a given timespan, and a string representing the module 'type',
 * a composition of it's class and specific name.
 */
typedef struct tcmoduleinstance_ TCModuleInstance;
struct tcmoduleinstance_ {
    int         id;       /* instance id */
    const char  *type;    /* packed class + name of module */
    uint32_t    features; /* subset of enabled features for this instance */

    void        *userdata; /* opaque to factory, used by each module */

    // FIXME: add status to enforce correct operation sequence?
};

/*
 * Extradata ordering notice (especially for demuxers)
 * - first all video tracks.
 * - then all audio tracks.
 * - then any other track, if any.
 */

/* can be shared between _all_ instances */
typedef struct tcmoduleclass_ TCModuleClass;
struct tcmoduleclass_ {
    uint32_t    version;

    int         id; /* opaque internal handle */

    const       TCModuleInfo *info;

    /* mandatory operations: */
    int (*init)(TCModuleInstance *self, uint32_t features);
    int (*fini)(TCModuleInstance *self);
    int (*configure)(TCModuleInstance *self, const char *options,
                     TCJob *vob, TCModuleExtraData *xdata[]);
    int (*stop)(TCModuleInstance *self);
    int (*inspect)(TCModuleInstance *self,
                   const char *param, const char **value);

    /*
     * not-mandatory operations, a module doing something useful implements
     * at least one of following.
     */
    int (*open)(TCModuleInstance *self, const char *filename,
                TCModuleExtraData *xdata[]);
    int (*close)(TCModuleInstance *self);

    int (*encode_audio)(TCModuleInstance *self,
                        TCFrameAudio *inframe, TCFrameAudio *outframe);
    int (*encode_video)(TCModuleInstance *self,
                        TCFrameVideo *inframe, TCFrameVideo *outframe);

    int (*decode_audio)(TCModuleInstance *self,
                        TCFrameAudio *inframe, TCFrameAudio *outframe);
    int (*decode_video)(TCModuleInstance *self,
                        TCFrameVideo *inframe, TCFrameVideo *outframe);

    int (*filter_audio)(TCModuleInstance *self, TCFrameAudio *frame);
    int (*filter_video)(TCModuleInstance *self, TCFrameVideo *frame);

    int (*flush_audio)(TCModuleInstance *self, TCFrameAudio *outframe,
                       int *frame_returned);
    int (*flush_video)(TCModuleInstance *self, TCFrameVideo *outframe,
                       int *frame_returned);

    int (*write_video)(TCModuleInstance *self, TCFrameVideo *frame);
    int (*write_audio)(TCModuleInstance *self, TCFrameAudio *frame);

    int (*read_video)(TCModuleInstance *self, TCFrameVideo *frame);
    int (*read_audio)(TCModuleInstance *self, TCFrameAudio *frame);
};

/**************************************************************************
 * TCModuleClass operations documentation:                                *
 **************************************************************************
 *
 * For all the following, unless specified:
 * Return Value:
 *      TC_OK: succesfull.
 *      TC_ERROR:
 *         error occurred. A proper message should be sent to user using
 *         tc_log*().
 *
 *
 * init:
 *      initialize a module, acquiring all needed resources.
 *      A module must also be configure()d before to be used.
 *      An initialized, but unconfigured, module CAN'T DELIVER
 *      a proper result when a specific operation (encode, demultiplex)
 *      is requested. To request an operation in a initialized but 
 *      unconfigured module will result in an undefined behaviour.
 * Parameters:
 *          self: pointer to the module instance to initialize.
 *      features: select feature of this module to initialize.
 * Return Value:
 *      See the initial note above.
 * Postconditions:
 *      The given module instance is ready to be configured.
 *
 *
 * fini:
 *      finalize an initialized module, releasing all acquired resources.
 *      A finalized module MUST be re-initialized before any new usage.
 * Parameters:
 *      self: pointer to the module instance to finalize.
 * Return Value:
 *      See the initial note above.
 * Preconditions:
 *      module was already initialized. To finalize a uninitialized module
 *      will cause an undefined behaviour.
 *      An unconfigured module can be finalized safely.
 * Postconditions:
 *      all resources acquired by given module are released.
 *
 *
 * configure:
 *      setup a module using module specific options and required data
 *      (via `vob' structure). It is requested to configure a module
 *      before to be used safely to perform any specific operation.
 *      Trying to configure a non-initialized module will cause an
 *      undefined behaviour.
 * Parameters:
 *      self: pointer to the module instance to configure.
 *      options: string contaning module options.
 *               Syntax is fixed (see optstr),
 *               semantic is module-dependent.
 *      vob: pointer to a TCJob structure.
 *      xdata: array of extradata pointer, one for each stream.
 *             decoders can use this array as input source, while
 *             encoders can use it as output source.
 *             In both cases, the content of the array must be valid (or,
 *             respectively, it is guaranteed valid) until the first stop()
 *             done on this module.
 * Return Value:
 *      See the initial note above.
 * Preconditions:
 *      The given module instance was already initialized AND stopped.
 *      A module MUST be stop()ped before to be configured again, otherwise
 *      an undefined behaviour will occur (expect at least resource leaks).
 * Postconditions:
 *      The given module instance is ready to perform any supported operation.
 *
 *
 * stop:
 *      reset a module and prepare for reconfiguration or finalization.
 *      This means to flush buffers, close open files and so on,
 *      but NOT release the reseource needed by a module to work.
 *      Please note that this operation can do actions similar, but
 *      not equal, to `fini'. Also note that `stop' can be invoked
 *      zero or multiple times during the module lifetime, but
 *      `fini' WILL be invoked one and only one time.
 * Parameters:
 *      self: pointer to the module instance to stop.
 * Return Value:
 *      See the initial note above.
 * Preconditions:
 *      The given module instance was already initialized. Try to (re)stop
 *      an unitialized module will cause an undefined behaviour.
 *      It's safe to stop an unconfigured module.
 * Postconditions:
 *      The given module instance is ready to be reconfigured safely.
 *
 *
 * inspect:
 *      expose the current value of an a tunable option in a module,
 *      represented as a string.
 *      Every module MUST support two special options:
 *      'all': will return a packed, human-readable representation
 *             of ALL tunable parameters in a given module, or an
 *             empty string if module hasn't any tunable option.
 *             This string must be in the same form accepted by
 *             `configure' operation.
 *      'help': will return a formatted, human-readable string
 *              with module overview, tunable options and explanation.
 * Parameters:
 *      self: pointer to the module instance to inspect.
 *      param: name of parameter to inspect
 *      value: when method succesfully returns, will point to a constant
 *             string (that MUST NOT be *free()d by calling code)
 *             containing the actual value of requested parameter.
 *             PLEASE NOTE that this value CAN change between
 *             invocations of this method.
 * Return value:
 *      TC_OK:
 *         succesfull. That means BOTH the request was honoured OR
 *         the requested parameter isn't known and was silently ignored.
 *      TC_ERROR:
 *         INTERNAL error, reason will be tc_log*()'d out.
 * Preconditions:
 *      module was already initialized.
 *      Inspecting a uninitialized module will cause an
 *      undefined behaviour.
 *
 *
 * open:
 *      open the file for processing.
 *      This method shall be implemented only by demuxer and muxer modules,
 *      otherwise it will be useless (e.g. never called by the core).
 *      Please note that this method can (and usually is) called multiple times
 *      during the processing.
 *      It is *NOT SAFE* to assume that
 *      number of calls to configure() == number of calls to open().
 * Parameters:
 *      self: pointer to a module instance.
 *      filename: path of the file to open. Always provided by the core.
 *                It is NOT SAFE to change or ignore it.
 *      xdata: array of extradata pointer, one for each stream.
 *             muxers can use this array as input source, while
 *             demuxers can use it as output source.
 *             In both cases, the content of the array must be valid (or,
 *             respectively, it is guaranteed valid) until the first close()
 *             done on this module.
 * Return Value:
 *      See the initial note above.
 * Preconditions:
 *      The given module instance was already initialized *AND* configured.
 *
 *
 * close:
 *      This method shall be implemented only by demuxer and muxer modules,
 *      otherwise it will be useless (e.g. never called by the core).
 *      Please note that this method can (and usually is) called multiple times
 *      during the processing.
 *      It is *NOT SAFE* to assume that
 *      number of calls to close() == number of calls to stop().
 * Parameters:
 *      self: pointer to a module instance.
 * Return Value:
 *      See the initial note above.
 * Postconditions:
 *      The given module instance has closed any file opened through a former
 *      open() method call.
 *
 *
 * decode_{audio,video}:
 * encode_{audio,video}:
 *      decode or encode a given audio/video frame, storing
 *      (de)compressed data into another frame.
 *      Specific module loaded implements various codecs.
 * Parameters:
 *      self: pointer to a module instance.
 *      inframe: pointer to {audio,video} frame data to decode/encode.
 *      outframe: pointer to {audio,video} frame which will hold
 *                (un)compressed data.
 * Return Value:
 *      See the initial note above.
 * Preconditions:
 *      module was already initialized AND configured.
 *      To use a uninitialized and/or unconfigured module
 *      for decoding/encoding will cause an undefined behaviour.
 *      inframe != NULL && outframe != NULL.
 *
 *
 * flush_{audio,video}:
 *      flush the internal module buffer. This method is called by
 *      the encoder core just after the encoder loop stopped.
 *      If multiple frames are buffered, this function should return
 *      only the first such frame; the function will be called multiple
 *      times to obtain subsequent frames.
 * Parameters:
 *      self: pointer to a module instance. 
 *      frame: pointer to an {audio,video} frame to be filled (if needed).
 *      frame_returned: on return, set to 1 if a frame was returned, else 0.
 * Return Value:
 *      See the initial note above.
 * Preconditions:
 *      As per encode_{audio,video}; also frame_returned != NULL.
 *
 *
 * filter_{audio,video}:
 *      apply an in-place transformation to a given audio/video frame.
 *      Specific module loaded determines the action performend on
 *      given frame.
 * Parameters:
 *      self: pointer to a module instance.
 *      frame: pointer to {audio,video} frame data to elaborate.
 * Return Value:
 *      See the initial note above.
 * Preconditions:
 *      module was already initialized AND configured.
 *      To use a uninitialized and/or unconfigured module
 *      for filter will cause an undefined behaviour.
 *
 *
 * write_{audio,video}:
 *      merge a given encoded {audio,video} frame in the output stream.
 * Parameters:
 *      self: pointer to a module instance.
 *      frame: pointer to {audio,video} frame to multiplex.
 *             if NULL, don't multiplex anything for this invokation.
 * Return value:
 *      TC_ERROR:
 *         an error occurred. A proper message should be sent to user using
 *         tc_log*().
 *      >0 number of bytes writed for multiplexed frame.
 * Preconditions:
 *      module was already initialized AND configured.
 *      To use a uninitialized and/or unconfigured module
 *      for multiplex will cause an undefined behaviour.
 *
 *
 * read_{audio,video}:
 *      extract given encode {audio,video} frame from the input stream.
 * Parameters:
 *      self: pointer to a module instance.
 *      frame: pointer to a {audio,video} frame to be filled with the one
 *             extracted from the stream.
 * Return value:
 *      TC_ERROR:
 *         error occurred. A proper message should be sent to user using
 *         tc_log*().
 *      >0 number of bytes read for demultiplexed frame.
 * Preconditions:
 *      module was already initialized AND configured.
 *      To use a uninitialized and/or unconfigured module
 *      for demultiplex will cause an undefined behaviour.
 */

#endif /* TCMODULE_DATA_H */
