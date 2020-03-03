/*
 *  ioxml.c
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

#include "src/transcode.h"
#include "libtc/libtc.h"

#ifdef HAVE_LIBXML2

#include "tcinfo.h"
#include "magic.h"
#include "tc.h"
#include "ioxml.h"

#define AUDIO_VIDEO_UNKNOWN 0x00
#define AUDIO_ITEM      0x01
#define VIDEO_ITEM      0x02
#define UNSUPPORTED_PARAM   0x00
#define IN_VIDEO_CODEC      0x01
#define IN_AUDIO_CODEC      0x02
#define IN_VIDEO_MAGIC      0x03
#define IN_AUDIO_MAGIC      0x04
#define OUT_VIDEO_HEIGHT    0x05
#define OUT_VIDEO_WIDTH     0x06
#define OUT_VIDEO_RES_FILTER    0x07


audiovideo_limit_t f_det_time(char *p_options)
{
   char *p_data,*p_temp,*p_temp1;
   double s_hh,s_mm,s_ss,s_app;
   audiovideo_limit_t   s_limit;

    s_hh=s_mm=s_ss=0;
    s_limit.s_frame=0;
    if (strcasecmp(p_options,"smpte") == 0)
        s_limit.s_smpte=smpte;
    else if (strcasecmp(p_options,"smpte-25") == 0)
        s_limit.s_smpte=smpte25;
    else if (strcasecmp(p_options,"smpte-30-drop") == 0)
        s_limit.s_smpte=smpte30drop;
    else if (strcasecmp(p_options,"npt") == 0)
        s_limit.s_smpte=npt;
    else
        s_limit.s_smpte=npt;
    p_data=strstr(p_options,"=");
    if ((p_data != NULL) || (s_limit.s_smpte == npt))
    {
        if (p_data != NULL)
            p_temp1=p_data+1;
        else
            p_temp1=p_options;
        if (strstr(p_temp1,":") != NULL)
        {
            if ((p_temp = strtok(p_temp1,":")) != NULL)
                s_hh=strtod(p_temp,NULL);
            if ((p_temp = strtok(NULL,":")) != NULL)
                s_mm=strtod(p_temp,NULL);
            if ((p_temp = strtok(NULL,":")) != NULL)
                s_ss=strtod(p_temp,NULL);
            if ((p_temp = strtok(NULL,":")) != NULL)
                s_limit.s_frame=strtod(p_temp,NULL);
            s_limit.s_time=3600*s_hh+60*s_mm+s_ss;
        }
        else
        {
            s_app=strtod(p_temp1,NULL);
            switch (*(p_temp1+strlen(p_temp1)-1))   //find the last char of temp1
            {
                case 'h':
                    s_app*=60;
                case 'm':
                    s_app*=60;
                case 's':
                    s_limit.s_time=(long)s_app;
                    s_limit.s_frame=0;
                break;
                default:
                    s_limit.s_time=0;
                    s_limit.s_frame=(long)s_app;
                    ;;
            }
        }
    }
    else
    {
        tc_log_warn(__FILE__,"Invalid parameter %s force default",p_options);
        s_limit.s_time=-1;
        s_limit.s_frame=0;
    }
    return(s_limit);
}


void f_free_tree(audiovideo_t *p_node)
{
    if (p_node) {
        audiovideo_t *p_temp = NULL , *p_temp1 = NULL;
        for (p_temp=p_node->p_next;p_temp !=NULL;)  {
            //first element is static
            p_temp1 = p_temp->p_next;
            free(p_temp);
            p_temp=p_temp1;
        }
    }
}

int f_parse_tree(xmlNodePtr p_node,audiovideo_t *p_audiovideo)
{
    static audiovideo_t *p_temp;
    audiovideo_limit_t s_limit;
    static int s_type,s_param;
    int s_rc;
    static int s_video_codec=TC_CODEC_UNKNOWN,s_audio_codec=TC_CODEC_UNKNOWN;

    s_rc=0;
    if (p_node != NULL)
    {
            if (xmlStrcmp(p_node->name, (const xmlChar*)"smil") == 0)
        {
                if(f_parse_tree(p_node->xmlChildrenNode,p_audiovideo))
                s_rc=1;
            }
            else if (xmlStrcmp(p_node->name, (const xmlChar*)"seq") == 0)
        {
            s_type=AUDIO_VIDEO_UNKNOWN;
            p_temp=tc_zalloc(sizeof(audiovideo_t));
            p_temp->s_end_a_time=-1;
            p_temp->s_end_v_time=-1;
            p_temp->s_start_a_time=-1;
            p_temp->s_start_v_time=-1;
            p_temp->s_end_audio=-1;
            p_temp->s_end_video=-1;
            p_temp->s_start_audio=-1;
            p_temp->s_start_video=-1;
            p_temp->s_video_smpte=npt;  //force npt
            p_temp->s_audio_smpte=npt;  //force npt
            p_temp->s_a_real_codec=TC_CODEC_UNKNOWN;
            p_temp->s_v_real_codec=TC_CODEC_UNKNOWN;
            p_temp->s_a_codec=TC_CODEC_UNKNOWN;
            p_temp->s_v_codec=TC_CODEC_UNKNOWN;
            p_temp->s_a_magic=TC_MAGIC_UNKNOWN;
            p_temp->s_v_magic=TC_MAGIC_UNKNOWN;
            p_temp->s_a_rate=0;
                        p_temp->s_a_bits=0;
                        p_temp->s_a_chan=0;
                        p_temp->s_v_width=0;
                        p_temp->s_v_height=0;
                        p_temp->s_v_tg_width=0;
                        p_temp->s_v_tg_height=0;
            p_temp->s_v_tg_width=0;     //target width
            p_temp->s_v_tg_height=0;    //target height
            if(p_audiovideo == NULL)
                p_audiovideo=p_temp;
            else
                p_audiovideo->p_next=p_temp;
                if(f_parse_tree(p_node->xmlChildrenNode,p_temp)) //visit the branch
                s_rc=1;
                if(f_parse_tree(p_node->next,p_temp))       //eventually go to the next seq item
                s_rc=1;
            }
        else if (xmlStrcmp(p_node->name, (const xmlChar*)"video") == 0)
        {
            if (s_type!=AUDIO_VIDEO_UNKNOWN)
            {
                p_temp=tc_zalloc(sizeof(audiovideo_t));
                p_temp->s_end_a_time=-1;
                p_temp->s_end_v_time=-1;
                p_temp->s_start_a_time=-1;
                p_temp->s_start_v_time=-1;
                p_temp->s_end_audio=-1;
                p_temp->s_end_video=-1;
                p_temp->s_start_audio=-1;
                p_temp->s_start_video=-1;
                p_temp->s_video_smpte=npt;  //force npt
                p_temp->s_audio_smpte=npt;  //force npt
                p_temp->s_a_codec=TC_CODEC_UNKNOWN;
                p_temp->s_v_codec=TC_CODEC_UNKNOWN;
                p_temp->s_a_real_codec=TC_CODEC_UNKNOWN;
                p_temp->s_v_real_codec=TC_CODEC_UNKNOWN;
                p_temp->s_a_magic=TC_MAGIC_UNKNOWN;
                p_temp->s_v_magic=TC_MAGIC_UNKNOWN;
                p_temp->s_a_rate=0;
                p_temp->s_a_bits=0;
                p_temp->s_a_chan=0;
                p_temp->s_v_width=0;
                p_temp->s_v_height=0;
                p_temp->s_v_tg_width=0;     //target width
                p_temp->s_v_tg_height=0;    //target height
                if(p_audiovideo != NULL)
                    p_audiovideo->p_next=p_temp;
                p_audiovideo=p_temp;
            }
            s_type=VIDEO_ITEM;  //set origin to video
                if(f_parse_tree((xmlNodePtr)p_node->properties,p_audiovideo)) //visit the properties
                s_rc=1;
                if(f_parse_tree(p_node->xmlChildrenNode,p_audiovideo)) //visit the branch
                s_rc=1;
                if(f_parse_tree(p_node->next,p_audiovideo))     //eventually go to the next audio/video item
                s_rc=1;
            }
        else if (xmlStrcmp(p_node->name, (const xmlChar*)"audio") == 0)
        {
            if (s_type!=AUDIO_VIDEO_UNKNOWN)
            {
                p_temp=tc_zalloc(sizeof(audiovideo_t));
                p_temp->s_end_a_time=-1;
                p_temp->s_end_v_time=-1;
                p_temp->s_start_a_time=-1;
                p_temp->s_start_v_time=-1;
                p_temp->s_end_audio=-1;
                p_temp->s_end_video=-1;
                p_temp->s_start_audio=-1;
                p_temp->s_start_video=-1;
                p_temp->s_video_smpte=npt;  //force npt
                p_temp->s_audio_smpte=npt;  //force npt
                p_temp->s_a_codec=TC_CODEC_UNKNOWN;
                p_temp->s_v_codec=TC_CODEC_UNKNOWN;
                p_temp->s_a_real_codec=TC_CODEC_UNKNOWN;
                p_temp->s_v_real_codec=TC_CODEC_UNKNOWN;
                p_temp->s_a_magic=TC_MAGIC_UNKNOWN;
                p_temp->s_v_magic=TC_MAGIC_UNKNOWN;
                p_temp->s_a_rate=0;
                p_temp->s_a_bits=0;
                p_temp->s_a_chan=0;
                p_temp->s_v_width=0;
                p_temp->s_v_height=0;
                p_temp->s_v_tg_width=0;     //target width
                p_temp->s_v_tg_height=0;    //target height
                if(p_audiovideo != NULL)
                    p_audiovideo->p_next=p_temp;
                p_audiovideo=p_temp;
            }
            s_type=AUDIO_ITEM;  //set origin to audio
                if(f_parse_tree((xmlNodePtr)p_node->properties,p_audiovideo)) //visit the properties
                s_rc=1;
                if(f_parse_tree(p_node->xmlChildrenNode,p_audiovideo)) //visit the branch
                s_rc=1;
                if(f_parse_tree(p_node->next,p_audiovideo))     //eventually go to the next audio/video item
                s_rc=1;
            }
        else if (xmlStrcmp(p_node->name, (const xmlChar*)"param") == 0)
        {
                if(f_parse_tree((xmlNodePtr)p_node->properties,p_audiovideo)) //visit the properties
                s_rc=1;
                if(f_parse_tree(p_node->xmlChildrenNode,p_audiovideo)) //visit the branch
                s_rc=1;
                if(f_parse_tree(p_node->next,p_audiovideo))     //eventually go to the next audio/video item
                s_rc=1;
            }
        else if (xmlStrcmp(p_node->name, (const xmlChar*)"src") == 0)
        {
            if (s_type==AUDIO_ITEM)
            {
                p_audiovideo->p_nome_audio=p_node->xmlChildrenNode->content;        //set the audio file name
            }
            else
            {
                p_audiovideo->p_nome_video=p_node->xmlChildrenNode->content;        //set the video file name
            }
                if(f_parse_tree(p_node->next,p_audiovideo))     //goto to begin and end of clip
                s_rc=1;
            }
        else if (xmlStrcmp(p_node->name, (const xmlChar*)"clipBegin") == 0)
        {
            s_limit=f_det_time((char *)p_node->xmlChildrenNode->content);
            if (s_type==AUDIO_ITEM)
            {
                p_audiovideo->s_audio_smpte=s_limit.s_smpte;
                p_audiovideo->s_start_a_time=s_limit.s_time;
                p_audiovideo->s_start_audio=s_limit.s_frame;
            }
            else
            {
                p_audiovideo->s_video_smpte=s_limit.s_smpte;
                p_audiovideo->s_start_v_time=s_limit.s_time;
                p_audiovideo->s_start_video=s_limit.s_frame;
            }
                if(f_parse_tree(p_node->next,p_audiovideo))     //goto the next param.
                s_rc=1;
            }
        else if (xmlStrcmp(p_node->name, (const xmlChar*)"clipEnd") == 0)
        {
            s_limit=f_det_time((char *)p_node->xmlChildrenNode->content);
            if (s_type==AUDIO_ITEM)
            {
                p_audiovideo->s_audio_smpte=s_limit.s_smpte;
                p_audiovideo->s_end_a_time=s_limit.s_time;
                p_audiovideo->s_end_audio=s_limit.s_frame+1;
            }
            else
            {
                p_audiovideo->s_video_smpte=s_limit.s_smpte;
                p_audiovideo->s_end_v_time=s_limit.s_time;
                p_audiovideo->s_end_video=s_limit.s_frame+1;
            }
                if(f_parse_tree(p_node->next,p_audiovideo))     //goto the next param.
                s_rc=1;
            }
        else if (xmlStrcmp(p_node->name, (const xmlChar*)"name") == 0)
        {
            if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"in-video-module") == 0)
                s_param=IN_VIDEO_MAGIC;
            else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"in-audio-module") == 0)
                s_param=IN_AUDIO_MAGIC;
            else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"in-video-codec") == 0)
                s_param=IN_VIDEO_CODEC;
            else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"in-audio-codec") == 0)
                s_param=IN_AUDIO_CODEC;
            else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"target-height") == 0)
                s_param=OUT_VIDEO_HEIGHT;
            else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"target-width") == 0)
                s_param=OUT_VIDEO_WIDTH;
            else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"resize-filter") == 0)
                s_param=OUT_VIDEO_RES_FILTER;
            else
                s_param=UNSUPPORTED_PARAM;

                if(f_parse_tree(p_node->next,p_audiovideo))     //goto the next param.
                s_rc=1;
            }
        else if (xmlStrcmp(p_node->name, (const xmlChar*)"value") == 0)
        {
            if ((s_type==AUDIO_ITEM) && ((s_param==IN_VIDEO_CODEC)||(s_param==IN_VIDEO_MAGIC)||(s_param==OUT_VIDEO_HEIGHT)||(s_param==OUT_VIDEO_WIDTH)))
            {
                if (s_param==OUT_VIDEO_HEIGHT)
                    tc_log_warn(__FILE__,"The target-height parameter cannot be used in audio item, %s skipped.",(char *)p_node->xmlChildrenNode->content);
                else if (s_param==OUT_VIDEO_WIDTH)
                    tc_log_warn(__FILE__,"The target-width parameter cannot be used in audio item, %s skipped.",(char *)p_node->xmlChildrenNode->content);
                else if (s_param==IN_VIDEO_MAGIC)
                    tc_log_warn(__FILE__,"The in-video-module parameter cannot be used in audio item, %s skipped.",(char *)p_node->xmlChildrenNode->content);
                else if (s_param==IN_VIDEO_CODEC)
                    tc_log_warn(__FILE__,"The in-video-codec parameter cannot be used in audio item, %s skipped.",(char *)p_node->xmlChildrenNode->content);
                s_rc=1;
            }
            else
            {
                switch(s_param)
                {
                    case OUT_VIDEO_RES_FILTER:
                        p_audiovideo->p_v_resize_filter=p_node->xmlChildrenNode->content;
                    break;
                    case OUT_VIDEO_HEIGHT:
                        p_audiovideo->s_v_tg_height=atoi((char *)p_node->xmlChildrenNode->content);
                    break;
                    case OUT_VIDEO_WIDTH:
                        p_audiovideo->s_v_tg_width=atoi((char *)p_node->xmlChildrenNode->content);
                    break;
                    case IN_VIDEO_MAGIC:
                        if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"dv") == 0)
                            p_audiovideo->s_v_magic=TC_MAGIC_DV_PAL;    //the same for PAL and NTSC
                        else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"avi") == 0)
                            p_audiovideo->s_v_magic=TC_MAGIC_AVI;
                        else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"mov") == 0)
                            p_audiovideo->s_v_magic=TC_MAGIC_AVI;
                        else
                        {
                            tc_log_warn(__FILE__,"The in-video-magic %s parameter isn't yet supported.",(char *)p_node->xmlChildrenNode->content);
                            s_rc=1;
                        }
                    break;
                    case IN_AUDIO_MAGIC:
                        if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"dv") == 0)
                            p_audiovideo->s_a_magic=TC_MAGIC_DV_PAL;    //the same for PAL and NTSC
                        else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"avi") == 0)
                            p_audiovideo->s_a_magic=TC_MAGIC_AVI;
                        else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"mov") == 0)
                            p_audiovideo->s_a_magic=TC_MAGIC_AVI;
                        else
                        {
                            tc_log_warn(__FILE__,"The in-audio-magic %s parameter isn't yet supported.",(char *)p_node->xmlChildrenNode->content);
                            s_rc=1;
                        }
                    break;
                    case IN_VIDEO_CODEC:
                        if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"rgb") == 0)
                            p_audiovideo->s_v_codec=TC_CODEC_RGB24;
                        else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"yuv2") == 0)
                            p_audiovideo->s_v_codec=TC_CODEC_YUV420P;
                        else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"yuv420p") == 0)
                            p_audiovideo->s_v_codec=TC_CODEC_YUV420P;
                        else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"yv12") == 0)
                            p_audiovideo->s_v_codec=TC_CODEC_YUV420P; //  What?!?
                        else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"yuy2") == 0)
                            p_audiovideo->s_v_codec=TC_CODEC_YUY2;
                        else if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"raw") == 0)
                            p_audiovideo->s_v_codec=TC_CODEC_RAW;
                        else
                        {
                            tc_log_warn(__FILE__,"The in-video-codec %s parameter isn't yet supported.",(char *)p_node->xmlChildrenNode->content);
                            s_rc=1;
                        }
                        if (s_video_codec == TC_CODEC_UNKNOWN)
                            s_video_codec=p_audiovideo->s_v_codec;
                        else if (s_video_codec != p_audiovideo->s_v_codec)
                        {
                            tc_log_warn(__FILE__,"The XML file must contain the same video codec.");
                            s_rc=1;
                        }
                    break;
                    case IN_AUDIO_CODEC:
                        if (xmlStrcmp((char *)p_node->xmlChildrenNode->content, (const xmlChar*)"pcm") == 0)
                            p_audiovideo->s_a_codec=TC_CODEC_PCM;
                        else
                        {
                            tc_log_warn(__FILE__,"The in-audio-codec %s parameter isn't yet supported.",(char *)p_node->xmlChildrenNode->content);
                            s_rc=1;
                        }
                        if (s_audio_codec == TC_CODEC_UNKNOWN)
                            s_audio_codec=p_audiovideo->s_a_codec;
                        else if (s_audio_codec != p_audiovideo->s_a_codec)
                        {
                            tc_log_warn(__FILE__,"The XML file must contain the same audio codec.");
                            s_rc=1;
                        }
                    break;

                    case UNSUPPORTED_PARAM:
                        tc_log_warn(__FILE__,"The %s parameter isn't yet supported.",(char *)p_node->xmlChildrenNode->content);
                        s_rc=1;
                    break;
                }
            }

                if(f_parse_tree(p_node->next,p_audiovideo))     //goto the next param.
                s_rc=1;
            }
    }
    return(s_rc);
}

void f_delete_unused_node(xmlNodePtr p_node)
{
    while (p_node != NULL)
    {
            xmlNodePtr p_kill_node = NULL;

            f_delete_unused_node(p_node->xmlChildrenNode);
            if (xmlStrcmp(p_node->name, (const xmlChar*)"smil") == 0)
        {
                    //leave it in the tree
            }
            else if (xmlStrcmp(p_node->name, (const xmlChar*)"seq") == 0)
        {
                if (p_node->xmlChildrenNode == NULL)
            {
                //This node don't have any video sections so i can kill it
                        p_kill_node = p_node;
                    }
            }
        else if ((xmlStrcmp(p_node->name, (const xmlChar*)"param") == 0) ||(xmlStrcmp(p_node->name, (const xmlChar*)"video") == 0) || (xmlStrcmp(p_node->name, (const xmlChar*)"audio") == 0))
        {
                    //leave it in the tree
            }
            else
        {
            //not an interesting node
                p_kill_node = p_node;
        }
            p_node = p_node->next;

            if (p_kill_node != NULL)
        {
            //kill the node from the tree
                xmlUnlinkNode(p_kill_node);
                xmlFreeNode(p_kill_node);
            }
    }
}


int f_complete_tree(audiovideo_t *p_audiovideo)
{
    audiovideo_t *p_temp;
    int s_video_codec=TC_CODEC_UNKNOWN,s_audio_codec=TC_CODEC_UNKNOWN;

    for (p_temp=p_audiovideo->p_next;p_temp != NULL;p_temp=p_temp->p_next)
    {
        if (p_temp->p_nome_video != NULL)
        {
            if (p_temp->s_start_v_time == -1)
            {
                p_temp->s_start_video=0;
                p_temp->s_start_v_time=0;
            }
            if (p_temp->s_end_v_time == -1)
            {
                p_temp->s_end_video=LONG_MAX;
                p_temp->s_end_v_time=0;
            }
            if (p_audiovideo->s_v_codec != TC_CODEC_UNKNOWN)
            {
                if ((s_video_codec!=TC_CODEC_UNKNOWN) && (p_audiovideo->s_v_codec != s_video_codec))
                {
                    tc_log_error(__FILE__,"The file must contain the same video codec (found 0x%lx but 0x%x is already defined)",p_audiovideo->s_v_codec,s_video_codec);
                    return(1);
                }
                s_video_codec=p_audiovideo->s_v_codec;
            }
        }
        if (p_temp->p_nome_audio != NULL)
        {
            if (p_temp->s_start_a_time == -1)
            {
                p_temp->s_start_audio=0;
                p_temp->s_start_a_time=0;
            }
            if (p_temp->s_end_a_time == -1)
            {
                p_temp->s_end_audio=LONG_MAX;
                p_temp->s_end_a_time=0;
            }
            if (p_audiovideo->s_a_codec != TC_CODEC_UNKNOWN)
            {
                if ((s_audio_codec!=TC_CODEC_UNKNOWN) && (p_audiovideo->s_a_codec != s_audio_codec))
                {
                    tc_log_error(__FILE__,"The file must contain the same audio codec (found 0x%lx but 0x%x is already defined)",p_audiovideo->s_a_codec,s_audio_codec);
                    return(1);
                }
                s_audio_codec=p_audiovideo->s_a_codec;
            }
        }
        else
        {
            p_temp->p_nome_audio=p_temp->p_nome_video;      //force audio to has the same input file of video
            p_temp->s_start_audio=p_temp->s_start_video;
            p_temp->s_end_audio=p_temp->s_end_video;
            p_temp->s_end_a_time=p_temp->s_end_v_time;
            p_temp->s_start_a_time=p_temp->s_start_v_time;

        }
    }
    for (p_temp=p_audiovideo->p_next;p_temp != NULL;p_temp=p_temp->p_next) //initialize all unset codec
    {
        if (p_temp->p_nome_video != NULL)
            p_audiovideo->s_v_codec=s_video_codec;
        if (p_temp->p_nome_audio != NULL)
            p_audiovideo->s_a_codec=s_audio_codec;
    }
    return(0);

}
int f_manage_input_xml(const char *p_name,int s_type,audiovideo_t *p_audiovideo)
{
    static xmlDocPtr p_doc;
    xmlNodePtr p_node;
    xmlNsPtr ns;

    if (s_type)     //read the file from p_name
    {
        p_doc = xmlParseFile(p_name);
        p_node = xmlDocGetRootElement(p_doc);
        if (p_node == NULL)
        {
            xmlFreeDoc(p_doc);
            tc_log_error(__FILE__,"Invalid file format");
            return(-1);
        }
        ns = xmlSearchNsByHref(p_doc, p_node, (const xmlChar *) "http://www.w3.org/2001/SMIL20/Language");
        if (ns == NULL)
        {
            xmlFreeDoc(p_doc);
            tc_log_error(__FILE__,"Invalid Namespace");
            return(-1);
        }
        ns = xmlSearchNs(p_doc, p_node, (const xmlChar *) "smil2");
        if (ns == NULL)
        {
            xmlFreeDoc(p_doc);
            tc_log_error(__FILE__,"Invalid Namespace");
            return(-1);
        }
        if (xmlStrcmp(p_node->name, (const xmlChar *) "smil"))
        {
            xmlFreeDoc(p_doc);
            tc_log_error(__FILE__,"Invalid Namespace");
            return(-1);
        }
        f_delete_unused_node(p_node);
        memset(p_audiovideo,'\0',sizeof(audiovideo_t));
        if(f_parse_tree(p_node,p_audiovideo))
            return(1);
        if (f_complete_tree(p_audiovideo))
            return(1);
    }
    else
    {
            f_free_tree(p_audiovideo);
            xmlFreeDoc(p_doc);
    }
    return(0);
}


#endif
