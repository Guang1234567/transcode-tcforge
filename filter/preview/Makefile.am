# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DMODULE_PATH=\"$(MODULE_PATH)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	$(DLDARWIN_CFLAGS) \
	$(LIBDV_CFLAGS) \
	$(X_CFLAGS) \
	$(SDL_CFLAGS)

if HAVE_LIBXV
LIBS_XV = -lXv
endif

if HAVE_SDL
LIBS_SDL = $(SDL_LIBS)
endif

LIBS = \
	$(LIBS_XV) \
	$(LIBS_SDL)

pkgdir = $(MODULE_PATH)

if HAVE_LIBXV
MOD_PV = filter_pv.la
endif

if HAVE_LIBDV
MOD_PREVIEW = filter_preview.la
endif

if HAVE_SDL
MOD_SDLVIEW = filter_sdlview.la
endif

pkg_LTLIBRARIES = $(MOD_PREVIEW) $(MOD_SDLVIEW) $(MOD_PV)

filter_preview_la_SOURCES = filter_preview.c display.c
filter_preview_la_LDFLAGS = -module -avoid-version
filter_preview_la_LIBADD = \
	$(X_LIBS) \
	$(X_PRE_LIBS) \
	-lXext \
	-lX11 \
	$(X_EXTRA_LIBS) \
	$(LIBDV_LIBS)

filter_pv_la_SOURCES = filter_pv.c pv.c
filter_pv_la_LDFLAGS = -module -avoid-version
filter_pv_la_LIBADD = \
	$(X_LIBS) \
	$(X_PRE_LIBS) \
	-lXext \
	-lX11 \
	$(LIBS_XV) \
	$(DLDARWIN_LIBS)

filter_sdlview_la_SOURCES = filter_sdlview.c
filter_sdlview_la_LDFLAGS = -module -avoid-version
filter_sdlview_la_LIBADD = $(SDL_LIBS)

EXTRA_DIST = \
	display.h \
	dv_types.h \
	filter_preview.h \
	font_xpm.h \
	pv.h

