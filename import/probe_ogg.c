/*
 *  probe_ogg.c
 *
 *  Copyright (C) Tilmann Bitterberg, July 2002
 *       Based heavily on code by Moritz Bunkus for ogminfo from
 *            http://www.bunkus.org/videotools/ogmtools/index.html
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

#include "tccore/tcinfo.h"
#include "src/transcode.h"
#include "ioaux.h"
#include "tc.h"
#include "libtc/libtc.h"
#include "libtc/ratiocodes.h"

#include <sys/mman.h>

#if (HAVE_OGG && HAVE_VORBIS)

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#ifdef HAVE_THEORA
#include <theora/theora.h>
#endif

#include "ogmstreams.h"

#define MAX_AUDIO_TRACKS 255
#define MAX_VIDEO_TRACKS 255
#define BLOCK_SIZE 4096

//#define  OGM_DEBUG

struct demux_t {
    int              serial;
    int              fd;
    int              vorbis;
    ogg_stream_state state;
};

enum { none, Vorbis, Theora, DirectShow, StreamHeader };

static int ogm_packet_type (ogg_packet pack)
{
    if ((pack.bytes >= 7) && ! strncmp(&pack.packet[1], "vorbis", 6))
	return Vorbis;
    else if ((pack.bytes >= 7) && ! strncmp(&pack.packet[1], "theora", 6))
	return Theora;
    else if ((pack.bytes >= 142) &&
	    !strncmp(&pack.packet[1],"Direct Show Samples embedded in Ogg", 35) )
	return DirectShow;
    else if (((*pack.packet & OGM_PACKET_TYPE_BITS ) == OGM_PACKET_TYPE_HEADER) &&
	    (pack.bytes >= (int)sizeof(ogm_stream_header) + 1))
	return StreamHeader;

    return none;
}

void probe_ogg(info_t *ipipe)
{
    ogg_sync_state    sync;
    ogg_page          page;
    ogg_packet        pack;
    char             *buf;
    int               nread, np, sno, nvtracks = 0, natracks = 0, i, idx;
    //int               endofstream = 0, k, n;
    struct demux_t    streams[MAX_AUDIO_TRACKS + MAX_VIDEO_TRACKS];
    int               fdin = -1;
    char vid_codec[5];
    ogm_stream_header *sth;

    fdin = ipipe->fd_in;

    if (fdin == -1) {
	tc_log_error(__FILE__, "Could not open file.");
	goto ogg_out;
    }

    ipipe->probe_info->magic=TC_MAGIC_OGG;

    memset(streams, 0, sizeof(streams));
    for (i = 0; i < (MAX_AUDIO_TRACKS + MAX_VIDEO_TRACKS); i++)
	streams[i].serial = -1;

    ogg_sync_init(&sync);

    while (1) {
	np = ogg_sync_pageseek(&sync, &page);
	if (np < 0) {
	    tc_log_error(__FILE__, "ogg_sync_pageseek failed");
	    goto ogg_out;
	}
	if (np == 0) {
	    buf = ogg_sync_buffer(&sync, BLOCK_SIZE);
	    if (!buf) {
		tc_log_error(__FILE__, "ogg_sync_buffer failed");
		goto ogg_out;
	    }

	    if ((nread = read(fdin, buf, BLOCK_SIZE)) <= 0) {
	    }
	    ogg_sync_wrote(&sync, nread);
	    continue;
	}

	if (!ogg_page_bos(&page)) {
	    break;
	} else {
	    ogg_stream_state sstate;
	    vorbis_info *inf = tc_malloc (sizeof(vorbis_info));
	    vorbis_comment *com = tc_malloc (sizeof(vorbis_comment));

	    if (!inf || !com) {
		tc_log_error(__FILE__, "Out of Memory at %d", __LINE__);
		goto ogg_out;
	    }
	    sno = ogg_page_serialno(&page);
	    if (ogg_stream_init(&sstate, sno)) {
		tc_log_error(__FILE__, "ogg_stream_init failed");
		goto ogg_out;
	    }
	    ogg_stream_pagein(&sstate, &page);
	    ogg_stream_packetout(&sstate, &pack);

	    switch (ogm_packet_type(pack))
	    {
		case Vorbis:
		    vorbis_info_init(inf);
		    vorbis_comment_init(com);

		    if(vorbis_synthesis_headerin(inf, com, &pack) < 0) {
			tc_log_warn(__FILE__, "Could not decode vorbis header "
				    "packet - invalid vorbis stream ()");
		    } else {
#ifdef OGM_DEBUG
			tc_log_msg(__FILE__, "(a%d/%d) Vorbis audio; "
				"rate: %ldHz, channels: %d, bitrate %3.2f kb/s",
				natracks + 1, natracks + nvtracks + 1, inf->rate,
				inf->channels, (double)inf->bitrate_nominal/1000.0);
#endif

			ipipe->probe_info->track[natracks].samplerate = inf->rate;
			ipipe->probe_info->track[natracks].chan = inf->channels;
			ipipe->probe_info->track[natracks].bits = 0; /* XXX --tibit*/
			ipipe->probe_info->track[natracks].format = TC_CODEC_VORBIS;
			ipipe->probe_info->track[natracks].bitrate = (double)inf->bitrate_nominal/1000.0;

			ipipe->probe_info->track[natracks].tid=natracks;
			if(ipipe->probe_info->track[natracks].chan>0) ++ipipe->probe_info->num_tracks;

			streams[natracks].serial = sno;
			streams[natracks].vorbis = 1;
			ac_memcpy(&streams[natracks].state, &sstate, sizeof(sstate));
			natracks++;
		    }
		    break;
#ifdef HAVE_THEORA
		case Theora:
		{
		    theora_info ti;
		    theora_comment tc;

		    theora_decode_header(&ti, &tc, &pack);

		    ipipe->probe_info->width  =  ti.width;
		    ipipe->probe_info->height =  ti.height;
		    ipipe->probe_info->fps    =  (double)ti.fps_numerator/ti.fps_denominator;
		    tc_frc_code_from_ratio(&(ipipe->probe_info->frc),
		    	    		   ti.fps_numerator, ti.fps_denominator);

		    ipipe->probe_info->codec=TC_CODEC_THEORA;

		    idx = natracks + MAX_AUDIO_TRACKS;

		    streams[idx].serial = sno;
		    ac_memcpy(&streams[idx].state, &sstate, sizeof(sstate));
		    nvtracks++;
		    break;
	        }
#endif
		case DirectShow:
		    if ((*(int32_t*)(pack.packet+96) == 0x05589f80) &&
			    (pack.bytes >= 184)) {
			tc_log_warn(__FILE__, "(v%d/%d) Found old video "
				    "header. Not supported.", nvtracks + 1,
				    natracks + nvtracks + 1);
		    } else if (*(int32_t*)pack.packet+96 == 0x05589F81) {
			tc_log_warn(__FILE__, "(a%d/%d) Found old audio "
				    "header. Not supported.", natracks + 1,
				    natracks + nvtracks + 1);
		    }
		    break;
		case StreamHeader:
		    sth = (ogm_stream_header *)(pack.packet + 1);

		    if (!strncmp(sth->streamtype, "video", 5)) {
#ifdef OGM_DEBUG
			unsigned long codec;
			codec = (sth->subtype[0] << 24) +
			    (sth->subtype[1] << 16) + (sth->subtype[2] << 8) + sth->subtype[3];
			tc_log_msg(__FILE__, "(v%d/%d) video; fps: %.3f width height: %dx%d "
				"codec: %p (%c%c%c%c)", nvtracks + 1,
				natracks + nvtracks + 1,
				(double)10000000 / (double)sth->time_unit,
				sth->sh.video.width, sth->sh.video.height, (void *)codec,
				sth->subtype[0], sth->subtype[1], sth->subtype[2],
				sth->subtype[3]);
#endif
			vid_codec[0] = sth->subtype[0];
			vid_codec[1] = sth->subtype[1];
			vid_codec[2] = sth->subtype[2];
			vid_codec[3] = sth->subtype[3];
			vid_codec[4] = '\0';

			//ipipe->probe_info->frames = AVI_video_frames(avifile);

			ipipe->probe_info->width  =  sth->sh.video.width;
			ipipe->probe_info->height =  sth->sh.video.height;
			ipipe->probe_info->fps    =  (double)10000000 / (double)sth->time_unit;
			tc_frc_code_from_value(&(ipipe->probe_info->frc),
  						  ipipe->probe_info->fps);

			ipipe->probe_info->codec=TC_CODEC_UNKNOWN; // gets rewritten

			if(strlen(vid_codec)==0) {
			    ipipe->probe_info->codec=TC_CODEC_RGB24;
			} else {

			    if(strcasecmp(vid_codec,"dvsd")==0)
				ipipe->probe_info->codec=TC_CODEC_DV;

			    if(strcasecmp(vid_codec,"DIV3")==0)
				ipipe->probe_info->codec=TC_CODEC_DIVX3;

			    if(strcasecmp(vid_codec,"DIVX")==0)
				ipipe->probe_info->codec=TC_CODEC_DIVX4;

			    if(strcasecmp(vid_codec,"DX50")==0)
				ipipe->probe_info->codec=TC_CODEC_DIVX5;

			    if(strcasecmp(vid_codec,"XVID")==0)
				ipipe->probe_info->codec=TC_CODEC_XVID;

			    if(strcasecmp(vid_codec,"MJPG")==0)
				ipipe->probe_info->codec=TC_CODEC_MJPEG;
			}

			idx = natracks + MAX_AUDIO_TRACKS;

			streams[idx].serial = sno;
			ac_memcpy(&streams[idx].state, &sstate, sizeof(sstate));
			nvtracks++;
		    } else if (!strncmp(sth->streamtype, "audio", 5)) {
			int codec;
			char buf[5];
			ac_memcpy(buf, sth->subtype, 4);
			buf[4] = 0;
			codec = strtoul(buf, NULL, 16);
#ifdef OGM_DEBUG
			tc_log_msg(__FILE__, "(a%d/%d) codec: %d (0x%04x) (%s) bits per "
				   "sample: %d channels: %hd  samples per second: %ld "
				   "avgbytespersec: %hd blockalign: %d",
				   natracks + 1, natracks + nvtracks + 1,
				   codec, codec,
				   codec == 0x1 ? "PCM" : codec == 55 ? "MP3" :
				   codec == 0x55 ? "MP3" :
				   codec == 0x2000 ? "AC3" : "unknown",
				   sth->bits_per_sample, sth->sh.audio.channels,
				   (long)sth->samples_per_unit,
				   sth->sh.audio.avgbytespersec,
				   sth->sh.audio.blockalign);
#endif
			idx = natracks;

			ipipe->probe_info->track[natracks].samplerate = sth->samples_per_unit;
			ipipe->probe_info->track[natracks].chan = sth->sh.audio.channels;
			ipipe->probe_info->track[natracks].bits =
			    (sth->bits_per_sample<4)?sth->bits_per_sample*8:sth->bits_per_sample;
			ipipe->probe_info->track[natracks].format = codec;
			ipipe->probe_info->track[natracks].bitrate = 0;

			ipipe->probe_info->track[natracks].tid=natracks;


			if(ipipe->probe_info->track[natracks].chan>0) ++ipipe->probe_info->num_tracks;

			streams[idx].serial = sno;
			ac_memcpy(&streams[idx].state, &sstate, sizeof(sstate));
			natracks++;
		    } else {
			tc_log_warn(__FILE__, "(%d) found new header of unknown/"
				"unsupported type\n", nvtracks + natracks + 1);
		    }
		    break;
		case none:
		    tc_log_warn(__FILE__, "OGG stream %d is of an unknown type "
			"(bad header?)", nvtracks + natracks + 1);
		    break;
	    } /* switch type */
	    free(inf);
	    free(com);
	    ogg_stream_clear(&sstate);
	} /* beginning of page */
    } /* while (1) */
ogg_out:
    //close(fdin);
    return;
}

#else   // (HAVE_OGG && HAVE_VORBIS)

void probe_ogg(info_t *ipipe)
{
    tc_log_error(__FILE__, "No support for Ogg/Vorbis compiled in");
    ipipe->probe_info->codec=TC_CODEC_UNKNOWN;
    ipipe->probe_info->magic=TC_MAGIC_UNKNOWN;
}

#endif
