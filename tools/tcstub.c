/*
 *  tcstub.c - stub (but with sane values) symbols for transcode
 *             support programs.
 *
 *  Copyright (C) Tilmann Bitterberg - August 2002
 *  updated and partially rewritten by
 *  Copyright (C) Francesco Romani - January 2006
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

#include "tcstub.h"

struct filter_struct filter[MAX_FILTERS];

/* FIXME: what about ex_asr and ex_par ? */
static vob_t vob = {
    .verbose = TC_INFO,

    .has_video = 1,
    .has_audio = 1,

    /* some sane settings, mostly identical to transcode's ones */
    .fps = PAL_FPS,
    .ex_fps = PAL_FPS,
    .im_v_width = PAL_W,
    .ex_v_width = PAL_W,
    .im_v_height= PAL_H,
    .ex_v_height= PAL_H,

    .im_v_codec = TC_CODEC_YUV420P,
    .im_a_codec = TC_CODEC_PCM,
    .ex_v_codec = TC_CODEC_YUV420P,
    .ex_a_codec = TC_CODEC_PCM,

    .im_frc = 3,
    .ex_frc = 3,

    .a_rate = RATE,
    .a_chan = CHANNELS,
    .a_bits = BITS,
    .a_vbr = AVBR,

    .video_in_file = "/dev/zero",
    .audio_in_file = "/dev/zero",
    .video_out_file = "/dev/null",
    .audio_out_file = "/dev/null",
    .audiologfile = "/dev/null",

    .mp3bitrate = ABITRATE,
    .mp3quality = AQUALITY,
    .mp3mode = AMODE,
    .mp3frequency = RATE,

    .divxlogfile = "/dev/null",
    .divxmultipass = VMULTIPASS,
    .divxbitrate = VBITRATE,
    .divxkeyframes = VKEYFRAMES,
    .divxcrispness = VCRISPNESS,

    .a_leap_frame = TC_LEAP_FRAME,
    .a_leap_bytes = 0,

    .export_attributes= TC_EXPORT_ATTRIBUTE_NONE,
};

static TCSession session = {
    .acceleration = AC_ALL,
};

// dependencies
// Yeah, this sucks
vob_t *tc_get_vob(void)
{
    return &vob;
}

TCSession *tc_get_session(void)
{
    return &session;
}

int tc_filter_add(const char *name, const char *options)
{
    return 0;
}

int tc_filter_find(const char *name)
{
    return 0;
}



#ifdef TC_FRAMEBUFFER_STUBS
void vframe_copy(vframe_list_t *dst, const vframe_list_t *src, int copy_data)
{
    return;
}

void aframe_copy(aframe_list_t *dst, const aframe_list_t *src, int copy_data)
{
    return;
}


vframe_list_t *tc_new_video_frame(int width, int height, int format,
                                  int partial)
{
    return NULL;
}

aframe_list_t *tc_new_audio_frame(double samples, int channels, int bits)
{
    return NULL;
}

void tc_del_video_frame(vframe_list_t *vptr)
{
    return;
}

void tc_del_audio_frame(aframe_list_t *aptr)
{
    return;
}

vframe_list_t *vframe_alloc_single(void)
{
    return NULL;
}

aframe_list_t *aframe_alloc_single(void)
{
    return NULL;
}

#endif /* TC_FRAMEBUFFER_STUBS */


int tc_progress_meter = 1;
int tc_progress_rate = 1;

int resize1 = 0;  // probe_source_xml()
int resize2 = 0;  // probe_source_xml()
int zoom = 0;  // probe_source_xml()


int tc_cluster_mode = 0;
pid_t tc_probe_pid = 0;

/* symbols needed by modules */
int verbose  = TC_INFO;
int tc_accel = -1;    //acceleration code
int flip = 0;
int max_frame_buffer = 0;
int gamma_table_flag = 0;

void tc_socket_config(void);
void tc_socket_disable(void);
void tc_socket_enable(void);
void tc_socket_list(void);
void tc_socket_load(void);
void tc_socket_parameter(void);
void tc_socket_preview(void);
void tc_socket_config(void) {}
void tc_socket_disable(void) {}
void tc_socket_enable(void) {}
void tc_socket_list(void) {}
void tc_socket_load(void) {}
void tc_socket_parameter(void) {}
void tc_socket_preview(void) {}
void tc_socket_poll(void) {}
void tc_socket_wait(void) {}

int load_plugin(const char *path, int id, int verbose)
{
    const char *error = NULL;
    char module[TC_BUF_MAX];
    int n;

    if (filter[id].name == NULL) {
        tc_log_error(__FILE__, "bad filter#%i name (%s)",
                     id, filter[id].name);
        return -1;
    }

    filter[id].options = NULL;

    /* replace "=" by "/0" in filter name */
    for (n = 0; n < strlen(filter[id].name); n++) {
        if (filter[id].name[n] == '=') {
            filter[id].name[n] = '\0';
            filter[id].options = filter[id].name + n + 1;
            break;
        }
    }

    tc_snprintf(module, sizeof(module), "%s/filter_%s.so", path, filter[id].name);

    /* try transcode's module directory */
    filter[id].handle = dlopen(module, RTLD_LAZY);

    if (filter[id].handle != NULL) {
        filter[id].entry = dlsym(filter[id].handle, "tc_filter");
    } else {
        if (verbose) {
            tc_log_error(__FILE__, "loading filter module '%s' failed (reason: %s)",
                         module, dlerror());
        }
        return -1;
    }

    error = dlerror();
    if (error != NULL)  {
        if (verbose) {
            tc_log_error(__FILE__, "error while loading '%s': %s\n",
                         module, error);
        }
        return -1;
    }
    return 0;
}

#include "libtc/ratiocodes.h"
void dummy_misc(void);
void dummy_misc(void)
{
    int n, d;
    tc_frc_code_to_ratio(3, &n, &d);
}

#include "libtc/tccodecs.h"
void dummy_tccodec(void);
void dummy_tccodec(void)
{
    const char *str;
    str = tc_codec_to_string(TC_CODEC_UNKNOWN);
}

#include "libtc/tcformats.h"
void dummy_tcformat(void);
void dummy_tcformat(void)
{
    const char *str;
    str = tc_format_to_string(TC_FORMAT_UNKNOWN);
}


#include "libtcutil/static_tcutil.h"

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
