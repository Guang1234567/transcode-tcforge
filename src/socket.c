/*
 * socket.c -- routines for controlling transcode over a socket
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */


#include "transcode.h"
#include "filter.h"
#include "socket.h"
#include "libtcexport/export.h"
#include "libtc/libtc.h"
#include "libtcutil/tcthread.h"

#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/*************************************************************************/

/* Pathname for listener socket */
static char socket_path[PATH_MAX+1] = "";
/* Socket descriptors for listener socket and client socket */
static int server_sock = -1;
static int client_sock = -1;

/* For communicating with "pv" module (FIXME: should go away) */
static TCMutex tc_socket_msg_lock;
static TCSockPVCmd sock_cmd = { .arg = 0 };

/*************************************************************************/
/*************************************************************************/

/**
 * sendall: Send data to a client_sock, handling partial writes transparently.
 *
 * Parameters:
 *      sock: Socket descriptor.
 *       buf: Data to write.
 *     count: Number of bytes to send.
 * Return value:
 *     Total number of bytes sent; -1 indicates that the send failed.
 */

static int sendall(int sock, const void *buf, size_t count)
{
    int total_sent = 0;

    if (sock < 0 || !buf || count < 0) {
        tc_log_warn(__FILE__, "sendall(): invalid parameters!");
        errno = EINVAL;
        return -1;
    }
    while (count > 0) {
        int retval = send(sock, buf, count, 0);
        if (retval <= 0) {
            tc_log_warn(__FILE__, "sendall(): socket write failed (%s)",
                        retval<0 ? strerror(errno) : "Connection closed");
            if (total_sent == 0)
                return -1;  /* Nothing sent yet, abort with error return */
            break;
        }
        total_sent += retval;
        buf = (int8_t *)buf + retval;
        count -= retval;
    }
    return total_sent;
}

/*************************************************************************/

/**
 * sendstr: Send a string to a client_sock, handling partial writes transparently.
 *
 * Parameters:
 *      sock: Socket descriptor.
 *       str: String to write.
 * Return value:
 *     Total number of bytes sent; -1 indicates that the send failed.
 */

static int sendstr(int sock, const char *str)
{
    return sendall(sock, str, strlen(str));
}

/*************************************************************************/

/**
 * WRITEME
 */

static void dump_processing(int sock)
{
    uint32_t dropped = 0, encoded = 0;
    int im = 0, fl = 0, ex = 0;
    char buf[TC_BUF_LINE];
    int n;

    dropped = tc_get_frames_dropped();
    encoded = tc_get_frames_encoded();
    tc_framebuffer_get_counters(&im, &fl, &ex);

    n = tc_snprintf(buf, sizeof(buf),
                    "E=%lu|D=%lu|im=%i|fl=%i|ex=%i",
                    encoded, dropped, im, fl, ex);
    if (n > 0)
        sendall(sock, buf, n);
}

/*************************************************************************/

/**
 * dump_vob:  Send the contents of the global `vob' structure over the
 * socket in a "parameter=value" format, one field per line.
 *
 * Parameters:
 *     sock: Socket descriptor to send data to.
 * Return value:
 *     None.
 */

static void dump_vob(int sock)
{
    vob_t *vob = tc_get_vob();
    char buf[TC_BUF_MAX];
    int n;

#define SEND(field,fmt) \
    n = tc_snprintf(buf, sizeof(buf), "%s=" fmt "\n", #field, vob->field); \
    if (n > 0) \
        sendall(sock, buf, n);

    /* Generated via find-and-replace from vob_t definition in transcode.h */
    SEND(vmod_probed, "%s");
    SEND(amod_probed, "%s");
    SEND(vmod_probed_xml, "%s");
    SEND(amod_probed_xml, "%s");
    SEND(verbose, "%d");
    SEND(video_in_file, "%s");
    SEND(audio_in_file, "%s");
    SEND(nav_seek_file, "%s");
    SEND(has_audio, "%d");
    SEND(has_audio_track, "%d");
    SEND(has_video, "%d");
    SEND(lang_code, "%d");
    SEND(a_track, "%d");
    SEND(v_track, "%d");
    SEND(s_track, "%d");
    SEND(sync, "%d");
    SEND(sync_ms, "%d");
    SEND(dvd_title, "%d");
    SEND(dvd_chapter1, "%d");
    SEND(dvd_chapter2, "%d");
    SEND(dvd_max_chapters, "%d");
    SEND(dvd_angle, "%d");
    SEND(ps_unit, "%d");
    SEND(ps_seq1, "%d");
    SEND(ps_seq2, "%d");
    SEND(ts_pid1, "%d");
    SEND(ts_pid2, "%d");
    SEND(vob_offset, "%d");
    SEND(vob_chunk, "%d");
    SEND(vob_chunk_num1, "%d");
    SEND(vob_chunk_num2, "%d");
    SEND(vob_chunk_max, "%d");
    SEND(vob_percentage, "%d");
    SEND(vob_psu_num1, "%d");
    SEND(vob_psu_num2, "%d");
    SEND(vob_info_file, "%s");
    SEND(pts_start, "%f");
    SEND(psu_offset, "%f");
    SEND(demuxer, "%d");
    SEND(v_format_flag, "%ld");
    SEND(v_codec_flag, "%ld");
    SEND(a_format_flag, "%ld");
    SEND(a_codec_flag, "%ld");
    SEND(quality, "%d");
    SEND(a_stream_bitrate, "%d");
    SEND(a_chan, "%d");
    SEND(a_bits, "%d");
    SEND(a_rate, "%d");
    SEND(a_padrate, "%d");
    SEND(im_a_size, "%d");
    SEND(ex_a_size, "%d");
    SEND(im_a_codec, "%d");
    SEND(a_leap_frame, "%d");
    SEND(a_leap_bytes, "%d");
    SEND(a_vbr, "%d");
    SEND(a52_mode, "%d");
    SEND(dm_bits, "%d");
    SEND(dm_chan, "%d");
    SEND(v_stream_bitrate, "%d");
    SEND(fps, "%f");
    SEND(im_frc, "%d");
    SEND(ex_fps, "%f");
    SEND(ex_frc, "%d");
    SEND(hard_fps_flag, "%d");
    SEND(pulldown, "%d");
    SEND(im_v_height, "%d");
    SEND(im_v_width, "%d");
    SEND(im_v_size, "%d");
    SEND(im_asr, "%d");
    SEND(im_par, "%d");
    SEND(im_par_width, "%d");
    SEND(im_par_height, "%d");
    SEND(ex_asr, "%d");
    SEND(ex_par, "%d");
    SEND(ex_par_width, "%d");
    SEND(ex_par_height, "%d");
    SEND(attributes, "%d");
    SEND(im_v_codec, "%d");
    SEND(encode_fields, "%d");
    SEND(dv_yuy2_mode, "%d");
    SEND(volume, "%f");
    SEND(ac3_gain[0], "%f");
    SEND(ac3_gain[1], "%f");
    SEND(ac3_gain[2], "%f");
    SEND(clip_count, "%d");
    SEND(ex_v_width, "%d");
    SEND(ex_v_height, "%d");
    SEND(ex_v_size, "%d");
    SEND(reduce_h, "%d");
    SEND(reduce_w, "%d");
    SEND(resize1_mult, "%d");
    SEND(vert_resize1, "%d");
    SEND(hori_resize1, "%d");
    SEND(resize2_mult, "%d");
    SEND(vert_resize2, "%d");
    SEND(hori_resize2, "%d");
    SEND(zoom_width, "%d");
    SEND(zoom_height, "%d");
    SEND(zoom_interlaced, "%d");
    SEND(zoom_filter, "%d");
    SEND(antialias, "%d");
    SEND(deinterlace, "%d");
    SEND(decolor, "%d");
    SEND(aa_weight, "%f");
    SEND(aa_bias, "%f");
    SEND(gamma, "%f");
    SEND(ex_clip_top, "%d");
    SEND(ex_clip_bottom, "%d");
    SEND(ex_clip_left, "%d");
    SEND(ex_clip_right, "%d");
    SEND(im_clip_top, "%d");
    SEND(im_clip_bottom, "%d");
    SEND(im_clip_left, "%d");
    SEND(im_clip_right, "%d");
    SEND(post_ex_clip_top, "%d");
    SEND(post_ex_clip_bottom, "%d");
    SEND(post_ex_clip_left, "%d");
    SEND(post_ex_clip_right, "%d");
    SEND(pre_im_clip_top, "%d");
    SEND(pre_im_clip_bottom, "%d");
    SEND(pre_im_clip_left, "%d");
    SEND(pre_im_clip_right, "%d");
    SEND(video_out_file, "%s");
    SEND(audio_out_file, "%s");
    SEND(avifile_in, "%p");  // not sure if there's any point sending these...
    SEND(avifile_out, "%p");
    SEND(avi_comment_fd, "%d");
    SEND(audio_file_flag, "%d");
    SEND(divxbitrate, "%d");
    SEND(divxkeyframes, "%d");
    SEND(divxquality, "%d");
    SEND(divxcrispness, "%d");
    SEND(divxmultipass, "%d");
    SEND(video_max_bitrate, "%d");
    SEND(divxlogfile, "%s");
    SEND(min_quantizer, "%d");
    SEND(max_quantizer, "%d");
    SEND(mp3bitrate, "%d");
    SEND(mp3frequency, "%d");
    SEND(mp3quality, "%f");
    SEND(mp3mode, "%d");
    SEND(audiologfile, "%s");
    SEND(ex_a_codec, "%d");
    SEND(ex_v_codec, "%d");
    SEND(ex_v_fcc, "%s");
    SEND(ex_a_fcc, "%s");
    SEND(ex_profile_name, "%s");
    SEND(pass_flag, "%d");
    SEND(encoder_flush, "%d");
    SEND(mod_path, "%s");
    SEND(ttime, "%p");  // format this as a -c style string?
    SEND(frame_interval, "%u");
    SEND(im_v_string, "%s");
    SEND(im_a_string, "%s");
    SEND(ex_v_string, "%s");
    SEND(ex_a_string, "%s");
    SEND(ex_m_string, "%s");
    SEND(m2v_requant, "%f");
    SEND(export_attributes, "%u");

#undef SEND
}

/*************************************************************************/
/*************************************************************************/

/* Socket actions. */

/*************************************************************************/

/**
 * handle_config():  Process a "config" command received on the socket.
 *
 * Parameters:
 *     params: Command parameters.
 * Return value:
 *     Nonzero on success, zero on failure.
 */

static int handle_config(char *params)
{
    char *filter_name, *filter_params;
    int filter_id;

    filter_name = params;
    filter_params = filter_name + strcspn(filter_name, " \t");
    *filter_params++ = 0;
    filter_params += strspn(filter_params, " \t");
    if (!*filter_name || !*filter_params)
        return 0;

    filter_id = tc_filter_find(filter_name);
    if (!filter_id)
        return 0;
    return tc_filter_configure(filter_id, filter_params) == 0;
}

/*************************************************************************/

/**
 * handle_disable():  Process a "disable" command received on the socket.
 *
 * Parameters:
 *     params: Command parameters.
 * Return value:
 *     Nonzero on success, zero on failure.
 */

static int handle_disable(char *params)
{
    int filter_id;

    filter_id = tc_filter_find(params);
    if (!filter_id)
        return 0;
    else
        return tc_filter_disable(filter_id);
}

/*************************************************************************/

/**
 * handle_enable():  Process an "enable" command received on the socket.
 *
 * Parameters:
 *     params: Command parameters.
 * Return value:
 *     Nonzero on success, zero on failure.
 */

static int handle_enable(char *params)
{
    int filter_id;

    filter_id = tc_filter_find(params);
    if (!filter_id)
        return 0;
    else
        return tc_filter_enable(filter_id);
}

/*************************************************************************/

/**
 * handle_help():  Process a "help" command received on the socket.
 *
 * Parameters:
 *     params: Command parameters.
 * Return value:
 *     Nonzero on success, zero on failure.
 */

static int handle_help(char *params)
{
    sendstr(client_sock,
            "load <filter> <initial string>\n"
            "unload <filter>\n"
            "enable <filter>\n"
            "disable <filter>\n"
            "config <filter> <string>\n"
            "parameters <filter>\n"
            "list [ load | enable | disable ]\n"
            "dump\n"
            "progress\n"
            "pause\n"
            "preview <command>\n"
            "  [ draw | undo | pause | fastfw |\n"
            "    slowfw | slowbw | rotate |\n"
            "    rotate | display | slower |\n"
            "    faster | toggle | grab ]\n"
            "status\n"
            "stop\n"
            "help\n"
            "version\n"
            "quit\n"
    );
    return 1;
}

/*************************************************************************/

/**
 * handle_list():  Process a "list" command received on the socket.
 *
 * Parameters:
 *     params: Command parameters.
 * Return value:
 *     Nonzero on success, zero on failure.
 */

static int handle_list(char *params)
{
    const char *list = NULL;

    if (strncasecmp(params, "load", 2) == 0)
        list = tc_filter_list(TC_FILTER_LIST_LOADED);
    else if (strncasecmp(params, "enable", 2) == 0)
        list = tc_filter_list(TC_FILTER_LIST_ENABLED);
    else if (strncasecmp(params, "disable", 2) == 0)
        list = tc_filter_list(TC_FILTER_LIST_DISABLED);

    if (list) {
        sendstr(client_sock, list);
        return 1;
    } else {
        return 0;
    }
}

/*************************************************************************/

/**
 * handle_load():  Process a "load" command received on the socket.
 *
 * Parameters:
 *     params: Command parameters.
 * Return value:
 *     Nonzero on success, zero on failure.
 */

static int handle_load(char *params)
{
    char *name, *options;

    name = params;
    options = strchr(params, ' ');
    if (options) {
        *options++ = 0;
        options += strspn(options, " \t");
    }
    return tc_filter_add(name, options);
}

/*************************************************************************/

/**
 * handle_parameter():  Process a "parameter" command received on the
 * socket.
 *
 * Parameters:
 *     params: Command parameters.
 * Return value:
 *     Nonzero on success, zero on failure.
 */

static int handle_parameter(char *params)
{
    int filter_id;
    const char *s;

    filter_id = tc_filter_find(params);
    if (!filter_id)
        return 0;
    s = tc_filter_get_conf(filter_id, NULL);
    if (!s)
        return 0;
    sendstr(client_sock, s);
    return 1;
}

/*************************************************************************/

/**
 * handle_preview():  Process a "preview" command received on the socket.
 *
 * Parameters:
 *     params: Command parameters.
 * Return value:
 *     Nonzero on success, zero on failure.
 */

static int handle_preview(char *params)
{
    int filter_id, cmd, arg;
    char *cmdstr, *argstr;

    /* Check that the preview filter is loaded, and load it if not */
    filter_id = tc_filter_find("pv");
    if (!filter_id) {
        filter_id = tc_filter_add("pv", "cache=20");
        if (!filter_id)
            return 0;
    }

    /* Parse out preview command name and (optional) argument */
    cmdstr = strtok(params, " \t");
    if (cmdstr)  // there are strtok() implementations that crash without this!
        argstr = strtok(NULL, " \t");
    else
        argstr = NULL;
    if (argstr)
        arg = atoi(argstr);
    else
        arg = 0;

    if        (!strncasecmp(cmdstr, "draw",    2)) {
        cmd = TC_SOCK_PV_DRAW;
    } else if (!strncasecmp(cmdstr, "pause",  2)) {
        cmd = TC_SOCK_PV_PAUSE;
    } else if (!strncasecmp(cmdstr, "undo", 2)) {
        cmd = TC_SOCK_PV_UNDO;
    } else if (!strncasecmp(cmdstr, "fastfw", 6)) {
        cmd = TC_SOCK_PV_FAST_FW;
    } else if (!strncasecmp(cmdstr, "fastbw", 6)) {
        cmd = TC_SOCK_PV_FAST_BW;
    } else if (!strncasecmp(cmdstr, "slowfw", 6)) {
        cmd = TC_SOCK_PV_SLOW_FW;
    } else if (!strncasecmp(cmdstr, "slowbw", 6)) {
        cmd = TC_SOCK_PV_SLOW_BW;
    } else if (!strncasecmp(cmdstr, "toggle", 6)) {
        cmd = TC_SOCK_PV_TOGGLE;
    } else if (!strncasecmp(cmdstr, "slower", 6)) {
        cmd = TC_SOCK_PV_SLOWER;
    } else if (!strncasecmp(cmdstr, "faster", 6)) {
        cmd = TC_SOCK_PV_FASTER;
    } else if (!strncasecmp(cmdstr, "rotate", 6)) {
        cmd = TC_SOCK_PV_ROTATE;
    } else if (!strncasecmp(cmdstr, "display", 6)) {
        cmd = TC_SOCK_PV_DISPLAY;
    } else if (!strncasecmp(cmdstr, "grab", 4)) {
        cmd = TC_SOCK_PV_SAVE_JPG;
    } else {
        return 0;
    }

    tc_mutex_lock(&tc_socket_msg_lock);
    sock_cmd.cmd = cmd;
    sock_cmd.arg = arg;
    tc_mutex_unlock(&tc_socket_msg_lock);

    return 1;
}

/*************************************************************************/

/**
 * handle:  Handle a single message from a socket.
 *
 * Parameters:
 *     buf: Line read from socket.
 * Return value:
 *     Zero if the socket is to be closed, nonzero otherwise.
 */

static int handle(char *buf)
{
    TCSession *session = tc_get_session();
    char *cmd, *params;
    int len, retval;

    len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n')
        buf[--len] = 0;
    if (len > 0 && buf[len-1] == '\r')
        buf[--len] = 0;
    //tc_log_msg(__FILE__, "read from socket: |%s|", buf);

    cmd = buf + strspn(buf, " \t");
    params = cmd + strcspn(cmd, " \t");
    *params++ = 0;
    params += strspn(params, " \t");
    
    if (!*cmd) {  // not strictly necessary, but lines up else if's nicely
        retval = 0;
    } else if (strncasecmp(cmd, "config", 2) == 0) {
        retval = handle_config(params);
    } else if (strncasecmp(cmd, "disable", 2) == 0) {
        retval = handle_disable(params);
    } else if (strncasecmp(cmd, "dump", 2) == 0) {
        dump_vob(client_sock);
        retval = 1;
    } else if (strncasecmp(cmd, "enable", 2) == 0) {
        retval = handle_enable(params);
    } else if (strncasecmp(cmd, "help", 2) == 0) {
        retval = handle_help(params);
    } else if (strncasecmp(cmd, "list", 2) == 0) {
        retval = handle_list(params);
    } else if (strncasecmp(cmd, "load", 2) == 0) {
        retval = handle_load(params);
    } else if (strncasecmp(cmd, "parameters", 3) == 0) {
        retval = handle_parameter(params);
    } else if (strncasecmp(cmd, "pause", 3) == 0) {
        tc_pause_request();
        retval = 1;
    } else if (strncasecmp(cmd, "preview", 3) == 0) {
        retval = handle_preview(params);
    } else if (strncasecmp(cmd, "progress", 5) == 0) {
        session->progress_meter = !session->progress_meter;
        retval = 1;
    } else if (strncasecmp(cmd, "processing", 10) == 0) {
        dump_processing(client_sock);
        retval = 1;
    } else if (strncasecmp(cmd, "quit", 2) == 0
            || strncasecmp(cmd, "exit", 2) == 0) {
        return 0;  // tell caller to close socket
    } else if (strncasecmp(cmd, "unload", 2) == 0) {
        retval = 0;  // FIXME: not implemented
    } else if (strncasecmp(cmd, "version", 2) == 0) {
        sendstr(client_sock, PACKAGE_VERSION "\n");
        retval = 1;
    } else if (strncasecmp(cmd, "stop", 4)) {
        tc_interrupt();
        tc_framebuffer_interrupt();
        retval = 1;
    } else {
        retval = 0;
    }

    sendstr(client_sock, retval ? "OK\n" : "FAILED\n");
    return 1;  // socket remains open
}

/*************************************************************************/
/*************************************************************************/

/* External interfaces. */

/*************************************************************************/

/**
 * tc_socket_init:  Initialize the socket code, and open a listener socket
 * with the given pathname.
 *
 * Parameters:
 *     socket_path_: Pathname to use for communication socket.
 * Return value:
 *     Nonzero on success, zero on failure.
 * Side effects:
 *     If `socket_path' points to an existing file, the file is removed.
 */

int tc_socket_init(const char *socket_path_)
{
    struct sockaddr_un server_addr;

    client_sock = -1;
    server_sock = -1;

    tc_mutex_init(&tc_socket_msg_lock);

    if (tc_snprintf(socket_path, sizeof(socket_path), "%s", socket_path_) < 0){
        tc_log_error(__FILE__, "Socket pathname too long (1)");
        *socket_path = 0;
        return 0;
    }

    errno = 0;
    if (unlink(socket_path) != 0 && errno != ENOENT) {
        tc_log_error(__FILE__, "Unable to remove \"%s\": %s",
                     socket_path, strerror(errno));
        return 0;
    }

    server_addr.sun_family = AF_UNIX;
    if (tc_snprintf(server_addr.sun_path, sizeof(server_addr.sun_path),
                    "%s", socket_path) < 0
    ) {
        tc_log_error(__FILE__, "Socket pathname too long");
        return 0;
    }
    server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) {
        tc_log_perror(__FILE__, "Unable to create server socket");
        return 0;
    }
    if (bind(server_sock, (struct sockaddr *)&server_addr,
             sizeof(server_addr))
    ) {
        tc_log_perror(__FILE__, "Unable to bind server socket");
        close(server_sock);
        server_sock = -1;
        unlink(socket_path);  // just in case
        return 0;
    }
    if (listen(server_sock, 5) < 0) {
        tc_log_perror(__FILE__, "Unable to activate server socket");
        close(server_sock);
        unlink(socket_path);
        return 0;
    }

    return 1;
}

/*************************************************************************/

/**
 * tc_socket_fini:  Close the listener and client sockets, if open, and
 * perform any other necessary cleanup.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 */

void tc_socket_fini(void)
{
    if (client_sock >= 0) {
        close(client_sock);
        client_sock = -1;
    }
    if (server_sock >= 0) {
        close(server_sock);
        server_sock = -1;
        unlink(socket_path);
    }
}

/*************************************************************************/

int tc_socket_get_pv_cmd(TCSockPVCmd *pvcmd)
{
    tc_mutex_lock(&tc_socket_msg_lock);
    pvcmd->cmd = sock_cmd.cmd;
    pvcmd->arg = sock_cmd.arg;
    sock_cmd.cmd = TC_SOCK_PV_NONE;
    tc_mutex_unlock(&tc_socket_msg_lock);

    return TC_OK;
}


/*************************************************************************/

/**
 * tc_socket_poll_internal:  Check server and (if connected) client sockets for
 * pending events, and process them, with tunable timeout.
 *
 * Parameters:
 *     blocking: if !0, blocks forever caller until next event.
 *               if 0, just check once if there is an event to process
 *               and execute (or just return) immediately.
 * Return value:
 *     None.
 * Preconditions:
 *     valid (!= -1) server socket avalaible.
 */

static void tc_socket_poll_internal(int blocking)
{
    fd_set rfds, wfds;
    char msgbuf[TC_BUF_MAX];
    int maxfd = -1, retval = -1;
    struct timeval tv = {0, 0}, *ptv = (blocking) ?NULL :&tv;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    
    FD_SET(server_sock, &rfds);
    maxfd = server_sock;

    if (client_sock >= 0) {
        FD_SET(client_sock, &rfds);
        //FD_SET(client_sock, &wfds);
        if (client_sock > maxfd)
            maxfd = client_sock;
    }
    retval = select(maxfd+1, &rfds, &wfds, NULL, ptv);

    if (retval == 0) {
        /* nothing interesting happened. It happens :) */
        return;
    }
    if (retval < 0 && errno != EINTR) {
        /* EINTR is an expected "exceptional" condition */
        tc_log_warn(__FILE__, "select(): %s", strerror(errno));
        return;
    }

    if (FD_ISSET(server_sock, &rfds)) {
        int newsock = accept(server_sock, NULL, 0);
        if (newsock < 0) {
            tc_log_warn(__FILE__, "Unable to accept new connection: %s",
                        strerror(errno));
            return;
        }
        if (client_sock >= 0) {
            /* We already have a connection, so drop this one */
            close(newsock);
        } else {
            client_sock = newsock;
        }
    }

    if (client_sock >= 0 && FD_ISSET(client_sock, &rfds)) {
        retval = recv(client_sock, msgbuf, sizeof(msgbuf)-1, 0);
        if (retval <= 0) {
            if (retval < 0)
                tc_log_perror(__FILE__, "Unable to read message from socket");
            close(client_sock);
            client_sock = -1;
        } else {
            if (retval > sizeof(msgbuf)-1)  // paranoia
                retval = sizeof(msgbuf)-1;
            msgbuf[retval] = 0;
            if (!handle(msgbuf)) {
                close(client_sock);
                client_sock = -1;
            }
        }
    }
}

/**
 * tc_socket_poll:  Check server and (if connected) client sockets for
 * pending events, and process them.
 * This call should never block caller, returnin immediately if there
 * isn't any event pending.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 */

void tc_socket_poll(void)
{
    if (server_sock < 0 && client_sock < 0) {
        /* Nothing to poll! */
        return;
    }
    tc_socket_poll_internal(0);
}

/**
 * tc_socket_wait:  Wait forever server and (if connected) client sockets
 * for next event, and process the next first one and exit.
 * This call will always block until next event happens.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 */

void tc_socket_wait(void)
{
    if (server_sock < 0 && client_sock < 0) {
        pause();
        /* if no server socket is avalaible (= no tc_socket_init
         * called before), nothing will ever happen, so
         * we must block forever to satisfy function requirements.
         */
        return;
    }
    tc_socket_poll_internal(1);
}


/*************************************************************************/

/**
 * tc_socket_submit:  Send a string to the socket.  Does nothing if no
 * socket is open.
 *
 * Parameters:
 *     str: String to send.
 * Return value:
 *     None.
 */

void tc_socket_submit(const char *str)
{
    if (socket >= 0)
        sendstr(client_sock, str);
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
