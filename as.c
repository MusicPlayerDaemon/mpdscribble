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

#include "as.h"
#include "file.h"
#include "misc.h"
#include "md5.h"
#include "conn.h"
#include "config.h"

#include <glib.h>

#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#define MAX_VAR_SIZE 8192
#define MAX_TIMESTAMP_SIZE 64

#define AS_HOST "http://post.audioscrobbler.com/"

/* don't submit more than this amount of songs in a batch. */
#define MAX_SUBMIT_COUNT 10

typedef enum
  {
    AS_NOTHING,
    AS_HANDSHAKING,
    AS_READY,
    AS_SUBMITTING,
    AS_BADAUTH,
  } as_state;

typedef enum
  {
    AS_COMMAND,
    AS_CHALLENGE,
    AS_SUBMIT,
  } as_handshaking;

typedef enum
  {
    AS_SUBMIT_OK,
    AS_SUBMIT_NOP,
    AS_SUBMIT_FAILED,
    AS_SUBMIT_HANDSHAKE,
  } as_submitting;

static char *g_submit_url = NULL;
static char *g_md5_response = NULL;
static int g_interval = 1;
static long g_lastattempt = 0;
static as_state g_state = AS_NOTHING;

static struct song *g_queue = NULL;
static struct song *g_queuep = NULL;
static int g_queue_size = 0;
static int g_submit_pending = 0;
static int g_sleep = 0;

static char *
add_var_internal (char *sep, char *key, signed char index, char *val,
                  int abort_if_empty)
{
  char *ret = (char *) malloc (MAX_VAR_SIZE);
  char *escaped;

  if (!ret)
    exit (ENOMEM);

  if (val)
    {
      escaped = g_uri_escape_string(val, NULL, false);
      if (!escaped)
        /* FIXME: there probably are other things which could cause uri_escape to fail. */
        fatal ("out of memory");
    }
  else
    {
      escaped = strdup2 ("");
    }

  if (index == -1)
    snprintf (ret, MAX_VAR_SIZE, "%s%s=%s", sep, key, escaped);
  else
    snprintf (ret, MAX_VAR_SIZE, "%s%s[%i]=%s", sep, key, index, escaped);
  free (escaped);

  return ret;
}

static char *
first_var (char *key, char *val, int abort_if_empty)
{
  return add_var_internal ("?", key, -1, val, abort_if_empty);
}

static char *
add_var (char *key, char *val, int abort_if_empty)
{
  return add_var_internal ("&", key, -1, val, abort_if_empty);
}

static char *
add_var_i (char *key, signed char index, char *val, int abort_if_empty)
{
  return add_var_internal ("&", key, index, val, abort_if_empty);
}

static char *
md5 (char *in)
{
  md5_state_t md5state;
  unsigned char md5result[16];
  char *tmp;
  int i;
  char a[3];

  if (!strlen(in))
    return NULL;

  md5_init (&md5state);
  md5_append (&md5state, (unsigned const char *) in, (int) strlen (in));
  md5_finish (&md5state, md5result);

  tmp = (char *) calloc (34, 1);
  if (!tmp)
    fatal ("out of memory");

  for (i = 0;i < 0x10; i++)
    {
      snprintf(a, 3, "%02x", md5result[i]);
      tmp[(i<<1)] = a[0];
      tmp[(i<<1)+1] = a[1];
    }

  return tmp;
}

static void
as_reset_timeout (void)
{
  g_lastattempt = now ();
}

static void
as_increase_interval (void)
{
  if (g_interval < 60)
    g_interval = 60;
  else
    g_interval <<= 1;

  if (g_interval > 60*60*2)
    g_interval = 60*60*2;

  warning ("waiting %i seconds before trying again.",
           g_interval);

  as_reset_timeout ();
}

static int
as_throttle (void)
{
  long t = now ();

  long left = g_lastattempt + g_interval - t;

  if (left < 0)
    {
      as_reset_timeout ();
      return 1;
    }

  return 0;
}

static int
as_parse_submit_response (char *line)
{
  static const char *FAILED = "FAILED";
  static const char *BADUSER = "BADUSER";
  static const char *BADAUTH = "BADAUTH";
  static const char *OK = "OK";
  static const char *INTERVAL = "INTERVAL";

  if (!strncmp (line, INTERVAL, strlen (INTERVAL)))
    {
      char *start = line + strlen (INTERVAL) + 1;
      /* atoi will probably return 0 on error,
         we do NOT want to set interval to 0 on error. */
      if (isdigit (*start))
        notice ("interval set to %i seconds.",
                g_interval = atoi (start));
      else
        notice ("error parsing interval command.");

      return AS_SUBMIT_NOP;
    }
  else if (!strncmp (line, OK, strlen (OK)))
    {
      notice ("OK");
      return AS_SUBMIT_OK;
    }
  else if (!strncmp (line, BADUSER, strlen (BADUSER))
           || !strncmp (line, BADAUTH, strlen (BADAUTH)))
    {
      warning ("md5 challenge incorrect,"
               " wrong username or password?");

      g_state = AS_NOTHING;
      return AS_SUBMIT_HANDSHAKE;
    }
  else if (!strncmp (line, FAILED, strlen (FAILED)))
    {
      char *start = line + strlen (FAILED);
      if (*start)
        warning ("submission rejected: %s", start);
      else
        warning ("submission rejected");
    }
  else
    {
      warning ("unknown response");
    }

  return AS_SUBMIT_FAILED;
}

static as_handshaking
as_parse_handshake_response (char *line)
{
  static const char *UPTODATE = "UPTODATE";
  static const char *UPDATE = "UPDATE";
  static const char *FAILED = "FAILED";
  static const char *BADUSER = "BADUSER";
  static const char *BADAUTH = "BADAUTH";
  static const char *INTERVAL = "INTERVAL";

  /* FIXME: some code duplication between this
     and as_parse_submit_response. */
  if (!strncmp (line, INTERVAL, strlen (INTERVAL)))
    {
      char *start = line + strlen (INTERVAL) + 1;
      /* atoi will probably return 0 on error,
         we do NOT want to set interval to 0 on error. */
      if (isdigit (*start))
        notice ("interval set to %i seconds.",
                g_interval = atoi (start));
      else
        warning ("error parsing interval command.");

      return AS_COMMAND;
    }
  else if (!strncmp (line, UPTODATE, strlen (UPTODATE)))
    {
      notice ("handshake ok.");
      g_interval = 1;
      return AS_CHALLENGE;
    }
  else if (!strncmp (line, UPDATE, strlen (UPDATE)))
    {
      warning ("handshake ok, however, a newer version"
               " of your audioscrobbler plugin is available (%s).", line);
      g_interval = 1;
      return AS_CHALLENGE;
    }
  else if (!strncmp (line, BADUSER, strlen (BADUSER))
           || !strncmp (line, BADAUTH, strlen (BADAUTH)))
    {
      warning ("handshake failed, username or password incorrect (%s).", line);
      g_state = AS_BADAUTH;
      return AS_COMMAND;
    }
  else if (!strncmp (line, FAILED, strlen (FAILED)))
    {
      warning ("handshake failed (%s).", line);
    }
  else
    {
      warning ("error parsing handshake response (%s).", line);
    }

  as_increase_interval ();

  /* only change to NOTHING if the state wasn't changed to
     something else already. */
  if (g_state == AS_HANDSHAKING)
    g_state = AS_NOTHING;

  return AS_COMMAND;
}

static void
as_song_cleanup (struct song *s, int free_struct)
{
  if (s->artist) free (s->artist);
  if (s->track) free (s->track);
  if (s->album) free (s->album);
  if (s->mbid) free (s->mbid);
  if (s->time) free (s->time);
  if (free_struct) free (s);
}

static void
as_handshake_callback (int length, char *response)
{
  as_handshaking state = AS_COMMAND;
  char *next;

  if (g_state != AS_HANDSHAKING)
    warning ("as_handshake_callback called when not handshaking,"
             " this is probably a bug.");

  if (!length)
    {
      g_state = AS_NOTHING;
      warning ("handshake timed out, ");
      as_increase_interval ();
      return;
    }

  /* although we cannot be certain response ends with a
     \0, a valid response ends with a useless newline.
     so we overwrite that newline with a \0 here. */
  response[length-1] = 0;

  next = strtok (response, "\n");
  do
    {
      switch (state)
        {
        case AS_COMMAND:
          state = as_parse_handshake_response (next);
          break;
        case AS_CHALLENGE:
          {
            char *response = concat (file_config.password, next, NULL);
            g_md5_response = md5 (response);
            state = AS_SUBMIT;
            break;
          }
        case AS_SUBMIT:
          g_submit_url = strdup2 (next);
          notice ("submit url: %s", g_submit_url);
          state = AS_COMMAND;
          g_state = AS_READY;
          break;
        }
    }
  while ((next = strtok (NULL, "\n")));
}

static void
as_queue_remove_oldest (int count)
{
  if (!count || !g_queue_size)
    return;

  while (count--)
    {
      struct song *tmp = g_queue;
      g_queue = g_queue->next;
      as_song_cleanup (tmp, 1);
      g_queue_size--;
    }
}

static struct song *
as_queue_remove (struct song *sng)
{
  struct song *tmp = g_queue;

  /* remove first entry in queue. */
  if (sng == tmp)
    {
      as_queue_remove_oldest (1);
      return g_queue;
    }

  /* it's not the first entry, so look for it. */
  while (tmp && sng != tmp->next)
    tmp = tmp->next;

  if (sng != tmp->next)
    fatal ("internal error in as_queue_remove, this is a bug.");

  /* take it out of the link chain. */
  tmp->next = sng->next;
  as_song_cleanup (sng, 1);

  g_queue_size--;

  return tmp->next;
}

static void
as_submit_callback (int length, char *response)
{
  char *next;
  int failed = 0;

  if (!length)
    {
      g_state = AS_READY;
      g_submit_pending = 0;
      warning ("submit timed out, ");
      as_increase_interval ();
      return;
    }

  if (g_state != AS_SUBMITTING)
    warning ("as_submit_callback called when not submitting,"
             " this is probably a bug.");

  /* although we cannot be certain response ends with a
     \0, a valid response ends with a useless newline.
     so we overwrite that newline with a \0 here. */
  response[length-1] = 0;

  next = strtok (response, "\n");
  do
    {
      switch (as_parse_submit_response (next))
        {
        case AS_SUBMIT_OK:
          /* submission was accepted, so clean up the cache. */
          as_queue_remove_oldest (g_submit_pending);
          g_submit_pending = 0;
          break;
        case AS_SUBMIT_FAILED:
          failed = 1;
          break;
        case AS_SUBMIT_NOP:
        case AS_SUBMIT_HANDSHAKE:
          break;
        }
    }
  while ((next = strtok (NULL, "\n")));

  if (failed)
    as_increase_interval ();

  /* only change to READY if the state wasn't changed to
     something else already. */
  if (g_state == AS_SUBMITTING)
    g_state = AS_READY;
}

static char *
as_timestamp (void)
{
  /* create timestamp for 1.1 protocol. */
  time_t t;
  char *utc = malloc (MAX_TIMESTAMP_SIZE);
  if (!utc)
    exit (ENOMEM);

  t = time (NULL);
  strftime (utc, MAX_TIMESTAMP_SIZE, "%Y-%m-%d %H:%M:%S", gmtime (&t));

  return utc;
}

static void
as_handshake (void)
{
  char *host;
  char *url;

  g_state = AS_HANDSHAKING;

  host = strdup2 (AS_HOST);

  /* construct the handshake url. */
  url = concatDX (host,
                  first_var ("hs", "true", 0),
                  add_var ("p", "1.1", 0),
                  add_var ("c", AS_CLIENT_ID, 0),
                  add_var ("v", AS_CLIENT_VERSION, 0),
                  add_var ("u", file_config.username, 0),
                  //                  add_var ("a", file_config.password, 0),
                  NULL);

  //  notice ("handshake url:\n%s", url);

  if (!conn_initiate (url, &as_handshake_callback, NULL, g_sleep))
    {
      warning ("something went wrong when trying to connect,"
               " probably a bug.");

      g_state = AS_NOTHING;
      as_increase_interval ();
    }

  free (url);
}

static void
as_submit (void)
{
  //MAX_SUBMIT_COUNT
  int count = 0;
  int queue_size = g_queue_size;
  struct song *queue = g_queue;
  char *url;
  char *post_data;
  char len[MAX_VAR_SIZE];
  char *a, *t, *l, *i, *b, *m;

  if (!g_queue_size)
    return;

  g_state = AS_SUBMITTING;

  /* construct the handshake url. */
  url = concatDX (strdup2 (g_submit_url),
                  first_var ("u", file_config.username, 0),
                  add_var ("s", g_md5_response, 0),
                  NULL);

  post_data = strdup2 ("\0");

  while (queue_size && (count < MAX_SUBMIT_COUNT))
    {
      snprintf (len, MAX_VAR_SIZE, "%i", queue->length);

      a = add_var_i ("a", count, queue->artist, 1);
      t = add_var_i ("t", count, queue->track, 1);
      l = add_var_i ("l", count, len, 1);
      i = add_var_i ("i", count, queue->time, 1);
      b = add_var_i ("b", count, queue->album, 0);
      m = add_var_i ("m", count, queue->mbid, 0);

      if (a && t && l && i)
        {
          /* build submit url. */
          post_data = concatDX (post_data, a, t, b, m, l, i, NULL);
          count++;
          queue = queue->next;
        }
      else
        {
          /* NOTE: this is completely untested :( */

          /* not submitting invalid etry, remove it from queue. */
          queue = as_queue_remove (queue);
        }

      queue_size--;
    }

  notice ("submitting %i song%s.", count, count==1 ? "" : "s");
  notice ("post data: %s", post_data);
  notice ("url: %s", url);

  g_submit_pending = count;
  if (!conn_initiate (url, &as_submit_callback, post_data, g_sleep))
    {
      warning ("something went wrong when trying to connect,"
               " probably a bug.");

      g_state = AS_READY;
      as_increase_interval ();
    }

  free (url);
}

int
as_songchange (const char *file, const char *artist, const char *track,
               const char *album, const char *mbid, const int length,
               const char *time)
{
  struct song *current;

  /* from the 1.2 protocol draft:

      You may still submit if there is no album title (variable b)
      You may still submit if there is no musicbrainz id available (variable m)

    everything else is mandatory.
  */
  if (!(artist && strlen (artist)))
    {
      warning ("empty artist, not submitting");
      warning ("please check the tags on %s", file);
      return -1;
    }

  if (!(track && strlen (track)))
    {
      warning ("empty title, not submitting");
      warning ("please check the tags on %s", file);
      return -1;
    }

  current = (struct song *) malloc (sizeof (struct song));
  if (!current)
    exit (ENOMEM);

  current->next = NULL;
  current->artist = strdup2 (artist);
  current->track = strdup2 (track);
  current->album = strdup2 (album);
  current->mbid = strdup2 (mbid);
  current->length = length;
  current->time = time ? strdup2 (time) : as_timestamp ();

  if (!current->artist) current->artist = strdup2 ("");
  if (!current->track) current->track = strdup2 ("");
  if (!current->album) current->album = strdup2 ("");
  if (!current->mbid) current->mbid = strdup2 ("");

  notice ("%s, songchange: %s - %s / %s (%i)",
          current->time, current->artist, current->album,
          current->track, current->length);

  g_queue_size++;

  if (!g_queue)
    {
      g_queue = current;
      g_queuep = current;
    }
  else
    {
      g_queuep->next = current;
      g_queuep = current;
    }

  return g_queue_size;
}


void
as_init (unsigned int seconds)
{
  int saved;
  g_sleep = seconds;

  if (g_state != AS_NOTHING)
    fatal ("as_init called twice, this is probably a bug.");

  notice ("starting mpdscribble (" AS_CLIENT_ID " " AS_CLIENT_VERSION ").");

  saved = file_read_cache ();
  notice ("(loaded %i song%s from cache)", saved, saved==1 ? "" : "s");

  conn_setup ();
}

void
as_poll (void)
{
  int i;

  switch (g_state)
    {
    case AS_NOTHING:
      if (as_throttle ())
        as_handshake ();
      break;
    case AS_SUBMITTING:
    case AS_HANDSHAKING:
      i = conn_poll ();
      break;
    case AS_READY:
      if (as_throttle ())
        as_submit ();
      break;
    case AS_BADAUTH:
    default:
      break;
    }
}

void
as_save_cache (void)
{
  int saved = file_write_cache (g_queue);
  if (saved >= 0)
    notice ("(saved %i song%s to cache)", saved, saved==1 ? "" : "s");
}

void
as_cleanup (void)
{
  struct song *sng = g_queue;

  as_save_cache ();

  while (sng)
    {
      as_song_cleanup (sng, 1);
      sng = sng->next;
    }

  if (g_submit_url)
    free (g_submit_url);
  if (g_md5_response)
    free (g_md5_response);

  conn_cleanup ();
}

unsigned int
as_sleep (void)
{
  /*
  long end;
  */

  if (!conn_pending ())
    return sleep (g_sleep);

  /*
  end = now () + seconds;
  while (now () < end)
  */
  as_poll ();

  return 0;
}
