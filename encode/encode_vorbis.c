/*
 *  encode_vorbis.c -- produces a vorbis stream using libvorbis.
 *  (C) 2007-2010 Francesco Romani <fromani at gmail dot com>
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
#include "src/framebuffer.h"
#include "libtcutil/optstr.h"

#include "libtc/ratiocodes.h"
#include "libtcmodule/tcmodule-plugin.h"

#define TC_ENCODER 1
#include "libtcext/tc_ogg.h"

#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#define MOD_NAME    "encode_vorbis.so"
#define MOD_VERSION "v0.0.8 (2009-09-20)"
#define MOD_CAP     "vorbis audio encoder using libvorbis"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_ENCODE|TC_MODULE_FEATURE_AUDIO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

//#define TC_VORBIS_DEBUG 1 // until 0.x.y at least


static const char tc_vorbis_help[] = ""
    "Overview:\n"
    "    this module produces a vorbis audio stream using libvorbis.\n"
    "Options:\n"
    "    help    produce module overview and options explanations\n";


/*************************************************************************/

typedef struct vorbisprivatedata_ VorbisPrivateData;
struct vorbisprivatedata_ {
    int                 flush_flag;
    int                 need_flush;

    vorbis_info         vi;
    vorbis_comment      vc;
    vorbis_dsp_state    vd; 
    vorbis_block        vb;

    OGGExtraData        xdata;    /* real extradata */

    int                 bits;
    int                 channels;
    int                 end_of_stream;

    uint32_t            frames;
    uint32_t            packets;
};


/*************************************************************************/


static int tc_frame_audio_add_ogg_packet(VorbisPrivateData *pd, 
                                         TCFrameAudio *f, ogg_packet *op)
{
    double ts = vorbis_granule_time(&(pd->vd), op->granulepos);
    int needed = sizeof(*op) + op->bytes;
    int avail = f->audio_size - f->audio_len;

    f->timestamp = (uint64_t)ts;
    if (avail < needed) {
        tc_log_error(__FILE__, "(%s) no space left for packet: (avail=%i|needed=%i)",
                     __func__, avail, needed);
        return TC_ERROR;
    }
    ac_memcpy(f->audio_buf + f->audio_len, op, sizeof(*op));
    f->audio_len += sizeof(*op);
    ac_memcpy(f->audio_buf + f->audio_len, op->packet, op->bytes);
    f->audio_len += op->bytes;

    if (op->e_o_s) {
        f->attributes |= TC_FRAME_IS_END_OF_STREAM; // useless?
    }
    return TC_OK;
}

#define DUP_PACKET(WHAT) do { \
    int ret = tc_ogg_dup_packet(&(pd->xdata.WHAT), &WHAT); \
    if (ret == TC_ERROR) { \
        goto no_ ## WHAT; \
    } \
} while (0)

// FIXME: better error checking
static int tc_ogg_new_extradata(VorbisPrivateData *pd)
{
    ogg_packet header;
    ogg_packet comment;
    ogg_packet code;

    vorbis_analysis_headerout(&pd->vd, &pd->vc, &header, &comment, &code);

    DUP_PACKET(header);
    DUP_PACKET(comment);
    DUP_PACKET(code);

    return TC_OK;

  no_code:
    tc_ogg_del_packet(&(pd->xdata.comment));
  no_comment:
    tc_ogg_del_packet(&(pd->xdata.header));
  no_header:
    return TC_ERROR;
}

#undef DUP_PACKET

/* FIXME: move into libtcext? */
static int tc_ogg_publish_extradata(VorbisPrivateData *pd,
                                    TCModuleExtraData *xdata[])
{
    xdata[0]->stream_id  = 0; /* not significant for us */
    xdata[0]->codec      = TC_CODEC_VORBIS;
    xdata[0]->extra.size = sizeof(OGGExtraData);
    xdata[0]->extra.data = &(pd->xdata);

    return TC_OK;
}

/*************************************************************************/

/* nasty, nasty floats... */
#define ZERO_QUALITY 0.00001

static int tc_vorbis_configure(TCModuleInstance *self,
                               const char *options,
                               TCJob *vob,
                               TCModuleExtraData *xdata[])
{
    VorbisPrivateData *pd = NULL;
    int samplerate = (vob->mp3frequency) ? vob->mp3frequency : vob->a_rate;
    float quality = TC_CLAMP(vob->mp3quality, 0.0, 9.9) / 10.0;
    int ret, br = vob->mp3bitrate * 1000;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    if (vob->dm_bits != 16) {
        tc_log_error(MOD_NAME, "Only 16-bit samples supported");
        return TC_ERROR;
    }
 
    pd->flush_flag    = vob->encoder_flush;
    pd->need_flush    = TC_FALSE;
    pd->channels      = vob->dm_chan;
    pd->bits          = vob->dm_bits;
    pd->packets       = 0;
    pd->frames        = 0;

    vorbis_info_init(&pd->vi);
    if (quality > ZERO_QUALITY) {
        ret = vorbis_encode_init_vbr(&pd->vi, pd->channels,
                                     samplerate, quality);
    } else {
        ret = vorbis_encode_init(&pd->vi, pd->channels, samplerate,
                                 -1, br, -1);
    }
    if (ret) {
        tc_log_error(MOD_NAME, "the Vorbis encoder could not set up a mode"
                               " according to the requested settings.");
        ret = TC_ERROR;
    } else {
        vorbis_comment_init(&pd->vc);
        vorbis_comment_add_tag(&pd->vc, "ENCODER", PACKAGE " " VERSION);
        vorbis_analysis_init(&pd->vd, &pd->vi);
        vorbis_block_init(&pd->vd, &pd->vb);

        ret = tc_ogg_new_extradata(pd);
        if (ret == TC_OK) {
            tc_ogg_publish_extradata(pd, xdata);
        }
    }
    return ret;
}

static int tc_vorbis_stop(TCModuleInstance *self)
{
    VorbisPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    vorbis_block_clear(&pd->vb);
    vorbis_dsp_clear(&pd->vd);
    vorbis_comment_clear(&pd->vc);
    vorbis_info_clear(&pd->vi);
    pd->need_flush = TC_FALSE;

    tc_ogg_del_extradata(&pd->xdata);
    return TC_OK;
}


static int tc_vorbis_outframe(VorbisPrivateData *pd, TCFrameAudio *f)
{
    int has_block = TC_FALSE;
    ogg_packet op;

    do {
        has_block = vorbis_analysis_blockout(&pd->vd, &pd->vb);
        if (has_block == 1) {
            int has_pkt;

            /* FIXME: analysis, assume we want to use bitrate management */
            vorbis_analysis(&pd->vb, NULL);
            vorbis_bitrate_addblock(&pd->vb);
        
            do {
                has_pkt = vorbis_bitrate_flushpacket(&pd->vd, &op);
                if (has_pkt) {
#ifdef TC_VORBIS_DEBUG
                    tc_log_info(MOD_NAME, "(%s) frame=%u packet=%u",
                                __func__, pd->frames, pd->packets);
#endif
                    tc_frame_audio_add_ogg_packet(pd, f, &op);
                    pd->packets++;
                }
            } while (has_pkt);
        }
    } while (has_block);
    pd->frames++;
    
    return TC_OK;
}


#define HAS_AUDIO(F) \
    (((F) != NULL) && ((F)->audio_size > 0))

#define MAX_S16F (32768.0f)

static int tc_vorbis_flush(TCModuleInstance *self, TCFrameAudio *frame)
{
    VorbisPrivateData *pd = NULL;
    int ret;

    TC_MODULE_SELF_CHECK(self, "flush_audio");

    pd = self->userdata;

#ifdef TC_VORBIS_DEBUG
    tc_log_info(MOD_NAME, "(%s) START FLUSH AUDIO FRAME", __func__);
    tc_log_info(MOD_NAME, "(%s) invoked out=%p", __func__, frame);
    tc_log_info(MOD_NAME,
                "(%s) invoked out->len=%i out->size=%i",
                __func__, frame->audio_len, frame->audio_size);
#endif

     if (pd->flush_flag && !pd->need_flush) {
        /* 
         * End of file. Tell the library we're at end of stream so that it 
         * can handle the  last frame and mark end of stream in the output 
         * properly 
         */
        vorbis_analysis_wrote(&pd->vd, 0);
     }
    ret = tc_vorbis_outframe(pd, frame);
    pd->need_flush = TC_FALSE;

#ifdef TC_VORBIS_DEBUG
    tc_log_info(MOD_NAME,
                "(%s) finished out->len=%i out->size=%i",
                __func__, frame->audio_len, frame->audio_size);
#endif
    return ret;
}


static int tc_vorbis_encode_audio(TCModuleInstance *self,
                                  TCFrameAudio *inframe,
                                  TCFrameAudio *outframe)
{
    VorbisPrivateData *pd = NULL;
    int ret, bps, samples, i = 0;
    int16_t *aptr = NULL;
    float **buffer;

    TC_MODULE_SELF_CHECK(self,     "encode_audio");
    TC_MODULE_SELF_CHECK(inframe,  "encode_audio");
    TC_MODULE_SELF_CHECK(outframe, "encode_audio");

    pd = self->userdata;

#ifdef TC_VORBIS_DEBUG
    tc_log_info(MOD_NAME,
                "(%s) START ENCODE AUDIO FRAME", __func__);
    tc_log_info(MOD_NAME, "(%s) invoked in=%p out=%p",
                __func__, inframe, outframe);
    tc_log_info(MOD_NAME,
                "(%s) invoked out->len=%i out->size=%i",
                __func__, outframe->audio_len, outframe->audio_size);
#endif

    aptr = (int16_t*)inframe->audio_buf;
    bps = (pd->channels * pd->bits) / 8;
    samples = inframe->audio_size / bps;
    buffer = vorbis_analysis_buffer(&pd->vd, samples);

    if (pd->channels == 1) {
        for (i = 0; i < samples; i++) {
            buffer[0][i] = aptr[i        ]/MAX_S16F;
        }
    } else { /* if (pd->channels == 2 ) */
        for (i = 0; i < samples; i++) {
            buffer[0][i] = aptr[i * 2    ]/MAX_S16F;
            buffer[1][i] = aptr[i * 2 + 1]/MAX_S16F;
        }
    }

    vorbis_analysis_wrote(&pd->vd, samples);
    ret = tc_vorbis_outframe(pd, outframe);
    pd->need_flush = TC_TRUE;

#ifdef TC_VORBIS_DEBUG
    tc_log_info(MOD_NAME,
                "(%s) finished out->len=%i out->size=%i",
                __func__, outframe->audio_len, outframe->audio_size);
#endif
    return ret;
}


static int tc_vorbis_inspect(TCModuleInstance *self,
                             const char *param, const char **value)
{
    VorbisPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "inspect");

    pd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = tc_vorbis_help;
    }

    return TC_OK;
}

TC_MODULE_GENERIC_INIT(tc_vorbis, VorbisPrivateData);

TC_MODULE_GENERIC_FINI(tc_vorbis);

/*************************************************************************/

static const TCCodecID tc_vorbis_codecs_audio_in[] = {
    TC_CODEC_PCM, TC_CODEC_ERROR
};
static const TCCodecID tc_vorbis_codecs_audio_out[] = { 
    TC_CODEC_VORBIS, TC_CODEC_ERROR
};
TC_MODULE_VIDEO_UNSUPPORTED(tc_vorbis);
TC_MODULE_CODEC_FORMATS(tc_vorbis);

TC_MODULE_INFO(tc_vorbis);

static const TCModuleClass tc_vorbis_class = {
    TC_MODULE_CLASS_HEAD(tc_vorbis),

    .init         = tc_vorbis_init,
    .fini         = tc_vorbis_fini,
    .configure    = tc_vorbis_configure,
    .stop         = tc_vorbis_stop,
    .inspect      = tc_vorbis_inspect,

    .encode_audio = tc_vorbis_encode_audio,
    .flush_audio  = tc_vorbis_flush,
};

TC_MODULE_ENTRY_POINT(tc_vorbis);

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

