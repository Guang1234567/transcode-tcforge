# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	$(GRAPHICSMAGICK_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/libtc \
	-I$(top_srcdir)/libtcutil \
	-fPIC -DPIC

noinst_LTLIBRARIES = libtcext.la

libtcext_la_SOURCES = \
	ext.c \
	ext_gm.c \
	ext_ogg.c

EXTRA_DIST = \
	tc_avcodec.h \
	tc_ext.h \
	tc_lzo.h \
	tc_magick.h \
	tc_ogg.h

