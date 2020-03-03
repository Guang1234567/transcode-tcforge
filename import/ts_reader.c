/*
 *  ts_reader.c
 *
 *  Copyright (C) Thomas Oestreich - December 2002
 *
 *  based on extract_mpeg2.c
 *  Copyright (C) 2000-2002 Michel Lespinasse <walken@zoy.org>
 *  Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
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

#include <sys/mman.h>

#ifdef HAVE_IO_H
#include <io.h>
#endif

#define BUFFER_SIZE 188
#define TS_PACK BUFFER_SIZE

static uint8_t buffer[BUFFER_SIZE];
static int demux_track = 0xe0;
static int demux_pid = 0;
//static int demux_pva = 0;

static int fd_in;



#define TRANS_ERROR    0x80
#define PAY_START      0x40
#define TRANS_PRIO     0x20
#define PID_MASK_HI    0x1F
//flags
#define TRANS_SCRMBL1  0x80
#define TRANS_SCRMBL2  0x40
#define ADAPT_FIELD    0x20
#define PAYLOAD        0x10
#define COUNT_MASK     0x0F

// adaptation flags
#define DISCON_IND     0x80
#define RAND_ACC_IND   0x40
#define ES_PRI_IND     0x20
#define PCR_FLAG       0x10
#define OPCR_FLAG      0x08
#define SPLICE_FLAG    0x04
#define TRANS_PRIV     0x02
#define ADAP_EXT_FLAG  0x01

// adaptation extension flags
#define LTW_FLAG       0x80
#define PIECE_RATE     0x40
#define SEAM_SPLICE    0x20

typedef struct  ts_packet_{
    uint8_t pid[2];
    uint8_t flags;
    uint8_t count;
    uint8_t data[184];
    uint8_t adapt_length;
    uint8_t adapt_flags;
    uint8_t pcr[6];
    uint8_t opcr[6];
    uint8_t splice_count;
    uint8_t priv_dat_len;
    uint8_t *priv_dat;
    uint8_t adapt_ext_len;
    uint8_t adapt_eflags;
    uint8_t ltw[2];
    uint8_t piece_rate[3];
    uint8_t dts[5];
    int rest;
    uint8_t stuffing;
} ts_packet;

static void init_ts(ts_packet *p)
{
	p->pid[0] = 0;
	p->pid[1] = 0;
	p->flags = 0;
	p->count = 0;
	p->adapt_length = 0;
	p->adapt_flags = 0;
	p->splice_count = 0;
	p->priv_dat_len = 0;
	p->priv_dat = NULL;
	p->adapt_ext_len = 0;
	p->adapt_eflags = 0;
	p->rest = 0;
	p->stuffing = 0;
}


static uint16_t get_pid(uint8_t *pid)
{
	uint16_t pp = 0;

	pp = (pid[0] & PID_MASK_HI)<<8;
	pp |= pid[1];

	return pp;
}

// tibit
void probe_ts(info_t *ipipe)
{
    int i;
    int found=0, doit=1;
    uint8_t sync = 0;
    ts_packet p;
    ssize_t size=0;

#define MAX_PID 20
    int pid[MAX_PID];
    int npid=0;

    // look for a syncword
    while (!found && doit) {
	i = tc_pread(ipipe->fd_in, (uint8_t *)&sync, 1);
	if (sync == 0x47) found = 1;
	if (i == 0) doit = 0;
    }

    for (i=0 ; i<MAX_PID; i++)
	pid[i] = -1;

    while (size < ipipe->factor*1024*1024) {

	if((i=tc_pread(ipipe->fd_in, buffer, TS_PACK-1)) != TS_PACK-1) {
	    tc_log_info(__FILE__, "end of stream");
	    return;
	}
	size += i;

	init_ts (&p);
	ac_memcpy (&p, buffer, 3);

	found = 0;
	for (i=0;i<npid;i++){
	    if ( get_pid (buffer) == pid[i] )
		found = 1;
	}

	if (!found) {
	    tc_log_info(__FILE__, "Found pid 0x%x", get_pid (buffer));
	    pid[npid] = get_pid (buffer);
	    npid++;
	    if (npid >= MAX_PID) {
		tc_log_warn(__FILE__, "Too many pids");
		return;
	    }
	}


	found = 0; doit = 1;
	// read away syncword

	while (!found && doit) {
	    i = tc_pread(ipipe->fd_in, (uint8_t *)&sync, 1);
	    if (sync == 0x47) found = 1;
	    if (i == 0) doit = 0;
	    size += i;
	}
	if(!doit) {
	    tc_log_info(__FILE__, "end of stream");
	    return;
	}
    }

    if (!npid)
	tc_log_info(__FILE__, "No pids found");
}

#define DEMUX_PAYLOAD_START 1

static int demux (uint8_t * buf, uint8_t * end, int flags)
{
    static int mpeg1_skip_table[16] = {
	0, 0, 4, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    /*
     * the demuxer keeps some state between calls:
     * if "state" = DEMUX_HEADER, then "head_buf" contains the first
     *     "bytes" bytes from some header.
     * if "state" == DEMUX_DATA, then we need to copy "bytes" bytes
     *     of ES data before the next header.
     * if "state" == DEMUX_SKIP, then we need to skip "bytes" bytes
     *     of data before the next header.
     *
     * NEEDBYTES makes sure we have the requested number of bytes for a
     * header. If we dont, it copies what we have into head_buf and returns,
     * so that when we come back with more data we finish decoding this header.
     *
     * DONEBYTES updates "buf" to point after the header we just parsed.
     */

#define DEMUX_HEADER 0
#define DEMUX_DATA 1
#define DEMUX_SKIP 2
    static int state = DEMUX_SKIP;
    static int state_bytes = 0;
    static uint8_t head_buf[264];

    uint8_t * header;
    int bytes;
    int len;

#define NEEDBYTES(x)						\
    do {							\
	int missing;						\
								\
	missing = (x) - bytes;					\
	if (missing > 0) {					\
	    if (header == head_buf) {				\
		if (missing <= end - buf) {			\
		    ac_memcpy (header + bytes, buf, missing);	\
		    buf += missing;				\
		    bytes = (x);				\
		} else {					\
		    ac_memcpy (header + bytes, buf, end - buf);	\
		    state_bytes = bytes + end - buf;		\
		    return 0;					\
		}						\
	    } else {						\
		ac_memcpy (head_buf, header, bytes);		\
		state = DEMUX_HEADER;				\
		state_bytes = bytes;				\
		return 0;					\
	    }							\
	}							\
    } while (0)

#define DONEBYTES(x)		\
    do {			\
	if (header != head_buf)	\
	    buf = header + (x);	\
    } while (0)

    if (flags & DEMUX_PAYLOAD_START)
	goto payload_start;
    switch (state) {
    case DEMUX_HEADER:
	if (state_bytes > 0) {
	    header = head_buf;
	    bytes = state_bytes;
	    goto continue_header;
	}
	break;
    case DEMUX_DATA:
	if (demux_pid || (state_bytes > end - buf)) {
	    if (fwrite (buf, end - buf, 1, stdout) != 1) {
		tc_log_perror(__FILE__, "Write error");
	    }
	    state_bytes -= end - buf;
	    return 0;
	}
	if (fwrite (buf, state_bytes, 1, stdout) != 1) {
	    tc_log_perror(__FILE__, "Write error");
	}
	buf += state_bytes;
	break;
    case DEMUX_SKIP:
	if (demux_pid || (state_bytes > end - buf)) {
	    state_bytes -= end - buf;
	    return 0;
	}
	buf += state_bytes;
	break;
    }

    while (1) {
	if (demux_pid) {
	    state = DEMUX_SKIP;
	    return 0;
	}
    payload_start:
	header = buf;
	bytes = end - buf;
    continue_header:
	NEEDBYTES (4);
	if (header[0] || header[1] || (header[2] != 1)) {
	    if (demux_pid) {
		state = DEMUX_SKIP;
		return 0;
	    } else if (header != head_buf) {
		buf++;
		goto payload_start;
	    } else {
		header[0] = header[1];
		header[1] = header[2];
		header[2] = header[3];
		bytes = 3;
		goto continue_header;
	    }
	}
	if (demux_pid) {
	    if ((header[3] >= 0xe0) && (header[3] <= 0xef))
		goto pes;
	    tc_log_error(__FILE__, "bad stream id %x", header[3]);
	    exit (1);
	}
	switch (header[3]) {
	case 0xb9:	/* program end code */
	    /* DONEBYTES (4); */
	    /* break;         */
	    return 1;
	case 0xba:	/* pack header */
	    NEEDBYTES (12);
	    if ((header[4] & 0xc0) == 0x40) {	/* mpeg2 */
		NEEDBYTES (14);
		len = 14 + (header[13] & 7);
		NEEDBYTES (len);
		DONEBYTES (len);
		/* header points to the mpeg2 pack header */
	    } else if ((header[4] & 0xf0) == 0x20) {	/* mpeg1 */
		DONEBYTES (12);
		/* header points to the mpeg1 pack header */
	    } else {
		tc_log_error(__FILE__, "weird pack header");
		exit (1);
	    }
	    break;
	default:
	    if (header[3] == demux_track) {
	    pes:
		NEEDBYTES (7);
		if ((header[6] & 0xc0) == 0x80) {	/* mpeg2 */
		    NEEDBYTES (9);
		    len = 9 + header[8];
		    NEEDBYTES (len);
		    /* header points to the mpeg2 pes header */
		} else {	/* mpeg1 */
		    len = 7;
		    while ((header-1)[len] == 0xff) {
			len++;
			NEEDBYTES (len);
			if (len > 23) {
			    tc_log_warn(__FILE__, "too much stuffing");
			    break;
			}
		    }
		    if (((header-1)[len] & 0xc0) == 0x40) {
			len += 2;
			NEEDBYTES (len);
		    }
		    len += mpeg1_skip_table[(header - 1)[len] >> 4];
		    NEEDBYTES (len);
		    /* header points to the mpeg1 pes header */
		}
		DONEBYTES (len);
		bytes = 6 + (header[4] << 8) + header[5] - len;
		if (demux_pid || (bytes > end - buf)) {
		    if (fwrite (buf, end - buf, 1, stdout) != 1) {
			tc_log_perror(__FILE__, "Write error");
		    }
		    state = DEMUX_DATA;
		    state_bytes = bytes - (end - buf);
		    return 0;
		} else if (bytes <= 0) {
		    continue;
		}
		if (fwrite (buf, bytes, 1, stdout) != 1) {
		    tc_log_perror(__FILE__, "Write error");
		}
		buf += bytes;
	    } else if (header[3] < 0xb9) {
		tc_log_info(__FILE__,
			    "looks like a video stream, not system stream");
		DONEBYTES (4);
	    } else {
		NEEDBYTES (6);
		DONEBYTES (6);
		bytes = (header[4] << 8) + header[5];
		if (bytes > end - buf) {
		    state = DEMUX_SKIP;
		    state_bytes = bytes - (end - buf);
		    return 0;
		}
		buf += bytes;
	    }
	}
    }
}

#if 0
static void ps_loop (void)
{
    uint8_t * end;

    do {
	end = buffer + fread (buffer, 1, BUFFER_SIZE, in_file);
	if (demux (buffer, end, 0))
	    break;	/* hit program_end_code */
    } while (end == buffer + BUFFER_SIZE);
}

static int pva_demux (uint8_t * buf, uint8_t * end)
{
    static int state = DEMUX_SKIP;
    static int state_bytes = 0;
    static uint8_t head_buf[12];

    uint8_t * header;
    int bytes;
    int len;

    switch (state) {
    case DEMUX_HEADER:
        if (state_bytes > 0) {
            header = head_buf;
            bytes = state_bytes;
            goto continue_header;
        }
        break;
    case DEMUX_DATA:
        if (state_bytes > end - buf) {
            fwrite (buf, end - buf, 1, stdout);
            state_bytes -= end - buf;
            return 0;
        }
        fwrite (buf, state_bytes, 1, stdout);
        buf += state_bytes;
        break;
    case DEMUX_SKIP:
        if (state_bytes > end - buf) {
            state_bytes -= end - buf;
            return 0;
        }
        buf += state_bytes;
        break;
    }

    while (1) {
    payload_start:
	header = buf;
	bytes = end - buf;
    continue_header:
	NEEDBYTES (2);
	if (header[0] != 0x41 || header[1] != 0x56) {
	    if (header != head_buf) {
		buf++;
		goto payload_start;
	    } else {
		header[0] = header[1];
		bytes = 1;
		goto continue_header;
	    }
	}
	NEEDBYTES (8);
	if (header[2] != 1) {
	    DONEBYTES (8);
	    bytes = (header[6] << 8) + header[7];
	    if (bytes > end - buf) {
		state = DEMUX_SKIP;
		state_bytes = bytes - (end - buf);
		return 0;
	    }
	    buf += bytes;
	} else {
	    len = 8;
	    if (header[5] & 0x10) {
		len = 12;
		NEEDBYTES (len);
	    }
	    DONEBYTES (len);
	    bytes = (header[6] << 8) + header[7] + 8 - len;
	    if (bytes > end - buf) {
		fwrite (buf, end - buf, 1, stdout);
		state = DEMUX_DATA;
		state_bytes = bytes - (end - buf);
		return 0;
	    } else if (bytes > 0) {
		fwrite (buf, bytes, 1, stdout);
		buf += bytes;
	    }
	}
    }
}
#endif

#if 0
static void pva_loop (void)
{
    uint8_t * end;

    do {
	end = buffer + fread (buffer, 1, BUFFER_SIZE, in_file);
	pva_demux (buffer, end);
    } while (end == buffer + BUFFER_SIZE);
}
#endif

static void ts_loop (void)
{
#define PACKETS (BUFFER_SIZE / 188)
    uint8_t * buf;
    uint8_t * data;
    uint8_t * end;
    int packets;
    int i;
    int pid;

    do {

      if((i=tc_pread(fd_in, buffer, TS_PACK)) != TS_PACK) {
	tc_log_info(__FILE__, "end of stream");
	return;
      }


	packets=1;

	for (i = 0; i < packets; i++) {
	    buf = buffer + i * 188;
	    end = buf + 188;
	    if (buf[0] != 0x47) {
		tc_log_error(__FILE__, "bad sync byte");
		exit (1);
	    }
	    pid = ((buf[1] << 8) + buf[2]) & 0x1fff;
	    if (pid != demux_pid) {
	      //tc_log_msg(__FILE__, "0x%x", pid);
	      continue;
	    }
	    data = buf + 4;
	    if (buf[3] & 0x20) {	/* buf contains an adaptation field */
		data = buf + 5 + buf[4];
		if (data > end)
		    continue;
	    }
	    if (buf[3] & 0x10)
		demux (data, end, (buf[1] & 0x40) ? DEMUX_PAYLOAD_START : 0);
	}
    } while (packets == PACKETS);
}

int ts_read(int _fd_in, int fd_out, int _demux_pid)
{

#ifdef HAVE_IO_H
  setmode (fileno (stdout), O_BINARY);
#endif

  demux_pid = _demux_pid;
  fd_in = _fd_in;

  ts_loop();
  return 0;

}
