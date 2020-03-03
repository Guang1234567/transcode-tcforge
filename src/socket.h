/* socket.h -- include file for control socket routines
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef SOCKET_H
#define SOCKET_H

#include <pthread.h>

/*************************************************************************/

/* External interface. */
int tc_socket_init(const char *socket_path);
void tc_socket_fini(void);
void tc_socket_poll(void);
void tc_socket_wait(void);
void tc_socket_submit(const char *str);

/* Variables and constants for communicating with the "pv" module
 * (FIXME: these should go away) */
enum tc_socket_msg_cmd_enum {
    TC_SOCK_PV_NONE = 0,
    TC_SOCK_PV_PAUSE,
    TC_SOCK_PV_DRAW,
    TC_SOCK_PV_UNDO,
    TC_SOCK_PV_SLOW_FW,
    TC_SOCK_PV_SLOW_BW,
    TC_SOCK_PV_FAST_FW,
    TC_SOCK_PV_FAST_BW,
    TC_SOCK_PV_SLOWER,
    TC_SOCK_PV_FASTER,
    TC_SOCK_PV_TOGGLE,
    TC_SOCK_PV_ROTATE,
    TC_SOCK_PV_DISPLAY,
    TC_SOCK_PV_SAVE_JPG,
};

typedef struct tcsockpvcmd_ TCSockPVCmd;
struct tcsockpvcmd_ {
    enum tc_socket_msg_cmd_enum cmd;
    int                         arg;
};

int tc_socket_get_pv_cmd(TCSockPVCmd *pvcmd);


/*************************************************************************/

#endif  /* SOCKET_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
