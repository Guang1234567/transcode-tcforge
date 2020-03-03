# # Process this file with automake to produce Makefile.in.

if WORDS_BIGENDIAN
BG = -DBIG_ENDIAN
endif

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	$(BG) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

pkgdir = $(MODULE_PATH)

pkg_LTLIBRARIES = filter_divxkey.la

filter_divxkey_la_SOURCES = filter_divxkey.c bitstream.c
filter_divxkey_la_LDFLAGS = -module -avoid-version

EXTRA_DIST = bitstream.h
