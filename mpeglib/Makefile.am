# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	$(XIO_CFLAGS) \
	-DMPEG_LITTLE_ENDIAN=1 \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/libtcutil \
	-fPIC -DPIC

noinst_LTLIBRARIES = libmpeg.la

# this can be improved

if ENABLE_EXPERIMENTAL
PLATFORM = platform_tc.c
else
PLATFORM = platform_posix.c
endif

libmpeg_la_SOURCES = \
	$(PLATFORM) \
	elementary.c \
	mpeglib.c \
	packet.c \
	probe.c \
	program.c \
	mpeglib_private.h \
	mpeglib.h \
	static_mpeglib.h

EXTRA_DIST = README.mpeglib

