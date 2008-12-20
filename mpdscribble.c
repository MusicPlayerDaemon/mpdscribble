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

static GMainLoop *main_loop;
static guint save_source_id;

static GTimer *timer;
static char mbid[MBID_BUFFER_SIZE];

static void signal_handler(G_GNUC_UNUSED int signum)
{
	g_main_loop_quit(main_loop);
}

static void sigpipe_handler(G_GNUC_UNUSED int signum)
{
	warning("broken pipe, disconnected from mpd");
}

static bool played_long_enough(int length)
{
	int elapsed = g_timer_elapsed(timer, NULL);

	/* http://www.lastfm.de/api/submissions "The track must have been
	   played for a duration of at least 240 seconds or half the track's
	   total length, whichever comes first. Skipping or pausing the
	   track is irrelevant as long as the appropriate amount has been
	   played."
	 */
	return elapsed > 240 || (length >= 30 && elapsed > length / 2);
}

static void song_changed(const struct mpd_song *song)
{
	notice("new song detected (%s - %s), id: %i, pos: %i",
	       song->artist, song->title, song->id, song->pos);

	g_timer_start(timer);

	if (file_config.musicdir && chdir(file_config.musicdir) == 0) {
		// yeah, I know i'm being silly, but I can't be arsed to
		// concat the parts :P
		if (getMBID(song->file, mbid))
			mbid[0] = 0x00;
		else
			notice("mbid is %s.", mbid);
	}

	as_now_playing(song->artist, song->title, song->album, mbid,
		       song->time);
}

/**
 * Regularly save the cache.
 */
static gboolean
timer_save_cache(G_GNUC_UNUSED gpointer data)
{
	as_save_cache();
	return true;
}

/**
 * Pause mode on the current song was activated.
 */
void
song_paused(void)
{
	g_timer_stop(timer);
}

/**
 * The current song continues to play (after pause).
 */
void
song_continued(void)
{
	g_timer_continue(timer);
}

/**
 * MPD started playing this song.
 */
void
song_started(const struct mpd_song *song)
{
	song_changed(song);
}

/**
 * MPD stopped playing this song.
 */
void
song_ended(const struct mpd_song *song)
{
	int q;

	if (!played_long_enough(song->time))
		return;

	/* FIXME:
	   libmpdclient doesn't have any way to fetch the musicbrainz id. */
	q = as_songchange(song->file, song->artist,
			  song->title,
			  song->album, mbid,
			  song->time >
			  0 ? song->time : (int)
			  g_timer_elapsed(timer,
					  NULL),
			  NULL);
	if (q != -1)
		notice
			("added (%s - %s) to submit queue at position %i.",
			 song->artist, song->title, q);
}

int main(int argc, char **argv)
{
	FILE *log;

	/* apparantly required for regex.h, which
	   is used in file.h */
	set_logfile(stderr, 2);
	if (!file_read_config(argc, argv))
		fatal("cannot read configuration file.\n");

	log = file_open_logfile();
	set_logfile(log, file_config.verbose);

	main_loop = g_main_loop_new(NULL, FALSE);

	lmc_connect(file_config.host, file_config.port);
	as_init();

	if (signal(SIGINT, signal_handler) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
	if (signal(SIGHUP, signal_handler) == SIG_IGN)
		signal(SIGHUP, SIG_IGN);
	if (signal(SIGTERM, signal_handler) == SIG_IGN)
		signal(SIGTERM, SIG_IGN);
	if (signal(SIGPIPE, sigpipe_handler) == SIG_IGN)
		signal(SIGPIPE, SIG_IGN);

	timer = g_timer_new();

	/* set up timeouts */

	save_source_id = g_timeout_add(file_config.cache_interval * 1000,
				       timer_save_cache, NULL);

	/* run the main loop */

	g_main_loop_run(main_loop);

	/* cleanup */

	notice("shutting down...");

	g_main_loop_unref(main_loop);

	g_timer_destroy(timer);

	as_cleanup();
	lmc_disconnect();
	file_cleanup();

	return 0;
}
