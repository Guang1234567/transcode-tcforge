/*
 * import_vag.c -- module for importing PlayStation VAG audio data
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#define MOD_NAME        "import_vag.so"
#define MOD_VERSION     "v1.1.0 (2009-12-30)"
#define MOD_CAP         "Imports PlayStation VAG-format audio"
#define MOD_AUTHOR      "Andrew Church"

/*%*
 *%* DESCRIPTION 
 *%*   This module decodes VAG-format audio (from PlayStation).
 *%*
 *%* #BUILD-DEPENDS
 *%*
 *%* #DEPENDS
 *%*
 *%* PROCESSING
 *%*   import/demuxer
 *%*
 *%* MEDIA
 *%*   audio
 *%*
 *%* #INPUT
 *%*
 *%* OUTPUT
 *%*   PCM
 *%*
 *%* OPTION
 *%*   blocksize (integer)
 *%*     stereo blocking size.
 *%*/


#define MOD_FEATURES \
    TC_MODULE_FEATURE_DECODE|TC_MODULE_FEATURE_AUDIO
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE

#include "src/transcode.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"
#include "libtcmodule/tcmodule-plugin.h"

/* For UNLIKELY() (FIXME: this should be defined somewhere useful) */
#include "aclib/ac_internal.h"


/* Maximum stereo block size we allow */
#define MAX_STEREO_BLOCK 0x1000
/* Default stereo block size */
#define DEF_STEREO_BLOCK 0x1000

/* Private data used by this module. */
typedef struct {
    enum {VAG,PCM} type; /* bits==1 in SShd block indicates 16-bit PCM data */
    int blocksize;       /* Stereo block size */
    uint8_t databuf[MAX_STEREO_BLOCK];  /* For accumulating data */
    int datalen;
    int datapos;
    int nclip;           /* Number of sampled clipped */
    int prevsamp[2][2];  /* prevsamp[ch][0] is the immediately previous sample;
                          * prevsamp[ch][1] is the sample before that */
    int totalread;       /* For statistics */
} PrivateData;

/* Local routine declarations. */
static int vag_decode(TCModuleInstance *self,
                      aframe_list_t *inframe, aframe_list_t *outframe);
static int do_decode(const uint8_t *inbuf, int16_t *outbuf, int chan,
                     PrivateData *pd);

/*************************************************************************/
/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * vag_init:  Initialize this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int vag_init(TCModuleInstance *self, uint32_t features)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    pd = tc_zalloc(sizeof(PrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }
    pd->type = VAG;
    pd->blocksize = DEF_STEREO_BLOCK;
    self->userdata = pd;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * vag_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int vag_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    tc_free(self->userdata);
    self->userdata = NULL;
    return TC_OK;
}

/*************************************************************************/

/**
 * vag_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int vag_configure(TCModuleInstance *self,
                         const char *options,
                         TCJob *vob,
                         TCModuleExtraData *xdata[])
{
    TC_MODULE_SELF_CHECK(self, "configure");

    return TC_OK;
}

/*************************************************************************/

/**
 * vag_stop:  Reset this instance of the module.  See tcmodule-data.h for
 * function details.
 */

static int vag_stop(TCModuleInstance *self)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    if (verbose & TC_DEBUG)
        tc_log_info(MOD_NAME, "%d bytes processed", pd->totalread);
    if (pd->nclip > 0)
        tc_log_info(MOD_NAME, "%d samples clipped", pd->nclip);

    pd->datalen = 0;
    pd->datapos = 0;
    pd->nclip = 0;
    pd->prevsamp[0][0] = 0;
    pd->prevsamp[0][1] = 0;
    pd->prevsamp[1][0] = 0;
    pd->prevsamp[1][1] = 0;
    pd->totalread = 0;

    return TC_OK;
}

/*************************************************************************/

/**
 * vag_inspect:  Return the value of an option in this instance of the
 * module.  See tcmodule-data.h for function details.
 */

static int vag_inspect(TCModuleInstance *self,
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
                "    Decodes PlayStation VAG format (ADPCM-style) audio.\n"
                "Options available:\n"
                "    blocksize=N   Set stereo blocking size (16-%d, default %d)\n",
                MAX_STEREO_BLOCK, DEF_STEREO_BLOCK);
        *value = buf;
        return TC_IMPORT_OK;
    }
    if (optstr_lookup(param, "blocksize")) {
        tc_snprintf(buf, sizeof(buf), "%d", pd->blocksize);
        *value = buf;
        return TC_IMPORT_OK;
    }
    return TC_IMPORT_OK;
}

/*************************************************************************/

static const TCCodecID vag_codecs_audio_in[] =
    { TC_CODEC_VAG, TC_CODEC_ERROR };
static const TCCodecID vag_codecs_audio_out[] =
    { TC_CODEC_PCM, TC_CODEC_ERROR };
TC_MODULE_VIDEO_UNSUPPORTED(vag);
TC_MODULE_CODEC_FORMATS(vag);

TC_MODULE_INFO(vag);

static const TCModuleClass vag_class = {
    TC_MODULE_CLASS_HEAD(vag),

    .init         = vag_init,
    .fini         = vag_fini,
    .configure    = vag_configure,
    .stop         = vag_stop,
    .inspect      = vag_inspect,

    .decode_audio = vag_decode,
};

TC_MODULE_ENTRY_POINT(vag)

/*************************************************************************/
/*************************************************************************/

/**
 * vag_decode:  Decode a frame of data.  See tcmodule-data.h for function
 * details.
 */

static int vag_decode(TCModuleInstance *self,
                      aframe_list_t *inframe, aframe_list_t *outframe)
{
    PrivateData *pd;
    uint8_t *inptr;
    int insize;
    int16_t *outptr;

    TC_MODULE_SELF_CHECK(self, "decode");
    TC_MODULE_SELF_CHECK(inframe, "decode");
    TC_MODULE_SELF_CHECK(outframe, "decode");

    pd = self->userdata;
    inptr = inframe->audio_buf;
    insize = inframe->audio_size;
    outptr = (int16_t *)outframe->audio_buf;
    outframe->audio_size = 0;

    /* Fill out any accumulated data block first */
    if (pd->datalen > 0) {
        int needed = 16 - pd->datalen;
        if (insize < needed) {
            /* Not enough for a 16-byte block--copy and exit */
            memcpy(pd->databuf + pd->datalen, inframe->audio_buf, insize);
            pd->datalen += insize;
            return TC_OK;
        } else {
            /* Finish off the partial block */
            memcpy(pd->databuf + pd->datalen, inframe->audio_buf, needed);
            insize -= needed;
            do_decode(pd->databuf, outptr, 0, pd);
            outptr += 28;
            pd->datalen = 0;
        }
    }

    /* Loop through all complete data blocks in the input */
    while (insize >= 16) {
        do_decode(inptr, outptr, 0, pd);
        inptr += 16;
        insize -= 16;
        outptr += 28;
    }

    /* Save any remaining data in the accumulation buffer */
    if (insize > 0) {
        memcpy(pd->databuf, inptr, insize);
        pd->datalen = insize;
    }

    return TC_OK;
}

/*************************************************************************/

/**
 * do_decode:  Decode a single block of 16 bytes into 28 samples (VAG mode)
 * or 8 samples (PCM mode).
 *
 * Parameters:
 *      inbuf: Pointer to 16 input bytes.
 *     outbuf: Pointer to buffer of at least 56 bytes (28 samples).
 *       chan: Channel selector (0 or 1); controls which previous-sample
 *             data is used.
 *         pd: Pointer to module instance's PrivateData structure.
 * Return value:
 *     The number of samples decoded.
 * Preconditions:
 *     inbuf != NULL
 *     outbuf != NULL
 *     chan == 0 || chan == 1
 *     pd != NULL
 */

static int do_decode(const uint8_t *inbuf, int16_t *outbuf, int chan,
                     PrivateData *pd)
{
    static const int predict[16][2] = {
        {  0,  0},
        { 60,  0},
        {115, 52},
        { 98, 55},
        {122, 60},
        {  0,  0},
        {  0, 60},
    };
    int type = inbuf[0] >> 4;
    int scale = 16 - (inbuf[0] & 0x0F);
    int prev0 = pd->prevsamp[chan][0];  /* Pull into variables for speed */
    int prev1 = pd->prevsamp[chan][1];
    int i;

    if (pd->type == PCM) {
        memcpy(outbuf, inbuf, 16);
        return 8;
    }

    for (i = 0; i < 28; i++) {
        int val;
        if (i%2 == 0)
            val = inbuf[2+i/2] & 0x0F;
        else
            val = inbuf[2+i/2] >> 4;
        if (val >= 8)
            val -= 16;
        val <<= scale;
        val = (prev0*predict[type][0] - prev1*predict[type][1] + (val<<2)) >>6;
        if (UNLIKELY(val > 0x7FFF)) {
            if (verbose & TC_DEBUG) {
                tc_log_warn(MOD_NAME, "clipping to +max: prev1=%c%04X prev0="
                            "%c%04X val=+%04X (type/scale/in=%X/%X/%X)",
                            prev1>=0 ? '+' : '-', prev1 & 0xFFFF,
                            prev0>=0 ? '+' : '-', prev0 & 0xFFFF,
                            val & 0xFFFF, type, 16-scale,
                            i%2==0 ? inbuf[2+i/2]&0x0F : inbuf[2+i/2]>>4);
            }
            val = 0x7FFF;
        }
        if (UNLIKELY(val < -0x8000)) {
            if (verbose & TC_DEBUG) {
                tc_log_warn(MOD_NAME, "clipping to -min: prev1=%c%04X prev0="
                            "%c%04X val=-%04X (type/scale/in=%X/%X/%X)",
                            prev1>=0 ? '+' : '-', prev1 & 0xFFFF,
                            prev0>=0 ? '+' : '-', prev0 & 0xFFFF,
                            val & 0xFFFF, type, 16-scale,
                            i%2==0 ? inbuf[2+i/2]&0x0F : inbuf[2+i/2]>>4);
            }
            val = -0x8000;
        }
        outbuf[i] = val;
        prev1 = prev0;
        prev0 = val;
    }

    /* Update private data */
    pd->prevsamp[chan][0] = prev0;
    pd->prevsamp[chan][1] = prev1;
    pd->totalread += 16;

    /* 28 samples decoded */
    return 28;
}

/*************************************************************************/
/*************************************************************************/

/* Old-fashioned module stuff */

static PrivateData static_pd;
static FILE *file;
static int16_t saved_samples[56];
static int saved_samples_count;
static int mpeg_mode;  // extracting from program stream?
static int mpeg_packet_left;  // for xread()
static int mpeg_check_for_header;
static int mpeg_stop;

static int verbose_flag;
static int capability_flag = TC_CAP_PCM;
#define MOD_PRE vagOLD
#define MOD_CODEC "(audio) PS-VAG"
#include "import_def.h"
#include "magic.h"

/*************************************************************************/

MOD_open
{
    uint8_t buf[16];

    if (param->flag != TC_AUDIO)
        return TC_ERROR;

    if (vob->a_chan != 1 && vob->a_chan != 2) {
        tc_log_error(MOD_NAME, "%d channels not supported (must be 1 or 2)",
                     vob->a_chan);
        return TC_ERROR;
    }
    if (vob->a_bits != 16) {
        tc_log_error(MOD_NAME, "%d bits not supported (must be 16)",
                     vob->a_bits);
        return TC_ERROR;
    }

    memset(&static_pd, 0, sizeof(static_pd));
    if (vob->im_a_string
     && sscanf(vob->im_a_string, "blocksize=%d", &static_pd.blocksize) == 1
    ) {
        if (static_pd.blocksize<16 || static_pd.blocksize>MAX_STEREO_BLOCK) {
            tc_log_error(MOD_NAME, "Block size %d out of range (16...%d)",
                         static_pd.blocksize, MAX_STEREO_BLOCK);
            return TC_ERROR;
        } else if (static_pd.blocksize & 15) {
            tc_log_error(MOD_NAME, "Block size %d not a multiple of 16",
                         static_pd.blocksize);
            return TC_ERROR;
        }
    } else {
        static_pd.blocksize = DEF_STEREO_BLOCK;
    }
    saved_samples_count = 0;

    param->fd = NULL;  /* we handle the reading ourselves */
    file = fopen(vob->audio_in_file, "r");
    if (!file) {
        tc_log_error(MOD_NAME, "Unable to open %s: %s", vob->audio_in_file,
                     strerror(errno));
        return TC_ERROR;
    }

    /* Check whether this is an MPEG stream and enable the hacks if needed */
    if (fread(buf, 5, 1, file) != 1) {
        tc_log_error(MOD_NAME, "File %s is empty!", vob->audio_in_file);
        fclose(file);
        file = NULL;
        return TC_ERROR;
    }
    if ((buf[0]<<24 | buf[1]<<16 | buf[2]<<8 | buf[3]) == TC_MAGIC_VOB) {
        mpeg_mode = 1;
        mpeg_packet_left = 0;
        mpeg_check_for_header = 1;
        mpeg_stop = 0;
        if ((buf[4] & 0xC0) == 0x40) {  /* mpeg2 */
            if (fread(buf, 9, 1, file) != 1) {
                tc_log_error(MOD_NAME, "%s: short file!", vob->audio_in_file);
                goto close_and_abort;
            }
            if ((buf[8] & 7) && fread(buf, buf[8] & 7, 1, file) != 1) {
                tc_log_error(MOD_NAME, "%s: short file!", vob->audio_in_file);
                goto close_and_abort;
            }
        } else if ((buf[4] & 0xF0) == 0x20) {  /* mpeg1 */
            if (fread(buf, 7, 1, file) != 1) {
                tc_log_error(MOD_NAME, "%s: short file!", vob->audio_in_file);
                goto close_and_abort;
            }
        } else {
            tc_log_error(MOD_NAME, "%s: bizarre MPEG stream!",
                         vob->audio_in_file);
            goto close_and_abort;
        }
    } else {
        mpeg_mode = 0;
        if (vob->a_chan == 2) {
            memcpy(static_pd.databuf, buf, 5);
            if (fread(static_pd.databuf+5,static_pd.blocksize-5,1,file) != 1) {
                tc_log_error(MOD_NAME, "%s: short file!", vob->audio_in_file);
                goto close_and_abort;
            }
            static_pd.datalen = static_pd.blocksize;
        } else {  /* mono */
            if (fread(buf+5, 11, 1, file) != 1) {
                tc_log_error(MOD_NAME, "%s: short file!", vob->audio_in_file);
                goto close_and_abort;
            }
            do_decode(buf, saved_samples, 0, &static_pd);
            saved_samples_count = 28;
        }
    }  /* if an MPEG stream */

    return TC_OK;

  close_and_abort:
    fclose(file);
    file = NULL;
    return TC_ERROR;
}

/*************************************************************************/

MOD_close
{
    if (verbose & TC_DEBUG)
        tc_log_info(MOD_NAME, "%d bytes processed", static_pd.totalread);
    if (static_pd.nclip > 0)
        tc_log_info(MOD_NAME, "%d samples clipped", static_pd.nclip);

    if (file) {
        fclose(file);
        file = NULL;
    }
    return TC_OK;
}

/*************************************************************************/

/**
 * xread:  Read data like fread(), but if `mpeg_mode' is nonzero, extract
 * the data from the MPEG stream.  Parameters and return value are as for
 * fread(), except that the buffer is of type uint8_t * for simplicity, and
 * the PrivateData parameter is passed so internal state can be referenced.
 * This is a really ugly hack; I hope the new module system gets moving
 * soon...
 */

static size_t xread(uint8_t *buf, size_t elsize, size_t els, FILE *f,
                    PrivateData *pd)
{
    int nread;  /* Total bytes read */
    uint8_t readbuf[2048];

    if (!mpeg_mode)
        return fread(buf, elsize, els, f);
    if (mpeg_stop)
        return TC_OK;

    nread = 0;
    if (mpeg_packet_left > 0) {
        if (mpeg_packet_left >= elsize*els) {
            nread = fread(buf, 1, elsize*els, f);
            mpeg_packet_left -= nread;
            return nread / elsize;
        } else {
            nread = fread(buf, 1, mpeg_packet_left, f);
            if (nread < mpeg_packet_left)  /* EOF */
                return TC_OK;
            mpeg_packet_left = 0;
        }
    }

    while (nread < elsize*els) {
        if (fread(readbuf, 4, 1, f) != 1)
            break;
        if (memcmp(readbuf, "\0\0\1", 3) != 0) {
            tc_log_warn(MOD_NAME, "No start code found at %ld",
                        (long)ftell(f)-4);
            break;
        }
        if (verbose & TC_DEBUG) {
            tc_log_msg(MOD_NAME, "Start code 0x%02X at %ld", readbuf[3],
                       (long)ftell(f)-4);
        }
        if (readbuf[3] == 0xB9) {  /* program end */
            if (verbose & TC_DEBUG)
                tc_log_msg(MOD_NAME, "Program end code found");
            mpeg_stop = 1;
            break;
        } else if (readbuf[3] == 0xBA) {
            if (fread(readbuf, 8, 1, f) != 1)
                break;
            if ((readbuf[0] & 0xC0) == 0x40) {  /* mpeg2 */
                if (fread(readbuf, 2, 1, f) != 1)
                    break;
                if ((readbuf[1]&7) && fread(readbuf, readbuf[1]&7, 1, f) != 1)
                    break;
            }
        } else {
            int packetlen;
            if (fread(readbuf+4, 2, 1, f) != 1)
                break;
            packetlen = readbuf[4]<<8 | readbuf[5];
            if (readbuf[3] != 0xBD) {
                while (packetlen > 0) {
                    int toread = sizeof(readbuf);
                    if (toread > packetlen)
                        toread = packetlen;
                    if (fread(readbuf, toread, 1, f) != 1)
                        break;
                    packetlen -= toread;
                }
                if (packetlen > 0)  /* i.e. read failed in the while loop */
                    break;
            } else {
                /* 0xBD==private stream 1: get stream ID (VAG audio is 0xFF) */
                if (fread(readbuf, 1, 1, f) != 1)
                    break;
                packetlen -= 1;
                if ((readbuf[0] & 0xC0) == 0x80) {  /* mpeg2 */
                    if (fread(readbuf, 2, 1, f) != 1)
                        break;
                    packetlen -= 2+readbuf[1];
                    if (fread(readbuf, readbuf[1], 1, f) != 1)
                        break;
                } else {  /* mpeg1 */
                    int skipbytes;
                    while (readbuf[0] == 0xFF) {
                        if (fread(readbuf, 1, 1, f) != 1)
                            break;
                        packetlen -= 1;
                    }
                    if (readbuf[0] == 0xFF)  /* i.e. read failed */
                        break;
                    if ((readbuf[0] & 0xC0) == 0x40) {
                        if (fread(readbuf, 2, 1, f) != 1)
                            break;
                        packetlen -= 2;
                        readbuf[0] = readbuf[1];
                    }
                    switch (readbuf[0] >> 4) {
                        case 0:  skipbytes =  1; break;
                        case 2:  skipbytes =  5; break;
                        case 3:  skipbytes = 10; break;
                        default: skipbytes =  0; break;
                    }
                    if (skipbytes) {
                        if (fread(readbuf, skipbytes, 1, f) != 1)
                            break;
                        packetlen -= skipbytes;
                    }
                }
                if (fread(readbuf, 1, 1, f) != 1)
                    break;
                packetlen -= 1;
                if (verbose & TC_DEBUG)
                    tc_log_msg(MOD_NAME, "... stream code 0x%02X", readbuf[0]);
                if (readbuf[0] != 0xFF) {
                    while (packetlen > 0) {
                        int toread = sizeof(readbuf);
                        if (toread > packetlen)
                            toread = packetlen;
                        if (fread(readbuf, toread, 1, f) != 1)
                            break;
                        packetlen -= toread;
                    }
                    if (packetlen > 0)  /* i.e. read failed */
                        break;
                } else {
                    /* A desired data packet, at last */
                    int toread;
                    if (packetlen < 3) {
                        /* okay, enough is enough */
                        tc_log_error(MOD_NAME,
                                     "private stream 1 packet too small!!");
                        return TC_OK;
                    }
                    if (fread(readbuf, 3, 1, f) != 1)
                        break;
                    packetlen -= 3;
                    /* FIXME: this won't work if we read 1 byte at a time */
                    if (mpeg_check_for_header
                     && packetlen >= 4
                     && nread+4 <= els*elsize
                    ) {
                        mpeg_check_for_header = 0;
                        if (fread(readbuf, 4, 1, f) != 1)
                            break;
                        packetlen -= 4;
                        if (memcmp(readbuf,"SShd",4) == 0 && packetlen >= 36) {
                            int bits, rate, chans, block, size;
                            if (fread(readbuf+4, 36, 1, f) != 1)
                                break;
                            packetlen -= 36;
                            bits  = readbuf[ 8]     | readbuf[ 9]<<8
                                  | readbuf[10]<<16 | readbuf[11]<<24;
                            rate  = readbuf[12]     | readbuf[13]<<8
                                  | readbuf[14]<<16 | readbuf[15]<<24;
                            chans = readbuf[16]     | readbuf[17]<<8
                                  | readbuf[18]<<16 | readbuf[19]<<24;
                            block = readbuf[20]     | readbuf[21]<<8
                                  | readbuf[22]<<16 | readbuf[23]<<24;
                            size  = readbuf[36]     | readbuf[37]<<8
                                  | readbuf[38]<<16 | readbuf[39]<<24;
                            if (bits == 1) {
                                pd->type = PCM;
                                bits = 16;
                            }
                            tc_log_info(MOD_NAME,
                                        "MPEG-embedded %s audio: %d/%d/%d,"
                                        " stereo blocksize %d, %d data bytes",
                                        pd->type==PCM ? "PCM" : "VAG",
                                        rate, bits, chans, block, size);
                        } else {
                            memcpy(buf+nread, readbuf, 4);
                            nread += 4;
                        }
                    }
                    /* Whew, now we can start reading */
                    toread = els*elsize - nread;
                    if (toread > packetlen)
                        toread = packetlen;
                    toread = fread(buf+nread, 1, toread, f);
                    nread += toread;
                    mpeg_packet_left = packetlen - toread;
                    if (mpeg_packet_left > 0)
                        break;  /* stopped reading in the middle */
                }
            }  /* if 0xBD */
        }  /* if not 0xB9/0xBA */
    }  /* while bytes left to read */

    return nread / elsize;
}

/*************************************************************************/

MOD_decode
{
    uint8_t inbuf[16];
    int outlimit = param->size / 2;
    int outcount = 0;  /* total samples for the frame */

    while (outcount < outlimit) {
        /* First save any samples in the output buffer */
        if (saved_samples_count > 0) {
            if (outcount + saved_samples_count > outlimit) {
                int nleft = outlimit - outcount;
                memcpy(param->buffer + outcount*2, saved_samples, nleft*2);
                outcount += nleft;
                saved_samples_count -= nleft;
                memmove(saved_samples, saved_samples + nleft,
                        saved_samples_count*2);
                break;
            } else {
                memcpy(param->buffer + outcount*2, saved_samples,
                       saved_samples_count*2);
                outcount += saved_samples_count;
                saved_samples_count = 0;
            }
        }
        /* Now read the next block of data and decode it to the output buffer*/
        if (vob->a_chan == 2 && static_pd.datapos >= static_pd.datalen) {
            /* Finished the previous stereo block, read a new one */
            if (xread(static_pd.databuf, static_pd.blocksize, 1, file, &static_pd) != 1) {
                if (verbose & TC_DEBUG)
                    tc_log_msg(MOD_NAME, "EOF reached");
                break;
            }
            static_pd.datalen = static_pd.blocksize;
            static_pd.datapos = 0;
        }
        if (xread(inbuf, 16, 1, file, &static_pd) != 1) {
            if (verbose & TC_DEBUG)
                tc_log_msg(MOD_NAME, "EOF reached");
            break;
        }
        if (vob->a_chan == 1) {
            saved_samples_count =
                do_decode(inbuf, saved_samples, 0, &static_pd);
        } else {
            uint16_t outbuf0[28], outbuf1[28];
            int i, nsamp;
            nsamp = do_decode(static_pd.databuf + static_pd.datapos,
                                     outbuf0, 0, &static_pd);
            (void)  do_decode(inbuf, outbuf1, 1, &static_pd);
            for (i = 0; i < nsamp; i++) {
                saved_samples[i*2  ] = outbuf0[i];
                saved_samples[i*2+1] = outbuf1[i];
            }
            saved_samples_count = nsamp*2;
            static_pd.datapos += 16;
        }
    }

    /* All done, set final size and return */
    param->size = outcount*2;
    return outcount<outlimit ? -1 : 0;
}

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
