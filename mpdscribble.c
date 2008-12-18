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

#include "file.h"
#include "misc.h"
#include "lmc.h"
#include "as.h"
#include "mbid.h"

#include <glib.h>

#include <stdbool.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static bool submitted = true;
static GTimer *timer;
static char mbid[MBID_BUFFER_SIZE];

static void
cleanup (void)
{
  notice ("shutting down...");

  as_cleanup ();
  lmc_disconnect ();
  file_cleanup ();
}

static void
signal_handler(G_GNUC_UNUSED int signum)
{
  exit (23);
}

static void
sigpipe_handler(G_GNUC_UNUSED int signum)
{
  warning ("broken pipe, disconnected from mpd");
}

static bool
played_long_enough(int length)
{
  int elapsed = g_timer_elapsed(timer, NULL);

  /* http://www.lastfm.de/api/submissions "The track must have been
     played for a duration of at least 240 seconds or half the track's
     total length, whichever comes first. Skipping or pausing the
     track is irrelevant as long as the appropriate amount has been
     played."
   */
  return elapsed > 240 ||
    (length >= 30 && elapsed > length / 2);
}

static void
song_changed(const struct mpd_song *song)
{
  if (song->artist == NULL || song->title == NULL) {
    notice("new song detected with tags missing (%s)", song->file);
    submitted = true;
    return;
  }

  notice("new song detected (%s - %s), id: %i, pos: %i",
         song->artist, song->title, song->id, song->pos);

  g_timer_start(timer);

  if (file_config.musicdir && chdir (file_config.musicdir) == 0)
    {
      // yeah, I know i'm being silly, but I can't be arsed to
      // concat the parts :P
      if (getMBID (song->file, mbid))
        mbid[0] = 0x00;
      else
        notice ("mbid is %s.", mbid);
    }

  submitted = false;

  as_now_playing(song->artist, song->title, song->album,
                 mbid, song->time);
}

int
main (int argc, char** argv)
{
  struct mpd_song *song = NULL, *prev;

  int last_id = -1;
  int elapsed = 0;
  int was_paused = 0;
  int next_save = 0;
  FILE * log;

  /* apparantly required for regex.h, which
     is used in file.h */
  set_logfile (stderr, 2);
  if (!file_read_config (argc, argv))
    fatal ("cannot read configuration file.\n");

  log = file_open_logfile ();
  set_logfile (log, file_config.verbose);

  lmc_connect (file_config.host, file_config.port);
  as_init (file_config.sleep);

  atexit (cleanup);

  if (signal (SIGINT, signal_handler) == SIG_IGN)
    signal (SIGINT, SIG_IGN);
  if (signal (SIGHUP, signal_handler) == SIG_IGN)
    signal (SIGHUP, SIG_IGN);
  if (signal (SIGTERM, signal_handler) == SIG_IGN)
    signal (SIGTERM, SIG_IGN);
  if (signal (SIGPIPE, sigpipe_handler) == SIG_IGN)
    signal (SIGPIPE, SIG_IGN);

  next_save = now () + file_config.cache_interval;

  timer = g_timer_new();

  while (1)
    {
      as_poll ();
      fflush (log);
      as_sleep ();

      prev = song;
      elapsed = lmc_current (&song);

      if (now () > next_save)
        {
          as_save_cache ();
          next_save = now () + file_config.cache_interval;
        }


      if (elapsed == MPD_STATUS_STATE_PAUSE)
        {
          if (!was_paused)
            g_timer_stop(timer);
          was_paused = 1;
          continue;
        }
      else if (elapsed != MPD_STATUS_STATE_PLAY)
        {
          last_id = -1;
          continue;
        }

      if (was_paused)
        {
          if (song != NULL && song->id == last_id)
            g_timer_continue(timer);
          was_paused = false;
        }

      /* submit the previous song */
      if (!submitted && prev != NULL &&
          (song == NULL || prev->id != song->id) &&
          played_long_enough(prev->time))
        {
          /* FIXME:
             libmpdclient doesn't have any way to fetch the musicbrainz id. */
          int q = as_songchange (prev->file, prev->artist, prev->title,
                                 prev->album, mbid,
                                 prev->time > 0 ? prev->time
                                 : (int)g_timer_elapsed(timer, NULL),
                                 NULL);
          if (q != -1)
            notice ("added (%s - %s) to submit queue at position %i.",
                    prev->artist, prev->title, q);
        }

      /* new song. */
      if (song != NULL && song->id != last_id)
        {
          song_changed(song);
          last_id = song->id;
        }

      if (prev != NULL)
        mpd_freeSong(prev);
    }

  g_timer_destroy(timer);

  return 0;
}
