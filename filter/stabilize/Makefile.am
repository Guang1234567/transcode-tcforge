# # Process this file with automake to produce Makefile.in.

if WORDS_BIGENDIAN
BG = -DBIG_ENDIAN
endif

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	$(BG) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src
	-C99

pkgdir = $(MODULE_PATH)

pkg_LTLIBRARIES = filter_stabilize.la filter_transform.la

filter_stabilize_la_SOURCES = filter_stabilize.c transform.c 
filter_stabilize_la_LDFLAGS = -module -avoid-version

filter_transform_la_SOURCES = filter_transform.c transform.c
filter_transform_la_LDFLAGS = -module -avoid-version

