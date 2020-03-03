/*
 * import_pv3.c -- module for importing audio/video data in the EarthSoft
 * PV3 codec
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#define MOD_NAME        "import_pv3.so"
#define MOD_VERSION     "v1.1 (2006-06-02)"
#define MOD_CAP         "Imports Earth Soft PV3 codec audio/video streams"
#define MOD_AUTHOR      "Andrew Church"

/*%*
 *%* DESCRIPTION 
 *%*   This module provides access to Earth Soft PV3 audio/video streams using
 *%*   win32 binary codecs and an internal win32 emulation layer (NO wine needed).
 *%*
 *%* #BUILD-DEPENDS
 *%*
 *%* DEPENDS
 *%*   PV3 win32 dlls.
 *%*
 *%* PROCESSING
 *%*   import/demuxer
 *%*
 *%* MEDIA
 *%*   video, audio
 *%*
 *%* #INPUT
 *%*
 *%* OUTPUT
 *%*   YUV420P, YUV422P, PCM
 *%*
 *%* OPTION
 *%*   dllpath (string)
 *%*     path/filename to load dv.dll from
 *%*/

#define MOD_FEATURES \
    TC_MODULE_FEATURE_DEMULTIPLEX|TC_MODULE_FEATURE_DECODE|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

#include "src/transcode.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"
#include "libtcvideo/tcvideo.h"
#include "aclib/ac.h"
#include "w32dll.h"

#ifndef PROBE_ONLY  // FIXME: temp hack, all the way down to probe_pv3()

/*************************************************************************/

/* Structures used by dv.dll: */

/* Main codec handle and function table */
struct pv3_codec_handle {
    struct {
        int (*init)(int a, int b);
        int (*fini)(void);
        int (*get_video_handle)(void);
        int (*get_audio_handle)(void);
    } *funcs;
};

/* Input video frame parameters */
struct pv3_input_vframe_params {
    uint8_t w8;         // width / 8
    uint8_t h8;         // height / 8
    uint16_t unknown1;
    uint32_t unknown2;
    int progressive;    // Version 2 only
};

/* Output video frame parameters */
struct pv3_output_vframe_params {
    uint32_t stride;
    void *outbuf;
};

/* video_functable.decode() parameter block */
struct pv3_video_decode_params {
    uint32_t dataset;       // Selects data set 0 or 1 (see pv3_decode())
    void *workbuf;          // Work buffer of at least 0x424? bytes
    struct pv3_input_vframe_params *in_params;
    const void **frameptr;  // Pointer to input frame
    struct pv3_output_vframe_params *out_params;
};

/* Video codec handle and function table. Note that particular versions of
 * dv.dll only handle a single file format; it is necessary to use the
 * proper version of dv.dll for the file to be transcoded. */
struct pv3_video_handle {
    union {
        struct {
            void *func0;
            void *func1;
            void *func2;
            void *func3;
            void *func4;
            void (*decode)(struct pv3_video_decode_params *params);
        } *funcs_v1;
        struct {
            void *func0;
            void *func1;
            void *func2;
            void *func3;
            void *func4;
            void *func5;
            void *func6;
            /* `unknown' here points to at least 0x500 bytes of memory */
            void (*set_quantizers)(void *unknown, const uint16_t *quantizers);
            void (*decode)(struct pv3_video_decode_params *params);
        } *funcs_v2;
    } u;
};

/* Raw audio data parameters */
struct pv3_audio_params {
    uint32_t rate;      // Sampling rate
    uint32_t pad04;
    uint64_t frame_index;  // Index of first audio frame (file start = 0)
    uint32_t frame_count;  // Number of audio frames for this video frame
    uint32_t pad14;
    void *audiobuf;
    uint32_t pad1C;
};

/* Encoded audio data parameters */
struct pv3_audio_encoded_params {
    uint32_t unknown;   // ???
    void *frame;        // Encoded frame
};

/* Audio codec handle and function table */
struct pv3_audio_handle {
    struct {
        void (*encode)(struct pv3_audio_params *in,
                       struct pv3_audio_encoded_params *out);
        void (*decode)(struct pv3_audio_encoded_params *in,
                       struct pv3_audio_params *out);
    } *funcs;
};

/*************************************************************************/

/* Maximum encoded frame size (as in PV3's AviUtl plugin) */
#define MAX_FRAME_SIZE  0x400000

/* Private data used by this module. */
typedef struct {
    char *dll_path;                     // Pathname (incl. file) for dv.dll
    W32DLLHandle codec_dll;             // DLL and codec handles
    struct pv3_codec_handle *codec_handle;
    struct pv3_video_handle *video_handle;
    struct pv3_audio_handle *audio_handle;
    uint32_t saved_fs;                  // Saved %fs value (for DLL)

    TCVHandle tcvhandle;                // tcvideo handle for YUY2->planar

    int fd;                             // File descriptor to read from
    int pv3_version;                    // PV3 file version (1 or 2)
    int width, height;                  // Width and height for v2 files
    int progressive;                    // Progressive flag for v2, 0=interlace
    uint16_t qtable[128];               // Quantizer tables for v2 files
    int framenum;                       // Frame number of loaded frame
    uint8_t framebuf[MAX_FRAME_SIZE];   // Buffer for loaded frame
} PrivateData;

/*************************************************************************/
/*************************************************************************/

/* PV3 codec DLL (dv.dll) interface. */

/*************************************************************************/

/**
 * pv3_call:  Call the given function with the given handle and up to two
 * additional arguments (of type intptr_t), using the calling sequence
 * expected by the DLL.  Used only by the DLL interface functions.
 *
 * Parameters:
 *         fs: Value to set %fs to.
 *     handle: Handle for the function.
 *       func: Function pointer to call.
 * Return value:
 *     Return value of the function, as an intptr_t.
 * Preconditions:
 *     func != NULL
 */

static intptr_t pv3_call(uint32_t fs, const void *handle, const void *func,...)
{
    va_list args;
    intptr_t arg1, arg2, retval;
    uint32_t spsave = 0;

    va_start(args, func);
    arg1 = va_arg(args, intptr_t);
    arg2 = va_arg(args, intptr_t);
    va_end(args);
    asm("mov %%eax, %%fs" : : "a" (fs));
    asm("mov %%esp, %5;" // Since the DLL uses the __stdcall calling
        "push %3;"       // convention, it will pop the "correct" number
        "push %2;"       // of arguments off the stack when it returns.
        "call *%1;"      // Explicitly save and restore the stack pointer
        "mov %5, %%esp;" // to make sure we get the right value back.
        : "=a" (retval)
        : "r" (func), "r" (arg1), "r" (arg2), "c" (handle), "m" (spsave));
    return retval;
}

/*************************************************************************/

/**
 * pv3_load_dll:  Load and initialize the PV3 codec DLL (dv.dll).
 *
 * Parameters:
 *     pd: PrivateData structure into which to store the DLL and codec
 *         handles.
 *     path: Path to dv.dll (including filename); if NULL or empty,
 *           "dv.dll" in the current directory is used.
 * Return value:
 *     Nonzero on success, zero on error.
 * Preconditions:
 *     pd != NULL
 */

static void pv3_unload_dll(PrivateData *pd);  /* forward declaration */

static int pv3_load_dll(PrivateData *pd)
{
    const char *path;
    void *(*entry)(void);

    pd->codec_dll = 0;
    pd->codec_handle = NULL;
    pd->video_handle = NULL;
    pd->audio_handle = NULL;

    path = pd->dll_path;
    if (!path || !*path)
        path = "dv.dll";

    if (!(pd->codec_dll = w32dll_load(path, 1))) {
        tc_log_error(MOD_NAME, "Cannot load %s: %s", path,
                     errno==ENOEXEC ? "Not a valid Win32 DLL file" :
                     errno==ETXTBSY ? "DLL initialization failed" :
                     strerror(errno));
        return 0;
    }

    /* Save %fs and restore before each call, just in case */
    asm("mov %%fs,%%eax" : "=a" (pd->saved_fs));

    entry = w32dll_lookup_by_name(pd->codec_dll, "_");
    if (!entry) {
        tc_log_error(MOD_NAME, "Cannot find dv.dll entry point");
        pv3_unload_dll(pd);
        return 0;
    }

    pd->codec_handle = (*entry)();
    if (!pd->codec_handle) {
        tc_log_error(MOD_NAME, "Unable to initialize dv.dll");
        pv3_unload_dll(pd);
        return 0;
    }

    pv3_call(pd->saved_fs, pd->codec_handle, pd->codec_handle->funcs->init,
             4, pd->pv3_version==1 ? 2 : 122);  /* magic numbers */
    pd->video_handle = (void *)pv3_call(pd->saved_fs, pd->codec_handle,
                                pd->codec_handle->funcs->get_video_handle);
    pd->audio_handle = (void *)pv3_call(pd->saved_fs, pd->codec_handle,
                                pd->codec_handle->funcs->get_audio_handle);
    if (!pd->video_handle || !pd->audio_handle) {
        tc_log_error(MOD_NAME, "Unable to retrieve codec handles");
        pv3_unload_dll(pd);
        return 0;
    }

    return 1;
}

/*************************************************************************/

/**
 * pv3_decode_frame:  Decode a frame.
 *
 * Parameters:
 *            pd: PrivateData structure.
 *      in_frame: Input (encoded) frame.
 *     out_video: Output video frame buffer (YUY2).
 *     out_audio: Output audio frame buffer.
 * Return value:
 *     Nonzero on success, zero on failure.
 * Preconditions:
 *     pd != NULL
 *     in_frame != NULL
 * Notes:
 *     The maximum theoretical video size is 2040x2040, for a total of
 *     just under 8MB of video data.  Audio will always be under 2048
 *     samples (audio frames), or 8192 bytes.
 */

static int pv3_decode_frame(PrivateData *pd, uint8_t *in_frame,
                            void *out_video, void *out_audio)
{
    if (!pd->codec_dll) {
        if (!pv3_load_dll(pd))
            return 0;
    }

    if (out_video) {
        struct pv3_input_vframe_params in_vparams;
        struct pv3_output_vframe_params out_vparams;
        struct pv3_video_decode_params vparams;
        char work_mem[0x800];
        int i;

        if (!pd->video_handle)
            return 0;
        memset(&in_vparams, 0, sizeof(in_vparams));
        if (pd->pv3_version == 1) {
            in_vparams.w8 = ((uint8_t *)in_frame)[4];
            in_vparams.h8 = ((uint8_t *)in_frame)[5];
        } else {
            in_vparams.w8 = pd->width/8;
            in_vparams.h8 = pd->height/8;
            in_vparams.progressive = pd->progressive;
        }
        memset(&out_vparams, 0, sizeof(out_vparams));
        out_vparams.stride = in_vparams.w8 * 8 * 2;
        out_vparams.outbuf = out_video;
        memset(&vparams, 0, sizeof(vparams));

        /* Set up quantizers for version 2 */
        if (pd->pv3_version == 2) {
            pv3_call(pd->saved_fs, pd->video_handle,
                     pd->video_handle->u.funcs_v2->set_quantizers,
                     work_mem, pd->qtable);
        }

        /* Process each set of data */
        vparams.workbuf = work_mem;
        vparams.in_params = &in_vparams;
        vparams.frameptr = (const void **)&in_frame;
        vparams.out_params = &out_vparams;
        for (i = 0; i < (pd->pv3_version==2 && !pd->progressive ? 4 : 2); i++){
            if (pd->pv3_version == 1) {
                vparams.dataset = i;
            } else {
                vparams.dataset = 1<<i;
            }
            if (pv3_call(pd->saved_fs, pd->video_handle,
                         pd->pv3_version == 1
                             ? pd->video_handle->u.funcs_v1->decode
                             : pd->video_handle->u.funcs_v2->decode,
                         &vparams) < 0
            ) {
                return 0;
            }
        }
    }

    if (out_audio) {
        int nsamples, i;
        uint16_t *dest = (uint16_t *)out_audio;

        if (pd->pv3_version == 1) {
            nsamples = in_frame[24]<<8 | in_frame[25];
        } else {  // PV3 version 2
            nsamples = in_frame[6]<<8 | in_frame[7];
        }
        if (nsamples > 0x800) {
            tc_log_warn(MOD_NAME, "Too many audio samples (%d) in frame %d,"
                        " truncating to %d", nsamples, pd->framenum, 0x800);
            nsamples = 0x800;
        }
        for (i = 0; i < nsamples*2; i++) {
            dest[i] = in_frame[0x200+i*2]<<8 | in_frame[0x201+i*2];
        }
    }

    return 1;
}

/*************************************************************************/

/**
 * pv3_unload_dll:  Shut down and unload the PV3 codec DLL.  Does nothing
 * if the codec has not been loaded.
 *
 * Parameters:
 *     pd: PrivateData structure in which handles are stored.
 * Return value:
 *     None.
 * Preconditions:
 *     pd != NULL
 */

static void pv3_unload_dll(PrivateData *pd)
{
    if (pd->codec_dll) {
        pd->video_handle = NULL;
        pd->audio_handle = NULL;
        if (pd->codec_handle) {
            pv3_call(pd->saved_fs, pd->codec_handle,
                     pd->codec_handle->funcs->fini);
        }
        pd->codec_handle = NULL;
        w32dll_unload(pd->codec_dll);
        pd->codec_dll = 0;
    }
}

/*************************************************************************/
/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * pv3_init:  Initialize this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int pv3_init(TCModuleInstance *self, uint32_t features)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "init");
    if (features == ~(uint32_t)0) {  // i.e. if called from old-style code
        self->features = MOD_FEATURES;
    } else {
        TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);
    }

    self->userdata = pd = tc_malloc(sizeof(PrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }
    pd->dll_path = NULL;
    pd->codec_dll = 0;
    pd->codec_handle = NULL;
    pd->video_handle = NULL;
    pd->audio_handle = NULL;
    pd->fd = -1;
    pd->framenum = -1;

    pd->tcvhandle = tcv_init();
    if (!pd->tcvhandle) {
        tc_log_error(MOD_NAME, "init: tcv_init() failed");
        free(pd);
        self->userdata = NULL;
        return TC_ERROR;
    }

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * pv3_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int pv3_fini(TCModuleInstance *self)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "fini");

    pd = self->userdata;

    pd->framenum = -1;
    if (pd->fd != -1) {
        close(pd->fd);
        pd->fd = -1;
    }
    if (pd->tcvhandle) {
        tcv_free(pd->tcvhandle);
        pd->tcvhandle = 0;
    }
    if (pd->codec_dll) {
        pv3_unload_dll(pd);
    }
    if (pd->dll_path) {
        free(pd->dll_path);
        pd->dll_path = NULL;
    }

    tc_free(self->userdata);
    self->userdata = NULL;
    return TC_OK;
}

/*************************************************************************/

/**
 * pv3_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int pv3_configure(TCModuleInstance *self,
                         const char *options,
                         TCJob *vob,
                         TCModuleExtraData *xdata[])
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    free(pd->dll_path);
    pd->dll_path = NULL;
    if (options) {
        char buf[1024];
        *buf = 0;
        optstr_get(options, "dllpath", "%1024s", buf);
        if (*buf)
            pd->dll_path = tc_strdup(buf);
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * pv3_stop:  Reset this instance of the module.  See tcmodule-data.h for
 * function details.
 */

static int pv3_stop(TCModuleInstance *self)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    pd->framenum = -1;
    if (pd->fd != -1) {
        close(pd->fd);
        pd->fd = -1;
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * pv3_inspect:  Return the value of an option in this instance of the
 * module.  See tcmodule-data.h for function details.
 */

static int pv3_inspect(TCModuleInstance *self,
                       const char *param, const char **value)
{
    PrivateData *pd;
    static char buf[TC_BUF_MAX];

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");

    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        tc_snprintf(buf, sizeof(buf),
                "Overview:\n"
                "    Decodes streams recorded by the Earth Soft PV3 recorder.\n"
                "Options available:\n"
                "    dllpath=path   Set path/filename to load dv.dll from\n");
        *value = buf;
    }
    if (optstr_lookup(param, "dllpath")) {
        tc_snprintf(buf, sizeof(buf), "%s", pd->dll_path ? pd->dll_path : "");
        *value = buf;
    }
    return TC_IMPORT_OK;
}

/*************************************************************************/

/**
 * pv3_demultiplex:  Demultiplex a frame of data.  See tcmodule-data.h for
 * function details.
 *
 * Notes:
 *     For PV3, we pass the entire frame to the codec DLL, so this just
 *     copies the input frame to the video frame buffer.  The audio is
 *     decoded here and passed on as PCM.
 */

static int pv3_demultiplex(TCModuleInstance *self,
                           vframe_list_t *vframe, aframe_list_t *aframe)
{
    PrivateData *pd;
    int framesize;
    off_t fpos;

    TC_MODULE_SELF_CHECK(self, "demultiplex");

    pd = self->userdata;
    if (pd->fd < 0) {
        tc_log_error(MOD_NAME, "demultiplex: no file opened!");
        return TC_ERROR;
    }
    fpos = lseek(pd->fd, 0, SEEK_CUR);  // for error messages

    /* Read frame header (but if this is a version 1 file and we're on the
     * first frame, the header will already have been read in) */
    if (!(pd->pv3_version == 1 && pd->framenum == -1)
     && (tc_pread(pd->fd, pd->framebuf, 512) != 512)
    ) {
        if (verbose & TC_DEBUG)
            tc_log_msg(MOD_NAME, "EOF reached");
        return TC_ERROR;
    }
    if (pd->pv3_version == 1 && memcmp(pd->framebuf, "PV3\1", 4) != 0) {
        tc_log_warn(MOD_NAME, "Not a valid PV3-1 frame at frame %d (ofs=%llX)",
                    pd->framenum+1, fpos);
        return TC_ERROR;
    }

    /* Find total frame length and read */
    framesize  = 512;  // header
    if (pd->pv3_version == 1) {
        framesize += (pd->framebuf[24]<<8 | pd->framebuf[25]) * 4;  // audio
        framesize  = (framesize+0xFFF) & -0x1000;                   // align
        /* Seems to reserve 8192-512 bytes for audio no matter what */
        if (framesize < 8192)
            framesize = 8192;
        framesize += pd->framebuf[28]<<24 | pd->framebuf[29]<<16    // video 0
                   | pd->framebuf[30]<< 8 | pd->framebuf[31];
        framesize  = (framesize+0x1F) & -0x20;                      // align
        framesize += pd->framebuf[32]<<24 | pd->framebuf[33]<<16    // video 1
                   | pd->framebuf[34]<< 8 | pd->framebuf[35];
        framesize  = (framesize+0xFFF) & -0x1000;                   // align
    } else {  // PV3 version 2
        framesize += (pd->framebuf[6]<<8 | pd->framebuf[7]) * 4;    // audio
        framesize  = (framesize+0xFFF) & -0x1000;                   // align
        framesize += pd->framebuf[384]<<24 | pd->framebuf[385]<<16  // video 0
                   | pd->framebuf[386]<< 8 | pd->framebuf[387];
        framesize  = (framesize+0x1F) & -0x20;                      // align
        framesize += pd->framebuf[388]<<24 | pd->framebuf[389]<<16  // video 1
                   | pd->framebuf[390]<< 8 | pd->framebuf[391];
        framesize  = (framesize+0x1F) & -0x20;                      // align
        framesize += pd->framebuf[392]<<24 | pd->framebuf[393]<<16  // video 2
                   | pd->framebuf[394]<< 8 | pd->framebuf[395];
        framesize  = (framesize+0x1F) & -0x20;                      // align
        framesize += pd->framebuf[396]<<24 | pd->framebuf[397]<<16  // video 3
                   | pd->framebuf[398]<< 8 | pd->framebuf[399];
        framesize  = (framesize+0xFFF) & -0x1000;                   // align
    }
    if (tc_pread(pd->fd, pd->framebuf+512, framesize-512) != framesize-512) {
        tc_log_warn(MOD_NAME, "Truncated frame at frame %d (ofs=%llX)",
                    pd->framenum+1, fpos);
        return TC_ERROR;
    }
    pd->framenum++;

    if (vframe) {
        ac_memcpy(vframe->video_buf, pd->framebuf, framesize);
        vframe->video_size = framesize;
        vframe->v_codec = TC_CODEC_PV3;
    }

    if (aframe) {
        /* The full frame won't fit in an audio buffer, so just decode it
         * here and pass it on as PCM. */
        if (pd->pv3_version == 1) {
            aframe->a_rate = pd->framebuf[12] << 24
                           | pd->framebuf[13] << 16
                           | pd->framebuf[14] <<  8
                           | pd->framebuf[15];
            aframe->audio_size = (pd->framebuf[24]<<8 | pd->framebuf[25]) * 4;
        } else {  // PV3 version 2
            aframe->a_rate = pd->framebuf[ 8] << 24
                           | pd->framebuf[ 9] << 16
                           | pd->framebuf[10] <<  8
                           | pd->framebuf[11];
            aframe->audio_size = (pd->framebuf[6]<<8 | pd->framebuf[7]) * 4;
        }
        aframe->a_bits = 16;
        aframe->a_chan = 2;
        if (!pv3_decode_frame(pd, pd->framebuf, NULL, aframe->audio_buf)) {
            tc_log_warn(MOD_NAME,
                        "demultiplex: decode audio failed, inserting silence");
            memset(aframe->audio_buf, 0, aframe->audio_size);
        }
        aframe->a_codec = TC_CODEC_PCM;
    }

    return framesize;
}

/*************************************************************************/

/**
 * pv3_decode_video:  Decode a frame of data.  See tcmodule-data.h for
 * function details.
 */

static int pv3_decode_video(TCModuleInstance *self,
                            vframe_list_t *inframe, vframe_list_t *outframe)
{
    vob_t *vob = tc_get_vob();  // for output codec--will this be in outframe?
    PrivateData *pd;
    static uint8_t yuy2_frame[2040*2040*2];  // max PV3 frame size

    TC_MODULE_SELF_CHECK(self, "decode_video");
    TC_MODULE_SELF_CHECK(inframe, "decode_video");
    TC_MODULE_SELF_CHECK(outframe, "decode_video");

    pd = self->userdata;

    if (!pv3_decode_frame(pd, inframe->video_buf, yuy2_frame, NULL))
        return TC_ERROR;

    // FIXME: do we set these here?
    if (pd->pv3_version == 1) {
        outframe->v_width = pd->framebuf[4] * 8;
        outframe->v_height = pd->framebuf[5] * 8;
    } else {
        outframe->v_width = pd->width;
        outframe->v_height = pd->height;
    }

    if (!tcv_convert(pd->tcvhandle, yuy2_frame, outframe->video_buf,
                     outframe->v_width, outframe->v_height, IMG_YUY2,
                     vob->im_v_codec==TC_CODEC_YUV422P ? IMG_YUV422P : IMG_YUV420P)
    ) {
        tc_log_warn(MOD_NAME, "Video format conversion failed");
        return TC_ERROR;
    }

    outframe->video_size = outframe->v_width * outframe->v_height;
    if (vob->im_v_codec == TC_CODEC_YUV422P) {
        outframe->video_size +=
            (outframe->v_width/2) * outframe->v_height * 2;
    } else {
        outframe->video_size +=
            (outframe->v_width/2) * (outframe->v_height/2) * 2;
    }

    return TC_OK;
}

/*************************************************************************/

static const TCCodecID pv3_codecs_in[] = { TC_CODEC_PV3, TC_CODEC_ERROR };
static const TCCodecID pv3_codecs_out[] = { TC_CODEC_YUV420P, TC_CODEC_YUV422P,
                                            TC_CODEC_ERROR };
static const TCCodecID pv3_audio_codecs[] = { TC_CODEC_ERROR };
static const TCFormatID pv3_formats_in[] = { TC_FORMAT_PV3, TC_FORMAT_ERROR };
static const TCFormatID pv3_formats_out[] = { TC_FORMAT_ERROR };

static const TCModuleInfo pv3_info = {
    .features         = MOD_FEATURES,
    .flags            = MOD_FLAGS,
    .name             = MOD_NAME,
    .version          = MOD_VERSION,
    .description      = MOD_CAP,
    .codecs_video_in  = pv3_codecs_in,
    .codecs_video_out = pv3_codecs_out,
    .codecs_audio_in  = pv3_audio_codecs,
    .codecs_audio_out = pv3_audio_codecs,
    .formats_in       = pv3_formats_in,
    .formats_out      = pv3_formats_out
};

static const TCModuleClass pv3_class = {
    TC_MODULE_CLASS_HEAD(pv3),

    .init         = pv3_init,
    .fini         = pv3_fini,
    .configure    = pv3_configure,
    .stop         = pv3_stop,
    .inspect      = pv3_inspect,

    .decode_video = pv3_decode_video,
    //.demultiplex  = pv3_demultiplex,  // FIXME: needs conversion to API3
};

TC_MODULE_ENTRY_POINT(pv3)

/*************************************************************************/
/*************************************************************************/

/* Old-fashioned module interface. */

static TCModuleInstance mod_video, mod_audio;

static int verbose_flag;
static int capability_flag = TC_CAP_YUV | TC_CAP_YUV422 | TC_CAP_PCM;
#define MOD_PRE pv3
#define MOD_CODEC "(video) PV3 | (audio) PCM"
#include "import_def.h"
#include "magic.h"

/*************************************************************************/

MOD_open
{
    TCModuleInstance *mod = NULL;
    PrivateData *pd = NULL;
    const char *fname = NULL;
    uint8_t buf[512];

    if (param->flag == TC_VIDEO) {
        mod = &mod_video;
        fname = vob->video_in_file;
    } else if (param->flag == TC_AUDIO) {
        mod = &mod_audio;
        fname = vob->audio_in_file;
    } else {
        return TC_ERROR;
    }

    if (pv3_init(mod, ~(uint32_t)0) < 0)
        return TC_ERROR;
    pd = mod->userdata;
    if (vob->im_v_string)
        pd->dll_path = tc_strdup(vob->im_v_string);

    param->fd = NULL;  /* we handle the reading ourselves */
    pd->fd = open(fname, O_RDONLY);
    if (pd->fd < 0) {
        tc_log_error(MOD_NAME, "Unable to open %s: %s", fname,
                     strerror(errno));
        free(pd->framebuf);
        pv3_fini(mod);
        return TC_ERROR;
    }
    if (tc_pread(pd->fd, buf, 512) != 512) {
        tc_log_error(MOD_NAME, "%s is too short", fname);
        free(pd->framebuf);
        pv3_fini(mod);
        return TC_ERROR;
    }
    if (memcmp(buf, "PV3", 3) != 0) {
        tc_log_warn(MOD_NAME, "%s is not a valid PV3 file", fname);
        free(pd->framebuf);
        pv3_fini(mod);
        return TC_ERROR;
    }
    if (buf[3] != 1 && buf[3] != 2) {
        tc_log_warn(MOD_NAME, "Invalid PV3 version %d in %s", buf[3], fname);
        free(pd->framebuf);
        pv3_fini(mod);
        return TC_ERROR;
    }
    pd->pv3_version = buf[3];
    if (pd->pv3_version == 1) {
        /* For version 1 files, copy the 512-byte header we just read
         * into the frame buffer, so that the demultiplexer has access
         * to it without requiring a seek on the input file */
        memcpy(pd->framebuf, buf, 512);
    } else {  /* A version 2 file */
        /* Copy file header data into private data structure */
        int i;
        pd->width = buf[4] * 16;
        pd->height = buf[5] * 8;
        pd->progressive = buf[6] & 1;
        for (i = 0; i < 128; i++) {
            pd->qtable[i] = buf[256+i*2]<<8 | buf[257+i*2];
        }
        /* Skip to the first frame header */
        {
            uint8_t dummy[16384-512];
            if (tc_pread(pd->fd, dummy, sizeof(dummy)) != sizeof(dummy)) {
                tc_log_error(MOD_NAME, "Unexpected EOF reading %s header",
                             fname);
                free(pd->framebuf);
                pv3_fini(mod);
                return TC_ERROR;
            }
        }
    }

    return TC_OK;
}

/*************************************************************************/

MOD_close
{
    TCModuleInstance *mod = NULL;

    if (param->flag == TC_VIDEO) {
        mod = &mod_video;
    } else if (param->flag == TC_AUDIO) {
        mod = &mod_audio;
    } else {
        return TC_ERROR;
    }

    pv3_fini(mod);
    return TC_OK;
}

/*************************************************************************/

MOD_decode
{
    TCModuleInstance *mod = NULL;
    PrivateData *pd = NULL;

    if (param->flag == TC_VIDEO) {
        mod = &mod_video;
    } else if (param->flag == TC_AUDIO) {
        mod = &mod_audio;
    } else {
        return TC_ERROR;
    }
    pd = mod->userdata;

    if (pd->fd < 0) {
        tc_log_error(MOD_NAME, "No file open in decode!");
        return TC_ERROR;
    }

    if (param->flag == TC_VIDEO) {
        vframe_list_t vframe1, vframe2;
        vframe1.video_buf = pd->framebuf;
        vframe2.video_buf = param->buffer;
        if (param->attributes & TC_FRAME_IS_OUT_OF_RANGE) {
            if (pv3_demultiplex(mod, &vframe2, NULL) < 0)
                return TC_ERROR;
        } else {
            if (pv3_demultiplex(mod, &vframe1, NULL) < 0)
                return TC_ERROR;
            if (pv3_decode_video(mod, &vframe1, &vframe2) < 0)
                return TC_ERROR;
        }
        param->size = vframe2.video_size;
    } else if (param->flag == TC_AUDIO) {
        aframe_list_t aframe;
        aframe.audio_buf = param->buffer;
        if (pv3_demultiplex(mod, NULL, &aframe) < 0)
            return TC_ERROR;
        param->size = aframe.audio_size;
    }

    return TC_OK;
}

#endif  // !PROBE_ONLY

/*************************************************************************/

#ifdef PROBE_ONLY

#include "tcinfo.h"
#include "tc.h"
#include "magic.h"

void probe_pv3(info_t *ipipe)
{
    uint8_t buf[0x4200];
    int aspect_w, aspect_h, interlaced;

    if (tc_pread(ipipe->fd_in, buf, sizeof(buf)) != sizeof(buf)) {
        tc_log_warn(MOD_NAME, "Premature end of input file");
        ipipe->error = 1;
        return;
    }
    /* Sanity check--this should be caught by the caller */
    if (memcmp(buf, "PV3", 3) != 0) {
        tc_log_warn(MOD_NAME, "Input is not PV3 video");
        ipipe->error = 1;
        return;
    }
    if (buf[3] != 1 && buf[3] != 2) {  /* version number */
        tc_log_warn(MOD_NAME, "Invalid PV3 version %d", buf[3]);
        ipipe->error = 1;
        return;
    }

    ipipe->probe_info->magic = TC_MAGIC_PV3;
    ipipe->probe_info->codec = TC_CODEC_PV3;

    if (buf[3] == 1) {
        /* Version 1 file processing */
        ipipe->probe_info->width = buf[4] * 8;
        ipipe->probe_info->height = buf[5] * 8;
        aspect_w = buf[6];
        aspect_h = buf[7];
        interlaced = !(buf[8] & 1);
        ipipe->probe_info->track[0].samplerate =
            buf[12]<<24 | buf[13]<<16 | buf[14]<<8 | buf[15];
    } else {
        /* Version 2 file processing */
        ipipe->probe_info->width = buf[4] * 16;
        ipipe->probe_info->height = buf[5] * 8;
        aspect_w = buf[0x4100]<<8 | buf[0x4101];
        aspect_h = buf[0x4102]<<8 | buf[0x4103];
        interlaced = !(buf[6] & 1);
        ipipe->probe_info->track[0].samplerate =
            buf[0x4008]<<24 | buf[0x4009]<<16 | buf[0x400A]<<8 | buf[0x400B];
    }
    if (aspect_w == 4 && aspect_h == 3)
        ipipe->probe_info->asr = 2;
    else if (aspect_w == 16 && aspect_h == 9)
        ipipe->probe_info->asr = 3;
    ipipe->probe_info->fps = (interlaced ? 30 : 60)/1.001;  // argh stupid NTSC
    ipipe->probe_info->frc = (interlaced ? 4 : 7);

    ipipe->probe_info->track[0].bits = 16;
    ipipe->probe_info->track[0].chan = 2;
    ipipe->probe_info->track[0].bitrate =
        ipipe->probe_info->track[0].samplerate * 32 / 1000;
    ipipe->probe_info->track[0].format = TC_CODEC_PCM;
    ipipe->probe_info->num_tracks = 1;
}

#endif  // PROBE_ONLY

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
