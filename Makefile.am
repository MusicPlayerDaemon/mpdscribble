AUTOMAKE_OPTIONS = foreign 1.9 dist-bzip2

bin_PROGRAMS = mpdscribble
mpdscribble_SOURCES = mpdscribble.c as.c conn.c escape.c file.c lmc.c \
  md5.c misc.c mbid.c as.h conn.h escape.h file.h lmc.h md5.h misc.h mbid.h

mpdscribble_SOURCES += \
	libmpdclient/libmpdclient.c libmpdclient/libmpdclient.h

man_MANS = mpdscribble.1
EXTRA_DIST = contrib setup.sh $(man_MANS)

AM_CFLAGS = $(libgobject_CFLAGS) $(libgthread_CFLAGS) $(libsoup_CFLAGS) "-I./libmpdclient"
LDADD = $(libgobject_LIBS) $(libgthread_LIBS) $(libsoup_LIBS)
