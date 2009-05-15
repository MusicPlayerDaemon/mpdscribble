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

#include "conn.h"
#include "file.h"
#include "as.h"
#include "config.h"

#include <libsoup/soup-uri.h>
#include <libsoup/soup-session-async.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct global {
	SoupSession *session;
	char *base;
	void *data;
	bool pending;
	callback_t *callback;
#ifdef HAVE_SOUP_24
	SoupURI *proxy;
#else
	SoupUri *proxy;
#endif
};

int g_thread_done = 0;

static void
#ifdef HAVE_SOUP_24
conn_callback(G_GNUC_UNUSED SoupSession * session,
	      SoupMessage * msg, gpointer data)
#else
conn_callback(SoupMessage * msg, gpointer data)
#endif
{
	struct global *g = data;
	assert(g->pending);

	g->pending = false;

	/* NOTE: does not support redirects */
	if (SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
#ifdef HAVE_SOUP_24
		g->callback(msg->response_body->length,
			   msg->response_body->data, g->data);
#else
		g->callback(msg->response.length, msg->response.body, g->data);
#endif
	} else
		g->callback(0, NULL, g->data);
}

struct global *conn_setup(void)
{
	struct global *g = g_new(struct global, 1);

	if(!g_thread_done) {
		g_type_init();
		g_thread_init(NULL);
		g_thread_done = 1;
	}

	g->pending = false;
	if (file_config.proxy != NULL)
		g->proxy = soup_uri_new(file_config.proxy);
	else
		g->proxy = NULL;

	return g;
}

void
conn_initiate(char *url, callback_t * callback, char *post_data, void *data, struct global *g)
{
	SoupMessage *msg;

	assert(!g->pending);

	g->data = data;

	g->callback = callback;

	g->base = url;

	g->session =
	    soup_session_async_new_with_options(SOUP_SESSION_PROXY_URI, g->proxy,
						NULL);

	if (post_data) {
		msg = soup_message_new(SOUP_METHOD_POST, g->base);
#ifdef HAVE_SOUP_24
		soup_message_set_request
		    (msg, "application/x-www-form-urlencoded",
		     SOUP_MEMORY_COPY, post_data, strlen(post_data));
		soup_message_headers_append(msg->request_headers, "User-Agent",
					    AS_CLIENT_ID "/" AS_CLIENT_VERSION);
		soup_message_headers_append(msg->request_headers, "Pragma",
					    "no-cache");
		soup_message_headers_append(msg->request_headers, "Accept",
					    "*/*");
#else
		soup_message_set_request
		    (msg, "application/x-www-form-urlencoded",
		     SOUP_BUFFER_SYSTEM_OWNED, g_strdup(post_data),
		     strlen(post_data));
		soup_message_add_header(msg->request_headers, "User-Agent",
					AS_CLIENT_ID "/" AS_CLIENT_VERSION);
		soup_message_add_header(msg->request_headers, "Pragma",
					"no-cache");
		soup_message_add_header(msg->request_headers, "Accept", "*/*");
#endif
	} else {
		msg = soup_message_new(SOUP_METHOD_GET, g->base);
	}

	soup_message_set_flags(msg, SOUP_MESSAGE_NO_REDIRECT);

	g->pending = true;
	soup_session_queue_message(g->session, msg, conn_callback, g);
}

bool conn_pending(struct global *g)
{
	return g->pending;
}

void conn_cleanup(struct global *g)
{
	free(g);
}
