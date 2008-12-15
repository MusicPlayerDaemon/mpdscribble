ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign 1.9 dist-bzip2

bin_PROGRAMS = mpdscribble
mpdscribble_SOURCES = mpdscribble.c as.c conn.c file.c lmc.c \
  misc.c mbid.c as.h conn.h file.h lmc.h misc.h mbid.h

mpdscribble_SOURCES += \
	libmpdclient/str_pool.c libmpdclient/str_pool.h \
	libmpdclient/resolver.c libmpdclient/resolver.h \
	libmpdclient/song.c libmpdclient/song.h \
	libmpdclient/libmpdclient.c libmpdclient/libmpdclient.h

AM_CPPFLAGS = $(libgobject_CFLAGS) $(libgthread_CFLAGS) $(libsoup_CFLAGS) "-I./libmpdclient"
LDADD = $(libgobject_LIBS) $(libgthread_LIBS) $(libsoup_LIBS)
