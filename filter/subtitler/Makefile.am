# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	$(X_CFLAGS) \
	$(FREETYPE2_CFLAGS) \
	$(ICONV_CFLAGS)

pkgdir = $(MODULE_PATH)

pkg_LTLIBRARIES = filter_subtitler.la

filter_subtitler_la_SOURCES = \
	filter_subtitler.c \
	color_processor.c \
	external_programs.c \
	frame_list.c \
	load_font.c \
	load_pictures.c \
	load_ppml_file.c \
	object_list.c \
	parser.c \
	text_processing.c \
	x11.c

filter_subtitler_la_LDFLAGS = -module -avoid-version

filter_subtitler_la_LIBADD = \
	$(X_LIBS) \
	$(X_PRE_LIBS) \
	-lXext \
	-lX11 \
	$(X_EXTRA_LIBS) \
	-lXpm \
	-lXaw \
	-lXt \
	-lm \
	$(FREETYPE2_LIBS) \
	$(ICONV_LIBS)

EXTRA_DIST = \
	CHANGES \
	HOWTO_USE_THIS \
	LICENSE \
	README.COLOR.PROCESSOR \
	README.PPML \
	demo.ppml \
	frame_list.h \
	load_font.h \
	object_list.h \
	rose.ppm \
	subtitler.h \
	subtitler_proto.h \
	sun.ppm \
	x11.h
