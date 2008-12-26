/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2009 The Music Player Daemon Project
 * Copyright (C) 2005-2008 Kuno Woudt <kuno@frob.nl>
 * Project homepage: http://musicpd.org
 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

int conn_initiate(char *url, callback_t * callback, char *post_data);
bool conn_pending(void);

#endif /* CONN_H */
