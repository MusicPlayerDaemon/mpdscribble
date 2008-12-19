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

#ifndef CONN_H
#define CONN_H

#include <stdbool.h>
#include <stddef.h>

typedef void callback_t(size_t, const char *);

#define CONN_FAIL 0
#define CONN_OK 1

#define conn_escape(X) curl_escape(X, 0)
#define conn_free curl_free

void conn_setup(void);
void conn_cleanup(void);

int conn_initiate(char *url, callback_t * callback, char *post_data,
		  unsigned int seconds);
bool conn_poll(void);
bool conn_pending(void);

#endif /* CONN_H */
