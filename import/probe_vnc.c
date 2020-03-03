/*
 *  probe_vnc.c
 *
 *  Copyright (C) Brian de Alwis 2004
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


/* Some VNC constants */
#define VNCREC_MAGIC_STRING	"vncLog0.0"
#define VNCREC_MAGIC_SIZE	(9)
#define VNC_RFB_PROTOCOL_SCANF_FORMAT "RFB %03d.%03d\n"
#define VNC_RFB_PROTO_VERSION_SIZE	(12)

#define VNC33_CHALLENGESIZE	(16)

#define VNC33_rfbConnFailed	(0)
#define VNC33_rfbNoAuth		(1)
#define VNC33_rfbVncAuth	(2)

#define VNC33_rfbVncAuthOK	(0)
#define VNC33_rfbVncAuthFailed	(1)
#define VNC33_rfbVncAuthTooMany	(2)

/*
   RFB 3.X protocol is as follows (from vncrec source code):
    * server send 12-byte (SIZE_SIZE_RFB_PROTO_VERSION) header
      encoding the RFB version as an ASCII string
    * server sends 4-byte number (big-endian) to alert auth
      requirements
    * if requiring auth, server then sends 16-byte (VNC33_CHALLENGESIZE)
      packet, which is to be encrypted and sent back (same size).
      The server then sends 32-bit word result on pass-fail.  Entire
      thing aborted if not passed.
    * client sends 1-byte message
    * server then sends a display-paramters message, containing
      (in order) the width (2-byte), height (2-byte), preferred pixel
      format (1-byte), and desktop name (1-byte with length, n bytes).
 */

void probe_vnc(info_t *ipipe)
{
    unsigned char buf[100];
    unsigned char matchingBuffer[100];
    int index = 0, major, minor, authReqs;
    int width, height;

    if(tc_pread(ipipe->fd_in, buf, sizeof(buf)) != sizeof(buf)) {
	tc_log_error(__FILE__, "end of stream");
	ipipe->error=1;
	return;
    }

    /* Check VNCREC magic */
    ac_memcpy(matchingBuffer, &buf[index], VNCREC_MAGIC_SIZE);
    matchingBuffer[VNCREC_MAGIC_SIZE] = 0;
    if(strcmp(matchingBuffer, VNCREC_MAGIC_STRING)) { /* NOT EQUAL */
	tc_log_error(__FILE__, "unsupported version of vncrec (\"%s\")",
	             matchingBuffer);
	ipipe->error=1;
	return;
    }
    index += VNCREC_MAGIC_SIZE;


    /* Ensure RFB protocol is valid */
    ac_memcpy(matchingBuffer, &buf[index], VNC_RFB_PROTO_VERSION_SIZE);
    matchingBuffer[VNC_RFB_PROTO_VERSION_SIZE] = 0;
    if(sscanf(matchingBuffer, VNC_RFB_PROTOCOL_SCANF_FORMAT, &major, &minor) != 2) {
	tc_log_error(__FILE__, "unknown RFB protocol (\"%s\")",
	             matchingBuffer);
	ipipe->error=1;
	return;
    }
    if (ipipe->verbose & TC_DEBUG) {
	tc_log_msg(__FILE__, "File recorded as RFB Protocol v%d.%d",
	           major, minor);
    }
    if(major != 3) {
	tc_log_error(__FILE__, "unsupported RFB protocol (only support v3)");
	ipipe->error=1;
	return;
    }
    index += VNC_RFB_PROTO_VERSION_SIZE;

    /* Check authentication requirements */
    authReqs = (buf[index] << 24) | (buf[index+1] << 16)
		| (buf[index+2] << 8) | buf[index+3];
    index += 4;
    switch(authReqs) {
      case VNC33_rfbNoAuth:
	if (ipipe->verbose & TC_DEBUG)
	    tc_log_msg(__FILE__, "No authorization required.");
	break;

      case VNC33_rfbVncAuth: {
	int authResp =
	index += VNC33_CHALLENGESIZE;
	authResp = (buf[index] << 24) | (buf[index+1] << 16)
		    | (buf[index+2] << 8) | buf[index+3];
	/* switch(authResp) { ... } */
	index += 4;
	break;
	}

      case VNC33_rfbConnFailed:
      default:
	tc_log_error(__FILE__, "apparently connection failed?");
	ipipe->error=1;
	return;
    }

    /* Receive display parameters */
    width = (buf[index] << 8) | buf[index+1];
    height = (buf[index+2] << 8) | buf[index+3];

    ipipe->probe_info->width  = width;
    ipipe->probe_info->height = height;
    ipipe->probe_info->fps = 25.;
    ipipe->probe_info->frc = 3;
    ipipe->probe_info->codec = TC_CODEC_RGB24;
    ipipe->probe_info->magic = ipipe->magic;

}
