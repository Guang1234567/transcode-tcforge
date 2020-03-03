/*
 *  ioxml.h
 *
 *  Copyright (C) Marzio Malanchini - Febrary 2002
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

#ifndef	IOXML_H
#define IOXML_H
#ifdef	HAVE_LIBXML2

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

enum
{
	npt,
	smpte,
	smpte30drop,
	smpte25
};


typedef struct _video_filter_t {
                		TCVZoomFilter s_zoom_filter;
                        } video_filter_t;

typedef struct _audiovideo_limit_t {
					int 	s_smpte;
                                	long	s_time;
                                	long	s_frame;
                        } audiovideo_limit_t;

typedef struct _audiovideo_t {
                                char				*p_nome_audio;
                                char				*p_nome_video;
                                long				s_start_audio;
                                long				s_start_a_time;
                                long				s_end_audio;
                                long				s_end_a_time;
                                long				s_start_video;
                                long				s_start_v_time;
                                long				s_end_video;
                                long				s_end_v_time;
				int				s_video_smpte;
				int				s_audio_smpte;
                                struct _audiovideo_t	        *p_next;
				long				s_a_real_codec;
				long				s_v_real_codec;
				long				s_a_codec;
				long				s_v_codec;
				long				s_a_magic;
				long				s_v_magic;
				double				s_fps;
				int				s_a_rate;
				int 				s_a_bits;
				int				s_a_chan;
				int 				s_v_width;
				int				s_v_height;
				int 				s_v_tg_width;
				int				s_v_tg_height;
				char				*p_v_resize_filter;
                        } audiovideo_t;

void f_free_tree(audiovideo_t *p_node);
int f_parse_tree(xmlNodePtr p_node,audiovideo_t *p_audiovideo);
int f_complete_tree(audiovideo_t *p_audiovideo);
void f_delete_unused_node(xmlNodePtr p_node);
int f_manage_input_xml(const char *p_name,int s_type,audiovideo_t *p_audiovideo);
audiovideo_limit_t f_det_time(char *p_options);

#endif
#endif
