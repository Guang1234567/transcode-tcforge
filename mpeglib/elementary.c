/*
 *  Copyright (C) 2005/2010 Francesco Romani <fromani@gmail.com>
 * 
 *  This Software is heavily based on tcvp's (http://tcvp.sf.net) 
 *  mpeg muxer/demuxer, which is
 *
 *  Copyright (C) 2001-2002 Michael Ahlberg, Mans Rullgard
 *  Copyright (C) 2003-2004 Michael Ahlberg, Mans Rullgard
 *  Copyright (C) 2005 Michael Ahlberg, Mans Rullgard
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without
 *  restriction, including without limitation the rights to use, copy,
 *  modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "mpeglib.h"
#include "mpeglib_private.h"

typedef struct mpeg_es_data mpeg_es_data_t;
struct mpeg_es_data {
    mpeg_stream_t s;
    int smap;
};

static mpeg_res_t mpeg_es_close(mpeg_t *MPEG)
{

    assert(MPEG != NULL);

    if (MPEG->priv != NULL) {
         mpeg_free(MPEG->priv);
         MPEG->priv = NULL;
    }
    
    MPEG->streams = NULL;
    MPEG->smap = NULL;

    return MPEG_OK;
}

#define MPEG_ES_PKT_SIZE                1024

static const mpeg_pkt_t* mpeg_es_read_packet(mpeg_t *MPEG, int stream_id)
{
    mpeg_es_data_t *me = NULL;
    mpeg_pkt_t *pes = NULL;
    int size = MPEG_ES_PKT_SIZE;

    assert(MPEG != NULL);
    
    pes = mpeg_pkt_new(MPEG_ES_PKT_SIZE);
    if (pes == NULL) {
        MPEG->errcode = MPEG_ERROR_NO_MEM; 
        mpeg_log(MPEG_LOG_ERR,
            "can't allocate PES packet\n");
        return NULL;
    }

    me = MPEG->priv;
    size = MPEG->MFILE->read(MPEG->MFILE, pes->data, 1, MPEG_ES_PKT_SIZE);
    if (size <= 0){
        MPEG->errcode = MPEG_ERROR_READ;
        mpeg_log(MPEG_LOG_ERR,
            "can't read PES packet from file\n");
        mpeg_pkt_del(pes);
        return NULL;
    }
    
    pes->size = size;
    return pes;
}

#define MPEG_ES_PROBE_BUFSIZE                   256

static mpeg_res_t mpeg_es_probe(mpeg_t *MPEG)
{
    int ret = MPEG_OK, i = 0;
    uint8_t data[MPEG_ES_PROBE_BUFSIZE];
    mpeg_es_data_t *me = NULL;
    mpeg_file_t *MFILE = NULL;
    
    assert(MPEG != NULL);

    me = MPEG->priv;
    MFILE = MPEG->MFILE;

    ret = MFILE->read(MFILE, data, 1, MPEG_ES_PROBE_BUFSIZE);
    if (ret != MPEG_ES_PROBE_BUFSIZE) {
        MPEG->errcode = MPEG_ERROR_READ;
        mpeg_log(MPEG_LOG_ERR,
                "can't read enough data to probe\n");
        return MPEG_ERR;
    }

    for (i = 0; i < MPEG_STREAM_TYPES_NUM; i++) {
        ret = mpeg_stream_types[i].probe(&(me->s), data, MPEG_ES_PROBE_BUFSIZE);
        if (ret == MPEG_OK) {
            me->s.stream_type = mpeg_stream_types[i].stream_type;
            me->s.video.stream_id = 
                    mpeg_stream_types[i].stream_id_base;
            me->s.video.codec = mpeg_stream_types[i].codec;
            break;
        }
    }
    /* all probes failed! */
    if (ret == MPEG_ERROR_PROBE_FAILED) {
        MPEG->errcode = MPEG_ERROR_BAD_FORMAT;
        mpeg_log(MPEG_LOG_ERR, "unknown file type. "
                       "This is really an mpeg ES?\n");
        ret = MPEG_ERR;
    }
    
    return ret;
}

mpeg_res_t mpeg_es_open(mpeg_t *MPEG, mpeg_file_t *MFILE, uint32_t flags)
{
    mpeg_es_data_t *me = NULL;
    int ret = MPEG_OK;

    /* mpeg_mallocz initialize correctyl smap for us */

    me = mpeg_mallocz(sizeof(mpeg_es_data_t));
    if (me == NULL) {
        MPEG->errcode = MPEG_ERROR_NO_MEM;                
        mpeg_log(MPEG_LOG_ERR,
                "can't allocate private descriptor\n");
        return MPEG_ERR;
    }

    MPEG->type      = MPEG_TYPE_ES;
    MPEG->n_streams = 1;
    MPEG->streams   = &me->s;
    MPEG->smap      = &me->smap;
    MPEG->priv      = me;

    MPEG->smap[0] = 0;
    
    MPEG->read_packet = mpeg_es_read_packet;
    MPEG->probe       = mpeg_es_probe;
    MPEG->close       = mpeg_es_close;
    
    MPEG->errcode = MPEG_ERROR_NONE;

    if (flags & MPEG_FLAG_PROBE) {
        ret = mpeg_probe(MPEG);
    }
    
    return ret;
}


