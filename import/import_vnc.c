#define MOD_NAME    "import_vnc.so"
#define MOD_VERSION "v0.0.3 (2007-07-15)"
#define MOD_CODEC   "(video) VNC"

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_VID | TC_CAP_RGB | TC_CAP_YUV;

#define MOD_PRE vnc
#include "import_def.h"

#include <sys/types.h>
#include <sys/wait.h>

#define TIMEOUT 5
/* seconds */


static int pid;
static char fifo[256];

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
    if (param->flag == TC_VIDEO) {
	char fps[32];
	char cmdbuf[1024];

	tc_snprintf (fifo, 256, "%s-%d", "/tmp/tc-vncfifo", getpid());
	tc_snprintf (fps, 32, "%f", vob->fps);
	tc_snprintf (cmdbuf, 1024, "%s -o %s", TCXPM2RGB_EXE, fifo);

	mkfifo (fifo, 0600);

	switch (pid = fork()) {
	    case 0:
		{
		    int n=0;
		    const char *a[16];
		    char *c = vob->im_v_string;

		    setenv("VNCREC_MOVIE_FRAMERATE", fps, 1);
		    setenv("VNCREC_MOVIE_CMD", cmdbuf, 1);

		    //close(STDOUT_FILENO);
		    //close(STDERR_FILENO);

		    a[n++] = "vncrec";
		    a[n++] = "-movie";
		    a[n++] = vob->video_in_file;
		    if ( vob->im_v_string) {
			char *d = c;
			while (1) {
			    if (c && *c) {
				d = strchr (c, ' ');
				if (d && *d) { *d = '\0';
				    while (*c == ' ') c++;
				    a[n++] = c;
				    tc_log_info(MOD_NAME, "XX |%s|", c);
				    // FIXME: ??? - fromani 20051016
				    c = strchr(c, ' ');
				} else {
				    tc_log_info(MOD_NAME, "XXXX |%s|", c);
				    // FIXME: ??? - fromani 20051016
				    a[n++] = c;
				    break;
				}
			    } else  {
				d++;
				while (*d == ' ') d++;
				if (strchr(d, ' ')) *strchr(d, ' ') = '\0';
				a[n++] = d;
				tc_log_info(MOD_NAME, "XXX |%s|", c);
				// FIXME: ??? - fromani 20051016
				break;
			    }
			}
		    }
		    a[n++] = NULL;
		    if (execvp (a[0], (char **)&a[0])<0) {
			tc_log_perror(MOD_NAME, "execvp vncrec failed. Is vncrec in your $PATH?");
			return (TC_IMPORT_ERROR);
		    }
		}
		break;
	    default:
		break;
	}


	return (TC_IMPORT_OK);
    }
    return (TC_IMPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/


MOD_decode
{
    if (param->flag == TC_VIDEO) {
	int fd;
	int status, ret, wret;
	fd_set rfds;
	struct timeval tv;
	int n;

	while (1) {

	    // timeout to catch when vncrec died
	    tv.tv_sec = TIMEOUT;
	    tv.tv_usec = 0;

	    fd = open(fifo, O_RDONLY | O_NONBLOCK);
	    if (fd < 0) { tc_log_perror(MOD_NAME, "open"); break; }

	    FD_ZERO(&rfds);
	    FD_SET(fd, &rfds);

	    status = select(fd+1, &rfds, NULL, NULL, &tv);

	    if (status) {

		if (FD_ISSET(fd, &rfds)) {

		    n = 0;
		    while (n < param->size) {
			n += tc_pread(fd, param->buffer+n, param->size-n);
		    }
		}

		// valid frame in param->buffer


		close(fd);
		return (TC_IMPORT_OK);
	    } else {
		kill(pid, SIGKILL);
		wret = wait(&ret);
		close(fd);
		return (TC_IMPORT_ERROR);
	    }

	    close(fd);
	    return (TC_IMPORT_OK);

	}
    }

    return (TC_IMPORT_ERROR);
}


/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
    if (param->flag == TC_VIDEO) {
	int ret;
	kill(pid, SIGKILL);
	wait(&ret);
	unlink (fifo);
    }

    return (TC_IMPORT_OK);
}
