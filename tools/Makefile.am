# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DMODULE_PATH=\"$(MODULE_PATH)\" \
	-DTCEXPORT_PROFILE=1 \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

bin_PROGRAMS = \
	avifix \
	aviindex \
	avimerge \
	avisplit \
	avisync \
	tccfgshow@TC_VERSUFFIX@ \
	tcexport@TC_VERSUFFIX@ \
	tcmodchain@TC_VERSUFFIX@ \
	tcmodinfo@TC_VERSUFFIX@ \
	tcmp3cut@TC_VERSUFFIX@ \
	tcyait@TC_VERSUFFIX@

avisync_SOURCES = \
	avisync.c \
	avimisc.c \
	buffer.c \
	buffer.h \
	aud_scan.c \
	aud_scan_avi.c \
	aud_scan_avi.h \
	aud_scan.h
avisync_LDADD = \
	$(AVILIB_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCUTIL_LIBS)

avimerge_SOURCES = \
	avimerge.c \
	avimisc.c \
	aud_scan.c \
	aud_scan_avi.c \
	aud_scan_avi.h \
	aud_scan.h
avimerge_LDADD = \
	$(AVILIB_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCUTIL_LIBS)

avifix_SOURCES = \
	avifix.c \
	avimisc.c
avifix_LDADD = \
	$(AVILIB_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCUTIL_LIBS)

aviindex_SOURCES = \
	aviindex.c \
	avimisc.c \
	aud_scan.c \
	aud_scan.h
aviindex_LDADD = \
	$(AVILIB_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCUTIL_LIBS)

avisplit_SOURCES = \
	avisplit.c \
	avimisc.c \
	aud_scan.c \
	aud_scan_avi.c \
	aud_scan.h
	aud_scan_avi.h
avisplit_LDADD = \
	$(AVILIB_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCUTIL_LIBS)

tccfgshow@TC_VERSUFFIX@_SOURCES = tccfgshow.c
tccfgshow@TC_VERSUFFIX@_CPPFLAGS = $(AM_CPPFLAGS) \
	$(DLDARWIN_CFLAGS)
tccfgshow@TC_VERSUFFIX@_LDADD = \
	$(LIBTC_LIBS) \
	$(LIBTCEXPORT_LIBS) \
	$(LIBTCUTIL_LIBS) \
	$(LIBTCMODULE_LIBS)

tcmodinfo@TC_VERSUFFIX@_SOURCES = tcmodinfo.c tcstub.c
tcmodinfo@TC_VERSUFFIX@_CPPFLAGS = $(AM_CPPFLAGS) \
	$(DLDARWIN_CFLAGS) \
	-DTC_FRAMEBUFFER_STUBS=1
tcmodinfo@TC_VERSUFFIX@_LDADD = \
	$(DLDARWIN_LIBS) \
	$(ACLIB_LIBS) \
	$(AVILIB_LIBS) \
	$(GRAPHICSMAGICK_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCEXT_LIBS) \
	$(LIBTCMODULE_LIBS) \
	$(LIBTCVIDEO_LIBS) \
	$(LIBTCUTIL_LIBS) \
	$(PTHREAD_LIBS) \
	$(WAVLIB_LIBS) \
	-lm
tcmodinfo@TC_VERSUFFIX@_LDFLAGS = -export-dynamic

tcmodchain@TC_VERSUFFIX@_SOURCES = tcmodchain.c tcstub.c
tcmodchain@TC_VERSUFFIX@_CPPFLAGS = $(AM_CPPFLAGS) \
	$(DLDARWIN_CFLAGS) \
	-DTC_FRAMEBUFFER_STUBS=1
tcmodchain@TC_VERSUFFIX@_LDADD = \
	$(DLDARWIN_LIBS) \
	$(ACLIB_LIBS) \
	$(AVILIB_LIBS) \
	$(GRAPHICSMAGICK_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCEXT_LIBS) \
	$(LIBTCUTIL_LIBS) \
	$(LIBTCMODULE_LIBS) \
	$(LIBTCVIDEO_LIBS) \
	$(PTHREAD_LIBS) \
	$(WAVLIB_LIBS) \
	-lm
tcmodchain@TC_VERSUFFIX@_LDFLAGS = -export-dynamic

tcexport@TC_VERSUFFIX@_SOURCES = tcexport.c \
	tcstub.c \
	rawsource.c \
	rawsource.h \
	runcontrol.c \
	../src/framebuffer.c \
	../src/dl_loader.c \
	../src/probe.c
tcexport@TC_VERSUFFIX@_CPPFLAGS = $(AM_CPPFLAGS) \
	$(DLDARWIN_CFLAGS)
tcexport@TC_VERSUFFIX@_LDADD = \
	$(DLDARWIN_LIBS) \
	$(ACLIB_LIBS) \
	$(AVILIB_LIBS) \
	$(GRAPHICSMAGICK_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCEXT_LIBS) \
	$(LIBTCVIDEO_LIBS) \
	$(LIBTCAUDIO_LIBS) \
	$(LIBTCEXPORT_LIBS) \
	$(LIBTCMODULE_LIBS) \
	$(LIBTCUTIL_LIBS) \
	$(XIO_LIBS) \
	$(PTHREAD_LIBS) \
	$(WAVLIB_LIBS) \
	-lm
tcexport@TC_VERSUFFIX@_LDFLAGS = -export-dynamic

tcmp3cut@TC_VERSUFFIX@_SOURCES = \
	tcmp3cut.c \
	aud_scan.c \
	aud_scan.h
tcmp3cut@TC_VERSUFFIX@_LDADD = \
	$(LIBTC_LIBS) \
	$(LIBTCUTIL_LIBS)

tcyait@TC_VERSUFFIX@_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/filter
tcyait@TC_VERSUFFIX@_SOURCES = tcyait.c
tcyait@TC_VERSUFFIX@_LDADD = -lm


