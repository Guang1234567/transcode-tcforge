
#ifndef _STATIC_MPEGLIB_H_
#define _STATIC_MPEGLIB_H_

#include "mpeglib/mpeglib.h"
void dummy_mpeglib(void);
void dummy_mpeglib(void) {
	mpeg_file_t *infile = NULL;
	mpeg_pkt_t* pkt = NULL;
	infile = mpeg_file_open("", "");
	pkt = mpeg_pkt_new(0);
	mpeg_pkt_del(pkt);
	mpeg_file_close(infile);
}

#endif // _STATIC_MPEGLIB_H_
