# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	$(XIO_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/libtcutil \
	-fPIC -DPIC

noinst_LTLIBRARIES = libavi.la libwav.la

# this can be improved

if ENABLE_EXPERIMENTAL
PLATFORM = platform_tc.c
else
PLATFORM = platform_posix.c
endif

libavi_la_SOURCES = \
	$(PLATFORM) \
	avidump.c \
	avilib.c \
	avilib.h

libwav_la_SOURCES = \
	$(PLATFORM) \
	wavlib.c \
	wavlib.h

EXTRA_DIST = README.avilib
