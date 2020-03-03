# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DMODULE_PATH=\"$(MODULE_PATH)\" \
	-DREGISTRY_PATH=\"$(REGISTRY_PATH)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/libtcutil

noinst_LTLIBRARIES = libtcmodule.la

libtcmodule_la_SOURCES = \
	tcmodule.c \
	tcmoduleinfo.c \
	tcmoduleregistry.c

EXTRA_DIST = \
	tcmodule-core.h \
	tcmodule-data.h \
	tcmodule-info.h \
	tcmodule-plugin.h \
	tcmodule-registry.h

pkgdir = $(REGISTRY_PATH)
pkg_DATA = modules.cfg
