# # Process this file with automake to produce Makefile.in.

AUTOMAKE_OPTIONS = foreign

if USE_DLDARWIN
LIBDLDARWIN = libdldarwin
endif

SUBDIRS = \
	aclib \
	avilib \
	mpeglib \
	$(LIBDLDARWIN) \
	libtcutil \
	libtcmodule \
	libtcext \
	libtc \
	libtcaudio \
	libtcvideo \
	libtcexport \
	encode \
	export \
	filter \
	import \
	multiplex \
	src \
	tccore \
	testsuite \
	tools \
	docs

EXTRA_DIST = STYLE transcode.spec hgversion.sh

docsdir = $(datadir)/doc/transcode@TC_VERSUFFIX@
docs_DATA = ChangeLog
