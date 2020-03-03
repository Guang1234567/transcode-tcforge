# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/import

pkgdir = $(MODULE_PATH)

pkg_LTLIBRARIES = import_framegen.la

import_framegen_la_SOURCES = import_framegen.c pink.c
import_framegen_la_CPPFLAGS = $(AM_CPPFLAGS)
import_framegen_la_LDFLAGS = -module -avoid-version

EXTRA_DIST = pink.h

