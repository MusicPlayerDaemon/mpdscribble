AC_PREREQ(2.60)
AC_INIT(mpdscribble, 0.23~git, mpdscribble)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/mpdscribble.c])
AC_CONFIG_AUX_DIR(build)
AM_INIT_AUTOMAKE([foreign 1.10 dist-bzip2 subdir-objects])
AM_CONFIG_HEADER([config.h])

AC_PROG_CC_C99


dnl
dnl OS specific defaults
dnl

AC_CANONICAL_HOST

case "$host_os" in
mingw32* | windows*)
	LIBS="$LIBS -lws2_32"
	;;
esac


dnl
dnl libc features
dnl

AC_CHECK_FUNCS(syslog)
if test $ac_cv_func_syslog = no; then
	# syslog is not in the default libraries.  See if it's in some other.
	for lib in bsd socket inet; do
		AC_CHECK_LIB($lib, syslog,
			[AC_DEFINE(HAVE_SYSLOG)
			LIBS="$LIBS -l$lib"; break])
	done
fi


dnl
dnl libraries
dnl

AC_SUBST(libgcrypt_CFLAGS, [])
AC_SUBST(libgcrypt_LIBS, [])

PKG_CHECK_MODULES([LIBMPDCLIENT], [libmpdclient >= 2.2],,
	[AC_MSG_ERROR([libmpdclient2 is required])])

PKG_CHECK_MODULES([libglib],
	[glib-2.0 >= 2.16],
	[glib216=yes], [glib216=no])
if test "x$glib216" != "xyes"; then
	PKG_CHECK_MODULES([libglib],
		[glib-2.0 >= 2.6],,
		[AC_MSG_ERROR([glib 2.6 is required])])

	# use libgcrypt's MD5 routines with GLib < 2.16
	AC_PATH_PROG(LIBGCRYPT_CONFIG, libgcrypt-config,
		[AC_MSG_ERROR([libgcrypt-config required])])
	libgcrypt_CFLAGS=`${LIBGCRYPT_CONFIG} --cflags`
	libgcrypt_LIBS=`${LIBGCRYPT_CONFIG} --libs`
fi

dnl
dnl HTTP client library
dnl

PKG_CHECK_MODULES([libcurl], [libcurl],, AC_MSG_ERROR([libcurl not found]))
AM_CONDITIONAL(HAVE_CURL, test x$with_http_client = xcurl)

dnl
dnl build options
dnl

AC_ARG_ENABLE(werror,
	AS_HELP_STRING([--enable-werror],
		[treat warnings as errors (default: disabled)]),,
	enable_werror=no)

if test "x$enable_werror" = xyes; then
	AM_CFLAGS="$AM_CFLAGS -Werror -pedantic-errors"
fi

AC_ARG_ENABLE(debug,
	AS_HELP_STRING([--enable-debug],
		[enable debugging (default: disabled)]),,
	enable_debug=no)

if test "x$enable_debug" = xno; then
	AM_CFLAGS="$AM_CFLAGS -DNDEBUG"
fi

AC_ARG_ENABLE(test,
	AS_HELP_STRING([--enable-test],
		[build the test programs (default: disabled)]),,
	enable_test=no)
AM_CONDITIONAL(ENABLE_TEST, test "x$enable_test" = xyes)


dnl
dnl CFLAGS
dnl

AC_SUBST(AM_CFLAGS)

CHECK_CFLAG([-Wall])
CHECK_CFLAG([-Wextra])
CHECK_CFLAG([-Wno-deprecated-declarations])
CHECK_CFLAG([-Wmissing-prototypes])
CHECK_CFLAG([-Wshadow])
CHECK_CFLAG([-Wpointer-arith])
CHECK_CFLAG([-Wstrict-prototypes])
CHECK_CFLAG([-Wcast-qual])
CHECK_CFLAG([-Wwrite-strings])


dnl
dnl Generate output files
dnl

AC_OUTPUT(Makefile)
