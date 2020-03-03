/*
 *  import_xml.c
 *
 *  Copyright (C) Marzio Malanchini - March 2002
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

#define MOD_NAME    "import_xml.so"
#define MOD_VERSION "v0.0.8 (2003-07-09)"
#define MOD_CODEC   "(video) * | (audio) *"

#include "src/transcode.h"
#include "tccore/tcinfo.h"
#include "libtcvideo/tcvideo.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = -1;

#define MOD_PRE xml
#include "import_def.h"

#include "ioxml.h"
#include "magic.h"
#include "probe_xml.h"


#define M_AUDIOMAX(a,b)  (b==LONG_MAX)?LONG_MAX:a*b

#define MAX_BUF 1024
char import_cmd_buf[MAX_BUF];
static FILE *s_fd_video=0;
static FILE *s_fd_audio=0;
static  audiovideo_t    s_audio,*p_audio=NULL;
static  audiovideo_t    s_video,*p_video=NULL,*p_video_prev;
static	int s_frame_size=0;
static char *p_vframe_buffer=NULL;
static	int s_v_codec;
static	long s_a_magic;
static	long s_v_magic;
static TCVHandle tcvhandle = 0;

int binary_dump=1;		//force the use of binary dump to create the correct XML tree



static int f_dim_check(audiovideo_t *p_temp,int *s_new_height,int *s_new_width)
{
	int s_rc;

	s_rc=0;
	if (p_temp->s_v_tg_width==0)
		*s_new_width=p_temp->s_v_width;
	else
	{
		s_rc=1;
		*s_new_width=p_temp->s_v_tg_width;
	}
	if (p_temp->s_v_tg_height==0)
		*s_new_height=p_temp->s_v_height;
	else
	{
		s_rc=1;
		*s_new_height=p_temp->s_v_tg_height;
	}
	return(s_rc);
}

static int f_calc_frame_size(audiovideo_t *p_temp,int s_codec)
{
	int s_new_height,s_new_width;

	if (f_dim_check(p_temp,&s_new_height,&s_new_width))
	{
		switch(s_codec)
		{
			case TC_CODEC_RGB24:
				return(3*s_new_width*s_new_height);
			break;
			default:
				return((3*s_new_width*s_new_height)/2);
			break;
		}
	}
	return(s_frame_size);
}


static video_filter_t *f_video_filter(char *p_filter)
{
	static video_filter_t s_v_filter;

	if (p_filter !=NULL)
	{
        s_v_filter.s_zoom_filter = tcv_zoom_filter_from_string(p_filter);
        if (s_v_filter.s_zoom_filter == TCV_ZOOM_NULL);
		{
			s_v_filter.s_zoom_filter = TCV_ZOOM_LANCZOS3;
		}
	}
	else //"lanczos3" default
	{
		s_v_filter.s_zoom_filter = TCV_ZOOM_LANCZOS3;
	}
	return (&s_v_filter);

}

static void f_mod_video_frame(transfer_t *param,audiovideo_t *p_temp,int s_codec,int s_cleanup)
{
	static uint8_t *p_pixel_tmp=NULL;
	int s_new_height,s_new_width;
	static video_filter_t *p_v_filter;
	static audiovideo_t *p_tmp=NULL;


	if (s_cleanup)
	{
		if (p_pixel_tmp !=NULL)
			free(p_pixel_tmp);
		return;
	}
	if (f_dim_check(p_temp,&s_new_height,&s_new_width))
	{
		if (p_tmp != p_temp)
		{
			p_tmp=p_temp;
			p_v_filter=f_video_filter(p_temp->p_v_resize_filter);
			if(verbose_flag)
				tc_log_info(MOD_NAME,"setting resize video filter to %s",
                            tcv_zoom_filter_to_string(p_v_filter->s_zoom_filter));
		}
		switch(s_codec)
		{
			case TC_CODEC_RGB24:
				if (p_pixel_tmp ==NULL)
					p_pixel_tmp = tc_zalloc(3*p_temp->s_v_tg_width * p_temp->s_v_tg_height);
				tcv_zoom(tcvhandle, p_vframe_buffer, p_pixel_tmp, p_temp->s_v_width, p_temp->s_v_height, 3, p_temp->s_v_tg_width, p_temp->s_v_tg_height, p_v_filter->s_zoom_filter);
			break;
			default: {
				int Y_size_in = p_temp->s_v_width * p_temp->s_v_height;
				int Y_size_out = p_temp->s_v_tg_width * p_temp->s_v_tg_height;
				int UV_size_in = (p_temp->s_v_width/2) * (p_temp->s_v_height/2);
				int UV_size_out = (p_temp->s_v_tg_width/2) * (p_temp->s_v_tg_height/2);
				if (p_pixel_tmp ==NULL)
					p_pixel_tmp = tc_zalloc(Y_size_out + 2*UV_size_out);
				tcv_zoom(tcvhandle, p_vframe_buffer, p_pixel_tmp, p_temp->s_v_width, p_temp->s_v_height, 1, p_temp->s_v_tg_width, p_temp->s_v_tg_height, p_v_filter->s_zoom_filter);
				tcv_zoom(tcvhandle, p_vframe_buffer + Y_size_in, p_pixel_tmp + Y_size_out, p_temp->s_v_width/2, p_temp->s_v_height/2, 1, p_temp->s_v_tg_width/2, p_temp->s_v_tg_height/2, p_v_filter->s_zoom_filter);
				tcv_zoom(tcvhandle, p_vframe_buffer + Y_size_in + UV_size_in, p_pixel_tmp + Y_size_out + UV_size_out, p_temp->s_v_width/2, p_temp->s_v_height/2, 1, p_temp->s_v_tg_width/2, p_temp->s_v_tg_height/2, p_v_filter->s_zoom_filter);
			}
			break;
		}
		ac_memcpy(param->buffer,p_pixel_tmp,param->size);	//copy the new image buffer
	}
	else
	{
		ac_memcpy(param->buffer,p_vframe_buffer,param->size);	//only copy the original buffer
	}
}


/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
	info_t	s_info_dummy;
	ProbeInfo s_probe_dummy1,s_probe_dummy2;
	long s_tot_dummy1,s_tot_dummy2;
	int s_frame_audio_size=0;

	if(param->flag == TC_VIDEO)
	{
		param->fd = NULL;
                if (p_video == NULL)
                {
			s_info_dummy.name=vob->video_in_file;	//init the video XML input file name
			s_info_dummy.verbose=vob->verbose;	//init the video XML input file name
                        if (f_build_xml_tree(&s_info_dummy,&s_video,&s_probe_dummy1,&s_probe_dummy2,&s_tot_dummy1,&s_tot_dummy2) == -1)	//create the XML tree
                        {
                                f_manage_input_xml(NULL,0,&s_video);
                                tc_log_warn(MOD_NAME,"file %s has invalid format content.",
						vob->video_in_file);
				return(TC_IMPORT_ERROR);
                        }
                        p_video=s_video.p_next;
                }
		if (p_video == NULL)
		{
                        tc_log_warn(MOD_NAME,"there isn't no file in  %s.",
					vob->video_in_file);
			return(TC_IMPORT_ERROR);
		}
		if(p_video->s_v_codec == TC_CODEC_UNKNOWN)
		{
			if (vob->dv_yuy2_mode == 1)
		    		s_v_codec=TC_CODEC_YUY2;
			else if (vob->dv_yuy2_mode == 0)
				s_v_codec=TC_CODEC_YUV420P;
			else
				s_v_codec=vob->im_v_codec;
		}
		else
		{
			s_v_codec=p_video->s_v_codec;
		}
		s_v_magic=p_video->s_v_magic;
		switch(s_v_magic)
		{
		   case TC_MAGIC_DV_PAL:
		   case TC_MAGIC_DV_NTSC:
			capability_flag=TC_CAP_RGB|TC_CAP_YUV|TC_CAP_DV|TC_CAP_PCM;
			switch(s_v_codec)
			{
				case TC_CODEC_RGB24:
					s_frame_size = 3*(p_video->s_v_width * p_video->s_v_height);
					if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                   "%s -i \"%s\" -x dv -d %d -C %ld-%ld |"
                                   " %s -x dv -y rgb -d %d -Q %d",
                                   TCEXTRACT_EXE, p_video->p_nome_video, vob->verbose, p_video->s_start_video, p_video->s_end_video,
                                   TCDECODE_EXE, vob->verbose, vob->quality) < 0)
					{
						tc_log_perror(MOD_NAME, "command buffer overflow");
						return(TC_IMPORT_ERROR);
					}
				break;
				case TC_CODEC_YUY2:
					s_frame_size = (3*(p_video->s_v_width * p_video->s_v_height))/2;
					if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                   "%s -i \"%s\" -x dv -d %d -C %ld-%ld |"
                                   " %s -x dv -y yuv420p -Y -d %d -Q %d",
                                   TCEXTRACT_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video,
                                   TCDECODE_EXE, vob->verbose, vob->quality) < 0)
					{
						tc_log_perror(MOD_NAME, "command buffer overflow");
						return(TC_IMPORT_ERROR);
					}
				break;
				case TC_CODEC_YUV420P:
					s_frame_size = (3*(p_video->s_v_width * p_video->s_v_height))/2;
					if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                   "%s -i \"%s\" -x dv -d %d -C %ld-%ld |"
                                   " %s -x dv -y yuv420p -d %d -Q %d",
                                   TCEXTRACT_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video,
                                   TCDECODE_EXE, vob->verbose, vob->quality) < 0)
					{
						tc_log_perror(MOD_NAME, "command buffer overflow");
						return(TC_IMPORT_ERROR);
					}
				break;
				case TC_CODEC_RAW:
					s_frame_size = (p_video->s_v_height==PAL_H) ? TC_FRAME_DV_PAL:TC_FRAME_DV_NTSC;
					if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                   "%s -i \"%s\" -x dv -d %d -C %ld-%ld",
                                   TCEXTRACT_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video) < 0)
					{
						tc_log_perror(MOD_NAME, "command buffer overflow");
						return(TC_IMPORT_ERROR);
					}
				break;
				default:
					tc_log_warn(MOD_NAME, "invalid import codec request 0x%x", s_v_codec);
					return(TC_IMPORT_ERROR);
			}
		   break;
		   case TC_MAGIC_MOV:
			capability_flag=TC_CAP_PCM|TC_CAP_RGB|TC_CAP_YUV;
			switch(s_v_codec)
			{
				case TC_CODEC_RGB24:
					s_frame_size = (3*(p_video->s_v_width * p_video->s_v_height));
					if (p_video->s_v_real_codec == TC_CODEC_DV)
					{
						if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                       "%s -x mov -i \"%s\" -d %d -C %ld,%ld -Q %d |"
                                       " %s -x dv -y rgb -d %d -Q %d",
                                       TCDECODE_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video, vob->quality,
                                       TCDECODE_EXE, vob->verbose,vob->quality) < 0)
						{
							tc_log_perror(MOD_NAME, "command buffer overflow");
							return(TC_IMPORT_ERROR);
						}
					}
					else
					{
						if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                       "%s -x mov -y rgb -i \"%s\" -d %d -C %ld,%ld -Q %d",
                                       TCDECODE_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video, vob->quality) < 0)
						{
							tc_log_perror(MOD_NAME, "command buffer overflow");
							return(TC_IMPORT_ERROR);
						}
					}
				break;
				case TC_CODEC_YUV420P:
					s_frame_size = (3*(p_video->s_v_width * p_video->s_v_height))/2;
					if (p_video->s_v_real_codec == TC_CODEC_DV)
					{
						if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                       "%s -x mov -i \"%s\" -d %d -C %ld,%ld -Q %d |"
                                       " %s -x dv -y yuv420p -d %d -Q %d",
                                       TCDECODE_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video, vob->quality,
                                       TCDECODE_EXE, vob->verbose,vob->quality) < 0)
						{
							tc_log_perror(MOD_NAME, "command buffer overflow");
							return(TC_IMPORT_ERROR);
						}
					}
					else
					{
						if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                       "%s -x mov -y yuv2 -i \"%s\" -d %d -C %ld,%ld -Q %d",
                                       TCDECODE_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video, vob->quality) < 0)
						{
							tc_log_perror(MOD_NAME, "command buffer overflow");
							return(TC_IMPORT_ERROR);
						}
					}
				break;
				default:
					tc_log_warn(MOD_NAME, "invalid import codec request 0x%x", s_v_codec);
					return(TC_IMPORT_ERROR);
			}
		   break;
		   case TC_MAGIC_AVI:
			capability_flag=TC_CAP_PCM|TC_CAP_RGB|TC_CAP_AUD|TC_CAP_VID;
			switch(s_v_codec)
			{
				case TC_CODEC_RGB24:
				s_frame_size = (3*(p_video->s_v_width * p_video->s_v_height));
				if(tc_snprintf(import_cmd_buf, MAX_BUF,
                               "%s -i \"%s\" -x avi -d %d -C %ld-%ld",
                               TCEXTRACT_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video) < 0)
				{
					tc_log_perror(MOD_NAME, "command buffer overflow");
					return(TC_IMPORT_ERROR);
				}
				break;
				default:
                       			tc_log_warn(MOD_NAME,"video codec 0x%x not yet supported.", s_v_codec);
					return(TC_IMPORT_ERROR);
					;
			}
		   break;
		  default:
                       	tc_log_warn(MOD_NAME,"video magic 0x%lx not yet supported.", s_v_magic);
			return(TC_IMPORT_ERROR);
		}
		if((s_fd_video = popen(import_cmd_buf, "r"))== NULL)
		{
			tc_log_warn(MOD_NAME,"Error cannot open the pipe.");
			return(TC_IMPORT_ERROR);
		}
		param->size=f_calc_frame_size(p_video,s_v_codec);	//setting the frame size
		p_vframe_buffer=tc_malloc(s_frame_size);
		if(verbose_flag)
			tc_log_info(MOD_NAME,"setting target video size to %d",param->size);
		if (!tcvhandle && !(tcvhandle = tcv_init()))
		{
			tc_log_error(MOD_NAME, "tcv_init() failed");
			return(TC_IMPORT_ERROR);
		}
		p_video_prev=p_video;
		p_video=p_video->p_next;
		if(verbose_flag)
			tc_log_info(MOD_NAME, "%s", import_cmd_buf);
		return(0);
	}
	if(param->flag == TC_AUDIO)
	{
		param->fd = NULL;
		if (p_audio== NULL)
		{
			if (vob->audio_in_file !=NULL)
				s_info_dummy.name=vob->audio_in_file;	//init the video XML input file name
			else
				s_info_dummy.name=vob->video_in_file;	//init the video XML input file name

			s_info_dummy.verbose=vob->verbose;	//init the video XML input file name
                        if (f_build_xml_tree(&s_info_dummy,&s_audio,&s_probe_dummy1,&s_probe_dummy2,&s_tot_dummy1,&s_tot_dummy2) == -1)	//create the XML tree
			{
				f_manage_input_xml(NULL,0,&s_audio);
				tc_log_warn(MOD_NAME,"file %s has invalid format content.", vob->audio_in_file);
				return(TC_IMPORT_ERROR);
			}
			p_audio=s_audio.p_next;
		}
		if (p_audio == NULL)
		{
                        tc_log_warn(MOD_NAME,"there isn't no file in  %s.", vob->audio_in_file);
			return(TC_IMPORT_ERROR);
		}
		s_frame_audio_size=(1.00 * p_audio->s_a_bits * p_audio->s_a_chan * p_audio->s_a_rate)/(8*p_audio->s_fps);
		if(verbose_flag)
			tc_log_info(MOD_NAME,"setting audio size to %d",s_frame_audio_size);
		s_a_magic=p_audio->s_a_magic;
		switch(s_a_magic)
		{
		   case TC_MAGIC_DV_PAL:
		   case TC_MAGIC_DV_NTSC:
			capability_flag=TC_CAP_RGB|TC_CAP_YUV|TC_CAP_DV|TC_CAP_PCM;
			if(tc_snprintf(import_cmd_buf, MAX_BUF,
                           "%s -i \"%s\" -d %d -x dv -C %ld-%ld |"
                           " %s -x dv -y pcm -d %d -Q %d",
                           TCEXTRACT_EXE, p_audio->p_nome_audio, vob->verbose,M_AUDIOMAX(s_frame_audio_size,p_audio->s_start_audio),M_AUDIOMAX(s_frame_audio_size,p_audio->s_end_audio),
                           TCDECODE_EXE, vob->verbose,vob->quality) < 0)
			{
				tc_log_perror(MOD_NAME, "command buffer overflow");
				return(TC_IMPORT_ERROR);
			}
		   break;
		   case TC_MAGIC_AVI:
			capability_flag=TC_CAP_PCM|TC_CAP_RGB|TC_CAP_AUD|TC_CAP_VID;
			if(tc_snprintf(import_cmd_buf, MAX_BUF,
                           "%s -i \"%s\" -d %d -x pcm -a %d -C %ld-%ld",
                           TCEXTRACT_EXE, p_audio->p_nome_audio, vob->verbose,vob->a_track,M_AUDIOMAX(s_frame_audio_size,p_audio->s_start_audio),M_AUDIOMAX(s_frame_audio_size,p_audio->s_end_audio)) < 0)
			{
				tc_log_perror(MOD_NAME, "command buffer overflow");
				return(TC_IMPORT_ERROR);
			}
		   break;
		   case TC_MAGIC_MOV:
			capability_flag=TC_CAP_PCM|TC_CAP_RGB|TC_CAP_YUV;
			if (p_audio->s_a_bits == 16)
				s_frame_audio_size >>= 1;
			if (p_audio->s_a_chan == 2)
				s_frame_audio_size >>= 1;
			if(tc_snprintf(import_cmd_buf, MAX_BUF,
                           "%s -i \"%s\" -d %d -x mov -y pcm -C %ld,%ld",
                           TCDECODE_EXE, p_audio->p_nome_audio, vob->verbose,M_AUDIOMAX(s_frame_audio_size,p_audio->s_start_audio),M_AUDIOMAX(s_frame_audio_size,p_audio->s_end_audio)) < 0)
			{
				tc_log_perror(MOD_NAME, "command buffer overflow");
				return(TC_IMPORT_ERROR);
			}
		   break;
		   default:
                        tc_log_warn(MOD_NAME,"audio magic 0x%lx not yet supported.",s_a_magic);
			return(TC_IMPORT_ERROR);
		}
		if((s_fd_audio = popen(import_cmd_buf, "r"))== NULL)
		{
			tc_log_warn(MOD_NAME,"Error cannot open the pipe.");
			return(TC_IMPORT_ERROR);
		}
		p_audio=p_audio->p_next;
		if(verbose_flag)
			tc_log_info(MOD_NAME, "%s", import_cmd_buf);
		return(0);
	}
	return(TC_IMPORT_ERROR);
}


/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

MOD_decode
{
	int s_audio_frame_size;
	int s_video_frame_size;
	static int s_audio_frame_size_orig=0;
	static int s_video_frame_size_orig=0;
	int s_frame_audio_size=0;

	if(param->flag == TC_AUDIO)
	{
                if (param->size < s_audio_frame_size_orig)
                {
                         param->size=s_audio_frame_size_orig;
                         s_audio_frame_size_orig=0;
                }
                s_audio_frame_size=fread(param->buffer, 1, param->size, s_fd_audio);
                if (s_audio_frame_size == 0)
                {
                        if (p_audio != NULL)    // is there a file ?
                        {
				s_frame_audio_size=(1.00 * p_audio->s_a_bits * p_audio->s_a_chan * p_audio->s_a_rate)/(8*p_audio->s_fps);
				if(verbose_flag)
					tc_log_info(MOD_NAME,"setting audio size to %d",s_frame_audio_size);
				s_a_magic=p_audio->s_a_magic;
				switch(s_a_magic)
				{
				   case TC_MAGIC_DV_PAL:
				   case TC_MAGIC_DV_NTSC:
					if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                   "%s -i \"%s\" -d %d -x dv -C %ld-%ld |"
                                   " %s -x dv -y pcm -d %d -Q %d",
                                   TCEXTRACT_EXE, p_audio->p_nome_audio, vob->verbose,M_AUDIOMAX(s_frame_audio_size,p_audio->s_start_audio),M_AUDIOMAX(s_frame_audio_size,p_audio->s_end_audio),
                                   TCDECODE_EXE, vob->verbose,vob->quality) < 0)
                                        {
                                                tc_log_perror(MOD_NAME, "command buffer overflow");
                                                return(TC_IMPORT_ERROR);
                                        }
				   break;
				   case TC_MAGIC_AVI:
					if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                   "%s -i \"%s\" -d %d -x pcm -a %d -C %ld-%ld",
                                   TCEXTRACT_EXE, p_audio->p_nome_audio, vob->verbose,vob->a_track,M_AUDIOMAX(s_frame_audio_size,p_audio->s_start_audio),M_AUDIOMAX(s_frame_audio_size,p_audio->s_end_audio)) < 0)
					{
						tc_log_perror(MOD_NAME, "command buffer overflow");
						return(TC_IMPORT_ERROR);
					}
				   break;
		   		   case TC_MAGIC_MOV:
					if (p_audio->s_a_bits == 16)
						s_frame_audio_size >>= 1;
					if (p_audio->s_a_chan == 2)
						s_frame_audio_size >>= 1;
					if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                   "%s -i \"%s\" -d %d -x mov -y pcm -C %ld,%ld",
                                   TCDECODE_EXE, p_audio->p_nome_audio, vob->verbose,M_AUDIOMAX(s_frame_audio_size,p_audio->s_start_audio),M_AUDIOMAX(s_frame_audio_size,p_audio->s_end_audio)) < 0)
					{
						tc_log_perror(MOD_NAME, "command buffer overflow");
						return(TC_IMPORT_ERROR);
					}
		   		   break;
				   default:
                        		tc_log_warn(MOD_NAME,"audio magic 0x%lx not yet supported.",s_a_magic);
					return(TC_IMPORT_ERROR);
				}
                                if((s_fd_audio = popen(import_cmd_buf, "r"))== NULL)
                                {
                                        tc_log_warn(MOD_NAME,"Error cannot open the pipe.");
                                        return(TC_IMPORT_ERROR);
                                }
				if(verbose_flag)
					tc_log_info(MOD_NAME, "%s", import_cmd_buf);
                                p_audio=p_audio->p_next;
                        }
			else
			{
				return(TC_IMPORT_ERROR);
			}
                        s_audio_frame_size=fread(param->buffer, 1,param->size, s_fd_audio);
		}
                if (param->size > s_audio_frame_size)
                {
                        s_audio_frame_size_orig=param->size;
                        param->size=s_audio_frame_size;
                }
		return(0);
	}
	if(param->flag == TC_VIDEO)
	{
                if (s_frame_size < s_video_frame_size_orig)
                {
                         s_frame_size=s_video_frame_size_orig;
                         s_video_frame_size_orig=0;
                }
		s_video_frame_size=fread(p_vframe_buffer, 1,s_frame_size, s_fd_video);
		f_mod_video_frame(param,p_video_prev,s_v_codec,0);
		if (s_video_frame_size == 0)
		{
			if (p_video !=NULL)	// is there a file ?
			{
				if(p_video->s_v_codec == TC_CODEC_UNKNOWN)
				{
					if (vob->dv_yuy2_mode == 1)
		    				s_v_codec=TC_CODEC_YUY2;
					else if (vob->dv_yuy2_mode == 0)
		    				s_v_codec=TC_CODEC_YUV420P;
					else
						s_v_codec=vob->im_v_codec;
				}
				else
				{
				    	s_v_codec=p_video->s_v_codec;
				}
				s_v_magic=p_video->s_v_magic;
				switch(s_v_magic)
				{
		   		   case TC_MAGIC_DV_PAL:
		   		   case TC_MAGIC_DV_NTSC:
					switch(s_v_codec)
					{
						case TC_CODEC_RGB24:
							s_frame_size = 3*(p_video->s_v_width * p_video->s_v_height);
							if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                           "%s -i \"%s\" -x dv -d %d -C %ld-%ld |"
                                           " %s -x dv -y rgb -d %d -Q %d",
                                           TCEXTRACT_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video,
                                           TCDECODE_EXE, vob->verbose, vob->quality) < 0)
							{
								tc_log_perror(MOD_NAME, "command buffer overflow");
								return(TC_IMPORT_ERROR);
							}
						break;
						case TC_CODEC_YUY2:
							s_frame_size = (3*(p_video->s_v_width * p_video->s_v_height))/2;
							if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                           "%s -i \"%s\" -x dv -d %d -C %ld-%ld |"
                                           " %s -x dv -y yuv420p -Y -d %d -Q %d",
                                           TCEXTRACT_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video,
                                           TCDECODE_EXE, vob->verbose, vob->quality) < 0)
							{
								tc_log_perror(MOD_NAME, "command buffer overflow");
								return(TC_IMPORT_ERROR);
							}
						break;
						case TC_CODEC_YUV420P:
							s_frame_size = (3*(p_video->s_v_width * p_video->s_v_height))/2;
							if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                           "%s -i \"%s\" -x dv -d %d -C %ld-%ld |"
                                           " %s -x dv -y yuv420p -d %d -Q %d",
                                           TCEXTRACT_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video,
                                           TCDECODE_EXE, vob->verbose, vob->quality) < 0)
							{
								tc_log_perror(MOD_NAME, "command buffer overflow");
								return(TC_IMPORT_ERROR);
							}
						break;
						case TC_CODEC_RAW:
							s_frame_size = (p_video->s_v_height==PAL_H) ? TC_FRAME_DV_PAL:TC_FRAME_DV_NTSC;
							if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                           "%s -i \"%s\" -x dv -d %d -C %ld-%ld",
                                           TCEXTRACT_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video) < 0)
							{
								tc_log_perror(MOD_NAME, "command buffer overflow");
								return(TC_IMPORT_ERROR);
							}
						break;
						default:
							;;
					}
		   		   break;
		   		   case TC_MAGIC_MOV:
					switch(s_v_codec)
					{
						case TC_CODEC_RGB24:
							s_frame_size = (3*(p_video->s_v_width * p_video->s_v_height));
							if (p_video->s_v_real_codec == TC_CODEC_DV)
							{
								if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                               "%s -x mov -i \"%s\" -d %d -C %ld,%ld -Q %d |"
                                               " %s -x dv -y rgb -d %d -Q %d",
                                               TCDECODE_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video, vob->quality,
                                               TCDECODE_EXE, vob->verbose,vob->quality) < 0)
								{
									tc_log_perror(MOD_NAME, "command buffer overflow");
									return(TC_IMPORT_ERROR);
								}
							}
							else
							{
								if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                               "%s -x mov -y rgb -i \"%s\" -d %d -C %ld,%ld -Q %d",
                                               TCDECODE_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video, vob->quality) < 0)
								{
									tc_log_perror(MOD_NAME, "command buffer overflow");
									return(TC_IMPORT_ERROR);
								}
							}
						break;
						case TC_CODEC_YUV420P:
							s_frame_size = (3*(p_video->s_v_width * p_video->s_v_height))/2;
							if (p_video->s_v_real_codec == TC_CODEC_DV)
							{
								if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                               "%s -x mov -i \"%s\" -d %d -C %ld,%ld -Q %d |"
                                               " %s -x dv -y yuv420p -d %d -Q %d",
                                               TCDECODE_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video, vob->quality,
                                               TCDECODE_EXE, vob->verbose,vob->quality) < 0)
								{
									tc_log_perror(MOD_NAME, "command buffer overflow");
									return(TC_IMPORT_ERROR);
								}
							}
							else
							{
								if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                               "%s -x mov -y yuv2 -i \"%s\" -d %d -C %ld,%ld -Q %d",
                                               TCDECODE_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video, vob->quality) < 0)
								{
									tc_log_perror(MOD_NAME, "command buffer overflow");
									return(TC_IMPORT_ERROR);
								}
							}
						break;
						default:
							;;
					}
		   		   break;
		   		   case TC_MAGIC_AVI:
					switch(s_v_codec)
					{
						case TC_CODEC_RGB24:
							s_frame_size = (3*(p_video->s_v_width * p_video->s_v_height));
							if(tc_snprintf(import_cmd_buf, MAX_BUF,
                                           "%s -i \"%s\" -x avi -d %d -C %ld-%ld",
                                           TCEXTRACT_EXE, p_video->p_nome_video,vob->verbose,p_video->s_start_video,p_video->s_end_video) < 0)
							{
								tc_log_perror(MOD_NAME, "command buffer overflow");
								return(TC_IMPORT_ERROR);
							}
						break;
						default:
                        				tc_log_warn(MOD_NAME,"video codec 0x%x not yet supported.",s_v_codec);
							return(TC_IMPORT_ERROR);
							;
					}
		   		   break;
				   default:
                        		tc_log_warn(MOD_NAME,"video magic 0x%lx not yet supported.",s_v_magic);
					return(TC_IMPORT_ERROR);
				}
                       		if((s_fd_video = popen(import_cmd_buf, "r"))== NULL)
                               	{
                                	tc_log_warn(MOD_NAME,"Error cannot open the pipe.");
     		                 	return(TC_IMPORT_ERROR);
               		        }
				param->size=f_calc_frame_size(p_video,s_v_codec);	//setting the frame size
				if(verbose_flag)
					tc_log_info(MOD_NAME,"setting target video size to %d",param->size);
				p_video_prev=p_video;
                       		p_video=p_video->p_next;
				if(verbose_flag)
					tc_log_info(MOD_NAME, "%s", import_cmd_buf);
			}
			else
			{
				return(TC_IMPORT_ERROR);
			}
			s_video_frame_size=fread(p_vframe_buffer, 1,s_frame_size, s_fd_video);	//read the new frame
			f_mod_video_frame(param,p_video_prev,s_v_codec,0);
		}
                if (s_frame_size > s_video_frame_size)
                {
                        s_video_frame_size_orig=s_frame_size;
                        s_frame_size=s_video_frame_size;
                }
		return(0);
	}
	return(TC_IMPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
	if(param->flag == TC_AUDIO)
	{
		s_fd_audio=0;
		param->fd=NULL;
		return(0);
	}
	if(param->flag == TC_VIDEO)
	{
		f_mod_video_frame(NULL,NULL,0,1); //cleanup
		s_fd_video=0;
		param->fd=NULL;
		tcv_free(tcvhandle);
		tcvhandle = 0;
		return(0);
	}
	return(TC_IMPORT_ERROR);
}



