/*
 * multiplex_ogg.c -- multiplex OGG streams using libogg.
 * (C) 2007-2010 Francesco Romani <fromani at gmail dot com>
 *
 * This file is part of transcode, a video stream processing tool
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Make; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "src/transcode.h"

#include "libtc/ratiocodes.h"
#include "libtcutil/optstr.h"
#include "libtcutil/cfgfile.h"

#include "libtcmodule/tcmodule-plugin.h"

#include "libtcext/tc_ogg.h"

#define MOD_NAME    "multiplex_ogg.so"
#define MOD_VERSION "v0.2.1 (2009-10-25)"
#ifdef HAVE_SHOUT
#define MOD_CAP     "create an ogg stream using libogg and broadcast using libshout"
#else  /* not HAVE_SHOUT */
#define MOD_CAP     "create an ogg stream using libogg"
#endif

#define MOD_FEATURES \
    TC_MODULE_FEATURE_MULTIPLEX|TC_MODULE_FEATURE_VIDEO|TC_MODULE_FEATURE_AUDIO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

//#define TC_OGG_DEBUG 1 // until 0.x.y at least

/*************************************************************************/

#ifdef HAVE_SHOUT
#include <shout/shout.h>
#endif

typedef struct tcshout_ TCShout;
struct tcshout_ {
    void *sh;

    int (*open)(TCShout *tcsh);
    int (*close)(TCShout *tcsh);
    int (*send)(TCShout *tcsh, const unsigned char *data, size_t len);
    void (*free)(TCShout *tcsh);
};


#ifdef HAVE_SHOUT

#define TC_SHOUT_BUF 512
#define TC_SHOUT_CONFIG_FILE "shout.cfg"

#define RETURN_IF_SHOUT_ERROR(MSG) do { \
    if (ret != SHOUTERR_SUCCESS) { \
        tc_log_error(MOD_NAME, "%s: %s", (MSG), shout_get_error(shout)); \
	    return TC_ERROR; \
    } \
} while (0)


static int tc_shout_configure(TCShout *tcsh, const char *id)
{
    char *hostname = NULL;
    char *mount = NULL;
    char *url = NULL;
    char *password = NULL;
    char *description = NULL;
    char *genre = NULL;
    char *name = NULL;
    int port = 0, public = 1;

    TCConfigEntry shout_conf[] = {
        { "host",     &hostname, TCCONF_TYPE_STRING, 0, 0, 0 },
        { "port",     &port,     TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 65535 },
        { "password", &password, TCCONF_TYPE_STRING, 0, 0, 0 },
        { "mount",    &mount,    TCCONF_TYPE_STRING, 0, 0, 0 },
        { "public",   &public,   TCCONF_TYPE_FLAG, 0, 0, 1 },
        { "description", &description, TCCONF_TYPE_STRING, 0, 0, 0 },
        { "genre",    &genre,    TCCONF_TYPE_STRING, 0, 0, 0 },
        { "name",     &name,     TCCONF_TYPE_STRING, 0, 0, 0 },
        { "url",      &url,      TCCONF_TYPE_STRING, 0, 0, 0 },
        { NULL,       0,         0, 0, 0, 0 }
    };

    if (tcsh->sh) {
        const char *dirs[] = { ".", NULL };
        shout_t *shout =  tcsh->sh;
        int ret = SHOUTERR_SUCCESS;

        if (verbose) {
            tc_log_info(MOD_NAME,
                        "reading configuration data for stream '%s'...", id);
        }
        tc_config_read_file(dirs, TC_SHOUT_CONFIG_FILE, id, shout_conf, MOD_NAME);

	    shout_set_format(shout, SHOUT_FORMAT_VORBIS); /* always true in here */
	    shout_set_public(shout, public); /* first the easy stuff */

        if (verbose) {
            tc_log_info(MOD_NAME, "sending to [%s:%i%s] (%s)",
                        hostname, port, mount,
                        (public) ?"public" :"private");
        }
        ret = shout_set_host(shout, hostname);
        RETURN_IF_SHOUT_ERROR("invalid SHOUT hostname");

	    ret =  shout_set_port(shout, port);
        RETURN_IF_SHOUT_ERROR("invalid SHOUT port");

        ret = shout_set_mount(shout, mount);
        RETURN_IF_SHOUT_ERROR("invalid SHOUT mount");

    	ret = shout_set_password(shout, password);
        RETURN_IF_SHOUT_ERROR("invalid SHOUT password");

    	if (description)
	    	shout_set_description(shout, description);

	    if (genre)
		    shout_set_genre(shout, genre);

    	if (name)
	    	shout_set_name(shout, name);

    	if (url)
	    	shout_set_url(shout, url);
    }
    return TC_OK;
}
#endif

static int tc_shout_real_open(TCShout *tcsh)
{
#ifdef HAVE_SHOUT
    shout_t *shout = tcsh->sh;
    int ret = shout_open(shout);
    RETURN_IF_SHOUT_ERROR("connecting to SHOUT server");
#endif
    return TC_OK;
}

static int tc_shout_real_close(TCShout *tcsh)
{ 
#ifdef HAVE_SHOUT
    shout_t *shout = tcsh->sh;
    shout_close(shout);
#endif
    return TC_OK;
}

static int tc_shout_real_send(TCShout *tcsh, const unsigned char *data, size_t len)
{ 
#ifdef HAVE_SHOUT
    shout_t *shout = tcsh->sh;
    int ret = shout_send(shout, data, len);
    RETURN_IF_SHOUT_ERROR("sending data to SHOUT server");
    shout_sync(shout);
#endif
    return TC_OK;
}

static void tc_shout_real_free(TCShout *tcsh)
{
#ifdef HAVE_SHOUT
    shout_free(tcsh->sh);
    tcsh->sh = NULL;
#endif
}


static int tc_shout_real_new(TCShout *tcsh, const char *id)
{ 
    int ret = TC_OK;

    tcsh->sh    = NULL;

    tcsh->open  = tc_shout_real_open;
    tcsh->close = tc_shout_real_close;
    tcsh->send  = tc_shout_real_send;
    tcsh->free  = tc_shout_real_free;
        
#ifdef HAVE_SHOUT
    tcsh->sh = shout_new();

    if (tcsh->sh) {
        ret = tc_shout_configure(tcsh, id);
    }
#endif
    return ret;
}

/*************************************************************************/

static int tc_shout_null_open(TCShout *tcsh)
{ 
    return TC_OK;
}

static int tc_shout_null_close(TCShout *tcsh)
{ 
    return TC_OK;
}

static int tc_shout_null_send(TCShout *tcsh, const unsigned char *data, size_t len)
{ 
    return TC_OK;
}

static void tc_shout_null_free(TCShout *tcsh)
{ 
    ; /* do nothing ... */
}


static int tc_shout_null_new(TCShout *tcsh, const char *id)
{
    if (tcsh) {

        tcsh->open  = tc_shout_null_open;
        tcsh->close = tc_shout_null_close;
        tcsh->send  = tc_shout_null_send;
        tcsh->free  = tc_shout_null_free;
        
        tcsh->sh    = NULL;

        return TC_OK;
    }
    return TC_ERROR;
}


/*************************************************************************/

static int tc_ogg_send(ogg_stream_state *os, FILE *f, TCShout *tcsh,
                       int (*ogg_send)(ogg_stream_state *os, ogg_page *og))
{
    int32_t bytes = 0;
    ogg_page og;
    int ret;

#ifdef TC_OGG_DEBUG
    tc_log_info(MOD_NAME, "(%s) begin", __func__);
#endif
    while (TC_TRUE) {
        ret = ogg_send(os, &og);
        if (ret == 0) {
            break;
        }
        if (fwrite(og.header, 1, og.header_len, f) != og.header_len) {
            tc_log_perror(MOD_NAME, "Write error");
            return -1;
        }
        tcsh->send(tcsh, og.header, og.header_len);
        bytes += og.header_len;
        if (fwrite(og.body,   1, og.body_len,   f) != og.body_len) {
            tc_log_perror(MOD_NAME, "Write error");
            return -1;
        }
        tcsh->send(tcsh, og.body, og.body_len);
        bytes += og.body_len;

#ifdef TC_OGG_DEBUG
        tc_log_info(MOD_NAME, "(%s) sent hlen=%lu blen=%lu gpos=%lu pkts=%i",
                    __func__,
                    (unsigned long)og.header_len,
                    (unsigned long)og.body_len,
                    (unsigned long)ogg_page_granulepos(&og),
                                   ogg_page_packets(&og));
#endif
    }
#ifdef TC_OGG_DEBUG
    tc_log_info(MOD_NAME, "(%s) end", __func__);
#endif
    return bytes;
}

static int tc_ogg_flush(ogg_stream_state *os, FILE *f, TCShout *tcsh)
{
    return tc_ogg_send(os, f, tcsh, ogg_stream_flush);
}

static int tc_ogg_write(ogg_stream_state *os, FILE *f, TCShout *tcsh)
{
    return tc_ogg_send(os, f, tcsh, ogg_stream_pageout);
}


/*************************************************************************/

static const char tc_ogg_help[] = ""
    "Overview:\n"
    "    this module create an OGG stream using libogg.\n"
    "Options:\n"
    "    stream  enable shout streaming using given label as identifier\n"
    "    help    produce module overview and options explanations\n";

static const TCCodecID tc_ogg_codecs_video_in[] = {
    TC_CODEC_THEORA, TC_CODEC_ERROR
};
static const TCCodecID tc_ogg_codecs_audio_in[] = {
    TC_CODEC_VORBIS, TC_CODEC_ERROR
};


typedef struct oggprivatedata_ OGGPrivateData;
struct oggprivatedata_ {
    uint32_t         features;

    int              vserial;
    int              aserial;
    int              hserial;

    ogg_stream_state vs; /* video stream */
    ogg_stream_state as; /* audio stream */
    ogg_stream_state hs; /* skeleton stream */
    FILE*            outfile;

    TCShout          tcsh;
    int              shouting; /* flag */
};

/*************************************************************************/

static void put_le16b(uint8_t *d, ogg_uint16_t v)
{
    d[0] = (v     ) & 0xff;
    d[1] = (v >> 8) & 0xff;
}

static void put_le32b(uint8_t *d, ogg_uint32_t v)
{
    d[0] = (v      ) & 0xff;
    d[1] = (v >>  8) & 0xff;
    d[2] = (v >> 16) & 0xff;
    d[3] = (v >> 24) & 0xff;
}

static void put_le64b(uint8_t *d, ogg_int64_t v)
{
    ogg_uint32_t h = (v >> 32);
    put_le32b(&(d[0]), v);
    put_le32b(&(d[4]), h);
}

enum {
    OGG_SKELETON_FISHEAD_SIZE       = 64,
    OGG_SKELETON_FISBONE_SIZE       = 80,
    OGG_SKELETON_VERSION_MAJOR      =  3,
    OGG_SKELETON_VERSION_MINOR      =  0,
    OGG_SKELETON_TAG_SIZE           =  8,
    OGG_SKELETON_FISBONE_HDR_OFFSET = 44,
    OGG_SKELETON_FISBONE_LEN        = 52
};

#define OGG_SKELETON_FISHEAD_TAG "fishead\0"
#define OGG_SKELETON_FISBONE_TAG "fisbone\0"


/* FISHEAD:
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1| Byte
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Identifier 'fishead\0'                                        | 0-3
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               | 4-7
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Version major                 | Version minor                 | 8-11
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Presentationtime numerator                                    | 12-15
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               | 16-19
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Presentationtime denominator                                  | 20-23
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               | 24-27
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Basetime numerator                                            | 28-31
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               | 32-35
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Basetime denominator                                          | 36-39
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               | 40-43
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| UTC                                                           | 44-47
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               | 48-51
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               | 52-55
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               | 56-59
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               | 60-63
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

static void tc_ogg_setup_fishead(OGGPrivateData *pd)
{
    uint8_t buf[OGG_SKELETON_FISHEAD_SIZE];
    ogg_packet op;

    memset(&op, 0, sizeof(op));
    memset(buf, 0, sizeof(buf));

    ac_memcpy(buf,      OGG_SKELETON_FISHEAD_TAG, 8);
    put_le16b(buf + 8,  OGG_SKELETON_VERSION_MAJOR);
    put_le16b(buf + 10, OGG_SKELETON_VERSION_MINOR);
    put_le64b(buf + 12, (ogg_int64_t)0);    /* presentationtime num */
    put_le64b(buf + 20, (ogg_int64_t)1000); /* presentationtime den */
    put_le64b(buf + 28, (ogg_int64_t)0);    /* basetime num */
    put_le64b(buf + 36, (ogg_int64_t)1000); /* basetime den */
    put_le32b(buf + 44, 0); /* UTC time, unused yet (so, zero-ified) */

    op.packet = buf;
    op.b_o_s  = 1; /* its the first packet of the stream */
    op.e_o_s  = 0; /* its not the last packet of the stream */
    op.bytes  = OGG_SKELETON_FISHEAD_SIZE;

    ogg_stream_packetin(&(pd->hs), &op);
}

static int tc_ogg_build_fisbone_theora(OGGPrivateData *pd,
                                       TCModuleExtraData *xdata,
                                       uint8_t *fb, size_t size)
{
    vob_t *vob = tc_get_vob(); /* FIXME */
    OGGExtraData *xd = xdata->extra.data; 
    int32_t fps_num = 0, fps_den = 0; /* FIXME */
    int ret;

    /* FIXME: DRY violation */
    ret = tc_frc_code_to_ratio(vob->ex_frc, &fps_num, &fps_den);
    if (ret == TC_NULL_MATCH) { /* watch out here */
        fps_num = 25;
        fps_den = 1;
    }

    ac_memcpy(fb,      OGG_SKELETON_FISBONE_TAG, 8); /* identifier */
    put_le32b(fb + 8,  OGG_SKELETON_FISBONE_HDR_OFFSET); /* offset of the message header fields */
    put_le32b(fb + 12, pd->vserial);
    put_le32b(fb + 16, 3); /* number of header packets */
    put_le64b(fb + 20, fps_num); /* granulrate num */ /* FIXME */
    put_le64b(fb + 28, fps_den); /* granulrate den */ /* FIXME */
    put_le64b(fb + 36, 0); /* start granule */
    put_le32b(fb + 44, 0); /* preroll */
    put_le32b(fb + 48, xd->granule_shift);
    ac_memcpy(fb + 52, "Content-Type: video/theora\r\n", 28); /* message header field, Content-Type */
    return TC_OK;
}

static int tc_ogg_build_fisbone_vorbis(OGGPrivateData *pd,
                                       TCModuleExtraData *xdata,
                                       uint8_t *fb, size_t size)
{
    vob_t *vob = tc_get_vob(); /* FIXME */
    int32_t granule_shift = 0;
    int32_t sample_rate = (vob->mp3frequency) 
                            ? vob->mp3frequency : vob->a_rate;

    ac_memcpy(fb,      OGG_SKELETON_FISBONE_TAG, 8); /* identifier */
    put_le32b(fb + 8,  OGG_SKELETON_FISBONE_HDR_OFFSET); /* offset of the message header fields */
    put_le32b(fb + 12, pd->aserial);
    put_le32b(fb + 16, 3); /* number of header packet */
    put_le64b(fb + 20, sample_rate); /* granulerate num */ /* FIXME */
    put_le64b(fb + 28, (ogg_int64_t)1); /* granulerate den */
    put_le64b(fb + 36, 0); /* start granule */
    put_le32b(fb + 44, 2); /* preroll */
    put_le32b(fb + 48, granule_shift);
    ac_memcpy(fb + 52, "Content-Type: audio/vorbis\r\n", 28);
    return TC_OK;
}

static void init_packet(ogg_packet *op, uint8_t *buf, size_t size)
{
    memset(op,  0, sizeof(*op));
    if (buf != NULL && size > 0) {
        memset(buf, 0, size);
    }

    op->packet = buf;
    op->b_o_s  = 0;
    op->e_o_s  = 0;
    op->bytes  = size;

    return;
}

static int tc_ogg_setup_fisbones(OGGPrivateData *pd,
                                 TCModuleExtraData *mod_vxd,
                                 TCModuleExtraData *mod_axd)
{
    uint8_t buf[OGG_SKELETON_FISBONE_SIZE];
    ogg_packet op;

    if (pd->features & TC_MODULE_FEATURE_VIDEO && mod_vxd != NULL) {
        init_packet(&op, buf, sizeof(buf));
        tc_ogg_build_fisbone_theora(pd, mod_vxd, buf,
                                    OGG_SKELETON_FISBONE_SIZE);
        op.bytes = OGG_SKELETON_FISBONE_SIZE;

        ogg_stream_packetin(&pd->hs, &op);
    }

    if (pd->features & TC_MODULE_FEATURE_AUDIO && mod_axd != NULL) {
        init_packet(&op, buf, sizeof(buf));
        tc_ogg_build_fisbone_vorbis(pd, mod_axd, buf,
                                    OGG_SKELETON_FISBONE_SIZE);
        op.bytes = OGG_SKELETON_FISBONE_SIZE;

        ogg_stream_packetin(&pd->hs, &op);
    }
    return TC_OK;
}

static int tc_ogg_close_stream(OGGPrivateData *pd, ogg_stream_state *os)
{
    ogg_packet op;

    init_packet(&op, NULL, 0);
    op.e_o_s = 1;

    ogg_stream_packetin(os, &op);
    
    return tc_ogg_flush(os, pd->outfile, &(pd->tcsh));
}

/*************************************************************************/

static int tc_ogg_feed_video(ogg_stream_state *os, TCFrameVideo *f)
{
    uint8_t *data = f->video_buf;
    int packets = 0, used = 0;
    ogg_packet op;
   
    while (used < f->video_len) {
        ac_memcpy(&op, data + used, sizeof(op));
        used += sizeof(op);
        op.packet = data + used;
        used += op.bytes;
        
        ogg_stream_packetin(os, &op);

        packets++;
    }
    return packets;
}

static int tc_ogg_feed_audio(ogg_stream_state *os, TCFrameAudio *f)
{
    int packets = 0, used = 0;
    uint8_t *data = f->audio_buf;
    ogg_packet op;

    while (used < f->audio_len) {
        ac_memcpy(&op, data + used, sizeof(op));
        used += sizeof(op);
        op.packet = data + used;
        used += op.bytes;
        
        ogg_stream_packetin(os, &op);

        packets++;
    }
    return packets;
}

/*************************************************************************/

static int is_supported(const TCCodecID *codecs, TCCodecID wanted)
{
    int i = 0, found  = TC_FALSE;
    for (i = 0; !found && codecs[i] != TC_CODEC_ERROR; i++) {
        if (codecs[i] == wanted) {
            found = TC_TRUE;
        }
    }
    return found;
}

#define RETURN_IF_ERROR(ret) do { \
    if ((ret) == TC_ERROR) { \
        return ret; \
    } \
} while (0)

#define SETUP_STREAM_HEADER(OS, XD, F, TCSH) do { \
    int ret; \
    if ((XD)) { \
        ogg_stream_packetin((OS), &((XD)->header)); \
        ret = tc_ogg_flush((OS), (F), (TCSH)); \
        RETURN_IF_ERROR(ret); \
    } \
} while (0)

#define SETUP_STREAM_METADATA(OS, XD, F, TCSH) do { \
    int ret; \
    if ((XD)) { \
        ogg_stream_packetin((OS), &((XD)->comment)); \
        ogg_stream_packetin((OS), &((XD)->code)); \
        ret = tc_ogg_flush((OS), (F), (TCSH)); \
        RETURN_IF_ERROR(ret); \
    } \
} while (0)

#define RETURN_IF_NOT_SUPPORTED(XD, CODECS, MSG) do { \
    if ((XD)) { \
        if (!is_supported((CODECS), (XD)->codec)) { \
            tc_log_error(MOD_NAME, (MSG)); \
            tc_log_error(MOD_NAME, "unrecognized codec 0x%X", (XD)->codec); \
            return TC_ERROR; \
        } \
    } \
} while (0)

static int tc_ogg_setup(OGGPrivateData *pd,
                        TCModuleExtraData *mod_vxd,
                        TCModuleExtraData *mod_axd)
{
    int ret;
    OGGExtraData *vxd = NULL, *axd = NULL;

    RETURN_IF_NOT_SUPPORTED(mod_vxd, tc_ogg_codecs_video_in, 
                            "unrecognized video extradata");
    RETURN_IF_NOT_SUPPORTED(mod_axd, tc_ogg_codecs_audio_in,
                            "unrecognized audio extradata");

    vxd = mod_vxd->extra.data;
    axd = mod_axd->extra.data;

    /* the BoS (primary headers) pages first */
    tc_ogg_setup_fishead(pd);
    ret = tc_ogg_flush(&(pd->hs), pd->outfile, &(pd->tcsh));
    RETURN_IF_ERROR(ret);

    SETUP_STREAM_HEADER(&(pd->vs),   vxd, pd->outfile, &(pd->tcsh));
    SETUP_STREAM_HEADER(&(pd->as),   axd, pd->outfile, &(pd->tcsh));

    /* then the secondary headers */
    ret = tc_ogg_setup_fisbones(pd, mod_vxd, mod_axd);
    RETURN_IF_ERROR(ret);
    ret = tc_ogg_flush(&(pd->hs), pd->outfile, &(pd->tcsh));
    RETURN_IF_ERROR(ret);

    SETUP_STREAM_METADATA(&(pd->vs), vxd, pd->outfile, &(pd->tcsh));
    SETUP_STREAM_METADATA(&(pd->as), axd, pd->outfile, &(pd->tcsh));

    /* mark the end of the skeleton track */
    ret = tc_ogg_close_stream(pd, &(pd->hs));
    RETURN_IF_ERROR(ret);

    /* now data pages can be written */
    return TC_OK;
}


/*************************************************************************/

static int tc_ogg_inspect(TCModuleInstance *self,
                          const char *param, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");

    if (optstr_lookup(param, "help")) {
        *value = tc_ogg_help;
    }

    return TC_OK;
}

static int tc_ogg_configure(TCModuleInstance *self,
                            const char *options,
                            TCJob *vob,
                            TCModuleExtraData *xdata[])
{
    char shout_id[128] = { '\0' };
    OGGPrivateData *pd = NULL;
    int ret, streamed = 0;
  
    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    pd->shouting = 0;

    if (options) {
        int dest = optstr_get(options, "stream", "%127s", shout_id);
        shout_id[127] = '\0'; /* paranoia keep us alive */
        if (dest == 1) {
            /* have a shout_id? */
            streamed = 1;
        }
    }

    if (streamed) {
        ret = tc_shout_real_new(&pd->tcsh, shout_id);
    } else {
        ret = tc_shout_null_new(&pd->tcsh, shout_id);
    }
    if (ret != TC_OK) {
        tc_log_error(MOD_NAME, "failed initializing SHOUT streaming support");
        return TC_ERROR;
    }
    pd->shouting = 1;

    return TC_OK;
}


static int tc_ogg_open(TCModuleInstance *self,
                       const char *filename,
                       TCModuleExtraData *xdata[])
{
    int ret;
    OGGPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "open");

    pd = self->userdata;

    srand(time(NULL));
    /* need some inequal serial numbers */
    pd->hserial = rand();
    pd->vserial = rand();
    if (pd->vserial == pd->hserial) {
        pd->vserial++;
    }
    pd->aserial = rand();
    if (pd->aserial == pd->vserial) {
        pd->aserial++;
    }
    ogg_stream_init(&(pd->hs), pd->hserial);
    ogg_stream_init(&(pd->vs), pd->vserial);
    ogg_stream_init(&(pd->as), pd->aserial);

    pd->outfile = fopen(filename, "wb");
    if (!pd->outfile) {
        tc_log_perror(MOD_NAME, "open output file");
        return TC_ERROR;
    }

    ret = pd->tcsh.open(&pd->tcsh);
    if (ret != TC_OK) {
        tc_log_error(MOD_NAME, "opening SHOUT connection");
        return TC_ERROR;
    }
    return tc_ogg_setup(pd, xdata[0], xdata[1]); /* BIG FAT FIXME */
}

/* do nothing succesfully */
static int tc_ogg_stop(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "stop");
    return TC_OK;
}

static int tc_ogg_close(TCModuleInstance *self)
{
    OGGPrivateData *pd = NULL;
    int ret;

    TC_MODULE_SELF_CHECK(self, "close");

    pd = self->userdata;

    /* skelton stream already closed into setup stage */
    ogg_stream_clear(&(pd->hs));
   
    /* FIXME: what about output rotation? */
    ret = tc_ogg_close_stream(pd, &(pd->vs));
    RETURN_IF_ERROR(ret);
    ogg_stream_clear(&(pd->vs));

    ret = tc_ogg_close_stream(pd, &(pd->as));
    RETURN_IF_ERROR(ret);
    ogg_stream_clear(&(pd->as));

    if (pd->outfile) {
        int err = fclose(pd->outfile);
        if (err) {
            return TC_ERROR;
        }
        pd->outfile = NULL;
    }

    if (pd->shouting) {
        pd->tcsh.close(&pd->tcsh);
        pd->tcsh.free(&pd->tcsh);
        pd->shouting = 0;
    }
    return TC_OK;
}

static int tc_ogg_write_video(TCModuleInstance *self,
                              TCFrameVideo *vframe)
{
    OGGPrivateData *pd = NULL;
    int ret = 0;

    TC_MODULE_SELF_CHECK(self, "write_video");

    pd = self->userdata;

    tc_ogg_feed_video(&(pd->vs), vframe);
    ret = tc_ogg_write(&(pd->vs), pd->outfile, &(pd->tcsh));

#ifdef TC_OGG_DEBUG
    tc_log_info(MOD_NAME, "(%s) tc_ogg_write_video()->%i",
                __func__, ret);
#endif
    return ret;
}

static int tc_ogg_write_audio(TCModuleInstance *self,
                              TCFrameAudio *aframe)
{
    OGGPrivateData *pd = NULL;
    int ret = 0;

    TC_MODULE_SELF_CHECK(self, "write_audio");

    pd = self->userdata;

    tc_ogg_feed_audio(&(pd->as), aframe);
    ret = tc_ogg_write(&(pd->as), pd->outfile, &(pd->tcsh));

#ifdef TC_OGG_DEBUG
    tc_log_info(MOD_NAME, "(%s) tc_ogg_write_audio()->%i",
                __func__, ret);
#endif
    return ret;
}


static int tc_ogg_init(TCModuleInstance *self, uint32_t features)
{
    OGGPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    pd = tc_malloc(sizeof(OGGPrivateData));
    if (pd == NULL) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }
    
    pd->features = features;
    self->userdata = pd;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }

    return TC_OK;
}




TC_MODULE_GENERIC_FINI(tc_ogg);

/*************************************************************************/

static const TCFormatID tc_ogg_formats_out[] = { 
    TC_FORMAT_OGG, TC_FORMAT_ERROR
};
/* a multiplexor is at the end of pipeline */
TC_MODULE_MPLEX_FORMATS_CODECS(tc_ogg);

TC_MODULE_INFO(tc_ogg);

static const TCModuleClass tc_ogg_class = {
    TC_MODULE_CLASS_HEAD(tc_ogg),

    .init         = tc_ogg_init,
    .fini         = tc_ogg_fini,
    .configure    = tc_ogg_configure,
    .stop         = tc_ogg_stop,
    .inspect      = tc_ogg_inspect,

    .open         = tc_ogg_open,
    .close        = tc_ogg_close,
    .write_video  = tc_ogg_write_video,
    .write_audio  = tc_ogg_write_audio,
};

TC_MODULE_ENTRY_POINT(tc_ogg);

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

