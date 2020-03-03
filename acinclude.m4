dnl TC_CHECK_STD_HEADERS
dnl Ensure that standard headers are available, and abort if not.
dnl
AC_DEFUN([TC_CHECK_STD_HEADERS],
    [AC_CACHE_CHECK(for standard header files, ac_cv_header_std,
        [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
]])],
            [ac_cv_header_std=yes],
            [ac_cv_header_std=no])])
if test x"$ac_cv_header_std" != x"yes"; then
    AC_MSG_ERROR([cannot compile with one or more of:
    errno.h fcntl.h limits.h stdarg.h stddef.h stdint.h stdio.h
    stdlib.h string.h time.h sys/stat.h sys/time.h sys/types.h
See \`config.log' for more details.])
fi
dnl Stop autoconf from running its own standard header check.
AC_PROVIDE([AC_HEADER_STDC])dnl
dnl Define HAVE_* macros in case anybody wants them anyway.
AC_DEFINE([STDC_HEADERS], 1,
          [Define to 1 if you have the ANSI C header files.])
AC_DEFINE([HAVE_ERRNO_H], 1,
          [Define to 1 if you have the <errno.h> include file.])
AC_DEFINE([HAVE_FCNTL_H], 1,
          [Define to 1 if you have the <fcntl.h> include file.])
AC_DEFINE([HAVE_LIMITS_H], 1,
          [Define to 1 if you have the <limits.h> include file.])
AC_DEFINE([HAVE_STDARG_H], 1,
          [Define to 1 if you have the <stdarg.h> include file.])
AC_DEFINE([HAVE_STDDEF_H], 1,
          [Define to 1 if you have the <stddef.h> include file.])
AC_DEFINE([HAVE_STDINT_H], 1,
          [Define to 1 if you have the <stdint.h> include file.])
AC_DEFINE([HAVE_STDIO_H], 1,
          [Define to 1 if you have the <stdio.h> include file.])
AC_DEFINE([HAVE_STDLIB_H], 1,
          [Define to 1 if you have the <stdlib.h> include file.])
AC_DEFINE([HAVE_STRING_H], 1,
          [Define to 1 if you have the <string.h> include file.])
AC_DEFINE([HAVE_TIME_H], 1,
          [Define to 1 if you have the <time.h> include file.])
AC_DEFINE([HAVE_SYS_STAT_H], 1,
          [Define to 1 if you have the <sys/stat.h> include file.])
AC_DEFINE([HAVE_SYS_TIME_H], 1,
          [Define to 1 if you have the <sys/time.h> include file.])
AC_DEFINE([HAVE_SYS_TYPES_H], 1,
          [Define to 1 if you have the <sys/types.h> include file.])
])

dnl Argh, we have to redefine this to stop headers.m4 from doing extra checks
AC_DEFUN([_AC_INCLUDES_DEFAULT_REQUIREMENTS],
[m4_divert_text([DEFAULTS],
[# Factoring default headers for most tests.
dnl If ever you change this variable, please keep autoconf.texi in sync.
ac_includes_default="\
#include <stdio.h>
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#if STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# if HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#if HAVE_STRING_H
# if !STDC_HEADERS && HAVE_MEMORY_H
#  include <memory.h>
# endif
# include <string.h>
#endif
#if HAVE_STRINGS_H
# include <strings.h>
#endif
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif
#if HAVE_UNISTD_H
# include <unistd.h>
#endif"
])])

dnl -----------------------------------------------------------------------

dnl TC_C_GCC_ATTRIBUTES
dnl See if __attribute__((format(...))) and __attribute__((unused)) are
dnl available.
dnl
AC_DEFUN([TC_C_GCC_ATTRIBUTES],
    [AC_CACHE_CHECK([__attribute__((...)) support],
        [ac_cv_c_gcc_attributes],
        [AC_TRY_COMPILE([],
            [extern int foo(char *,...) __attribute__((format(printf,1,2)));
             __attribute__((unused)) static int bar;],
            [ac_cv_c_gcc_attributes=yes],
            [ac_cv_c_gcc_attributes=no])])
    if test x"$ac_cv_c_gcc_attributes" = x"yes"; then
        AC_DEFINE([HAVE_GCC_ATTRIBUTES], 1,
               [Compiler understands __attribute__((...))])
    fi])

dnl -----------------------------------------------------------------------

dnl TC_C_ATTRIBUTE_ALIGNED
dnl Define ATTRIBUTE_ALIGNED_MAX to the maximum alignment if this is supported.
dnl
AC_DEFUN([TC_C_ATTRIBUTE_ALIGNED],
    [AC_CACHE_CHECK([__attribute__((aligned())) support],
	[ac_cv_c_attribute_aligned],
	[ac_cv_c_attribute_aligned=0
	for ac_cv_c_attr_align_try in 2 4 8 16 32 64; do
	    AC_TRY_COMPILE([],
		[static char c __attribute__((aligned($ac_cv_c_attr_align_try))) = 0; return c;],
		[ac_cv_c_attribute_aligned=$ac_cv_c_attr_align_try])
	done])
    if test x"$ac_cv_c_attribute_aligned" != x"0"; then
	AC_DEFINE_UNQUOTED([ATTRIBUTE_ALIGNED_MAX],
	    [$ac_cv_c_attribute_aligned],[maximum supported data alignment])
    fi])

dnl -----------------------------------------------------------------------

dnl TC_TRY_CFLAGS (CFLAGS, [ACTION-IF-WORKS], [ACTION-IF-FAILS])
dnl Check if $CC supports a given set of CFLAGS.

AC_DEFUN([TC_TRY_CFLAGS],
    [AC_MSG_CHECKING([if $CC supports $1 flags])
    SAVE_CFLAGS="$CFLAGS"
    CFLAGS="$1"
    AC_TRY_COMPILE([],[],[ac_cv_try_cflags_ok=yes],[ac_cv_try_cflags_ok=no])
    CFLAGS="$SAVE_CFLAGS"
    AC_MSG_RESULT([$ac_cv_try_cflags_ok])
    if test x"$ac_cv_try_cflags_ok" = x"yes"; then
	ifelse([$2],[],[:],[$2])
    else
	ifelse([$3],[],[:],[$3])
    fi])

dnl -----------------------------------------------------------------------

dnl TC_CHECK_V4L([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for video4linux headers, and define HAVE_STRUCT_V4L2_BUFFER
dnl
AC_DEFUN([TC_CHECK_V4L],
[
AC_MSG_CHECKING([whether v4l support is requested])
AC_ARG_ENABLE(v4l,
  AC_HELP_STRING([--enable-v4l],
    [enable v4l/v4l2 support (no)]),
  [case "${enableval}" in
    yes) ;;
    no)  ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-v4l) ;;
  esac],
  [enable_v4l=no])
AC_MSG_RESULT($enable_v4l)

have_v4l=no
if test x"$enable_v4l" = x"yes" ; then
  AC_CHECK_HEADERS([linux/videodev.h], [v4l=yes], [v4l=no])
  AC_CHECK_HEADERS([linux/videodev2.h], [v4l2=yes], [v4l2=no],
    [#include <linux/types.h>])

  if test x"$v4l2" = x"yes" ; then
    AC_MSG_CHECKING([for struct v4l2_buffer in videodev2.h])
    dnl (includes, function-body, [action-if-found], [action-if-not-found])
    AC_TRY_COMPILE([
#include <linux/types.h>
#include <linux/videodev2.h>
],   [
struct v4l2_buffer buf;
buf.memory = V4L2_MEMORY_MMAP
],    [AC_DEFINE([HAVE_STRUCT_V4L2_BUFFER], 1,
        [define if your videodev2 header has struct v4l2_buffer])
        AC_MSG_RESULT([yes])],
      [AC_MSG_RESULT([no])])
  fi

  if test x"$v4l" = x"yes" -o x"$v4l2" = x"yes" ; then
    have_v4l=yes
    ifelse([$1], , :, [$1])
  else
    AC_MSG_ERROR([v4l is requested, but cannot find headers])
  fi
fi
])

dnl -----------------------------------------------------------------------

dnl TC_CHECK_BKTR([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for bktr headers
dnl
AC_DEFUN([TC_CHECK_BKTR],
[
AC_MSG_CHECKING([whether bktr support is requested])
AC_ARG_ENABLE(bktr,
  AC_HELP_STRING([--enable-bktr],
    [enable bktr support (no)]),
  [case "${enableval}" in
    yes) ;;
    no)  ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-bktr) ;;
  esac],
  [enable_bktr=no])
AC_MSG_RESULT($enable_bktr)

have_bktr="no"
if test x"$enable_bktr" = x"yes" ; then
  AC_CHECK_HEADERS([dev/ic/bt8xx.h], [have_bktr="yes"])
  if test x"$have_bktr" = x"no" ; then
    AC_CHECK_HEADERS([dev/bktr/ioctl_bt848.h], [have_bktr="yes"])
  fi
  if test x"$have_bktr" = x"no" ; then
    AC_CHECK_HEADERS([machine/ioctl_bt848.h], [have_bktr="yes"])
  fi

  if test x"$have_bktr" = x"yes" ; then
    have_bktr="yes"
    ifelse([$1], , :, [$1])
  else
    AC_MSG_ERROR([bktr is requested, but cannot find headers])
  fi
fi
])

dnl -----------------------------------------------------------------------

dnl TC_CHECK_SUNAU([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for sunau headers
dnl
AC_DEFUN([TC_CHECK_SUNAU],
[
AC_MSG_CHECKING([whether sunau support is requested])
AC_ARG_ENABLE(sunau,
  AC_HELP_STRING([--enable-sunau],
    [enable sunau support (no)]),
  [case "${enableval}" in
    yes) ;;
    no)  ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-sunau) ;;
  esac],
  [enable_sunau=no])
AC_MSG_RESULT($enable_sunau)

have_sunau="no"
if test x"$enable_sunau" = x"yes" ; then
  AC_CHECK_HEADERS([sys/audioio.h], [have_sunau="yes"])

  if test x"$have_sunau" = x"yes" ; then
    have_sunau="yes"
    ifelse([$1], , :, [$1])
  else
    AC_MSG_ERROR([sunau is requested, but cannot find headers])
  fi
fi
])

dnl -----------------------------------------------------------------------

dnl TC_CHECK_OSS([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for OSS headers
dnl
AC_DEFUN([TC_CHECK_OSS],
[
AC_MSG_CHECKING([whether OSS support is requested])
AC_ARG_ENABLE(oss,
  AC_HELP_STRING([--enable-oss],
    [enable OSS audio support (no)]),
  [case "${enableval}" in
    yes) ;;
    no)  ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-oss) ;;
  esac],
  [enable_oss=no])
AC_MSG_RESULT($enable_oss)

have_oss="no"
if test x"$enable_oss" = x"yes" ; then
  AC_CHECK_HEADERS([sys/soundcard.h], [have_oss="yes"])
  if test x"$have_oss" = x"no" ; then
    AC_CHECK_HEADERS([soundcard.h], [have_oss="yes"])
  fi

  if test x"$have_oss" = x"yes" ; then
    have_oss="yes"
    ifelse([$1], , :, [$1])
  else
    AC_MSG_ERROR([OSS is requested, but cannot find headers])
  fi
fi
])

dnl -----------------------------------------------------------------------

dnl TC_CHECK_ALSA([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for ALSA headers
dnl
AC_DEFUN([TC_CHECK_ALSA],
[
AC_MSG_CHECKING([whether ALSA support is requested])
AC_ARG_ENABLE(alsa,
  AC_HELP_STRING([--enable-alsa],
    [enable ALSA audio support (no)]),
  [case "${enableval}" in
    yes) ;;
    no)  ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-alsa) ;;
  esac],
  [enable_alsa=no])
AC_MSG_RESULT($enable_alsa)

have_alsa="no"
if test x"$enable_alsa" = x"yes" ; then
  AC_CHECK_HEADERS([alsa/asoundlib.h], [have_alsa="yes"])

  if test x"$have_alsa" = x"yes" ; then
    have_alsa="yes"
    ifelse([$1], , :, [$1])
  else
    AC_MSG_ERROR([ALSA is requested, but cannot find headers])
  fi
fi
])

dnl -----------------------------------------------------------------------

dnl TC_PATH_IBP([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for ibp libraries, and define IBP_LIBS
dnl
AC_DEFUN([TC_PATH_IBP],
[
AC_MSG_CHECKING([whether ibp and lors support is requested])
AC_ARG_ENABLE(ibp,
  AC_HELP_STRING([--enable-ibp],
    [enable ibp support (no)]),
  [case "${enableval}" in
    yes) ;;
    no)  ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-ibp) ;;
  esac],
  enable_ibp=no)
AC_MSG_RESULT($enable_ibp)

have_ibp=no
if test x"$enable_ibp" = x"yes" ; then
  AC_MSG_CHECKING(for ibp and lors)
  if test x"$have_libxml2" = x"yes" ; then
    OLD_LIBS="$LIBS"
    AC_ARG_WITH(libfdr,
      AC_HELP_STRING([--with-libfdr=DIR],
        [base directory for libfdr]),
      [CPPFLAGS="-I$with_libfdr/include $CPPFLAGS"
        LIBFDR=yes
        IBP_LIBS1="-L$with_libfdr/lib -lfdr $LIBS"],
      [AC_CHECK_LIB(fdr, jval_v,
        [IBP_LIBS1="-lfdr"],
        [AC_MSG_ERROR(unable to locate libfdr)])])

    AC_ARG_WITH(libibp,
      AC_HELP_STRING([--with-libibp=DIR],
        [base directory for libibp]),
      [CPPFLAGS="-I$with_libibp/include $CPPFLAGS"
        LIBIBP=yes
        IBP_LIBS1="-L$with_libibp/lib -libp $PTHREAD_LIBS $IBP_LIBS1"],
      [LIBS="$PTHREAD_LIBS $IBP_LIBS1"
        AC_CHECK_LIB(ibp, IBP_allocate,
          [IBP_LIBS1="-libp $PTHREAD_LIBS $IBP_LIBS1"],
          [AC_MSG_ERROR(unable to locate libibp)])])

    AC_ARG_WITH(libexnode,
      AC_HELP_STRING([--with-libexnode=DIR],
        [base directory for libexnode]),
      [CPPFLAGS="-I$with_libexnode/include/libexnode $CPPFLAGS"
        LIBEXNODE=yes
        IBP_LIBS1="-L$with_libexnode/lib -lexnode $IBP_LIBS1"],
      [LIBS="$IBP_LIBS1"
        AC_CHECK_LIB(exnode, exnodeCreateExnode,
          [IBP_LIBS1="-lexnode $IBP_LIBS1"],
          [AC_MSG_ERROR(unable to locate libexnode)],
          [$IBP_LIBS1])])

    AC_ARG_WITH(liblbone,
      AC_HELP_STRING([--with-liblbone=DIR],
        [base directory for liblbone]),
      [CPPFLAGS="-I$with_liblbone/include $CPPFLAGS"
        LIBLBONE=yes
        IBP_LIBS1="-L$with_liblbone/lib -llbone $IBP_LIBS1"],
      [LIBS="$IBP_LIBS1"
        AC_CHECK_LIB(lbone,lbone_checkDepots,
          [IBP_LIBS1="-llbone $IBP_LIBS1"],
          [AC_MSG_ERROR(unable to locate liblbone)])])

    AC_ARG_WITH(libend2end,
      AC_HELP_STRING([--with-libend2end=DIR],
        [base directory for libend2end]),
      [CPPFLAGS="-I$with_libend2end/include $CPPFLAGS"
        LIBE2E=yes
        IBP_LIBS1="-L$with_libend2end/lib -lend2end -lmd5 -ldes -laes $IBP_LIBS1"],
      [LIBS="-lmd5 -ldes -laes $IBP_LIBS1 -lz"
        AC_CHECK_LIB(end2end, ConditionMapping,
          [IBP_LIBS1="-lend2end -lmd5 -ldes -laes $IBP_LIBS1"],
          [AC_MSG_ERROR(unable to locate libend2end)])])

    AC_ARG_WITH(liblors,
      AC_HELP_STRING([--with-liblors=DIR],
        [base directory for liblors]),
      [CPPFLAGS="-I$with_liblors/include $CPPFLAGS"
        LIBLORS=yes
        IBP_LIBS1="-L$with_liblors/lib -llors $IBP_LIBS1"],
      [LIBS="$IBP_LIBS1 $LIBXML2_LIBS -lz"
        AC_CHECK_LIB(lors,lorsExnodeCreate,
          [IBP_LIBS1="-llors $IBP_LIBS1"],
          [AC_MSG_ERROR(unable to locate liblors)])])

    LIBS="$OLD_LIBS"
    IBP_LIBS="$IBP_LIBS1"
    have_ibp=yes
  fi
  AC_MSG_RESULT($have_ibp)
  if test x"$have_ibp" = x"yes" ; then
    ifelse([$1], , :, [$1])
  else
    ifelse([$2], , :, [$2])
  fi
else
  IBP_LIBS=""
  ifelse([$2], , :, [$2])
fi
AC_SUBST([IBP_LIBS])
])

dnl -----------------------------------------------------------------------
dnl -----------------------------------------------------------------------

dnl TC_PKG_CHECK(name, req-enable, var-name, conf-script, header, lib,
dnl     symbol, pkgconfig-name, url)
dnl Test for pkg-name, and define var-name_CFLAGS and var-name_LIBS
dnl   and HAVE_var-name if found
dnl
dnl 1 name          name of package; required (used in messages, option names)
dnl 2 req-enable    enable by default, 'required', 'optional', 'yes' or 'no'; required
dnl 3 var-name      name stub for variables, preferably uppercase; required
dnl 4 conf-script   name of "-config" script or 'no'
dnl 5 header        header file to check or 'none'
dnl 6 lib           library to check or 'none'
dnl 7 symbol        symbol from the library to check or ''
dnl 8 pkg           package (pkg-config name if applicable)
dnl 9 url           homepage for the package

AC_DEFUN([TC_PKG_CHECK],
[
if test x"$2" = x"required" -o x"$2" = x"optional" ; then
  enable_$1="yes"
else
  AC_MSG_CHECKING([whether $1 support is requested])
  AC_ARG_ENABLE($1,
    AC_HELP_STRING([--enable-$1],
      [build with $1 support ($2)]),
    [case "${enableval}" in
      yes) ;;
      no)  ;;
      *) AC_MSG_ERROR(bad value ${enableval} for --enable-$1) ;;
    esac],
    enable_$1="$2")
  AC_MSG_RESULT($enable_$1)
fi

AC_ARG_WITH($1-prefix,
  AC_HELP_STRING([--with-$1-prefix=PFX],
    [prefix where $1 is installed (/usr)]),
  w_$1_p="$withval", w_$1_p="")

AC_ARG_WITH($1-includes,
  AC_HELP_STRING([--with-$1-includes=DIR],
    [directory where $1 headers ($5) are installed (/usr/include)]),
  w_$1_i="$withval", w_$1_i="")

AC_ARG_WITH($1-libs,
  AC_HELP_STRING([--with-$1-libs=DIR],
    [directory where $1 libraries (lib$6.so) are installed (/usr/lib)]),
  w_$1_l="$withval", w_$1_l="")

have_$1="no"
this_pkg_err="no"

if test x"$enable_$1" = x"yes" ; then

  dnl pkg-config

  pkg_config_$1="no"
  AC_MSG_CHECKING([for pkgconfig support for $1])
  if test x"$PKG_CONFIG" != x"no" ; then
    if $PKG_CONFIG $8 --exists ; then
      pkg_config_$1="yes"
    fi
  fi
  AC_MSG_RESULT($pkg_config_$1)

  dnl *-config

  if test x"$4" != x"no" ; then
    if test x"$w_$1_p" != x"" ; then
      if test -x $w_$1_p/bin/$4 ; then
        $1_config="$w_$1_p/bin/$4"
      fi
    fi
    AC_PATH_PROG($1_config, $4, no)
  else
    $1_config="no"
  fi

  # get and test the _CFLAGS

  AC_MSG_CHECKING([how to determine $3_CFLAGS])
  if test x"$w_$1_i" != x"" ; then
    $1_ii="-I$w_$1_i"
    AC_MSG_RESULT(user)
  else
    if test x"$pkg_config_$1" != x"no" ; then
      $1_ii="`$PKG_CONFIG $8 --cflags`"
      AC_MSG_RESULT(pkg-config)
    else
      if test x"$$1_config" != x"no" ; then
        $1_ii="`$$1_config --cflags`"
        AC_MSG_RESULT($$1_config)
      else
        if test x"$w_$1_p" != x"" ; then
          $1_ii="-I$w_$1_p/include"
          AC_MSG_RESULT(prefix)
        else
          $1_ii="-I/usr/include"
          AC_MSG_RESULT(default)
        fi
      fi
    fi
  fi
  ipaths="" ; xi=""
  for i in $$1_ii ; do
    case $i in
      -I*) ipaths="$ipaths $i" ;;
        *) xi="$xi $i" ;;
    esac
  done
  $1_ii="$ipaths"
  $1_ii="`echo $$1_ii | sed -e 's/  */ /g'`"
  $3_EXTRA_CFLAGS="$$3_EXTRA_CFLAGS $xi"
  $3_EXTRA_CFLAGS="`echo $$3_EXTRA_CFLAGS | sed -e 's/  */ /g'`"

  if test x"$5" != x"none" ; then
    save_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $$1_ii"
    AC_CHECK_HEADER([$5],
      [$3_CFLAGS="$$1_ii"],
      [TC_PKG_ERROR($1, $5, $2, $8, $9, [cannot compile $5])])
    CPPFLAGS="$save_CPPFLAGS"
  elif test x"$pkg_config_$1" != x"no" ; then
     $3_CFLAGS="`$PKG_CONFIG $8 --cflags`"
  fi

  # get and test the _LIBS

  AC_MSG_CHECKING([how to determine $3_LIBS])
  if test x"$w_$1_l" != x"" ; then
    $1_ll="-L$w_$1_l"
    AC_MSG_RESULT(user)
  else
    if test x"$pkg_config_$1" != x"no" ; then
      $1_ll="`$PKG_CONFIG $8 --libs`"
      AC_MSG_RESULT(pkg-config)
    else
      if test x"$$1_config" != x"no" ; then
        $1_ll="`$$1_config --libs`"
        AC_MSG_RESULT($$1_config)
      else
        if test x"$w_$1_p" != x"" ; then
          $1_ll="-L$w_$1_p${deflib}"
          AC_MSG_RESULT(prefix)
        else
          $1_ll="-L/usr${deflib}"
          AC_MSG_RESULT(default)
        fi
      fi
    fi
  fi
  lpaths="" ; xlibs="" ; xlf=""
  for l in $$1_ll ; do
    case $l in
      -L*) lpaths="$lpaths $l" ;;
      -l*) test x"$l" != x"-l$6" && xlibs="$xlibs $l" ;;
        *) xlf="$xlf $l" ;;
    esac
  done
  $1_ll="$lpaths"
  $1_ll="`echo $$1_ll | sed -e 's/  */ /g'`"
  xl=""
  for i in $xlibs $xlf ; do
    echo " $$3_EXTRA_LIBS " | grep -vq " $i " && xl="$xl $i"
  done
  $3_EXTRA_LIBS="$$3_EXTRA_LIBS $xl"
  $3_EXTRA_LIBS="`echo $$3_EXTRA_LIBS | sed -e 's/  */ /g'`"

  if test x"$6" != x"none" ; then
    save_LDFLAGS="$LDFLAGS"
    LDFLAGS="$LDFLAGS $$1_ll"
    AC_CHECK_LIB([$6], [$7],
      [$3_LIBS="$$1_ll -l$6 $$3_EXTRA_LIBS"],
      [TC_PKG_ERROR($1, lib$6, $2, $8, $9, [cannot link against lib$6])],
      [$$3_EXTRA_LIBS])
    LDFLAGS="$save_LDFLAGS"
  elif test x"$pkg_config_$1" != x"no" ; then
     $3_LIBS="`$PKG_CONFIG $8 --libs`"
  fi

  if test x"$this_pkg_err" = x"no" ; then
    have_$1="yes"
  fi

else
  $3_CFLAGS=""
  $3_LIBS=""
fi
])

dnl -----------------------------------------------------------------------
dnl -----------------------------------------------------------------------

dnl TC_PKG_CONFIG_CHECK(name, req-enable, var-name, pkgconfig-name, url)
dnl Test for pkg-name, and define var-name_CFLAGS and var-name_LIBS
dnl   and HAVE_var-name if found; like TC_PKG_CHECK, but using pkg-config
dnl
dnl 1 name          name of package; required (used in messages, option names)
dnl 2 req-enable    enable by default, 'required', 'optional', 'yes' or 'no'; required
dnl 3 var-name      name stub for variables, preferably uppercase; required
dnl 4 pkg           pkg-config name
dnl 5 url           homepage for the package

AC_DEFUN([TC_PKG_CONFIG_CHECK],
[
if test x"$2" = x"required" -o x"$2" = x"optional" ; then
  enable_$1="yes"
else
  AC_MSG_CHECKING([whether $1 support is requested])
  AC_ARG_ENABLE($1,
    AC_HELP_STRING([--enable-$1],
      [build with $1 support ($2)]),
    [case "${enableval}" in
      yes) ;;
      no)  ;;
      *) AC_MSG_ERROR(bad value ${enableval} for --enable-$1) ;;
    esac],
    enable_$1="$2")
  AC_MSG_RESULT($enable_$1)
fi

have_$1="no"
this_pkg_err="no"

if test x"$enable_$1" = x"yes" ; then
   PKG_CHECK_MODULES($3, $4,
       [have_$1="yes"],
       [TC_PKG_ERROR($1, $4, $3, $4, $5)])
else
  $3_CFLAGS=""
  $3_LIBS=""
fi
])



dnl -----------------------------------------------------------------------
dnl -----------------------------------------------------------------------

dnl TC_PKG_ERROR(name, object, req-enable, pkg, url, [error message])
dnl
AC_DEFUN([TC_PKG_ERROR],
[
this_pkg_err="yes"
if test x"$3" != x"optional" ; then
  tc_pkg_err="yes"

  prob=""
  if test x"$3" = x"required" ; then
    prob="requirement failed"
  else
    prob="option '--enable-$1' failed"
  fi
  msg="ERROR: $prob: $6
$2 can be found in the following packages:
  $4  $5

"
  tc_pkg_err_text="$tc_pkg_err_text$msg"
fi
])

dnl -----------------------------------------------------------------------

dnl TC_PKG_INIT
dnl
AC_DEFUN([TC_PKG_INIT],
[
tc_pkg_err="no"
])

dnl -----------------------------------------------------------------------

dnl TC_PKG_HAVE(pkg, PKG)
dnl
AC_DEFUN([TC_PKG_HAVE],
[
if test x"$have_$1" = x"yes" ; then
  AC_DEFINE([HAVE_$2], 1, [have $1 support])
fi
AM_CONDITIONAL(HAVE_$2, test x"$have_$1" = x"yes")
AC_SUBST($2_CFLAGS)
AC_SUBST($2_LIBS)

msg=`printf "%-30s %s" "$1" "$have_$1"`
tc_pkg_rpt_text="$tc_pkg_rpt_text$msg
"
])

dnl -----------------------------------------------------------------------

dnl TC_PKG_REPORT()
dnl
AC_DEFUN([TC_PKG_REPORT],
[
echo "$tc_pkg_rpt_text"
echo ""

if test x"$tc_pkg_err" = x"yes" ; then
  echo "$tc_pkg_err_text"
  echo ""
  echo "Please see the INSTALL file in the top directory of the"
  echo "transcode sources for more information about building"
  echo "transcode with this configure script."
  echo ""
  exit 1
fi
])
