mpdscribble
===========

mpdscribble - A Music Player Daemon (MPD) client which submits
information about tracks being played to a scrobbler (e.g. last.fm).


Installing mpdscribble
----------------------

Requirements
^^^^^^^^^^^^

- a C++20 compliant compiler (e.g. gcc or clang)
- `libmpdclient 2.2 <https://www.musicpd.org/libs/libmpdclient/>`__
- `libcurl <https://curl.haxx.se/>`__
- `libgcrypt <https://gnupg.org/software/libgcrypt/index.html>`__
- `Meson 0.47 <http://mesonbuild.com/>`__ and `Ninja <https://ninja-build.org/>`__


Compiling mpdscribble
^^^^^^^^^^^^^^^^^^^^^

Download and unpack the source code.  In the mpdscribble directory, type::

 meson build

The configure option ``--help`` lists all available compile time
options.

Compile and install::

 cd build
 ninja install

Now edit the config file at ``~/.mpdscribble/mpdscribble.conf`` (or ``/etc/mpdscribble.conf``), and enter your last.fm
account information.


Rating / Love
-------------

With MPD 0.17 or later, clients can submit track ratings over the
client-to-client protocol.  To do that, the client sends the following
command to mpd::

 sendmessage mpdscribble love

The song that is currently playing will be rated with the "love"
attribute, as soon as it gets submitted to the scrobbler.


Links
-----

- `Home page and download <http://www.musicpd.org/clients/mpdscribble/>`__
- `git repository <https://github.com/MusicPlayerDaemon/mpdscribble/>`__
- `Bug tracker <https://github.com/MusicPlayerDaemon/mpdscribble/issues>`__
