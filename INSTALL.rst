mpdscribble INSTALL
===================

Requirements
------------

- a C++14 compliant compiler (e.g. gcc or clang)
- `libmpdclient 2.2 <https://www.musicpd.org/libs/libmpdclient/>`__
- `Boost 1.62 <https://www.boost.org/>`__
- `glib 2.6 <https://wiki.gnome.org/Projects/GLib>`__
- libsoup (2.2 or 2.4) or libcurl
- `libcurl <https://curl.haxx.se/>`__
- `libgcrypt <https://gnupg.org/software/libgcrypt/index.html>`__
- `Meson 0.47 <http://mesonbuild.com/>`__ and `Ninja <https://ninja-build.org/>`__


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
