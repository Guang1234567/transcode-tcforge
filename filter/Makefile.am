# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

pkgdir = $(MODULE_PATH)

if CAN_COMPILE_C_ALTIVEC
if OS_DARWIN
ALTIVEC = -faltivec
else
ALTIVEC = -maltivec -mabi=altivec
endif
endif

if HAVE_GRAPHICSMAGICK
FILTER_COMPARE = filter_compare.la
FILTER_LOGO = filter_logo.la
FILTER_LOGOAWAY = filter_logoaway.la
endif

if HAVE_FFMPEG
FILTER_RESAMPLE = filter_resample.la
if HAVE_LIBPOSTPROC
FILTER_PP = filter_pp.la
endif
endif

if HAVE_FREETYPE2
FILTER_TEXT = filter_text.la
endif

if HAVE_GCC2
F_TOMSMOCOMP =
else  # GCC2
if ARCH_X86
if HAVE_ASM_MMX
if HAVE_ASM_SSE
F_TOMSMOCOMP = tomsmocomp
endif # ARCH_X86
endif # HAVE_ASM_MMX
endif # HAVE_ASM_SSE
endif # GCC2

AM_CFLAGS = $(ALTIVEC)

pkg_LTLIBRARIES = \
	filter_29to23.la \
	filter_32detect.la \
	filter_32drop.la \
	filter_aclip.la \
	filter_ascii.la \
	filter_astat.la \
	filter_barrel.la \
	$(FILTER_COMPARE) \
	filter_control.la \
	filter_cpaudio.la \
	filter_decimate.la \
	filter_denoise3d.la \
	filter_detectclipping.la \
	filter_detectsilence.la \
	filter_dnr.la \
	filter_doublefps.la \
	filter_facemask.la \
	filter_fieldanalysis.la\
	filter_fields.la \
	filter_fps.la \
	filter_hqdn3d.la \
	filter_invert.la \
	filter_ivtc.la \
	filter_levels.la \
	$(FILTER_LOGO) \
	$(FILTER_LOGOAWAY) \
	filter_lowpass.la \
	filter_mask.la \
	filter_modfps.la \
	filter_msharpen.la \
	filter_normalize.la \
	filter_null.la \
	$(FILTER_PP) \
	$(FILTER_RESAMPLE) \
	filter_skip.la \
	filter_slowmo.la \
	filter_smartbob.la \
	filter_smartdeinter.la \
	filter_smartyuv.la \
	filter_smooth.la \
	filter_testframe.la \
	$(FILTER_TEXT) \
	filter_unsharp.la \
	filter_whitebalance.la \
	filter_xsharpen.la \
	filter_yait.la

filter_29to23_la_SOURCES = filter_29to23.c
filter_29to23_la_LDFLAGS = -module -avoid-version

filter_32detect_la_SOURCES = filter_32detect.c
filter_32detect_la_LDFLAGS = -module -avoid-version

filter_32drop_la_SOURCES = filter_32drop.c
filter_32drop_la_LDFLAGS = -module -avoid-version

filter_aclip_la_SOURCES = filter_aclip.c
filter_aclip_la_LDFLAGS = -module -avoid-version

filter_ascii_la_SOURCES = filter_ascii.c
filter_ascii_la_LDFLAGS = -module -avoid-version

filter_astat_la_SOURCES = filter_astat.c
filter_astat_la_LDFLAGS = -module -avoid-version

filter_barrel_la_SOURCES = filter_barrel.c
filter_barrel_la_LDFLAGS = -module -avoid-version

filter_compare_la_SOURCES = filter_compare.c
filter_compare_la_CPPFLAGS = $(AM_CPPFLAGS) $(GRAPHICSMAGICK_CFLAGS)
filter_compare_la_LDFLAGS = -module -avoid-version
filter_compare_la_LIBADD = $(GRAPHICSMAGICK_LIBS) $(LIBTCEXT_LIBS)

filter_control_la_SOURCES = filter_control.c
filter_control_la_LDFLAGS = -module -avoid-version

filter_cpaudio_la_SOURCES = filter_cpaudio.c
filter_cpaudio_la_LDFLAGS = -module -avoid-version

filter_decimate_la_SOURCES = filter_decimate.c
filter_decimate_la_LDFLAGS = -module -avoid-version

filter_denoise3d_la_SOURCES = filter_denoise3d.c
filter_denoise3d_la_LDFLAGS = -module -avoid-version

filter_detectclipping_la_SOURCES = filter_detectclipping.c
filter_detectclipping_la_LDFLAGS = -module -avoid-version

filter_detectsilence_la_SOURCES = filter_detectsilence.c
filter_detectsilence_la_LDFLAGS = -module -avoid-version
filter_detectsilence_la_LIBADD = -lm

filter_dnr_la_SOURCES = filter_dnr.c
filter_dnr_la_LDFLAGS = -module -avoid-version
filter_dnr_la_LIBADD = -lm

filter_doublefps_la_SOURCES = filter_doublefps.c
filter_doublefps_la_LDFLAGS = -module -avoid-version

filter_facemask_la_SOURCES = filter_facemask.c
filter_facemask_la_LDFLAGS = -module -avoid-version

filter_fieldanalysis_la_SOURCES = filter_fieldanalysis.c
filter_fieldanalysis_la_LDFLAGS = -module -avoid-version

filter_fields_la_SOURCES = filter_fields.c
filter_fields_la_LDFLAGS = -module -avoid-version

filter_fps_la_SOURCES = filter_fps.c
filter_fps_la_LDFLAGS = -module -avoid-version

filter_hqdn3d_la_SOURCES = filter_hqdn3d.c
filter_hqdn3d_la_LDFLAGS = -module -avoid-version

filter_ivtc_la_SOURCES = filter_ivtc.c
filter_ivtc_la_LDFLAGS = -module -avoid-version

filter_invert_la_SOURCES = filter_invert.c
filter_invert_la_LDFLAGS = -module -avoid-version

filter_levels_la_SOURCES = filter_levels.c
filter_levels_la_LDFLAGS = -module -avoid-version

filter_logo_la_SOURCES = filter_logo.c
filter_logo_la_CPPFLAGS = $(AM_CPPFLAGS) $(GRAPHICSMAGICK_CFLAGS)
filter_logo_la_LDFLAGS = -module -avoid-version
filter_logo_la_LIBADD = $(GRAPHICSMAGICK_LIBS) $(LIBTCEXT_LIBS)

filter_logoaway_la_SOURCES = filter_logoaway.c
filter_logoaway_la_CPPFLAGS = $(AM_CPPFLAGS) $(GRAPHICSMAGICK_CFLAGS)
filter_logoaway_la_LDFLAGS = -module -avoid-version
filter_logoaway_la_LIBADD = $(GRAPHICSMAGICK_LIBS) $(LIBTCEXT_LIBS)

filter_lowpass_la_SOURCES = filter_lowpass.c
filter_lowpass_la_LDFLAGS = -module -avoid-version

filter_mask_la_SOURCES = filter_mask.c
filter_mask_la_LDFLAGS = -module -avoid-version

filter_modfps_la_SOURCES = filter_modfps.c
filter_modfps_la_LDFLAGS = -module -avoid-version
filter_modfps_la_LIBADD = -lm

filter_msharpen_la_SOURCES = filter_msharpen.c
filter_msharpen_la_LDFLAGS = -module -avoid-version

filter_normalize_la_SOURCES = filter_normalize.c
filter_normalize_la_LDFLAGS = -module -avoid-version
filter_normalize_la_LIBADD = -lm

filter_null_la_SOURCES = filter_null.c
filter_null_la_LDFLAGS = -module -avoid-version

filter_pp_la_SOURCES = filter_pp.c
filter_pp_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBPOSTPROC_CFLAGS)
filter_pp_la_LIBADD = $(LIBPOSTPROC_LIBS)
filter_pp_la_LDFLAGS = -module -avoid-version

filter_skip_la_SOURCES = filter_skip.c
filter_skip_la_LDFLAGS = -module -avoid-version

filter_resample_la_SOURCES = filter_resample.c
filter_resample_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBAVCODEC_CFLAGS)
filter_resample_la_LDFLAGS = -module -avoid-version
filter_resample_la_LIBADD = $(LIBAVCODEC_LIBS)

filter_slowmo_la_SOURCES = filter_slowmo.c
filter_slowmo_la_LDFLAGS = -module -avoid-version

filter_smartbob_la_SOURCES = filter_smartbob.c
filter_smartbob_la_LDFLAGS = -module -avoid-version

filter_smartdeinter_la_SOURCES = filter_smartdeinter.c
filter_smartdeinter_la_LDFLAGS = -module -avoid-version

filter_smartyuv_la_SOURCES = filter_smartyuv.c
filter_smartyuv_la_LDFLAGS = -module -avoid-version

filter_smooth_la_SOURCES = filter_smooth.c
filter_smooth_la_LDFLAGS = -module -avoid-version

filter_testframe_la_SOURCES = filter_testframe.c
filter_testframe_la_LDFLAGS = -module -avoid-version

filter_text_la_SOURCES = filter_text.c
filter_text_la_CPPFLAGS = $(AM_CPPFLAGS) $(FREETYPE2_CFLAGS)
filter_text_la_LDFLAGS = -module -avoid-version
filter_text_la_LIBADD  = $(FREETYPE2_LIBS)

filter_unsharp_la_SOURCES = filter_unsharp.c
filter_unsharp_la_LDFLAGS = -module -avoid-version

filter_whitebalance_la_SOURCES = filter_whitebalance.c
filter_whitebalance_la_LDFLAGS = -module -avoid-version

filter_xsharpen_la_SOURCES = filter_xsharpen.c
filter_xsharpen_la_LDFLAGS = -module -avoid-version

filter_yait_la_SOURCES = filter_yait.c
filter_yait_la_LDFLAGS = -module -avoid-version 
filter_yait_la_LIBADD = -lm


EXTRA_DIST =  \
	parse_csv.awk.in \
	filter_list.awk.in \
	mmx.h \
	yait.h

if HAVE_X11
PREVIEW = preview

if HAVE_FREETYPE2
if HAVE_XAW_SIMPLE
if HAVE_LIBXPM
if HAVE_ICONV
SUBTITLER = subtitler
endif
endif
endif
endif

endif

SUBDIRS = \
	divxkey \
	extsub \
	$(PREVIEW) \
	stabilize \
	$(SUBTITLER) \
	$(F_TOMSMOCOMP) \
	yuvdenoise

pkg_SCRIPTS = \
	filter_list.awk \
	parse_csv.awk
