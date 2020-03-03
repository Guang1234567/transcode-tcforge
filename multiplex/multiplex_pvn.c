/*
 * multiplex_pvn.c -- module for writing PVN video streams
 * (http://www.cse.yorku.ca/~jgryn/research/pvnspecs.html)
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "src/transcode.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"
#include "libtcvideo/tcvideo.h"

#ifdef OMS_COMPATIBLE
#define MOD_NAME        "export_pvn.so"
#else
#define MOD_NAME        "multiplex_pvn.so"
#endif

#define MOD_VERSION     "v1.1.0 (2009-02-08)"
#define MOD_CAP         "Writes PVN video files"
#define MOD_AUTHOR      "Andrew Church"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_MULTIPLEX|TC_MODULE_FEATURE_VIDEO
    
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE


#ifdef OMS_COMPATIBLE
#define METHOD extern
#else
#define METHOD static
#endif

/*************************************************************************/

/* Local data structure: */

typedef struct {
    int width, height;     // Frame width and height (to catch changes)
    int fd;                // Output file descriptor
    int framecount;        // Number of frames written
    off_t framecount_pos;  // File position of frame count (for rewriting)
    int decolor;
    double ex_fps;
} PrivateData;

/*************************************************************************/
#ifdef OMS_COMPATIBLE
/* the needed forward declarations                                       */

int pvn_init(TCModuleInstance *self, uint32_t features);
int pvn_configure(TCModuleInstance *self, const char *options, TCJob *vob,
                  TCModuleExtraData *xdata[]);
int pvn_fini(TCModuleInstance *self);
int pvn_write_video(TCModuleInstance *self,
                    TCFrameVideo *vframe);
#endif

/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * pvn_stop:  Reset this instance of the module.  See tcmodule-data.h for
 * function details.
 */
static int pvn_stop(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "stop");
    return TC_OK;
}

/**
 * pvn_stop:  Close the file used for processing.  See tcmodule-data.h for
 * function details.
 */
static int pvn_close(TCModuleInstance *self)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "close");

    pd = self->userdata;

    if (pd->fd != -1) {
        if (pd->framecount > 0 && pd->framecount_pos > 0) {
            /* Write out final frame count, if we can */
            if (lseek(pd->fd, pd->framecount_pos, SEEK_SET) != (off_t)-1) {
                char buf[11];
                int len = tc_snprintf(buf, sizeof(buf), "%10d",pd->framecount);
                if (len > 0)
                    tc_pwrite(pd->fd, buf, len);
            }
        }
        close(pd->fd);
        pd->fd = -1;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * pvn_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

METHOD int pvn_configure(TCModuleInstance *self,
                         const char *options,
                         TCJob *vob,
                         TCModuleExtraData *xdata[])
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd->width   = vob->ex_v_width;
    pd->height  = vob->ex_v_height;
    pd->decolor = vob->decolor;
    pd->ex_fps  = vob->ex_fps;

    return TC_OK;
}

METHOD int pvn_open(TCModuleInstance *self, const char *filename,
                    TCModuleExtraData *xdata[])
{
    char buf[TC_BUF_MAX] = { '\0' };
    int len = 0;
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "open");

    /* FIXME: stdout should be handled in a more standard fashion */
    if (strcmp(filename, "-") == 0) {  // allow /dev/stdout too?
        pd->fd = 1;
    } else {
        pd->fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (pd->fd < 0) {
            tc_log_error(MOD_NAME, "Unable to open %s: %s",
                         filename, strerror(errno));
            goto fail;
        }
    }
    len = tc_snprintf(buf, sizeof(buf), "PV%da\r\n%d %d\r\n",
                      pd->decolor ? 5 : 6,
                      pd->width, pd->height);
    if (len < 0)
        goto fail;
    if (tc_pwrite(pd->fd, buf, len) != len) {
        tc_log_error(MOD_NAME, "Unable to write header to %s: %s",
                     filename, strerror(errno));
        goto fail;
    }
    pd->framecount_pos = lseek(pd->fd, 0, SEEK_CUR);  // failure okay
    len = tc_snprintf(buf, sizeof(buf), "%10d\r\n8\r\n%lf\r\n",
                      0, (double)pd->ex_fps);
    if (len < 0)
        goto fail;
    if (tc_pwrite(pd->fd, buf, len) != len) {
        tc_log_error(MOD_NAME, "Unable to write header to %s: %s",
                     filename, strerror(errno));
        goto fail;
    }

    return TC_OK;

  fail:
    pvn_close(self);
    return TC_ERROR;
}

/*************************************************************************/

/**
 * pvn_init:  Initialize this instance of the module.  See tcmodule-data.h
 * for function details.
 */

METHOD int pvn_init(TCModuleInstance *self, uint32_t features)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    self->userdata = pd = tc_malloc(sizeof(PrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }
    pd->fd = -1;
    pd->framecount = 0;
    pd->framecount_pos = 0;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * pvn_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

METHOD int pvn_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    pvn_stop(self);
    tc_free(self->userdata);
    self->userdata = NULL;

    return TC_OK;
}

/*************************************************************************/


/*************************************************************************/

/**
 * pvn_inspect:  Return the value of an option in this instance of the
 * module.  See tcmodule-data.h for function details.
 */

static int pvn_inspect(TCModuleInstance *self,
                       const char *param, const char **value)
{
    static char buf[TC_BUF_MAX];

    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");

    if (optstr_lookup(param, "help")) {
        tc_snprintf(buf, sizeof(buf),
                "Overview:\n"
                "    Writes a PVN video stream (format PV6a, 8-bit data).\n"
                "    A grayscale file (PV5a) is written instead if the -K\n"
                "    switch is given to transcode.\n"
                "    The RGB colorspace must be used (-V rgb24).\n"
                "No options available.\n");
        *value = buf;
    }
    return TC_OK;
}


/*************************************************************************/

/**
 * pvn_write_video:  Multiplex a frame of data.  See tcmodule-data.h for
 * function details.
 */

METHOD int pvn_write_video(TCModuleInstance *self,
                           TCFrameVideo *vframe)
{
    PrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "multiplex");

    pd = self->userdata;
    if (pd->fd == -1) {
        tc_log_error(MOD_NAME, "multiplex: no file opened!");
        return TC_ERROR;
    }

    if (vframe->v_width != pd->width || vframe->v_height != pd->height) {
        tc_log_error(MOD_NAME, "Video frame size changed in midstream!");
        return TC_ERROR;
    }
    if (vframe->v_codec != TC_CODEC_RGB24) {
        tc_log_error(MOD_NAME, "Invalid codec for video frame!");
        return TC_ERROR;
    }
    if (vframe->video_len != pd->width * pd->height * 3
     && vframe->video_len != pd->width * pd->height  // for grayscale
    ) {
        tc_log_error(MOD_NAME, "Invalid size for video frame!");
        return TC_ERROR;
    }
    if (tc_pwrite(pd->fd, vframe->video_buf, vframe->video_len)
        != vframe->video_len
    ) {
        tc_log_error(MOD_NAME, "Error writing frame %d to output file: %s",
                     pd->framecount, strerror(errno));
        return TC_ERROR;
    }
    pd->framecount++;
    return vframe->video_len;
}

/*************************************************************************/

static const TCCodecID pvn_codecs_audio_in[] = { 
    TC_CODEC_ERROR 
};
static const TCCodecID pvn_codecs_video_in[] = { 
    TC_CODEC_RGB24, TC_CODEC_ERROR 
};
static const TCFormatID pvn_formats_out[] = { 
    TC_FORMAT_PVN, TC_CODEC_ERROR 
};
/* a multiplexor is at the end of pipeline */
TC_MODULE_MPLEX_FORMATS_CODECS(pvn);

TC_MODULE_INFO(pvn);

static const TCModuleClass pvn_class = {
    TC_MODULE_CLASS_HEAD(pvn),

    .init        = pvn_init,
    .fini        = pvn_fini,
    .configure   = pvn_configure,
    .stop        = pvn_stop,
    .inspect     = pvn_inspect,

    .open        = pvn_open,
    .close       = pvn_close,
    .write_video = pvn_write_video,
};

TC_MODULE_ENTRY_POINT(pvn);

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
