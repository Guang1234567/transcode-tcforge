# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/import \
	-I$(top_srcdir)/src

pkgdir = $(MODULE_PATH)

if ENABLE_NUV
MOD = import_nuv.la
endif

pkg_LTLIBRARIES = $(MOD)

import_nuv_la_SOURCES = import_nuv.c RTjpegN.c
import_nuv_la_CPPFLAGS = $(AM_CPPFLAGS) $(LZO_CFLAGS)
import_nuv_la_LDFLAGS = -module -avoid-version
import_nuv_la_LIBADD = $(LZO_LIBS) -lm

EXTRA_DIST =  \
	LICENSE.rtjpeg \
	README.rtjpeg \
	RTjpegN.h \
	nuppelvideo.h
