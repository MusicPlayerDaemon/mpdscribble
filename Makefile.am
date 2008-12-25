ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign 1.9 dist-bzip2

doc_DATA = README AUTHORS NEWS
dist_man_MANS = doc/mpdscribble.1
dist_sysconf_DATA = doc/mpdscribble.conf

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

AM_CPPFLAGS += -DFILE_CONF='"$(sysconfdir)/mpdscribble.conf"'

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

install-dist_sysconfDATA:
	test -z "$(sysconfdir)" || $(mkdir_p) "$(DESTDIR)$(sysconfdir)"
	@list='$(dist_sysconf_DATA)'; \
	for p in $$list; do \
	    dest=$(am__strip_dir) \
	    if test -f $(DESTDIR)$(sysconfdir)/$$dest; then \
	        echo "$@ will not overwrite existing $(DESTDIR)$(sysconfdir)/$$dest"; \
	    else \
	        if test -f "$$p"; then d=; else d="$(srcdir)/"; fi; \
	        echo " $(INSTALL_DATA) $$d$$p $(DESTDIR)$(sysconfdir)/$$dest"; \
	        $(INSTALL_DATA) $$d$$p $(DESTDIR)$(sysconfdir)/$$dest; \
	    fi; \
	done
