/*
 *  optstr.h
 *
 *  Copyright (C) Tilmann Bitterberg 2003
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
 *  optstr is a general purpose option string parser
 *
 *  Usage example:
 *  int main (void)
 *  {
 *      int sum, top, bottom, quant;
 *      int n;
 *      char s[100];
 *      char options[] = "ranges=5-10:range=8,12,100:percent=16%:help";
 *
 *      if (optstr_get (options, "help", "") >= 0)
 *              usage();
 *
 *      optstr_get (options, "range", "%d,%d,%d", &bottom, &top, &sum);
 *      optstr_get (options, "ranges", "%d-%d", &bottom, &top);
 *      optstr_get (options, "range", "%d,%d", &bottom, &top);
 *      optstr_get (options, "string", "%[^:]", s);
 *      n = optstr_get (options, "percent", "%d%%", &quant);
 *      printf("found %d argumens\n", n);
 *
 *      return 0;
 *  }
 *
 */

#ifndef OPTSTR_H
#define OPTSTR_H

#define ARG_MAXIMUM (16)
#define ARG_SEP ':'
#define ARG_CONFIG_LEN 8192

/*
 * optstr_lookup:
 *     Finds the _exact_ 'needle' in 'haystack' (naming intentionally 
 *     identical to the 'strstr' (3) linux man page)
 *
 * Parameters:
 *     needle: substring to be searched
 *     haystack: string which is supposed to contain the substring
 * Return Value:
 *     constant pointer to first substring found, or NULL if substring
 *     isn't found.
 * Side effects:
 *     none
 * Preconditions:
 *     none
 * Postconditions:
 *     none
 */
const char * optstr_lookup(const char *haystack, const char *needle);

/*
 * optstr_get:
 *     extract values from option string
 *
 * Parameters:
 *     options: a null terminated string of options to parse,
 *              syntax is "opt1=val1:opt_bool:opt2=val1-val2"
 *              where ':' is the seperator.
 *     name: the name to look for in options; eg "opt2"
 *     fmt: the format to scan values (printf format); eg "%d-%d"
 *     (...): variables to assign; eg &lower, &upper
 * Return value:
 *     -2 internal error
 *     -1 `name' is not in `options'
 *     0  `name' is in `options'
 *     >0 number of arguments assigned
 * Side effects:
 *     none
 * Preconditions:
 *     none
 * Postconditions:
 *     none
 */
int optstr_get(const char *options, const char *name, const char *fmt, ...)
#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((format(scanf,3,4)))
#endif
;

/*
 * optstr_filter_desc:
 *     Generate a Description of a filter; this description will be a row in
 *     CSV format. Example:
 *     "filter_foo", "comment", "0.1", "no@one", "VRY", "1"\n
 *     WARNING: this function will be deprecated soon since new capabilities
 *     code has more flexibility and expressiveness.
 *
 * Parameters:
 *     buf: a write buffer, will contain the result of the function. 'buf'
 *          must be at least ARG_CONFIG_LEN characters large.
 *     filter_(name|comment|version|author):
 *          obvious, various filter meta data
 *     capabilities: string of filter capabilities.
 *                   "V":  Can do Video
 *                   "A":  Can do Audio
 *                   "R":  Can do RGB
 *                   "Y":  Can do YUV420
 *                   "4":  Can do YUV422
 *                   "M":  Can do Multiple Instances
 *                   "E":  Is a PRE filter
 *                   "O":  Is a POST filter
 *                   Valid examples:
 *                   "VR"  : Video and RGB
 *                   "VRY" : Video and YUV and RGB
 *            frames_needed: a string of how many frames the filter needs
 *                           to take effect. Usually this is "1".
 * Return value:
 *     1 Not enough space in `buf' parameter
 *     0 Successfull
 * Side effects:
 *     none
 * Preconditions:
 *     none
 * Postconditions:
 *     none
 */
int optstr_filter_desc(char *buf,
                       const char *filter_name,
                       const char *filter_comment,
                       const char *filter_version,
                       const char *filter_author,
                       const char *capabilities,
                       const char *frames_needed);

/*
 * optstr_frames_needed:
 *     extract the how many frames the filter needs from an CSV row.
 *
 * Parameters:
 *     filter_desc: the CSV row
 *     needed_frames: the result will be stored in this variable
 * Return value:
 *     1 An error happend
 *     0 Successfull
 * Side effects:
 *     none
 * Preconditions:
 *     none
 * Postconditions:
 *     none
 */
int optstr_frames_needed(const char *filter_desc, int *needed_frames);

/*
 * optstr_param:
 *     Generate a description of one filter parameter. The output will be
 *     in CSV format.
 *     Example: "radius", "Search radius", "%d", "8", "8", "24"\n
 *
 * Parameters:
 *     buf: a write buffer, will contain the result of the function. 'buf'
 *          must be at least ARG_CONFIG_LEN characters large.
 *     name: the name of the parameter (eg "radius")
 *     comment: a short description (eg "Search radius")
 *     fmt: a printf style parse string (eg "%d")
 *     val: current value (eg "8")
 *     (...): always pairs (but this is actually NOT checked): legal values 
 *            for the parameter (eg "8", "24" -- meaning, the radius 
 *            parameter is valid from 8 to 24).
 * Return value:
 *      1 An Error happened
 *      0 Successfull
 * Side effects:
 *      none
 * Preconditions:
 *      none
 * Postconditions:
 *      none
 *
 * More examples:
 *   "pos", "Position (0-width x 0-height)", "%dx%d", "0x0", "0", "width", "0", "height"
 *    "%dx%d" is interesting, because this parameter takes two values in this format
 *            so we must supply two ranges (one for each parameter), when this
 *            param is valid ("0", "width", "0", "height")
 *
 *   "flip", "Mirror image", "", "0"
 *     This is a boolean, defaults to false. A boolean has no argument, eg "filter_foo=flip"
 *
 */
int optstr_param(char *buf,
                 const char *name,
                 const char *comment,
                 const char *fmt,
                 const char *val,
                 ...); /* char *valid_from1, char *valid_to1 */

#endif /* OPTSTR_H */
