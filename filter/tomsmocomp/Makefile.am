# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

AM_CFLAGS = -fno-omit-frame-pointer

pkgdir = $(MODULE_PATH)

pkg_LTLIBRARIES = filter_tomsmocomp.la

filter_tomsmocomp_la_SOURCES = \
	filter_tomsmocomp.c \
	tomsmocompfilter_mmx.c \
	tomsmocompfilter_3dnow.c \
	tomsmocompfilter_sse.c

#####################################################################
# gcc-4.x only allows mmx inline assembly keywords when
# -mmmx is specified.
# (http://osdir.com/ml/video.transcode.devel/2005-07/msg00004.html)
# it's okay to force -mmmx because the filter won't be built
# without an MMX capable CPU anyway, as specified in ../Makefile.am.
#####################################################################
filter_tomsmocomp_la_CFLAGS = -mmmx
filter_tomsmocomp_la_LDFLAGS = -module -avoid-version

EXTRA_DIST = \
	Readme_TomsMoComp.txt \
	SearchLoop0A.inc \
	SearchLoopBottom.inc \
	SearchLoopEdgeA.inc \
	SearchLoopEdgeA8.inc \
	SearchLoopOddA.inc \
	SearchLoopOddA2.inc \
	SearchLoopOddA6.inc \
	SearchLoopOddAH.inc \
	SearchLoopOddAH2.inc \
	SearchLoopTop.inc \
	SearchLoopVA.inc \
	SearchLoopVAH.inc \
	StrangeBob.inc \
	TomsMoCompAll.inc \
	TomsMoCompAll2.inc \
	WierdBob.inc \
	dscaler_interface.h \
	filter_tomsmocomp.h \
	tomsmocompfilter.h \
	tomsmocompmacros.h
