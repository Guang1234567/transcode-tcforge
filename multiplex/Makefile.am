# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/tccore

pkgdir = $(MODULE_PATH)

if HAVE_FFMPEG
MULTIPLEX_LAVF = multiplex_lavf.la
endif

if HAVE_OGG
MULTIPLEX_OGG = multiplex_ogg.la
endif

if HAVE_MJPEGTOOLS
MULTIPLEX_Y4M = multiplex_y4m.la
endif

pkg_LTLIBRARIES = \
	multiplex_avi.la \
	$(MULTIPLEX_LAVF) \
	multiplex_null.la \
	$(MULTIPLEX_OGG) \
	multiplex_pvn.la \
	multiplex_raw.la \
	$(MULTIPLEX_Y4M) \
	multiplex_wav.la


multiplex_avi_la_SOURCES = multiplex_avi.c
multiplex_avi_la_LDFLAGS = -module -avoid-version
multiplex_avi_la_LIBADD = $(AVILIB_LIBS)

multiplex_lavf_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBAVFORMAT_CFLAGS)
multiplex_lavf_la_LDFLAGS = -module -avoid-version
multiplex_lavf_la_LIBADD = $(LIBAVFORMAT_LIBS)

multiplex_null_la_SOURCES = multiplex_null.c
multiplex_null_la_LDFLAGS = -module -avoid-version

multiplex_ogg_la_SOURCES = multiplex_ogg.c
multiplex_ogg_la_CPPFLAGS = $(AM_CPPFLAGS) $(OGG_CFLAGS) $(SHOUT_CFLAGS)
multiplex_ogg_la_LDFLAGS = -module -avoid-version
multiplex_ogg_la_LIBADD = $(OGG_LIBS) $(SHOUT_LIBS)

multiplex_pvn_la_SOURCES = multiplex_pvn.c
multiplex_pvn_la_LDFLAGS = -module -avoid-version

multiplex_raw_la_SOURCES = multiplex_raw.c
multiplex_raw_la_LDFLAGS = -module -avoid-version

multiplex_y4m_la_SOURCES = multiplex_y4m.c
multiplex_y4m_la_CPPFLAGS = $(AM_CPPFLAGS) $(MJPEGTOOLS_CFLAGS)
multiplex_y4m_la_LDFLAGS = -module -avoid-version
multiplex_y4m_la_LIBADD = $(MJPEGTOOLS_LIBS)

multiplex_wav_la_SOURCES = multiplex_wav.c
multiplex_wav_la_LDFLAGS = -module -avoid-version
multiplex_wav_la_LIBADD = $(WAVLIB_LIBS)

EXTRA_DIST = \
	shout.cfg


