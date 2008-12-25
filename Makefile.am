ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign 1.9 dist-bzip2

doc_DATA = README AUTHORS NEWS
man_MANS = doc/mpdscribble.1
EXTRA_DIST = $(man_MANS) setup.sh

bin_PROGRAMS = src/mpdscribble

libmpdclient_SOURCES = \
	src/libmpdclient/str_pool.c src/libmpdclient/str_pool.h \
	src/libmpdclient/resolver.c src/libmpdclient/resolver.h \
	src/libmpdclient/song.c src/libmpdclient/song.h \
	src/libmpdclient/libmpdclient.c src/libmpdclient/libmpdclient.h

src_mpdscribble_SOURCES = \
	src/mpdscribble.c \
	src/daemon.c src/daemon.h \
	src/as.c src/as.h \
	src/conn.c src/conn.h \
	src/cmdline.c src/cmdline.h \
	src/file.c src/file.h \
	src/journal.c src/journal.h \
	src/lmc.c src/lmc.h \
	src/log.c src/log.h \
	src/mbid.c src/mbid.h \
	$(libmpdclient_SOURCES)

AM_CPPFLAGS = $(libgobject_CFLAGS) $(libgthread_CFLAGS) $(libsoup_CFLAGS) \
	$(libgcrypt_CFLAGS) \
	$(libglib_CFLAGS) \
	-I$(srcdir)/src -I$(srcdir)/src/libmpdclient
LDADD = $(libgobject_LIBS) $(libgthread_LIBS) $(libsoup_LIBS) \
	$(libgcrypt_LIBS)
	$(libglib_LIBS)

SPARSE = sparse
SPARSE_FLAGS =
SPARSE_CPPFLAGS = $(DEFAULT_INCLUDES) \
	-I$(shell $(CC) -print-file-name=include) \
	-I$(shell $(CC) -print-file-name=include-fixed)
sparse-check:
	for i in $(src_mpdscribble_SOURCES); \
	do \
		$(SPARSE) -I. $(CFLAGS) $(AM_CPPFLAGS) $(SPARSE_FLAGS) $(SPARSE_CPPFLAGS) $(srcdir)/$$i || exit; \
	done
