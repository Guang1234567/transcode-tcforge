# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DMODULE_PATH=\"$(MODULE_PATH)\" \
	-I$(top_srcdir) \
	$(LIBAVCODEC_CFLAGS) \
	$(DLDARWIN_CFLAGS)

AM_LDFLAGS = -export-dynamic

bin_PROGRAMS = transcode@TC_VERSUFFIX@
bin_SCRIPTS = gtranscode2.py


transcode@TC_VERSUFFIX@_LDADD = \
	$(DLDARWIN_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCUTIL_LIBS) \
	$(LIBTCEXT_LIBS) \
	$(LIBTCAUDIO_LIBS) \
	$(LIBTCVIDEO_LIBS) \
	$(LIBTCEXPORT_LIBS) \
	$(LIBTCMODULE_LIBS) \
	$(ACLIB_LIBS) \
	$(XIO_LIBS) \
	$(PTHREAD_LIBS) \
	-lm

EXTRA_DIST = \
	audio_trans.h \
	cmdline.h \
	cmdline_def.h \
	counter.h \
	decoder.h \
	dl_loader.h \
	runcontrol.h \
	filter.h \
	frame_threads.h \
	framebuffer.h \
	probe.h \
	socket.h \
	split.h \
	synchronizer.h \
	tc_defaults.h \
	transcode.h \
	video_trans.h 

transcode@TC_VERSUFFIX@_SOURCES = \
	transcode.c \
	audio_trans.c \
	cmdline.c \
	counter.c \
	decoder.c \
	dl_loader.c \
	runcontrol.c \
	encoder-buffer.c \
	filter.c \
	frame_threads.c \
	framebuffer.c \
	probe.c \
	socket.c \
	synchronizer.c \
	split.c \
	video_trans.c

