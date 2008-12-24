/*
    This file is part of mpdscribble.
    another audioscrobbler plugin for music player daemon.
    Copyright © 2005 Kuno Woudt <kuno@frob.nl>

    mpdscribble is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    mpdscribble is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with mpdscribble; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef FILE_H
#define FILE_H

/*
  default locations for files.

  FILE_ETC_* are paths for a system-wide install.
  FILE_USR_* will be used instead if FILE_USR_CONF exists.
*/

#define FILE_CONF "/etc/mpdscribble.conf"
#define FILE_CACHE "/var/cache/mpdscribble/mpdscribble.cache"
#define FILE_LOG "/var/log/mpdscribble/mpdscribble.log"
#define FILE_HOME_CONF "~/.mpdscribble/mpdscribble.conf"
#define FILE_HOME_CACHE "~/.mpdscribble/mpdscribble.cache"
#define FILE_HOME_LOG "~/.mpdscribble/mpdscribble.log"

#define FILE_DEFAULT_PORT 6600
#define FILE_DEFAULT_HOST "localhost"

struct song;

enum file_location { file_etc, file_home, file_unknown, };

struct config {
	char *username;
	char *password;
	char *log;
	char *cache;
	char *musicdir;
	char *conf;
	char *host;
	char *proxy;
	int port;
	int sleep;
	int cache_interval;
	int verbose;
	enum file_location loc;
};

extern struct config file_config;

char *file_expand_tilde(const char *path);

int file_read_config(int argc, char **argv);
void file_cleanup(void);

#endif /* FILE_H */
