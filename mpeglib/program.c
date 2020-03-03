/*
 *  Copyright (C) 2005 Francesco Romani <fromani@gmail.com>
 * 
 *  This Software is heavily based on tcvp's (http://tcvp.sf.net) 
 *  mpeg muxer/demuxer, which is
 *
 *  Copyright (C) 2001-2002 Michael Ahlberg, M<C3><A5>ns Rullg<C3><A5>rd
 *  Copyright (C) 2003-2004 Michael Ahlberg, M<C3><A5>ns Rullg<C3><A5>rd
 *  Copyright (C) 2005 Michael Ahlberg, M<C3><A5>ns Rullg<C3><A5>rd
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
#include <string.h>
#include <unistd.h>

#include "mpeglib.h"
#include "mpeglib_private.h"

enum {
    DVD_PTSSKIP =                1,
    DVD_FLUSH =                  2,
    DVD_STILL =                  3,
    DVD_AUDIO_ID =               4
};

typedef struct dvd_ptsskip dvd_ptsskip_t;
struct dvd_ptsskip {
    int type;
    int64_t offset;
};

typedef struct dvd_flush dvd_flush_t;
struct dvd_flush {
    int type;
    int drop;
};

typedef struct dvd_audio_id dvd_audio_id_t;
struct dvd_audio_id {
    int type;
    int id;
};

typedef union dvd_event dvd_event_t;
union dvd_event {
    int type;
    dvd_ptsskip_t ptsskip;
    dvd_flush_t flush;
    dvd_audio_id_t audio;
};


typedef struct mpeg_ps_data mpeg_ps_data_t;
struct mpeg_ps_data {
    int *imap; /** stream_id -> stream_num */
    int *map;  /** stream_num -> stream_id */
    int rate;
    int64_t pts_offset;
    uint64_t duration;
    int ns; /* starting number of streams */
};

#define MATCH_STREAM_ID(pid, id) \
    (((id) == MPEG_STREAM_ANY) || ((pid) == (id)))
    
static const mpeg_pkt_t* mpeg_ps_read_packet(mpeg_t *MPEG, int stream_id)
{
    mpeg_ps_data_t *s = MPEG->priv;
    mpeg_pkt_t *mp = NULL;
    int sx = -1;

    assert(MPEG != NULL);
    
    do {
        if(mp != NULL) {
            mpeg_pkt_del(mp);
        }
        mp = mpeg_pes_read_packet(MPEG, FALSE);
        if(mp == NULL) {
            /* 
             * do not pollute the errcode set by
             * mpeg_pes_read_packet()!
             */
            return NULL;
        }

        sx = s->imap[mp->stream_id];
    
        if(IS_AC3(mp->stream_id) || IS_DTS(mp->stream_id)) {
            /* FIXME: why? */
            mp->data += 4;
            mp->size -= 4;
        } else if(IS_LPCM(mp->stream_id)) {
            /* FIXME: why? */
            int aup = htob_16(unaligned16(mp->data + 2));
            mp->data += 7;
            mp->size -= 7;
            if(mp->flags & MPEG_PKT_FLAG_PTS) {
                mp->pts -= 27000000LL * aup / 
                MPEG->streams[sx].common.bit_rate;
            }   
        } else if(IS_SPU(mp->stream_id)) {
            mp->data++;
            mp->size--;
        } else if(mp->stream_id == DVD_PESID){
            dvd_event_t *de = (dvd_event_t *) mp->data;
            switch(de->type){
            case DVD_PTSSKIP:
                s->pts_offset = de->ptsskip.offset;
                break;
            case DVD_FLUSH:
                mp->type = MPEG_PKT_TYPE_FLUSH;
                mp->stream_id = de->flush.drop;
                return mp;
            case DVD_STILL:
                mp->type = MPEG_PKT_TYPE_STILL;
                return mp;
            case DVD_AUDIO_ID:
                s->imap[s->map[1]] = -1;
                s->imap[de->audio.id] = 1;
                s->map[1] = de->audio.id;
                break;
            }
            continue;
        }
    } while(sx < 0 || !MATCH_STREAM_ID(stream_id, mp->stream_id));

    if(mp->flags & MPEG_PKT_FLAG_PTS) {
        mp->pts += s->pts_offset;
        mp->dts += s->pts_offset;
        mp->flags |= MPEG_PKT_FLAG_PTS;
        if(mp->pts) {
            // XXX: this can fail!
            s->rate = MPEG->MFILE->tell(MPEG->MFILE) * 90 / mp->pts;
        }
        s->duration = mp->pts;
    }

    if(mp->flags & MPEG_PKT_FLAG_DTS) {
        mp->dts = mp->dts * 300;
        mp->flags |= MPEG_PKT_FLAG_DTS;
    }

    return mp;
}


static mpeg_res_t mpeg_ps_close(mpeg_t *MPEG)
{
    mpeg_ps_data_t *ps = MPEG->priv;

    if(MPEG->streams > 0) {
        mpeg_free(MPEG->streams);
        MPEG->streams = NULL;
    }
    
    if(ps != NULL) {
        if(ps->imap != NULL) {
            mpeg_free(ps->imap);
            ps->imap = NULL;
        }
        if(ps->map != NULL); {
            mpeg_free(ps->map);
            ps->map = NULL;
        }
        mpeg_free(ps);
        MPEG->priv = NULL;
    }

    return MPEG_OK;
}

#ifdef PARSE_STREAM_MAP
static mpeg_res_t mpeg_ps_parse_stream_map(mpeg_t *MPEG)
{
    mpeg_pkt_t *pes = NULL;  
    int l = 0, ns = MPEG_STREAMS_NUM_BASE, pkt_cnt = 0;
    int probed = FALSE;
    mpeg_res_t ret = MPEG_OK;
    uint8_t *pm = NULL;
    int mpeg_stream_counter[MPEG_STREAM_TYPES_NUM] = { 0 };
    mpeg_stream_t *sp = NULL;
    mpeg_ps_data_t *ps = NULL;
    
    assert(MPEG != NULL);
    
    sp = MPEG->streams;
    ps = MPEG->priv;

    mpeg_log(MPEG_LOG_INFO, "MPEG-PS: looking for program stream map...\n");
    
    for(pkt_cnt = 0; pkt_cnt < MPEG_PKTS_MAX_PROBE; pkt_cnt++) {
        pes = mpeg_pes_read_packet(MPEG, TRUE);
        if(pes == NULL) {
            break;
        }
        
        if(pes->stream_id == MPEG_PROGRAM_STREAM_MAP) {
            probed = TRUE;      
            pm = pes->data + 2;
            l = htob_16(unaligned16(pm));
            pm += l + 2;
            l = htob_16(unaligned16(pm));
            pm += 2;

            while(l > 0) {
                uint32_t stype = *pm++;
                uint32_t sid = *pm++;
                uint32_t il = htob_16(unaligned16(pm));
                int sti;

                pm += 2;

                if(MPEG->n_streams == ns) {
                    ns *= 2;
                    // XXX
                    MPEG->streams = realloc(MPEG->streams, 
                                            ns * sizeof(*MPEG->streams));
                    sp = &MPEG->streams[MPEG->n_streams];
                }

                ps->imap[sid] = MPEG->n_streams;
                ps->map[MPEG->n_streams] = sid;

                sti = stream_type2codec(stype);
                if(sti >= 0) {
                    memset(sp, 0, sizeof(*sp));
                    sp->stream_type = mpeg_stream_types[sti].stream_type;
                    sp->common.stream_id = mpeg_stream_types[sti].stream_id_base + 
                    mpeg_stream_counter[sti]++;
                    sp->common.codec = mpeg_stream_types[sti].codec;
                    sp->common.index = MPEG->n_streams++;

                    while(il > 0) {
                        int dl = mpeg_parse_descriptor(sp, pm, 0);
                        pm += dl;
                        il -= dl;
                        l -= dl;
                    }

                    sp++;
                } else {
                    pm += il;
                    l -= il;
                }
                l -= 4;
            }
        
            mpeg_log(MPEG_LOG_INFO, "MPEG-PS: program stream map found "
                    "at packet %i\n", pkt_cnt); 
            break;
        }
        mpeg_pkt_del(pes);
    }
    
    // XXX unwanted errcode pollution?
    if(!probed) {
        MPEG->errcode = MPEG_ERROR_PROBE_FAILED;
        mpeg_log(MPEG_LOG_WARN, "MPEG-PS: program stream map not found "
                    "after %i packets, giving up...\n",
                pkt_cnt);
        return MPEG_ERR;
    }
    return MPEG_OK;
}
#endif

static mpeg_res_t mpeg_ps_probe_streams(mpeg_t *MPEG, int ns)
{
    int pkt_cnt = 0, probed = 0, pkt_unk = 0;
    mpeg_res_t ret = MPEG_OK;
    mpeg_pkt_t *pes = NULL;
    mpeg_stream_t *sp = NULL;
    mpeg_ps_data_t *ps = NULL;
    
    assert(MPEG != NULL);
    
    sp = MPEG->streams;
    ps = MPEG->priv;

    mpeg_log(MPEG_LOG_INFO, "MPEG-PS: probing each stream individually...\n");
    
    for(pkt_cnt = 0; pkt_cnt < MPEG_PKTS_MAX_PROBE; pkt_cnt++) {
        pes = mpeg_pes_read_packet(MPEG, TRUE);
        if(pes == NULL) {
            break;
        }

        if(IS_MPVIDEO(pes->stream_id) || IS_MPAUDIO(pes->stream_id) ||
           IS_AC3(pes->stream_id) || IS_LPCM(pes->stream_id)) {
            if(ps->imap[pes->stream_id] < 0) {
                if(MPEG->n_streams == ns){
                    ns *= 2;
                    MPEG->streams = realloc(MPEG->streams, 
                                        ns * sizeof(*MPEG->streams));
                    sp = &MPEG->streams[MPEG->n_streams];
                }

                memset(sp, 0, sizeof(*sp));
                ps->imap[pes->stream_id] = MPEG->n_streams;
                ps->map[MPEG->n_streams] = pes->stream_id;

                if(IS_MPVIDEO(pes->stream_id)) {
                    /* Well, this is a layer violation? */
                    mpeg_probe_mpvideo(sp, pes->data, pes->size);
                    sp->stream_type = MPEG_STREAM_TYPE_VIDEO;
                    sp->common.stream_id = pes->stream_id;                          
                    sp->common.codec = "video/mpeg";
                } else if(IS_MPAUDIO(pes->stream_id))  {
                    mpeg_probe_mpaudio(sp, pes->data, pes->size);
                    sp->stream_type = MPEG_STREAM_TYPE_AUDIO;
                    sp->common.stream_id = pes->stream_id;                     
                    sp->common.codec = "audio/mpeg";
                } else if(IS_AC3(pes->stream_id)) {
                    mpeg_probe_ac3(sp, pes->data, pes->size);
                    sp->stream_type = MPEG_STREAM_TYPE_AUDIO;
                    sp->common.stream_id = pes->stream_id;                     
                    sp->common.codec = "audio/ac3";
                } else if(IS_LPCM(pes->stream_id)) {
                    /* no probing function. yet. */
                    sp->stream_type = MPEG_STREAM_TYPE_AUDIO;
                    sp->common.stream_id = pes->stream_id;
                    sp->common.codec = "audio/lpcm";
                }
                sp->common.start_time = pes->pts; // XXX
        
                sp->common.index = MPEG->n_streams++;
                sp++;
                probed++;
            }
        } else {
            pkt_unk++;
        }    
        mpeg_pkt_del(pes);
    }
    mpeg_log(MPEG_LOG_INFO, "MPEG-PS: found %i packets of unknown streams\n", 
                             pkt_unk);

    /* it's not _right_ if we do not found nothing on file */
    if(probed == 0) {
        MPEG->errcode = MPEG_ERROR_PROBE_FAILED;
        mpeg_log(MPEG_LOG_WARN, "MPEG-PS: unable to find any known stream "
                                "on this file\n");
        ret = MPEG_ERR;
    }
    return ret;
}

static uint64_t mpeg_get_timestamp(mpeg_t *MPEG)
{
     mpeg_pkt_t *pes = NULL;
     uint64_t ts = -1;
     int pkt_cnt = 0;

     do {
        pes = mpeg_pes_read_packet(MPEG, FALSE);
        if(pes == NULL) {
            break;
        }
        if(pes->flags & MPEG_PKT_FLAG_PTS) {
            ts = pes->pts;
        }
        mpeg_pkt_del(pes);
    } while(ts == -1 && pkt_cnt++ < MPEG_PKTS_MAX_PROBE * 2);
    return ts;
}

#define MEGABYTE                        (1UL << 20)

static mpeg_res_t mpeg_ps_compute_duration(mpeg_t *MPEG)
{
    mpeg_ps_data_t *ps = NULL;
    mpeg_res_t ret = MPEG_ERROR_NONE;
    
    assert(MPEG != NULL);

    ps = MPEG->priv;
    
    if(!MPEG->MFILE->streamed && 
            MPEG->MFILE->get_size(MPEG->MFILE) > MEGABYTE) {
        uint64_t stime, etime = -1, tt;
        uint64_t spos, epos;

#ifdef DEBUG    
        mpeg_log(MPEG_LOG_INFO, "MPEG-PS: determining stream length\n");
#endif
    
        ret = MPEG->MFILE->seek(MPEG->MFILE, 0, SEEK_SET);
        if(ret != MPEG_OK) {
            MPEG->errcode = MPEG_ERROR_SEEK;
            mpeg_log(MPEG_LOG_ERR, "MPEG-PS: can't seek to "
                         "the begin of file\n");
            return MPEG_ERR;
        }   
    
        stime = mpeg_get_timestamp(MPEG);
        spos = MPEG->MFILE->tell(MPEG->MFILE);

#ifdef DEBUG    
        mpeg_log(MPEG_LOG_INFO, "MPEG-PS: start timestamp %lli us @%lli\n",
                          stime / 27, spos);
#endif  
        ret = MPEG->MFILE->seek(MPEG->MFILE, -MEGABYTE, SEEK_END);
        if(ret != MPEG_OK) {
            MPEG->errcode = MPEG_ERROR_SEEK;
            mpeg_log(MPEG_LOG_ERR, "MPEG-PS: can't seek to "
                         "the end of file\n");
            return MPEG_ERR;
        }   

        while((tt = mpeg_get_timestamp(MPEG)) != -1) {
            etime = tt;
        }
        epos = MPEG->MFILE->tell(MPEG->MFILE);

#ifdef DEBUG    
        mpeg_log(MPEG_LOG_INFO, "MPEG-PS: last timestamp %lli us @%lli\n",
                          etime / 27, epos);
#endif
        if(stime != -1 && etime != -1){
            uint64_t dt = etime - stime;
            uint64_t dp = epos - spos;
            ps->rate = dp * 90 / dt;
            ps->duration = 300LL * dt;
        } else {
            MPEG->errcode = MPEG_ERROR_BAD_FORMAT;
            return MPEG_ERR;
        }
    }
   
    return MPEG_OK;
}

static mpeg_res_t mpeg_ps_probe(mpeg_t *MPEG)
{
    mpeg_res_t ret = MPEG_OK;
    mpeg_ps_data_t *ps = NULL;

    assert(MPEG != NULL);
    assert(MPEG->priv != NULL);

    ps = MPEG->priv;
    
    if(ps->ns >= 1) {
#ifdef  PARSE_STREAM_MAP
        ret = mpeg_ps_parse_stream_map(MPEG);
#else      
        ret = mpeg_ps_probe_streams(MPEG, ps->ns);
#endif  
        /* 
         * should be the last function called 
         * here duing to the seek policy 
         */
        mpeg_ps_compute_duration(MPEG);
    } else {
       MPEG->errcode = MPEG_ERROR_PROBE_FAILED; 
    }
    return ret;
}

static int mpeg_stream_offset(int stream_id)
{
    int off = stream_id & 0xff;
    if(IS_MPVIDEO(stream_id)) {
        off = stream_id - MPEG_STREAM_ID_BASE_VIDEO;
    } else if(IS_MPAUDIO(stream_id)) {
        off = stream_id - MPEG_STREAM_ID_BASE_AUDIO;
    } else if(IS_AC3(stream_id)) {
        off = stream_id - MPEG_STREAM_ID_BASE_AC3;
    } else if(IS_LPCM(stream_id)) {
        off = stream_id - MPEG_STREAM_ID_BASE_LPCM;
    }
    return off;
}

typedef int (*s_checker_t)(int id);

static int is_mpvideo(int id)
{
    return IS_MPVIDEO(id);
}

static int is_mpaudio(int id)
{
    return IS_MPAUDIO(id);
}

static int is_ac3(int id)
{
    return IS_AC3(id);
}

static int is_dts(int id)
{
    return IS_DTS(id);
}

static int is_lpcm(int id)
{
    return IS_LPCM(id);
}

static int is_spu(int id)
{
    return IS_SPU(id);
}

static int mpeg_ps_build_stream_map(mpeg_t *MPEG, int use_tc_order)
{
    int i = 0;
    assert(MPEG != NULL);

    MPEG->smap = mpeg_mallocz(sizeof(int) * MPEG->n_streams);
    if(MPEG->smap == NULL) {
        MPEG->errcode = MPEG_ERROR_NO_MEM;
        mpeg_log(MPEG_LOG_ERR, "can't allocate stream map\n");
        return MPEG_ERR;
    }

    // FIXME: factorize me up
    if(use_tc_order == TRUE) {
        s_checker_t checkers[] = {
            is_mpvideo, is_mpaudio, is_ac3, is_lpcm, is_dts, is_spu, NULL 
        };
        int k = 0, j = 0, sid = 0, off = 0, count = 0;
        
        for(k = 0; checkers[k] != NULL; k++) {
            if(count > 0) {
                j += count;
            }
            count = 0;
            for(i = 0; i < MPEG->n_streams; i++) {
                sid = MPEG->streams[i].common.stream_id;
                off = mpeg_stream_offset(sid);
                if(checkers[k](sid) != 0) {
                    MPEG->smap[j + off] = i;
                    count++;
                }
            }
        }
    } else {
        for(i = 0; i < MPEG->n_streams; i++) {
            MPEG->smap[i] = i;
        }
    }

    return MPEG_OK;
}

mpeg_res_t mpeg_ps_open(mpeg_t *MPEG, mpeg_file_t *MFILE, uint32_t flags)
{
    mpeg_ps_data_t *ps = NULL;
    mpeg_res_t ret = MPEG_OK;

    ps = mpeg_mallocz(sizeof(mpeg_ps_data_t));
    if(ps == NULL) {
        MPEG->errcode = MPEG_ERROR_NO_MEM;
        mpeg_log(MPEG_LOG_ERR,
            "can't allocate PS private data\n");
        return MPEG_ERR;
    }
    
    ps->ns = MPEG_STREAMS_NUM_BASE;
    
    MPEG->type = MPEG_TYPE_PS;
    MPEG->MFILE = MFILE;
    MPEG->priv = ps;

    MPEG->read_packet = mpeg_ps_read_packet;
    MPEG->probe = mpeg_ps_probe;
    MPEG->close = mpeg_ps_close;

    MPEG->streams = mpeg_mallocz(ps->ns * sizeof(*MPEG->streams));
    if(MPEG->streams == NULL) {
        MPEG->errcode = MPEG_ERROR_NO_MEM;
        mpeg_log(MPEG_LOG_ERR,
            "can't allocate stream descriptors\n");
        return MPEG_ERR;
    }   
                    
    ps->imap = mpeg_mallocf(0x100 * sizeof(*ps->imap), 0xff);
    if(ps->imap == NULL) {
        MPEG->errcode = MPEG_ERROR_NO_MEM;
        mpeg_log(MPEG_LOG_ERR,
            "can't allocate imap\n");
        return MPEG_ERR;
    }   
                    
    ps->map = mpeg_mallocf(0x100 * sizeof(*ps->map), 0xff);
    if(ps->map == NULL) {
        MPEG->errcode = MPEG_ERROR_NO_MEM;
        mpeg_log(MPEG_LOG_ERR,
            "can't allocate map\n");
        return MPEG_ERR;
    }   

    if(flags & MPEG_FLAG_PROBE) {
        ret = mpeg_probe(MPEG);
    }

    mpeg_ps_build_stream_map(MPEG, (flags & MPEG_FLAG_TCORDER)); 

    /* adjust error level */
    return ret;
}



