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

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define MAX_SKIP_ERROR (3 + file_config.sleep) /* in seconds. */

const char *program_name;

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

int
main (int argc, char** argv)
{
  lmc_song song;

  int last_id = -1;
  int last_el = 0;
  int last_mark = 0;
  int elapsed = 0;
  int submitted = 1;
  int was_paused = 0;
  int next_save = 0;
  char mbid[MBID_BUFFER_SIZE];
  FILE * log;

  /* apparantly required for regex.h, which
     is used in file.h */
  program_name = argv[0];
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
  mbid[0] = 0x00;

  while (1)
    {
      int max_skip_error;

      as_poll ();
      fflush (log);
      as_sleep ();
      elapsed = lmc_current (&song);
      max_skip_error = MAX_SKIP_ERROR + lmc_xfade_hack ();

      if (now () > next_save)
        {
          as_save_cache ();
          next_save = now () + file_config.cache_interval;
        }


      if (elapsed == LMC_PAUSED)
        {
          was_paused = 1;
          continue;
        }
      else if (elapsed == LMC_NOTPLAYING)
        {
          last_id = -1;
          continue;
        }

      if (was_paused)
        {
          /* song is paused, reset the measured time to the amount
             the song has actually played. */
          was_paused = 0;
          last_el = elapsed;
          last_mark = now ();
        }

      /* new song. */
      if (song.id != last_id)
        {
          if (song.artist && song.title)
            notice ("new song detected (%s - %s), id: %i, pos: %i", song.artist, song.title, song.id, song.pos);
          else
            notice ("new song detected with tags missing (%s)", song.file); 
          last_id = song.id;
          last_el = elapsed;
          last_mark = now ();

          submitted = 1;

          if (file_config.musicdir && chdir (file_config.musicdir) != 0)
            {
              // yeah, I know i'm being silly, but I can't be arsed to 
              // concat the parts :P
              if (getMBID (song.file, mbid))
                mbid[0] = 0x00;
              else
                notice ("mbid is %s.", mbid);
            }

          if (song.time < 30)
            notice ("however, song is too short, not submitting.");
          /* don't submit the song which is being played when we start,.. too
             many double submits when restarting the client during testing in
             the first half of a song ;) */
          else if (elapsed > max_skip_error*2)
            notice ("skipping detected (%i), not submitting.", elapsed);
          else
            submitted = 0;

          if (song.artist != NULL && song.title != NULL)
            as_now_playing(song.artist, song.title, song.album,
                           mbid, song.time);
        }
      /* not a new song, so check for skipping. */
      else if (!submitted)
        {
          int time2 = now();
          int calculated = last_el + (time2 - last_mark);

          if ((elapsed+max_skip_error < calculated)
              || (elapsed-max_skip_error > calculated))
            {
              notice ("skipping detected (%i to %i), not submitting.", elapsed,
                calculated);
              submitted = 1;
            }

          last_el = elapsed;
          last_mark = time2;
        }

      if (!submitted && ((elapsed > 240) || (elapsed > (song.time/2))))
        {
          /* FIXME:
             libmpdclient doesn't have any way to fetch the musicbrainz id. */
          int q = as_songchange (song.file, song.artist, song.title,
                                 song.album, mbid, song.time, NULL);
          if (q != -1)
            notice ("added (%s - %s) to submit queue at position %i.",
                    song.artist, song.title, q);

          submitted = 1;
        }
    }

  return 0;
}
