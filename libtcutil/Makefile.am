# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = -I$(top_srcdir)

noinst_LTLIBRARIES = libtcutil.la

if HAVE_IBP
XIO_FILES = libxio.c
endif

if !HAVE_GETOPT_LONG_ONLY
GETOPT_FILES = getopt.c getopt1.c
endif

if HAVE_GETTIMEOFDAY
TIMER_FILES = tctimer.c
endif

libtcutil_la_SOURCES = \
	cfgfile.c \
	tcglob.c \
	ioutils.c \
	tclist.c \
	logging.c \
	memutils.c \
	optstr.c \
	strlcat.c \
	strlcpy.c \
	strutils.c \
	tcthread.c \
	$(GETOPT_FILES) \
	$(TIMER_FILES) \
	$(XIO_FILES)

EXTRA_DIST = \
	cfgfile.h \
	common.h \
	getopt.h \
	tcglob.h \
	ioutils.h \
	tclist.h \
	logging.h \
	memutils.h \
	optstr.h \
	static_optstr.h \
	static_tclist.h \
	static_tctimer.h \
	static_xio.h \
	strutils.h \
	tcutil.h \
	tctimer.h \
	tcthread.h \
	xio.h

