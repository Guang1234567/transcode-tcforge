/*
 * test-tcmoduleinfo.c -- testsuite for tcmoduleinfo* functions; 
 *                        everyone feel free to add more tests and improve
 *                        existing ones.
 * (C) 2006-2010 - Francesco Romani <fromani -at- gmail -dot- com>
 *
 * This file is part of transcode, a video stream processing tool.
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */



#include "src/transcode.h"
#include "libtc/tccodecs.h"
#include "libtcmodule/tcmodule-info.h"

static const TCCodecID empty_codecs[] = { TC_CODEC_ERROR };
static const TCFormatID empty_formats[] = { TC_FORMAT_ERROR };
static TCModuleInfo empty = {
    TC_MODULE_FEATURE_NONE,
    TC_MODULE_FLAG_NONE,
    "",
    "",
    "",
    empty_codecs,
    empty_codecs,
    empty_codecs,
    empty_codecs,
    empty_formats,
    empty_formats
};

static const TCCodecID pass_enc_codecs[] = { TC_CODEC_ANY, TC_CODEC_ERROR };
static TCModuleInfo pass_enc = {
    TC_MODULE_FEATURE_ENCODE | TC_MODULE_FEATURE_VIDEO
        | TC_MODULE_FEATURE_AUDIO | TC_MODULE_FEATURE_EXTRA,
    TC_MODULE_FLAG_RECONFIGURABLE,
    "encode_pass.so",
    "0.0.1 (2005-11-14)",
    "accepts everything, outputs verbatim",
    pass_enc_codecs,
    pass_enc_codecs,
    pass_enc_codecs,
    pass_enc_codecs,
    empty_formats,
    empty_formats
};

static const TCCodecID fake_pcm_codecs[] = { TC_CODEC_PCM, TC_CODEC_ERROR };
static TCModuleInfo fake_wav_mplex = {
    TC_MODULE_FEATURE_MULTIPLEX | TC_MODULE_FEATURE_AUDIO,
    TC_MODULE_FLAG_RECONFIGURABLE,
    "mplex_wav.so",
    "0.0.1 (2006-06-11)",
    "accepts pcm, writes wav (fake!)",
    empty_codecs,
    empty_codecs,
    fake_pcm_codecs,
    empty_codecs,
    empty_formats,
    empty_formats
};

static const TCCodecID fake_yuv_codecs[] = { TC_CODEC_YUV420P, TC_CODEC_ERROR };
static TCModuleInfo fake_y4m_mplex = {
    TC_MODULE_FEATURE_MULTIPLEX | TC_MODULE_FEATURE_VIDEO,
    TC_MODULE_FLAG_RECONFIGURABLE,
    "mplex_y4m.so",
    "0.0.1 (2006-06-11)",
    "accepts yuv420p, writes YUV4MPEG2 (fake!)",
    fake_yuv_codecs,
    empty_codecs,
    empty_codecs,
    empty_codecs,
    empty_formats,
    empty_formats
};


static const TCCodecID fake_mplex_codecs[] = { TC_CODEC_ANY, TC_CODEC_ERROR };
static TCModuleInfo fake_mplex = {
    TC_MODULE_FEATURE_MULTIPLEX | TC_MODULE_FEATURE_VIDEO
        | TC_MODULE_FEATURE_AUDIO | TC_MODULE_FEATURE_EXTRA,
    TC_MODULE_FLAG_RECONFIGURABLE,
    "mplex_null.so",
    "0.0.1 (2005-11-14)",
    "accepts and discards everything",
    fake_mplex_codecs,
    empty_codecs,
    fake_mplex_codecs,
    empty_codecs,
    empty_formats,
    empty_formats
};

static const TCCodecID pcm_pass_codecs[] = { TC_CODEC_PCM, TC_CODEC_ERROR };
static TCModuleInfo pcm_pass = {
    TC_MODULE_FEATURE_ENCODE | TC_MODULE_FEATURE_AUDIO,
    TC_MODULE_FLAG_RECONFIGURABLE,
    "encode_pcm.so",
    "0.0.1 (2006-03-11)",
    "passthrough pcm",
    empty_codecs,
    empty_codecs,
    pcm_pass_codecs,
    pcm_pass_codecs,
    empty_formats,
    empty_formats
};

static const TCCodecID yuv_pass_codecs[] = { TC_CODEC_YUV420P, TC_CODEC_ERROR };
static TCModuleInfo yuv_pass = {
    TC_MODULE_FEATURE_ENCODE | TC_MODULE_FEATURE_VIDEO,
    TC_MODULE_FLAG_RECONFIGURABLE,
    "encode_yuv.so",
    "0.0.1 (2006-03-11)",
    "passthrough yuv",
    yuv_pass_codecs,
    yuv_pass_codecs,
    empty_codecs,
    empty_codecs,
    empty_formats,
    empty_formats
};

static const TCCodecID fake_mpeg_codecs_in[] = { TC_CODEC_YUV420P, TC_CODEC_ERROR };
static const TCCodecID fake_mpeg_codecs_out[] = { TC_CODEC_MPEG1VIDEO, TC_CODEC_MPEG2VIDEO, TC_CODEC_XVID, TC_CODEC_ERROR };
static TCModuleInfo fake_mpeg_enc = {
    TC_MODULE_FEATURE_ENCODE | TC_MODULE_FEATURE_VIDEO,
    TC_MODULE_FLAG_NONE,
    "encode_mpeg.so",
    "0.0.1 (2005-11-14)",
    "fake YUV420P -> MPEG video encoder",
    fake_mpeg_codecs_in,
    fake_mpeg_codecs_out,
    empty_codecs,
    empty_codecs,
    empty_formats,
    empty_formats
};

static const TCCodecID fake_vorbis_codecs_in[] = { TC_CODEC_PCM, TC_CODEC_ERROR };
static const TCCodecID fake_vorbis_codecs_out[] = { TC_CODEC_VORBIS, TC_CODEC_ERROR };
static TCModuleInfo fake_vorbis_enc = {
    TC_MODULE_FEATURE_ENCODE | TC_MODULE_FEATURE_AUDIO,
    TC_MODULE_FLAG_NONE,
    "encode_vorbis.so",
    "0.0.1 (2005-11-14)",
    "fake PCM -> Vorbis audio encoder",
    empty_codecs,
    empty_codecs,
    fake_vorbis_codecs_in,
    fake_vorbis_codecs_out,
    empty_formats,
    empty_formats
};

static const TCCodecID fake_avi_v_codecs_in[] = {
        TC_CODEC_MPEG1VIDEO, TC_CODEC_XVID, TC_CODEC_YUV420P,
        TC_CODEC_ERROR
};
static const TCCodecID fake_avi_a_codecs_in[] = {
        TC_CODEC_MP3, TC_CODEC_PCM,
        TC_CODEC_ERROR
};
static TCModuleInfo fake_avi_mplex = {
    TC_MODULE_FEATURE_MULTIPLEX | TC_MODULE_FEATURE_VIDEO
        | TC_MODULE_FEATURE_AUDIO,
    TC_MODULE_FLAG_NONE,
    "mplex_avi.so",
    "0.0.1 (2005-11-14)",
    "fakes an AVI muxer",
    fake_avi_v_codecs_in,
    empty_codecs,
    fake_avi_a_codecs_in,
    empty_codecs,
    empty_formats,
    empty_formats
 };


static int test_match_helper(int seqno, int codec, int type,
                             const TCModuleInfo *m1,
                             const TCModuleInfo *m2,
                             int expected)
{
    int match = tc_module_info_match(codec, type, m1, m2);
    int err = 0;
#ifdef VERBOSE    
    const char *str = tc_codec_to_string(codec);
    
    tc_log_msg(__FILE__, "codec: %s (0x%x)", str, codec);
#endif
    if (match != expected) {
        tc_log_warn(__FILE__, "#%02i FAILED '%s' <-%c-> '%s'",
                        seqno,
                        m1->name,
                        (expected == 1) ?'-' :'!',
                        m2->name);
        err = 1;
    } else {
        tc_log_info(__FILE__, "#%02i OK    '%s' <-%c-> '%s'",
                        seqno,
                        m1->name,
                        (expected == 1) ?'-' :'!',
                        m2->name);
    }
    return err;
}

static int test_module_match(int *total)
{
    int i = 1, errors = 0;

    errors += test_match_helper(i++, TC_CODEC_ANY, TC_AUDIO, &empty, &empty, 0);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_VIDEO, &empty, &empty, 0);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_AUDIO, &empty, &fake_mpeg_enc, 0);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_VIDEO, &empty, &fake_mpeg_enc, 0);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_AUDIO, &fake_mpeg_enc, &empty, 0);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_VIDEO, &fake_mpeg_enc, &empty, 0);

    errors += test_match_helper(i++, TC_CODEC_ANY, TC_AUDIO, &pass_enc, &fake_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_VIDEO, &pass_enc, &fake_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_AUDIO, &pass_enc, &fake_avi_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_VIDEO, &pass_enc, &fake_avi_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_AUDIO, &pcm_pass, &fake_avi_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_PCM, TC_AUDIO, &pass_enc, &fake_avi_mplex, 1);
 
//  this is tricky. Should fail since there are two *encoders* chained
//  and this make no sense *in our current architecture*.
//  but from tcmoduleinfo infrastructure POV, it make perfectly sense (yet)
//  since encoders involved have compatible I/O capabilities, so it doesn't fail.
//    errors += test_match_helper(i++, TC_CODEC_ANY, &pass_enc, &fake_mpeg_enc, 0);

    errors += test_match_helper(i++, TC_CODEC_MPEG2VIDEO, TC_AUDIO, &fake_mpeg_enc, &fake_vorbis_enc, 0);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_VIDEO, &fake_mpeg_enc, &fake_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_AUDIO, &fake_mpeg_enc, &fake_mplex, 0);
    errors += test_match_helper(i++, TC_CODEC_MPEG1VIDEO, TC_VIDEO, &fake_mpeg_enc, &fake_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_ANY, TC_VIDEO, &fake_mpeg_enc, &fake_avi_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_MPEG1VIDEO, TC_VIDEO, &fake_mpeg_enc, &fake_avi_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_XVID, TC_VIDEO, &fake_mpeg_enc, &fake_avi_mplex, 1);

    errors += test_match_helper(i++, TC_CODEC_VORBIS, TC_AUDIO, &fake_vorbis_enc, &fake_mpeg_enc, 0);
    errors += test_match_helper(i++, TC_CODEC_VORBIS, TC_AUDIO, &fake_vorbis_enc, &fake_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_VORBIS, TC_AUDIO, &fake_vorbis_enc, &fake_avi_mplex, 0);

    errors += test_match_helper(i++, TC_CODEC_PCM, TC_AUDIO, &pcm_pass, &fake_wav_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_PCM, TC_AUDIO, &pcm_pass, &fake_y4m_mplex, 0);
    errors += test_match_helper(i++, TC_CODEC_PCM, TC_VIDEO, &pcm_pass, &fake_y4m_mplex, 0);
    errors += test_match_helper(i++, TC_CODEC_MPEG1VIDEO, TC_VIDEO, &fake_mpeg_enc, &fake_wav_mplex, 0);

    errors += test_match_helper(i++, TC_CODEC_YUV420P, TC_VIDEO, &yuv_pass, &fake_y4m_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_YUV420P, TC_VIDEO, &yuv_pass, &fake_wav_mplex, 0);
    errors += test_match_helper(i++, TC_CODEC_YUV420P, TC_VIDEO, &yuv_pass, &fake_avi_mplex, 1);
    errors += test_match_helper(i++, TC_CODEC_YUV420P, TC_VIDEO, &yuv_pass, &fake_mplex, 1);

    if (total) {
        *total = i;
    }
    return errors;
}

int main(int argc, char *argv[])
{
    int errors = 0, total = 0;;
    
    libtc_init(&argc, &argv);

    errors = test_module_match(&total);

    putchar('\n');
    tc_log_info(__FILE__, "test summary: %i test%s %i error%s (%s)",
                total,
                (total > 1) ?"s" :"",
                errors,
                (errors > 1) ?"s" :"",
                (errors > 0) ?"FAILED" :"PASSED");
    return (errors > 0) ?1 :0;
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
