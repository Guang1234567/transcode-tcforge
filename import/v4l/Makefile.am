# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/import

pkgdir = $(MODULE_PATH)

if HAVE_LIBV4L2
if HAVE_LIBV4LCONVERT
MOD_V4LCAM = import_v4lcam.la
endif
endif

if HAVE_V4L
MOD_V4LDEV = import_v4l.la import_v4l2.la
endif

pkg_LTLIBRARIES = $(MOD_V4LDEV) $(MOD_V4LCAM)

import_v4l_la_SOURCES = import_v4l.c
import_v4l_la_LDFLAGS = -module -avoid-version

import_v4l2_la_SOURCES = import_v4l2.c
import_v4l2_la_LDFLAGS = -module -avoid-version

import_v4lcam_la_SOURCES = import_v4lcam.c
import_v4lcam_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBV4L2_CFLAGS) $(LIBV4LCONVERT_CFLAGS)
import_v4lcam_la_LDFLAGS = -module -avoid-version
import_v4lcam_la_LIBADD = $(LIBV4L2_LIBS) $(LIBV4LCONVERT_LIBS)

EXTRA_DIST = videodev.h videodev2.h

