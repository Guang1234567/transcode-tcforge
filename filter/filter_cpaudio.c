/*
 *  filter_cpaudio.c
 *
 *  Copyright (C) William H Wittig - May 2003
 *  Still GPL, of course
 *
 *  This filter takes the audio signal on one channel and dupes it on
 *  the other channel.
 *  Only supports 16 bit stereo (for now)
 *
 * based on filter_null.c from transcode - orignal copyright below
 *
 *  Copyright (C) Thomas Oestreich - June 2001
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

#define MOD_NAME    "filter_cpaudio.so"
#define MOD_VERSION "v0.1 (2003-04-30)"
#define MOD_CAP     "copy one audio channel to the other channel filter plugin"
#define MOD_AUTHOR  "William H Wittig"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"


/*-------------------------------------------------
 * local utility functions
 *-------------------------------------------------*/

static void help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help\n"
"* Overview\n"
"    Copies audio from one channel to another\n"
"* Options\n"
"     'source=['l<eft>' or 'r<ight>']\n"
		, MOD_CAP);
}

/*-------------------------------------------------
 * single function interface
 *-------------------------------------------------*/

int tc_filter(frame_list_t *ptr_, char *options)
{
  aframe_list_t *ptr = (aframe_list_t *)ptr_;
  vob_t *vob=NULL;
  static int sourceChannel = 0;    // Init to left. '1' = right

  if (ptr->tag & TC_FILTER_GET_CONFIG) {
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "William H Wittig", "AO", "1");
      optstr_param (options, "source", "Source channel (l=left, r=right)", "%c", "l", "l", "r");
      return 0;
  }

  //----------------------------------
  // filter init
  //----------------------------------

  if (ptr->tag & TC_FILTER_INIT)
  {
    if ((vob = tc_get_vob()) == NULL)
        return (-1);

    if (vob->dm_bits != 16)
    {
      tc_log_error (MOD_NAME, "This filter only works for 16 bit samples\n");
      return (-1);
    }

    if (options != NULL)
    {
      char srcChannel;

      optstr_get(options, "source", "%c", &srcChannel);

      if (srcChannel == 'l')
         sourceChannel = 0;
      else
         sourceChannel = 1;
    }

    if (options)
      if (optstr_lookup (options, "help"))
      {
        help_optstr();
      }

    // filter init ok.

    if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    if(verbose) tc_log_info(MOD_NAME, "options=%s", options);

    return(0);
  }

  //----------------------------------
  // filter close
  //----------------------------------

  if(ptr->tag & TC_FILTER_CLOSE)
  {
    return(0);
  }

  //----------------------------------
  // filter frame routine
  //----------------------------------

  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  if(ptr->tag & TC_POST_M_PROCESS && ptr->tag & TC_AUDIO && !(ptr->attributes & TC_FRAME_IS_SKIPPED))
  {
    int16_t* data = (int16_t *)ptr->audio_buf;
    int len = ptr->audio_size / 2; // 16 bits samples
    int i;

    // if(verbose) tc_log_msg(MOD_NAME, "Length: %d, Source: %d", len, sourceChannel);

    for (i = 0; i < len; i += 2) // Implicitly assumes even number of samples (e.g. l,r pairs)
    {
        if (sourceChannel == 0)
            data[i+1] = data[i];
        else
            data[i] = data[i+1];
    }
  }
  return(0);
}
