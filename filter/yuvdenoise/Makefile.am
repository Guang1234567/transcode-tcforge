# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

pkgdir = $(MODULE_PATH)

pkg_LTLIBRARIES = \
	filter_yuvdenoise.la \
	filter_yuvmedian.la

filter_yuvdenoise_la_SOURCES = \
	filter_yuvdenoise.c \
	deinterlace.c \
	denoise.c \
	motion.c
filter_yuvdenoise_la_LDFLAGS = -module -avoid-version
filter_yuvdenoise_la_LIBADD = $(ACLIB_LIBS)

filter_yuvmedian_la_SOURCES = filter_yuvmedian.c
filter_yuvmedian_la_LDFLAGS = -module -avoid-version

EXTRA_DIST = \
	deinterlace.h \
	denoise.h \
	global.h \
	mjpeg_types.h \
	motion.h
