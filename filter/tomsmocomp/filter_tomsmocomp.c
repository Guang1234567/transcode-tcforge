/*
 *  filter_tomsmocomp.c
 *
 *  Filter access code (c) by Matthias Hopf - July 2004
 *  Base code taken from DScaler's tomsmocomp filter (c) 2002 Tom Barry,
 *  ported by Dirk Ziegelmeier for kdetv.
 *  Code base kdetv-cvs20040727.
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

#define MOD_NAME    "filter_tomsmocomp.so"
#define MOD_VERSION "v0.1 (2004-07-31)"
#define MOD_CAP     "Tom's MoComp deinterlacing filter"
#define MOD_AUTHOR  "Tom Barry et al."

#define DS_HISTORY_SIZE 4		/* Need 4 fields(!) for tomsmocomp */

#include "filter_tomsmocomp.h"
#include <assert.h>

static tomsmocomp_t *tmc_global = NULL;

static void help_optstr (void) {
    tc_log_info(MOD_NAME, "(%s) help"
"* Overview:\n"
"  TomsMoComp.dll is a filter that uses motion compensation and adaptive\n"
"  processing to deinterlace video source. It uses a variable amount of\n"
"  CPU time based upon the user specified 'searcheffort' parameter.\n"
"  The search effort may currently be set anywhere from 0 (a smarter Bob)\n"
"  to about 30 (too CPU intensive for everybody). Only certain values are\n"
"  actually implemented (currently 0,1,3,5,9,11,13,15,19,21,max) but the\n"
"  nearest value will be used.  Values above 15 have not been well tested\n"
"  and should probably be avoided for now.\n"
"\n"
"  TomsMoComp should run on all MMX machines or higher. It has also has\n"
"  some added code for 3DNOW instructions for when it is running on a\n"
"  K6-II or higher and some SSEMMX for P3 & Athlon.\n"
"\n"
"* Options:\n"
"  topfirst - assume the top field, lines 0,2,4,... should be displayed\n"
"    first.  The default is TopFirst, which seems to occur most.\n"
"    Note: DV video is usually BottomFirst!\n"
"    You may have to look at a few frames to see which looks best.\n"
"    The difference will be hardly visible, though.\n"
"    (0=BottomFirst, 1=TopFirst)  Default: 1\n"
"\n"
// "    New - setting TopFirst=-1 will automatically pick up whatever Avisynth reports.\n"
"\n"
"  searcheffort - determines how much effort (CPU time) will be used to\n"
"    find moved pixels. Currently numbers from 0 to 30 with 0 being\n"
"    practically just a smarter bob and 30 being fairly CPU intensive.\n"
"    (0 .. 30)  Default: 15\n"
"\n"
"  usestrangebob - not documented :-(((\n"
"    (0 / 1)  Default: 0\n"
"\n"
"  cpuflags - Manually set CPU capabilities (expert only) (hex)\n"
"    (0x08 MMX  0x20 3DNOW  0x80 SSE)  Default: autodetect\n"
"\n"
"* Known issues and limitations:\n"
"  1) Assumes YUV (YUY2 or YV12) Frame Based input.\n"
"  2) Currently still requires the pixel width to be a multiple of 4.\n"
"  3) TomsMoComp is for pure video source material, not for IVTC.\n"
		, MOD_CAP);
}

static void do_deinterlace (tomsmocomp_t *tmc) {
    int i;
    TPicture pictHist[DS_HISTORY_SIZE], *pictHistPts[DS_HISTORY_SIZE];

    /* Set up dscaler interface */
    for (i = 0; i < DS_HISTORY_SIZE; i++)
	pictHistPts[i] = &pictHist[i];
    tmc->DSinfo.PictureHistory = pictHistPts;
    assert (DS_HISTORY_SIZE >= 4);

    if (tmc->TopFirst) {
	/* !@#$ Do we start counting at 0 or 1?!? */
	tmc->DSinfo.PictureHistory[0]->Flags = PICTURE_INTERLACED_ODD;
	tmc->DSinfo.PictureHistory[0]->pData = tmc->frameIn + tmc->rowsize;
	tmc->DSinfo.PictureHistory[1]->Flags = PICTURE_INTERLACED_EVEN;
	tmc->DSinfo.PictureHistory[1]->pData = tmc->frameIn;
	tmc->DSinfo.PictureHistory[2]->Flags = PICTURE_INTERLACED_ODD;
	tmc->DSinfo.PictureHistory[2]->pData = tmc->framePrev + tmc->rowsize;
	tmc->DSinfo.PictureHistory[3]->Flags = PICTURE_INTERLACED_EVEN;
	tmc->DSinfo.PictureHistory[3]->pData = tmc->framePrev;
    } else {
	tmc->DSinfo.PictureHistory[0]->Flags = PICTURE_INTERLACED_EVEN;
	tmc->DSinfo.PictureHistory[0]->pData = tmc->frameIn;
	tmc->DSinfo.PictureHistory[1]->Flags = PICTURE_INTERLACED_ODD;
	tmc->DSinfo.PictureHistory[1]->pData = tmc->frameIn + tmc->rowsize;
	tmc->DSinfo.PictureHistory[2]->Flags = PICTURE_INTERLACED_EVEN;
	tmc->DSinfo.PictureHistory[2]->pData = tmc->framePrev;
	tmc->DSinfo.PictureHistory[3]->Flags = PICTURE_INTERLACED_ODD;
	tmc->DSinfo.PictureHistory[3]->pData = tmc->framePrev + tmc->rowsize;
    }

    /* Call dscaler code */
#ifdef HAVE_ASM_SSE
    if (tmc->cpuflags & AC_SSE) {
	filterDScaler_SSE (&tmc->DSinfo,
			   tmc->SearchEffort, tmc->UseStrangeBob);
    } else
#endif
#ifdef HAVE_ASM_3DNOW
    if (tmc->cpuflags & AC_3DNOW) {
	filterDScaler_3DNOW (&tmc->DSinfo,
			     tmc->SearchEffort, tmc->UseStrangeBob);
    } else
#endif
#ifdef HAVE_ASM_MMX
    if (tmc->cpuflags & AC_MMX) {
	filterDScaler_MMX (&tmc->DSinfo,
			   tmc->SearchEffort, tmc->UseStrangeBob);
    } else
#endif
    {
	assert (0);
    }

}

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

int tc_filter(frame_list_t *ptr_, char *options)
{
    vframe_list_t *ptr = (vframe_list_t *)ptr_;
    vob_t *vob = NULL;
    tomsmocomp_t *tmc = tmc_global;

    //----------------------------------
    // filter init
    //----------------------------------

    if (ptr->tag & TC_FILTER_INIT) {

	if (! (vob = tc_get_vob ()))
	    return -1;

	if (! (tmc = tmc_global = tc_zalloc (sizeof (tomsmocomp_t)))) {
	    return -1;
	}

	if (! (tmc->tcvhandle = tcv_init())) {
	    tc_log_error(MOD_NAME, "tcv_init() failed");
	    return -1;
	}

	if (verbose)
	    tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

	/* default values */
	tmc->SearchEffort   = 11;
	tmc->UseStrangeBob  = 0;
	tmc->TopFirst       = 1;

	/* video parameters */
	switch (vob->im_v_codec) {
	case TC_CODEC_YUY2:
	case TC_CODEC_YUV420P:
	case TC_CODEC_YUV422P:
	    break;
	default:
	    tc_log_error (MOD_NAME, "only working with YUV (4:2:2 and 4:2:0) and YUY2 frame data...");
	    return -1;
	}
	tmc->codec     = vob->im_v_codec;
	tmc->width     = vob->im_v_width;
	tmc->height    = vob->im_v_height;
	tmc->size      = vob->im_v_width * vob->im_v_height * 2;
	tmc->cpuflags  = tc_get_session()->acceleration; /* XXX ugly */

	tmc->rowsize   = vob->im_v_width * 2;

	if (options) {
	    optstr_get (options, "topfirst",       "%d",
			&tmc->TopFirst);
	    optstr_get (options, "searcheffort",   "%d",
			&tmc->SearchEffort);
	    optstr_get (options, "usestrangebob",  "%d",
			&tmc->UseStrangeBob);
	    optstr_get (options, "cpuflags",  "%x",
			&tmc->cpuflags);

	    if (optstr_lookup(options, "help")) {
		help_optstr ();
	    }
	}

	/* frame memory */
	if (! (tmc->framePrev = calloc (1, tmc->size)) ||
	    ! (tmc->frameIn   = calloc (1, tmc->size)) ||
	    ! (tmc->frameOut  = calloc (1, tmc->size))) {
	    tc_log_msg(MOD_NAME, "calloc() failed");
	    return -1;
	}

	tmc->DSinfo.Overlay      = tmc->frameOut;
	tmc->DSinfo.OverlayPitch = tmc->rowsize;
	tmc->DSinfo.LineLength   = tmc->rowsize;
	tmc->DSinfo.FrameWidth   = tmc->width;
	tmc->DSinfo.FrameHeight  = tmc->height;
	tmc->DSinfo.FieldHeight  = tmc->height / 2;
	tmc->DSinfo.InputPitch   = 2* tmc->rowsize;

	tmc->DSinfo.pMemcpy = ac_memcpy;

	if (verbose) {
	    tc_log_info(MOD_NAME, "topfirst %s,  searcheffort %d,  usestrangebob %s",
		   tmc->TopFirst ? "True":"False", tmc->SearchEffort,
		   tmc->UseStrangeBob ? "True":"False");
	    tc_log_info(MOD_NAME, "cpuflags%s%s%s%s",
		   tmc->cpuflags & AC_SSE ? " SSE":"",
		   tmc->cpuflags & AC_3DNOW ? " 3DNOW":"",
		   tmc->cpuflags & AC_MMX ? " MMX":"",
		   !(tmc->cpuflags & (AC_SSE|AC_3DNOW|AC_MMX)) ? " None":"");
	}

	return 0;
    }

    //----------------------------------
    // filter close
    //----------------------------------

    if (ptr->tag & TC_FILTER_CLOSE) {
	free (tmc->framePrev);
	free (tmc->frameIn);
	free (tmc->frameOut);
	tmc->framePrev = tmc->frameIn = tmc->frameOut = NULL;
	tcv_free(tmc->tcvhandle);
	tmc->tcvhandle = 0;
	return 0;
    }

    //----------------------------------
    // filter description
    //----------------------------------
    if (ptr->tag & TC_FILTER_GET_CONFIG) {
	char buf[255];
	optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION,
			    MOD_AUTHOR, "VY4E", "1");
	tc_snprintf (buf, sizeof(buf), "%d", tmc->TopFirst);
	optstr_param (options, "topfirst", "Assume the top field should be displayed first" ,"%d", buf, "0", "1");
	tc_snprintf (buf, sizeof(buf), "%d", tmc->SearchEffort);
	optstr_param (options, "searcheffort", "CPU time used to find moved pixels" ,"%d", buf, "0", "30");
	tc_snprintf (buf, sizeof(buf), "%d", tmc->UseStrangeBob);
	optstr_param (options, "usestrangebob", "?Unknown?" ,"%d", buf, "0", "1");
	tc_snprintf (buf, sizeof(buf), "%02x", tmc->cpuflags);
	optstr_param (options, "cpuflags", "Manual specification of CPU capabilities" ,"%x", buf, "00", "ff");
    }

    //----------------------------------
    // filter frame routine
    //----------------------------------

    // need to process frames in-order
    if ((ptr->tag & TC_PRE_S_PROCESS) && (ptr->tag & TC_VIDEO)) {

	uint8_t *tmp;
	uint8_t *planes[3];
	YUV_INIT_PLANES(planes, ptr->video_buf, IMG_YUV_DEFAULT,
			tmc->width, tmc->height);

	/* Convert / Copy to yuy2 */
	switch (tmc->codec) {
	case TC_CODEC_YUY2:
	    ac_memcpy (tmc->frameIn, ptr->video_buf, tmc->size);
	    break;
	case TC_CODEC_YUV420P:
	    tcv_convert(tmc->tcvhandle, ptr->video_buf, tmc->frameIn,
			tmc->width, tmc->height, IMG_YUV_DEFAULT, IMG_YUY2);
	    break;
	case TC_CODEC_YUV422P:
	    tcv_convert(tmc->tcvhandle, ptr->video_buf, tmc->frameIn,
			tmc->width, tmc->height, IMG_YUV422P, IMG_YUY2);
	    break;
	}

	if (! (ptr->tag & TC_FRAME_IS_SKIPPED)) {

	    /* Do the deinterlacing */
	    do_deinterlace (tmc);

	    /* Now convert back */
	    switch (tmc->codec) {
	    case TC_CODEC_YUY2:
		ac_memcpy (ptr->video_buf, tmc->frameOut, tmc->size);
		break;
	    case TC_CODEC_YUV420P:
		tcv_convert(tmc->tcvhandle, tmc->frameOut,  ptr->video_buf,
			    tmc->width, tmc->height, IMG_YUY2, IMG_YUV_DEFAULT);
		break;
	    case TC_CODEC_YUV422P:
		tcv_convert(tmc->tcvhandle, tmc->frameOut,  ptr->video_buf,
			    tmc->width, tmc->height, IMG_YUY2, IMG_YUV422P);
		break;
	    default:
		tc_log_error(MOD_NAME, "codec: %x\n", tmc->codec);
		assert (0);
	    }
	}

	// The current frame gets the next previous frame
	tmp = tmc->framePrev;
	tmc->framePrev = tmc->frameIn;
	tmc->frameIn   = tmp;
    }

    return 0;
}

