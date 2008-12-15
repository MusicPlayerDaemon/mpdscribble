AC_PREREQ(2.60)
AC_INIT(mpdscribble, 0.13~git, max@duempel.org, mpdscribble)
AC_CONFIG_SRCDIR([mpdscribble.c])
AC_CONFIG_AUX_DIR(build)
AM_INIT_AUTOMAKE([foreign 1.9 dist-bzip2])
AM_CONFIG_HEADER([config.h])

AC_PROG_CC

PKG_CHECK_MODULES([libgobject],[gobject-2.0],,[AC_MSG_ERROR([libgobject-2.0 not found])])
AC_SUBST(libgobject_CFLAGS)
AC_SUBST(libgobject_LIBS)

PKG_CHECK_MODULES([libgthread],[gthread-2.0],,[AC_MSG_ERROR([libgthread-2.0 not found])])
AC_SUBST(libgthread_CFLAGS)
AC_SUBST(libgthread_LIBS)

PKG_CHECK_MODULES([libsoup],[libsoup-2.2],,[AC_MSG_ERROR([libsoup-2.2 not found])])
AC_SUBST(libsoup_CFLAGS)
AC_SUBST(libsoup_LIBS)

AC_OUTPUT(Makefile)
