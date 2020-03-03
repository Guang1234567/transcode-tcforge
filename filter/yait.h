/*
 *  yait.h
 *
 *  Copyright (C) Allan Snider - February 2007
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
 */

#ifndef YAIT_H
#define YAIT_H

#include <stdio.h>
#include <math.h>

#define	YAIT_VERSION		"v0.1"

/* program defaults */
#define	Y_LOG_FN		"yait.log"  /* log file read */
#define	Y_OPS_FN		"yait.ops"  /* frame operation file written */
#define	Y_DEINT_MODE	3           /* default transcode de-interlace mode */

#define	Y_THRESH		1.1     /* even/odd ratio to detect interlace */
#define	Y_MTHRESH		1.02    /* minimum ratio allowing de-interlace */
#define	Y_WEIGHT		0.001   /* normalized delta to filter ratio noise */

#define	Y_FTHRESH		1.4     /* force de-interlace if over this ratio */
#define	Y_FWEIGHT		0.01    /* force de-interlace if over this weight */

#define	Y_OP_ODD		0x10
#define	Y_OP_EVEN		0x20
#define	Y_OP_PAT		0x30

#define	Y_OP_NOP		0x0
#define	Y_OP_SAVE		0x1
#define	Y_OP_COPY		0x2
#define	Y_OP_DROP		0x4
#define	Y_OP_DEINT		0x8

/* group flags */
#define	Y_HAS_DROP		    1
#define	Y_BANK_DROP		    2
#define	Y_WITHDRAW_DROP		3
#define	Y_BORROW_DROP		4
#define	Y_RETURN_DROP		5
#define	Y_FORCE_DEINT		6
#define	Y_FORCE_DROP		7
#define	Y_FORCE_KEEP		8

#endif /* YAIT_H */
