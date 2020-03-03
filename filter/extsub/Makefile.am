# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

pkgdir = $(MODULE_PATH)

pkg_LTLIBRARIES = filter_extsub.la filter_extsub2.la

filter_extsub_la_SOURCES = filter_extsub.c subproc.c subtitle_buffer.c
filter_extsub_la_LDFLAGS = -module -avoid-version

filter_extsub2_la_SOURCES = filter_extsub.c subproc.c subtitle_buffer.c
filter_extsub2_la_LDFLAGS = -module -avoid-version

EXTRA_DIST = subproc.h subtitle_buffer.h
