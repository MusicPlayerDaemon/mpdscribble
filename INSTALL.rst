mpdscribble INSTALL
===================

Requirements
------------

- a C99 compliant compiler (e.g. gcc)
- `libmpdclient 2.2 <https://www.musicpd.org/libs/libmpdclient/>`__
- `glib 2.6 <https://wiki.gnome.org/Projects/GLib>`__
- libsoup (2.2 or 2.4) or libcurl


Compiling mpdscribble
---------------------

Download and unpack the source code.  In the mpdscribble directory, type::

 ./configure --sysconfdir=/etc --enable-debug

The configure option ``--help`` lists all available compile time
options.

Compile and install::

 make
 sudo make install

Now edit the file ``/etc/mpdscribble.conf``, and enter your last.fm
account information.
