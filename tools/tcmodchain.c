/*
 * tcmodchain.c -- simple module system explorer frontend
 * (C) 2006-2010 - Francesco Romani <fromani at gmail dot com>
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


#include <glob.h>
#include "tcstub.h"
#include "libtcext/tc_ext.h"

#define EXE "tcmodchain"

/*************************************************************************/


void version(void)
{
    printf("%s v%s (C) 2006-2010 Transcode Team\n", EXE, VERSION);
}

enum {
    STATUS_DONE = -1, /* used internally */
    STATUS_OK = 0,
    STATUS_BAD_PARAM,
    STATUS_MODULE_ERROR,
    STATUS_MODULE_MISMATCH,
    STATUS_GLOB_FAILED,
};


#define MAX_MODS     (16)

typedef struct modrequest_ ModRequest;
struct modrequest_ {
    char **rawdata; /* main reference */

    const char *type; /* commodity */
    const char *name; /* commodity */

    TCModule module;
};


static void modrequest_init(ModRequest *modr);
static int modrequest_load(TCFactory factory,
                           ModRequest *modr, const char *str);
static int modrequest_scan(const char *modpath, const char *modstr,
                           glob_t *globbuf);
static int modrequest_fill(TCFactory factory, ModRequest *mods,
                           size_t maxmods, size_t *modnum, glob_t *globbuf);
#define modrequest_free(GLOBBUF)    globfree((GLOBBUF))
static int modrequest_unload(TCFactory factory, ModRequest *modr);

/*************************************************************************/

enum {
    TC_MODULE_DEMUXER  =  1, /* future */
    TC_MODULE_DECODER  =  2, /* future */
    TC_MODULE_FILTER   =  4, /* future */
    TC_MODULE_ENCODER  =  8,
    TC_MODULE_MUXER    = 16,

    TC_MODULE_FIXED    = 1024,
    TC_MODULE_TEMPLATE = 2048,
};


static uint32_t parse_modstr(const char *str)
{
    uint32_t ret = 0;
    if (str != NULL && strlen(str) > 0) {
        struct {
            const char *name;
            int kind;
            size_t off;
        } tags[] = {
            { "encode:", TC_MODULE_ENCODER, strlen("encode:") },
            { "multiplex:", TC_MODULE_MUXER, strlen("multiplex:") },
            { NULL, 0, 0 },
        };
        int i = 0;

        for (i = 0; tags[i].name != NULL; i++) {
            if (strncmp(str, tags[i].name, tags[i].off) == 0) {
                ret |= tags[i].kind;
                str += tags[i].off;
                break;
            }
        }

        /* found something supported/making sense */
        if (ret != 0 && (str != NULL && strlen(str) > 0)) {
            if (!strcmp(str, "*")) {
                ret |= TC_MODULE_TEMPLATE;
            } else {
                ret |= TC_MODULE_FIXED;
            }
        }
    }
    return ret;
}

static int parse_path(const char *fullpath, char *modstr, size_t buflen)
{
    const char *pc = NULL;
    char *pr = NULL;

    if (!fullpath || !modstr || (buflen < TC_BUF_MIN)) {
        return TC_ERROR;
    }

    pc = strrchr(fullpath, '/');
    if (pc == NULL || strlen(pc + 1) > buflen) {
        return TC_ERROR;
    }
    strlcpy(modstr, pc + 1, buflen);

    pr = strrchr(modstr, '.');
    if (pr == NULL) {
        return TC_ERROR;
    }
    *pr = '\0';

    pr = strchr(modstr, '_');
    if (pr == NULL) {
        return TC_ERROR;
    }
    *pr = ':';

    return TC_OK;
}


/*************************************************************************/

static void modrequest_init(ModRequest *modr)
{
    if (modr != NULL) {
        modr->rawdata = NULL;
        modr->type = NULL;
        modr->name = NULL;
        modr->module = NULL;
    }
}

static int modrequest_load(TCFactory factory,
                           ModRequest *modr, const char *str)
{
    size_t pieces = 0;

    if (factory == NULL || modr == NULL || str == NULL) {
        tc_log_warn(EXE, "wrong parameters for modrequest_load");
        return TC_ERROR;
    }

    modr->rawdata = tc_strsplit(str, ':', &pieces);
    if (modr->rawdata == NULL || pieces != 2) {
        tc_log_warn(EXE, "malformed module string: %s", str);
        return TC_ERROR;
    }
    modr->type = modr->rawdata[0];
    modr->name = modr->rawdata[1];

    modr->module = tc_new_module(factory, modr->type, modr->name, TC_NONE);
    if (modr->module == NULL) {
        tc_log_warn(EXE, "failed creation of module: %s", str);
        return TC_ERROR;
    }
    return TC_OK;
}

static int modrequest_unload(TCFactory factory, ModRequest *modr)
{
    if (factory == NULL || modr == NULL) {
        tc_log_warn(EXE, "wrong parameters for modrequest_load");
        return TC_ERROR;
    }

    tc_del_module(factory, modr->module);
    tc_strfreev(modr->rawdata);

    /* re-blank fields */
    modrequest_init(modr);

    return TC_OK;
}

static int modrequest_scan(const char *modpath, const char *modstr,
                           glob_t *globbuf)
{
    char path_model[PATH_MAX];
    char buf[TC_BUF_MIN];
    const char *pc = NULL;
    int err = 0;

    pc = strchr(modstr, ':');
    if (pc == NULL) {
        return 1;
    }
    if ((pc - modstr + 1) > sizeof(buf)) {
        return 2; /* XXX watch out here */
    }
    strlcpy(buf, modstr, pc - modstr + 1);

    tc_snprintf(path_model, sizeof(path_model), "%s/%s_*.so", modpath, buf);
    err = glob(path_model, GLOB_ERR, NULL, globbuf);

    if (err) {
        tc_log_error(EXE, "error while scanning for modules: %s",
                     (err == GLOB_NOSPACE)   ?"can't get enough memory" :
                     (err == GLOB_ABORTED)   ?"read error" :
                          /* GLOB_NOMATCH */ "no modules found");
        return -1;
    }
    return 0;
}

static int modrequest_fill(TCFactory factory, ModRequest *mods,
                           size_t maxmods, size_t *modnum, glob_t *globbuf)
{
    char modstr[TC_BUF_MIN];
    int i = 0, count = 0, lim = 0;

    if (!factory || !mods || !globbuf) {
        return TC_ERROR;
    }
    if (maxmods < globbuf->gl_pathc) {
        tc_log_warn(EXE, "found %u candidate modules, but "
                              "only %u allowed (dropping remaining)",
                    (unsigned)globbuf->gl_pathc, (unsigned)maxmods);
    }
    lim = TC_MIN(maxmods, globbuf->gl_pathc);

    for (i = 0; i < lim; i++) {
        int ret;

        ret = parse_path(globbuf->gl_pathv[i], modstr, TC_BUF_MIN);
        if (ret != 0) {
            tc_log_warn(EXE, "error while parsing '%s', skipping",
                                  globbuf->gl_pathv[i]);
            continue;
        }
        ret = modrequest_load(factory, &mods[i], modstr);
        if (ret != 0) {
            tc_log_warn(EXE, "error while loading '%s', skipping",
                                  modstr);
            continue;
        }
        count++;
    }

    if (modnum != NULL) {
        *modnum += count;
    }

    return TC_OK;
}

/*************************************************************************/

typedef struct cmdletdata_ CmdLetData;
struct cmdletdata_ {
    ModRequest  mods[MAX_MODS];
    size_t      modsnum;

    const char  *modpath;
    TCFactory   factory;
    int         type;
};

typedef  int (*CmdLet)(CmdLetData *cdata, int argc, char **argv);


#define CLEANUP(CDATA) do { \
    int i = 0; \
    for (i = 0; i < (CDATA)->modsnum; i++) { \
        modrequest_unload((CDATA)->factory, &((CDATA)->mods[i])); \
    } \
    (CDATA)->modsnum = 0; \
} while (0)
 
/* XXX */
static int check_module_pair(const ModRequest *head,
                             const ModRequest *tail,
                             const ModRequest *ref,
                             int type,
                             int verbose)
{
    int ret = 0;

    if (head->module == NULL || tail->module == NULL) {
        tc_log_error(EXE, "check_module_pair: missing module handle");
        return -1;
    }

    ret = tc_module_info_match(TC_CODEC_ANY, type,
                               tc_module_get_info(head->module),
                               tc_module_get_info(tail->module));
    if (verbose >= TC_DEBUG) {
        tc_log_info(EXE, "%s:%s | %s:%s [%s]",
                    head->type, head->name, tail->type, tail->name,
                    (ret == 1) ?"OK" :"MISMATCH"); 
    } else if (verbose >= TC_INFO) {
        if (ret == 1) {
            printf("%s\n", ref->name);
        }
    }
    return ret;
}

static int cmdlet_usage(CmdLetData *unused, int ac, char **av)
{
    version();
    printf("Usage: %s [options] module [module... [module...]]\n",
           EXE);
    printf("    -A                check against audio capabilities\n");
    printf("    -V                check against video capabilities\n");
    printf("    -L                list mode (see manpage for details)\n");
    printf("    -C                check mode (see manpage for details)\n");
    printf("    -d verbosity      verbosity mode [1 == TC_INFO]\n");
    printf("    -m PATH           use PATH as module path\n");
    printf("    -v                show program version and exit\n");
    printf("    -h                show this help message\n");
    return STATUS_OK;
}


static int cmdlet_check(CmdLetData *cdata, int ac, char **av)
{
    int i = 0, matches = 0;
    int status = STATUS_OK; /* let's be optimisc, once in lifetime */

    if (ac < 2) {
        tc_log_error(EXE, "not enough arguments for `check' mode");
        return STATUS_BAD_PARAM;
    }

    for (i = 0; i < ac; i++) {
        modrequest_load(cdata->factory, &cdata->mods[cdata->modsnum], av[i]);
        cdata->modsnum++;
    }

    status = STATUS_OK;
    if (cdata->modsnum >= 2) {
        /* N modules, so N - 1 interfaces */
        for (i = 0; i < cdata->modsnum - 1; i++) {
            int ret = check_module_pair(&cdata->mods[i], &cdata->mods[i + 1],
                                        &cdata->mods[i], cdata->type,
                                        (verbose >= TC_INFO) ?TC_DEBUG :TC_QUIET);
            if (ret != -1) { /* no error */
                matches += ret;
            }
                
        }
        if (matches < cdata->modsnum - 1) {
            status = STATUS_MODULE_MISMATCH;
        }
    }

    if (verbose) {
        tc_log_info(EXE, "module chain %s type %s",
                    (status == STATUS_OK) ?"OK" :"ILLEGAL",
                    (cdata->type == TC_VIDEO) ?"video" :"audio");
    }

    CLEANUP(cdata);
    return status;
}

static int cmdlet_list(CmdLetData *cdata, int ac, char **av)
{
    glob_t globbuf;
    int ret, i = 0, fid = 0 /* fixed id */, tid = 0; /* template id */
    uint32_t modkind[2] = { 0, 0 };
    ModRequest fixed;

    if (ac != 2) {
        tc_log_error(EXE, "wrong number of arguments for `list' mode");
        return STATUS_BAD_PARAM;
    }
    /* we support only encoder|multiplexor, yet */
    modkind[0] = parse_modstr(av[0]);
    if (!(modkind[0] & TC_MODULE_ENCODER)) {
        tc_log_error(EXE, "unknown/unsupported module '%s'", av[0]);
        return STATUS_BAD_PARAM;
    }
    modkind[1] = parse_modstr(av[1]);
    if (!(modkind[1] & TC_MODULE_MUXER)) {
        tc_log_error(EXE, "unknown/unsupported module '%s'", av[1]);
        return STATUS_BAD_PARAM;
    }

    if ((modkind[0] & TC_MODULE_FIXED)
     && (modkind[1] & TC_MODULE_TEMPLATE)) {
        fid = 0;
        tid = 1;    
    } else if ((modkind[0] & TC_MODULE_TEMPLATE)
            && (modkind[1] & TC_MODULE_FIXED)) {
        fid = 1;
        tid = 0;
    } else {
        tc_log_error(EXE, "incorrect arguments,"
                          " maybe you want to use `check' mode?");
        return STATUS_BAD_PARAM;
    }

    modrequest_init(&fixed);
    ret = modrequest_load(cdata->factory, &fixed, av[fid]);
    if (ret != TC_OK) {
        return STATUS_MODULE_ERROR;
    }

    ret = modrequest_scan(cdata->modpath, av[tid], &globbuf);
    if (ret != 0) {
        return STATUS_GLOB_FAILED;
    }
    ret = modrequest_fill(cdata->factory, cdata->mods, MAX_MODS,
                          &cdata->modsnum, &globbuf);
    if (ret != TC_OK) {
        return STATUS_MODULE_ERROR;
    }

    for (i = 0; i < cdata->modsnum; i++) {
        const ModRequest *H = (tid == 0) ?(&cdata->mods[i]) :(&fixed);
        const ModRequest *T = (tid == 1) ?(&cdata->mods[i]) :(&fixed);
        check_module_pair(H, T, &(cdata->mods[i]), cdata->type,
                          (verbose == 0) ?TC_INFO :verbose);
    }

    CLEANUP(cdata);
    modrequest_free(&globbuf);
    ret = modrequest_unload(cdata->factory, &fixed);
    if (ret != TC_OK) {
        return STATUS_MODULE_ERROR;
    }
    return STATUS_OK;
}

/*************************************************************************/

int main(int argc, char *argv[])
{
    /* needed by filter modules */
    TCVHandle tcv_handle = tcv_init();
    CmdLet cmdlet = cmdlet_usage;
    int ch, ret, status, i = 0;

    CmdLetData cdata = {
        .modpath = tc_module_default_path(),
        .factory = NULL,
        .modsnum = 0,
        .type    = 0,
    };

    ac_init(AC_ALL);
    libtc_init(&argc, &argv);
    tc_ext_init();

    filter[0].id = 0; /* to make gcc happy */
    for (i = 0; i < MAX_MODS; i++) {
        modrequest_init(&cdata.mods[i]);
    }

    while (1) {
        ch = getopt(argc, argv, "AVLCd:?vhm:");
        if (ch == -1) {
            break;
        }

        switch (ch) {
          case 'A':
            cdata.type = TC_AUDIO;
            break;
          case 'V':
            cdata.type = TC_VIDEO;
            break;
          case 'L':
            cmdlet = cmdlet_list;
            break;
          case 'C':
            cmdlet = cmdlet_check;
            break;
          case 'd':
            if (optarg[0] == '-') {
                cmdlet_usage(&cdata, argc, argv);
                return STATUS_BAD_PARAM;
            }
            verbose = atoi(optarg);
            break;
          case 'm':
            cdata.modpath = optarg;
            break;
          case 'v':
            version();
            return STATUS_OK;
          case '?': /* fallthrough */
          case 'h': /* fallthrough */
          default:
            cmdlet_usage(&cdata, argc, argv);
            return STATUS_OK;
        }
    }

    if (cdata.type == 0) {
        tc_log_error(EXE, "unknown/unsupported media type");
        return STATUS_BAD_PARAM;
    }

    /* XXX: watch out here */
    argc -= optind;
    argv += optind;

    /* 
     * we can't distinguish from OMS and NMS modules at glance, so try
     * first using new module system
     */
    cdata.factory = tc_new_module_factory(cdata.modpath, verbose);

    status = cmdlet(&cdata, argc, argv);

    ret = tc_del_module_factory(cdata.factory); /* XXX: unchecked */
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
