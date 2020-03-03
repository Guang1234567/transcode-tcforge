/**
 *  @file filter_skip.c Skip all listed frames
 *
 *  Copyright (C) Thomas Oestreich - June 2001,
 *                Thomas Wehrspann - January 2005
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

/*
 * ChangeLog:
 * v0.0.1 (2001-11-27)
 *
 * v0.2 (2005-01-05) Thomas Wehrspann
 *    -Rewritten, based on filter_cut
 *    -Documentation added
 *    -New help function
 *    -optstr_filter_desc now returns
 *     the right capability flags
 */

#define MOD_NAME    "filter_skip.so"
#define MOD_VERSION "v0.2 (2005-01-05)"
#define MOD_CAP     "skip all listed frames"
#define MOD_AUTHOR  "Thomas Oestreich, Thomas Wehrspann"

#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtcutil/optstr.h"

#include "libtc/framecode.h"


/**
 * Help text.
 * This function prints out a small description of this filter and
 * the command-line options when the "help" parameter is given
 *********************************************************/
static void help_optstr(void)
{
  tc_log_info (MOD_NAME, "(%s) help\n"
"* Overview\n"
"    This filter skips all listed frames.\n"
"\n"
"* Options\n"
"                    'help' Prints out this help text\n"
"    'start-end/step [...]' List of frame ranges to skip (start-end/step) []\n"
	       , MOD_CAP);
}


/**
 * Main function of a filter.
 * This is the single function interface to transcode. This is the only function needed for a filter plugin.
 * @param ptr     frame accounting structure
 * @param options command-line options of the filter
 *
 * @return 0, if everything went OK.
 *********************************************************/
int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  static struct fc_time *list;
  static double avoffset=1.0;
  char separator[] = " ";

  vob_t *vob=NULL;

  if(ptr->tag & TC_FILTER_GET_CONFIG) {
    optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VARY4E", "1");

    optstr_param (options, "start-end/step [...]", "Skip frames", "%s", "");
      return 0;
  }

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------
  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL) return(-1);

    // filter init ok.

    if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    if(verbose & TC_DEBUG) tc_log_info(MOD_NAME, "options=%s", options);

    if(options == NULL) return(0);

    // Parameter parsing
    if(options == NULL) return(0);
    else if (optstr_lookup (options, "help")) {
      help_optstr();
      return (0);
    } else {
      if( parse_fc_time_string( options, vob->fps, separator, verbose, &list ) == -1 ) {
        help_optstr();
        return (-1);
      }
    }

    avoffset = vob->fps/vob->ex_fps;

    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------
  if(ptr->tag & TC_FILTER_CLOSE) {
    return(0);
  }

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------
  // tag variable indicates, if we are called before
  // transcodes internal video/audio frame processing routines
  // or after and determines video/audio context
  if((ptr->tag & TC_PRE_S_PROCESS) && (ptr->tag & TC_VIDEO)) {

      // fc_frame_in_time returns the step frequency
      int ret = fc_frame_in_time(list, ptr->id);

      if ((ret && !(ptr->id%ret)))
        ptr->attributes |= TC_FRAME_IS_SKIPPED;

  } else if ((ptr->tag & TC_PRE_S_PROCESS) && (ptr->tag & TC_AUDIO)){

    int ret;
    int tmp_id;

    tmp_id = (int)((double)ptr->id*avoffset);
    ret = fc_frame_in_time(list, tmp_id);
    if ((ret && !(tmp_id%ret)))
      ptr->attributes |= TC_FRAME_IS_SKIPPED;

  }

  return(0);
}
