bin_PROGRAMS = mpdscribble

libmpdclient_SOURCES = \
	libmpdclient/str_pool.c libmpdclient/str_pool.h \
	libmpdclient/resolver.c libmpdclient/resolver.h \
	libmpdclient/song.c libmpdclient/song.h \
	libmpdclient/libmpdclient.c libmpdclient/libmpdclient.h

mpdscribble_SOURCES = \
	mpdscribble.c \
	as.c as.h \
	conn.c conn.h \
	file.c file.h \
	journal.c journal.h \
	lmc.c lmc.h \
	log.c log.h \
	mbid.c mbid.h \
	$(libmpdclient_SOURCES)
  
AM_CPPFLAGS = $(libgobject_CFLAGS) $(libgthread_CFLAGS) $(libsoup_CFLAGS) "-I./libmpdclient"
LDADD = $(libgobject_LIBS) $(libgthread_LIBS) $(libsoup_LIBS)
