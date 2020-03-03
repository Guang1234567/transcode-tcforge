/*
 *  multiplex_y4m.c -- pack a yuv420p stream in YUV4MPEG2 format
 *  (C) 2005-2010 Francesco Romani <fromani at gmail dot com>
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

#include "src/transcode.h"
#include "libtcutil/optstr.h"
#include "libtc/ratiocodes.h"

#include "libtcmodule/tcmodule-plugin.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#if defined(HAVE_MJPEGTOOLS_INC)
#include "yuv4mpeg.h"
#include "mpegconsts.h"
#else
#include "mjpegtools/yuv4mpeg.h"
#include "mjpegtools/mpegconsts.h"
#endif

#define MOD_NAME    "multiplex_y4m.so"
#define MOD_VERSION "v0.2.0 (2009-02-08)"
#define MOD_CAP     "write YUV4MPEG2 video stream"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_MULTIPLEX|TC_MODULE_FEATURE_VIDEO
    

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE
    

static const char tc_y4m_help[] = ""
    "Overview:\n"
    "    this module writes a yuv420p video stream using YUV4MPEG2 format\n"
    "Options:\n"
    "    help    produce module overview and options explanations\n";

typedef struct {
    int fd_vid;

    y4m_frame_info_t frameinfo;
    y4m_stream_info_t streaminfo;

    int width;
    int height;
    vob_t *vob;
} Y4MPrivateData;


static int tc_y4m_inspect(TCModuleInstance *self,
                      const char *options, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");
    
    if (optstr_lookup(options, "help")) {
        *value = tc_y4m_help;
    }

    return TC_OK;
}

static int tc_y4m_open(TCModuleInstance *self, const char *filename,
                       TCModuleExtraData *xdata[])
{
    int asr, ret;
    y4m_ratio_t framerate;
    y4m_ratio_t asr_rate;
    Y4MPrivateData *pd = NULL;
    vob_t *vob = NULL;

    TC_MODULE_SELF_CHECK(self, "open");

    pd  = self->userdata;
    vob = pd->vob;

    /* avoid fd loss in case of failed configuration */
    if (pd->fd_vid == -1) {
        pd->fd_vid = open(filename,
                          O_RDWR|O_CREAT|O_TRUNC,
                          S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
        if (pd->fd_vid == -1) {
            tc_log_error(MOD_NAME, "failed to open video stream file '%s'"
                                   " (reason: %s)", filename,
                                   strerror(errno));
            return TC_ERROR;
        }
    }
    y4m_init_stream_info(&(pd->streaminfo));

    //note: this is the real framerate of the raw stream
    framerate = (vob->ex_frc == 0) ?mpeg_conform_framerate(vob->ex_fps)
                                   :mpeg_framerate(vob->ex_frc);
    if (framerate.n == 0 && framerate.d == 0) {
    	framerate.n = vob->ex_fps * 1000;
	    framerate.d = 1000;
    }
    
    asr = (vob->ex_asr < 0) ?vob->im_asr :vob->ex_asr;
    tc_asr_code_to_ratio(asr, &asr_rate.n, &asr_rate.d); 

    y4m_init_stream_info(&(pd->streaminfo));
    y4m_si_set_framerate(&(pd->streaminfo), framerate);
    if (vob->encode_fields == TC_ENCODE_FIELDS_TOP_FIRST) {
        y4m_si_set_interlace(&(pd->streaminfo), Y4M_ILACE_TOP_FIRST);
    } else if (vob->encode_fields == TC_ENCODE_FIELDS_BOTTOM_FIRST) {
        y4m_si_set_interlace(&(pd->streaminfo), Y4M_ILACE_BOTTOM_FIRST);
    } else if (vob->encode_fields == TC_ENCODE_FIELDS_PROGRESSIVE) {
        y4m_si_set_interlace(&(pd->streaminfo), Y4M_ILACE_NONE);
    }
    /* XXX */
    y4m_si_set_sampleaspect(&(pd->streaminfo),
                            y4m_guess_sar(pd->width,
                                          pd->height,
                                          asr_rate));
    y4m_si_set_height(&(pd->streaminfo), pd->height);
    y4m_si_set_width(&(pd->streaminfo), pd->width);
    /* Y4M_CHROMA_420JPEG   4:2:0, H/V centered, for JPEG/MPEG-1 */
    /* Y4M_CHROMA_420MPEG2  4:2:0, H cosited, for MPEG-2         */
    /* Y4M_CHROMA_420PALDV  4:2:0, alternating Cb/Cr, for PAL-DV */
    y4m_si_set_chroma(&(pd->streaminfo), Y4M_CHROMA_420JPEG); // XXX
    
    ret = y4m_write_stream_header(pd->fd_vid, &(pd->streaminfo));
    if (ret != Y4M_OK) {
        tc_log_warn(MOD_NAME, "failed to write video YUV4MPEG2 header: %s",
                              y4m_strerr(ret));
        return TC_ERROR;
    }
    return TC_OK;
}

static int tc_y4m_configure(TCModuleInstance *self,
                            const char *options,
                            TCJob *vob,
                            TCModuleExtraData *xdata[])
{
    Y4MPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    pd->width  = vob->ex_v_width;
    pd->height = vob->ex_v_height;
    pd->vob    = vob;
    
    return TC_OK;
}

static int tc_y4m_stop(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "stop");

    return TC_OK;
}

static int tc_y4m_close(TCModuleInstance *self)
{
    Y4MPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "close");

    pd = self->userdata;

    if (pd->fd_vid != -1) {
        int err = close(pd->fd_vid);
        if (err) {
            tc_log_error(MOD_NAME, "closing video file: %s",
                                   strerror(errno));
            return TC_ERROR;
        }
        y4m_fini_frame_info(&pd->frameinfo);
        y4m_fini_stream_info(&(pd->streaminfo));
   
        pd->fd_vid = -1;
    }

    return TC_OK;
}

static int tc_y4m_write_video(TCModuleInstance *self,
                              TCFrameVideo *vframe)
{
    uint8_t *planes[3] = { NULL, NULL, NULL };
    ssize_t w_vid = 0;
    int ret = 0;

    Y4MPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "write_video");

    pd = self->userdata;

    y4m_init_frame_info(&pd->frameinfo);
    YUV_INIT_PLANES(planes, vframe->video_buf, IMG_YUV420P,
                    pd->width, pd->height);
        
    ret = y4m_write_frame(pd->fd_vid, &(pd->streaminfo),
                             &pd->frameinfo, planes);
    if (ret != Y4M_OK) {
        tc_log_warn(MOD_NAME, "error while writing video frame: %s",
                              y4m_strerr(ret));
        return TC_ERROR;
    }
    w_vid = vframe->video_len;

    return (int)w_vid;
}

static int tc_y4m_init(TCModuleInstance *self, uint32_t features)
{
    Y4MPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    pd = tc_malloc(sizeof(Y4MPrivateData));
    if (pd == NULL) {
        return TC_ERROR;
    }

    pd->width  = 0;
    pd->height = 0;
    pd->fd_vid = -1;

    y4m_init_stream_info(&(pd->streaminfo));
    /* frameinfo will be initialized at each multiplex call  */

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }

    self->userdata = pd;
    return TC_OK;
}

static int tc_y4m_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    tc_y4m_stop(self);

    tc_free(self->userdata);
    self->userdata = NULL;

    return TC_OK;
}


/*************************************************************************/

static const TCCodecID tc_y4m_codecs_video_in[] = { 
    TC_CODEC_YUV420P, TC_CODEC_ERROR
};
static const TCCodecID tc_y4m_codecs_audio_in[] = { 
    TC_CODEC_ERROR
};
static const TCFormatID tc_y4m_formats_out[] = { 
    TC_FORMAT_YUV4MPEG, TC_FORMAT_ERROR
};
/* a multiplexor is at the end of pipeline */
TC_MODULE_MPLEX_FORMATS_CODECS(tc_y4m);

TC_MODULE_INFO(tc_y4m);

static const TCModuleClass tc_y4m_class = {
    TC_MODULE_CLASS_HEAD(tc_y4m),

    .init         = tc_y4m_init,
    .fini         = tc_y4m_fini,
    .configure    = tc_y4m_configure,
    .stop         = tc_y4m_stop,
    .inspect      = tc_y4m_inspect,

    .open         = tc_y4m_open,
    .close        = tc_y4m_close,
    .write_video  = tc_y4m_write_video,
};

TC_MODULE_ENTRY_POINT(tc_y4m);

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

