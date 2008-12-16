/*
    This file is part of mpdscribble,
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

#include "conn.h"
#include "file.h"
#include "misc.h"
#include "as.h"
#include "config.h"

#include <libsoup/soup.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct global {
  SoupSession *session;
#ifdef HAVE_SOUP_24
  SoupURI *base_uri;
#else
  SoupUri *base_uri;
#endif
  char *base;
  bool pending;
  callback_t *callback;
  GMainLoop *mainloop;
#ifdef HAVE_SOUP_24
  SoupURI *proxy;
#else
  SoupUri *proxy;
#endif
};

static struct global g;

static void
#ifdef HAVE_SOUP_24
conn_callback(G_GNUC_UNUSED SoupSession * session,
              SoupMessage * msg, gpointer uri)
#else
conn_callback (SoupMessage * msg, gpointer uri)
#endif
{
  assert(g.pending);

  if (uri)
    soup_uri_free (uri);

  g.pending = false;

  if (SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
#ifdef HAVE_SOUP_24
    g.callback(msg->response_body->length, msg->response_body->data);
#else
    g.callback(msg->response.length, msg->response.body);
#endif
  } else
    g.callback(0, NULL);

  g_main_loop_quit (g.mainloop);
  g_main_loop_unref (g.mainloop);
}


void
conn_setup (void)
{
  g_type_init ();
  g_thread_init (NULL);

  g.base_uri = NULL;
  g.pending = false;
  if (file_config.proxy != NULL)
    g.proxy = soup_uri_new(file_config.proxy);
  else
    g.proxy = NULL;
}

static int
conn_mainloop_quit(G_GNUC_UNUSED void *data)
{
  g_main_loop_quit (g.mainloop);
  return 0;
}

int
conn_initiate (char *url, callback_t *callback, char *post_data, 
               unsigned int seconds)
{
  SoupMessage *msg;

  assert(!g.pending);

  g.callback = callback;

  g.base = url;
  g.base_uri = soup_uri_new (g.base);
  if (!g.base_uri)
    fatal ("Could not parse '%s' as a URL", g.base);

  g.session = soup_session_async_new_with_options (
		SOUP_SESSION_PROXY_URI, g.proxy,
		NULL);

  if (post_data)
    {
      msg = soup_message_new (SOUP_METHOD_POST, g.base);
#ifdef HAVE_SOUP_24
      soup_message_set_request
        (msg, "application/x-www-form-urlencoded",
         SOUP_MEMORY_COPY, post_data, strlen (post_data));
      soup_message_headers_append (msg->request_headers, "User-Agent",
                               AS_CLIENT_ID "/" AS_CLIENT_VERSION);
      soup_message_headers_append (msg->request_headers, "Pragma", "no-cache");
      soup_message_headers_append (msg->request_headers, "Accept", "*/*");
#else
      soup_message_set_request
        (msg, "application/x-www-form-urlencoded",
         SOUP_BUFFER_USER_OWNED, post_data, strlen (post_data));
      soup_message_add_header (msg->request_headers, "User-Agent", 
                               AS_CLIENT_ID "/" AS_CLIENT_VERSION);
      soup_message_add_header (msg->request_headers, "Pragma", "no-cache");
      soup_message_add_header (msg->request_headers, "Accept", "*/*");
#endif
    }
  else
    {
      msg = soup_message_new (SOUP_METHOD_GET, g.base);
    }

  soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);

  g.pending = true;
  soup_session_queue_message (g.session, msg,
                              conn_callback, soup_uri_new (g.base));

  g.mainloop = g_main_loop_new (g_main_context_default (), FALSE);
  g_timeout_add (seconds * 1000, &conn_mainloop_quit, NULL);

  return CONN_OK;
}

bool
conn_pending (void)
{
  return g.pending;
}

bool
conn_poll (void)
{
  g_main_loop_run (g.mainloop);

  /*
  while (g_main_context_pending (NULL))
    g_main_context_iteration(NULL, FALSE);
  */

  if (!g.pending && g.base_uri)
    {
      soup_uri_free (g.base_uri);
      g.base_uri = NULL;
    }

  return g.pending;
}


void
conn_cleanup (void)
{
}

