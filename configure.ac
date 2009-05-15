AC_PREREQ(2.60)
AC_INIT(mpdscribble, 0.18~git, mpdscribble)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/mpdscribble.c])
AC_CONFIG_AUX_DIR(build)
AM_INIT_AUTOMAKE([foreign 1.10 dist-bzip2])
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
fi

PKG_CHECK_MODULES([libsoup],[libsoup-2.4],
	found_soup_24=yes,
	found_soup_24=no)
if test x$found_soup_24 = xyes; then
	AC_DEFINE([HAVE_SOUP_24], [1], [Defined if libsoup-2.4 instead of libsoup-2.2 is used])
else
	PKG_CHECK_MODULES([libsoup],[libsoup-2.2],,
		[AC_MSG_ERROR([libsoup (2.4 or 2.2) not found])])
fi


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


dnl
dnl CFLAGS
dnl

AC_SUBST(AM_CFLAGS)

CHECK_CFLAG([-Wall])
CHECK_CFLAG([-Wextra])
CHECK_CFLAG([-Wno-deprecated-declarations])
CHECK_CFLAG([-Wmissing-prototypes])
CHECK_CFLAG([-Wdeclaration-after-statement])
CHECK_CFLAG([-Wshadow])
CHECK_CFLAG([-Wpointer-arith])
CHECK_CFLAG([-Wstrict-prototypes])
CHECK_CFLAG([-Wcast-qual])
CHECK_CFLAG([-Wwrite-strings])


dnl
dnl Generate output files
dnl

AC_OUTPUT(Makefile)
