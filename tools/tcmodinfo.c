/*
 *  tcmodinfo.c
 *
 *  Copyright (C) Tilmann Bitterberg - August 2002
 *  updated and partially rewritten by
 *  Copyright (C) Francesco Romani - January 2006
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

#include "config.h"
#include "tcstub.h"
#include "libtc/mediainfo.h"
#include "libtcext/tc_ext.h"
#include "libtcmodule/tcmodule-registry.h"

#define EXE "tcmodinfo"

enum {
    STATUS_OK = 0,
    STATUS_BAD_PARAM,
    STATUS_NO_MODULE,
    STATUS_MODULE_ERROR,
    STATUS_NO_SOCKET,
    STATUS_SOCKET_ERROR,
    STATUS_BAD_MODULES,
    STATUS_MODULE_FAILED,
};

void version(void)
{
    printf("%s (%s v%s) (C) 2001-2010 Tilmann Bitterberg, "
           "Transcode Team\n", EXE, PACKAGE, VERSION);
}

static void usage(void)
{
    version();
    tc_log_info(EXE, "Usage: %s [options]", EXE);
    fprintf(stderr, "    -i name           Module name information (like \'smooth\')\n");
    fprintf(stderr, "    -p                Print the compiled-in module path\n");
    fprintf(stderr, "    -d verbosity      Verbosity mode [1 == TC_INFO]\n");
    fprintf(stderr, "    -m path           Use PATH as module path\n");
    fprintf(stderr, "    -r path           Use PATH as registry path\n");
    fprintf(stderr, "    -M element        Request to module informations about <element>\n");
    fprintf(stderr, "    -C string         Request to configure module using configuration <string>\n");
    fprintf(stderr, "    -t type           Type of module (filter, encode, multiplex)\n");
    fprintf(stderr, "    -F format         Print which module will be used for `format'\n");
    fprintf(stderr, "    -s socket         Connect to transcode socket\n");
    fprintf(stderr, "\n");
}

static int do_connect_socket(const char *socketfile)
{
    int sock, retval;
    struct sockaddr_un server;
    char buf[OPTS_SIZE];
    fd_set rfds;
    struct timeval tv;
    ssize_t n;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("opening stream socket");
        return STATUS_NO_SOCKET;
    }
    server.sun_family = AF_UNIX;
    strlcpy(server.sun_path, socketfile, sizeof(server.sun_path));

    if (connect(sock, (struct sockaddr *) &server,
                sizeof(struct sockaddr_un)) < 0) {
        close(sock);
        perror("connecting stream socket");
        return STATUS_NO_SOCKET;
    }

    while (1) {
        /* Watch stdin (fd 0) to see when it has input. */
        FD_ZERO(&rfds);
        FD_SET(0, &rfds); // stdin
        FD_SET(sock, &rfds);
        /* Wait up to five seconds. */
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        retval = select(sock+1, &rfds, NULL, NULL, NULL);
        /* Don't rely on the value of tv now! */

        memset(buf, 0, sizeof (buf));  // null-termination in advance, slowly

        if (retval>0) {
            if (FD_ISSET(0, &rfds)) {
                if (!fgets(buf, OPTS_SIZE, stdin)) {
                    perror("reading on stdin");
                    break;
                }
            }
            if (FD_ISSET(sock, &rfds)) {
                if ( (n = read(sock, buf, OPTS_SIZE)) < 0) {
                    perror("reading on stream socket");
                    break;
                } else if (n == 0) { // EOF
                    fprintf (stderr, "Server closed connection\n");
                    break;
                }
                printf("%s", buf);
                continue;
            }
        }

        if (write(sock, buf, strlen(buf)) < 0) {
            perror("writing on stream socket");
        }

        memset(buf, 0, sizeof (buf));

        if (read(sock, buf, OPTS_SIZE) < 0)
            perror("reading on stream socket");

        printf("%s", buf);

        if (!isatty(0))
            break;
    }

    close(sock);
    return STATUS_OK;
}

static int query_new_module(TCModule module,
                            const char *modcfg, const char *modarg)
{
    const char *answer = NULL;
    int status = STATUS_OK;

    if (verbose >= TC_DEBUG) {
        tc_log_info(EXE, "using new module system");
    }
    if (strlen(modcfg) > 0) {
        TCModuleExtraData *xdata[] = { NULL, NULL };
        int ret = tc_module_configure(module, modcfg, tc_get_vob(), xdata);
        if (ret == TC_OK) {
            status = STATUS_OK;
        } else {
            status = STATUS_MODULE_FAILED;
            tc_log_error(EXE, "configure returned error");
        }
        tc_module_stop(module);
    } else {
        if (verbose >= TC_INFO) {
            /* overview and options */
            tc_module_inspect(module, "help", &answer);
            puts(answer);
            /* module capabilities */
            tc_module_show_info(module, verbose);
        }
        if (strlen(modarg) > 0) {
            tc_log_info(EXE, "informations about '%s' for "
                             "module:", modarg);
            tc_module_inspect(module, modarg, &answer);
            puts(answer);
        }
        status = STATUS_OK;
    }

    return status;
}

static int query_old_module(const char *filename, const char *modpath)
{
    int ret, status = STATUS_OK;
    char namebuf[NAME_LEN] = { '\0' };
    char options[OPTS_SIZE] = { '\0' };

    vframe_list_t ptr;

    memset(&ptr, 0, sizeof(ptr));

    /* compatibility support only for filters */
    if (verbose >= TC_DEBUG) {
        tc_log_info(EXE, "using old module system");
    }
    /* ok, fallback to old module system */
    strlcpy(namebuf, filename, NAME_LEN);
    filter[0].name = namebuf;
    
    ret = load_plugin(modpath, 0, verbose);
    if (ret != 0) {
        tc_log_error(__FILE__, "unable to load filter `%s' (path=%s)",
                               filter[0].name, modpath);
        status = STATUS_NO_MODULE;
    } else {
        strlcpy(options, "help", OPTS_SIZE);
        ptr.tag = TC_FILTER_INIT;
        if ((ret = filter[0].entry(&ptr, options)) != 0) {
            status = STATUS_MODULE_ERROR;
        } else {
            memset(options, 0, OPTS_SIZE);
            ptr.tag = TC_FILTER_GET_CONFIG;
            ret = filter[0].entry(&ptr, options);

            if (ret == 0) {
                if (verbose >= TC_INFO) {
                    fputs("START\n", stdout);
                    fputs(options, stdout);
                    fputs("END\n", stdout);
                }
                status = STATUS_OK;
            }
        }
    }
    return status;
}

int main(int argc, char *argv[])
{
    int ch;
    const char *filename = NULL;
    const char *modpath = tc_module_default_path();
    const char *regpath = tc_module_registry_default_path();
    const char *modtype = "filter";
    const char *modarg = ""; /* nothing */
    const char *modcfg = ""; /* nothing */
    const char *socketfile = NULL;
    const char *fmtname = NULL;
    int print_mod = 0;
    int connect_socket = 0;
    int ret = 0;
    int status = STATUS_NO_MODULE;

    /* needed by filter modules */
    TCVHandle tcv_handle = tcv_init();
    TCRegistry registry = NULL;
    TCFactory factory = NULL;
    TCModule module = NULL;

    ac_init(AC_ALL);
    tc_ext_init();

    if (argc == 1) {
        usage();
        return STATUS_BAD_PARAM;
    }

    libtc_init(&argc, &argv);

    while (1) {
        ch = getopt(argc, argv, "C:F:d:i:?vhpm:M:r:s:t:");
        if (ch == -1) {
            break;
        }

        switch (ch) {
          case 'd':
            if (optarg[0] == '-') {
                usage();
                return STATUS_BAD_PARAM;
            }
            verbose = atoi(optarg);
            break;
          case 'i':
            if (optarg[0] == '-') {
                usage();
                return STATUS_BAD_PARAM;
            }
            filename = optarg;
            break;
          case 'C':
            modcfg = optarg;
            break;
          case 'F':
            fmtname = optarg;
            break;
          case 'm':
            modpath = optarg;
            break;
          case 'M':
            modarg = optarg;
            break;
          case 'r':
            regpath = optarg;
            break;
          case 't':
            if (!optarg) {
                usage();
                return STATUS_BAD_PARAM;
            }
            if (!strcmp(optarg, "filter")
             || !strcmp(optarg, "encode")
             || !strcmp(optarg, "multiplex")) {
                modtype = optarg;
            } else {
                modtype = NULL;
            }
            break;
          case 's':
            if (optarg[0] == '-') {
                usage();
                return STATUS_BAD_PARAM;
            }
            connect_socket = 1;
            socketfile = optarg;
            break;
          case 'p':
            print_mod = 1;
            break;
          case 'v':
            version();
            return STATUS_OK;
          case '?': /* fallthrough */
          case 'h': /* fallthrough */
          default:
            usage();
            return STATUS_OK;
        }
    }

    if (print_mod) {
        printf("%s\n", modpath);
        return STATUS_OK;
    }

    if (connect_socket) {
        do_connect_socket(socketfile);
        return STATUS_OK;
    }

    if (!filename && !fmtname) {
        usage();
        return STATUS_BAD_PARAM;
    }

    if (!modtype || !strcmp(modtype, "import")) {
        tc_log_error(EXE, "Unknown module type (not in filter, encode, multiplex)");
        return STATUS_BAD_PARAM;
    }

    if (strlen(modcfg) > 0 && strlen(modarg) > 0) {
        tc_log_error(EXE, "Cannot configure and inspect module on the same time");
        return STATUS_BAD_PARAM;
    }

    /* 
     * we can't distinguish from OMS and NMS modules at glance, so try
     * first using new module system
     */
    factory = tc_new_module_factory(modpath, TC_MAX(verbose - 4, 0)); /* FIXME */
    registry = tc_new_module_registry(factory, regpath, TC_MAX(verbose - 4, 0)); /* FIXME */

    if (fmtname) {
        TCCodecID codec = tc_codec_from_string(fmtname);
        TCFormatID format = tc_format_from_string(fmtname);
        if (codec == TC_CODEC_ERROR && format == TC_FORMAT_ERROR) {
            tc_log_error(EXE, "unrecognized format `%s'", fmtname);
        } else {
            const char *modnames = tc_get_module_name_for_format(registry,
                                                                 modtype,
                                                                 fmtname);
            if (modnames) {
                puts(modnames);
            }
        }
    } else {
        module = tc_new_module(factory, modtype, filename, TC_NONE);

        if (module != NULL) {
            status = query_new_module(module, modcfg, modarg);
            tc_del_module(factory, module);
        } else if (!strcmp(modtype, "filter")) {
            status = query_old_module(filename, modpath);
        }
    }

    ret = tc_del_module_registry(registry);
    ret = tc_del_module_factory(factory);
    tcv_free(tcv_handle);
    return status;
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
