mpdscribble
===========

mpdscribble - A Music Player Daemon (MPD) client which submits
information about tracks being played to a scrobbler (e.g. last.fm).

Home page: https://www.musicpd.org/clients/mpdscribble/


Installing mpdscribble
----------------------

See the `INSTALL <INSTALL>`__ file.


Rating / Love
-------------

With MPD 0.17 or later, clients can submit track ratings over the
client-to-client protocol.  To do that, the client sends the following
command to mpd::

 sendmessage mpdscribble love

The song that is currently playing will be rated with the "love"
attribute, as soon as it gets submitted to the scrobbler.


Support
-------

If you find a bug, please file a bug report at MPD's bug tracker:

  http://www.musicpd.org/mantis
