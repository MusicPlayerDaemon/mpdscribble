/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2019 The Music Player Daemon Project
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

#include "Daemon.hxx"
#include "CommandLine.hxx"
#include "ReadConfig.hxx"
#include "Config.hxx"
#include "Log.hxx"
#include "MpdObserver.hxx"
#include "Scrobbler.hxx"
#include "HttpClient.hxx"

#include <glib.h>
#ifndef WIN32
#include <glib-unix.h>
#endif

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static GMainLoop *main_loop;
static guint save_source_id;

static GTimer *timer;

#ifndef WIN32
static gboolean exit_signal_handler(G_GNUC_UNUSED gpointer user_data) {
	g_main_loop_quit(main_loop);
	return true;
}

static gboolean submit_signal_handler(G_GNUC_UNUSED gpointer user_data) {
  as_submit_now();
	return true;
}

static void setup_signals()
{
	signal(SIGPIPE, SIG_IGN);

	g_unix_signal_add(SIGINT, exit_signal_handler, nullptr);
	g_unix_signal_add(SIGTERM, exit_signal_handler, nullptr);
	g_unix_signal_add(SIGHUP, exit_signal_handler, nullptr);

	g_unix_signal_add(SIGUSR1, submit_signal_handler, nullptr);
}
#endif

static bool played_long_enough(int elapsed, int length)
{
	/* http://www.lastfm.de/api/submissions "The track must have been
	   played for a duration of at least 240 seconds or half the track's
	   total length, whichever comes first. Skipping or pausing the
	   track is irrelevant as long as the appropriate amount has been
	   played."
	 */
	return elapsed > 240 || (length >= 30 && elapsed > length / 2);
}

/**
 * This function determines if a song is played repeatedly: according
 * to MPD, the current song hasn't changed, and now we're comparing
 * the "elapsed" value with the previous one.
 */
static bool
song_repeated(const struct mpd_song *song, int elapsed, int prev_elapsed)
{
	return elapsed < 60 && prev_elapsed > elapsed &&
		played_long_enough(prev_elapsed - elapsed,
				   mpd_song_get_duration(song));
}

static void song_changed(const struct mpd_song *song)
{
	g_message("new song detected (%s - %s), id: %u, pos: %u\n",
		  mpd_song_get_tag(song, MPD_TAG_ARTIST, 0),
		  mpd_song_get_tag(song, MPD_TAG_TITLE, 0),
		  mpd_song_get_id(song), mpd_song_get_pos(song));

	g_timer_start(timer);

	as_now_playing(mpd_song_get_tag(song, MPD_TAG_ARTIST, 0),
		       mpd_song_get_tag(song, MPD_TAG_TITLE, 0),
		       mpd_song_get_tag(song, MPD_TAG_ALBUM, 0),
		       mpd_song_get_tag(song, MPD_TAG_TRACK, 0),
		       mpd_song_get_tag(song, MPD_TAG_MUSICBRAINZ_TRACKID, 0),
		       mpd_song_get_duration(song));
}

/**
 * Regularly save the cache.
 */
static gboolean
timer_save_journal(G_GNUC_UNUSED gpointer data)
{
	as_save_cache();
	return true;
}

/**
 * Pause mode on the current song was activated.
 */
void
song_paused()
{
	g_timer_stop(timer);
}

/**
 * The current song continues to play (after pause).
 */
void
song_continued()
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
 * MPD is still playing the song.
 */
void
song_playing(const struct mpd_song *song, int elapsed)
{
	int prev_elapsed = g_timer_elapsed(timer, nullptr);

	if (song_repeated(song, elapsed, prev_elapsed)) {
		/* the song is playing repeatedly: make it virtually
		   stop and re-start */
		g_debug("repeated song detected");

		song_ended(song, false);
		song_started(song);
	}
}

/**
 * MPD stopped playing this song.
 */
void
song_ended(const struct mpd_song *song, bool love)
{
	int elapsed = g_timer_elapsed(timer, nullptr);

	if (!played_long_enough(elapsed, mpd_song_get_duration(song)))
		return;

	/* FIXME:
	   libmpdclient doesn't have any way to fetch the musicbrainz id. */
	as_songchange(mpd_song_get_uri(song),
		      mpd_song_get_tag(song, MPD_TAG_ARTIST, 0),
		      mpd_song_get_tag(song, MPD_TAG_TITLE, 0),
		      mpd_song_get_tag(song, MPD_TAG_ALBUM, 0),
		      mpd_song_get_tag(song, MPD_TAG_TRACK, 0),
		      mpd_song_get_tag(song, MPD_TAG_MUSICBRAINZ_TRACKID, 0),
		      mpd_song_get_duration(song) > 0
		      ? mpd_song_get_duration(song)
		      : g_timer_elapsed(timer, nullptr),
		      love,
		      nullptr);
}

int main(int argc, char **argv)
{
	daemonize_close_stdin();

	parse_cmdline(argc, argv);

	if (!file_read_config())
		g_error("cannot read configuration file\n");

	log_init(file_config.log, file_config.verbose);

	daemonize_init(file_config.daemon_user, file_config.pidfile);

	if (!file_config.no_daemon)
		daemonize_detach();

	daemonize_write_pidfile();
	daemonize_set_user();

#ifndef NDEBUG
	if (!file_config.no_daemon)
#endif
		daemonize_close_stdout_stderr();

	main_loop = g_main_loop_new(nullptr, false);

	lmc_connect(file_config.host, file_config.port);
	http_client_init();
	as_init(file_config.scrobblers);

#ifndef WIN32
	setup_signals();
#endif

	timer = g_timer_new();

	/* set up timeouts */

	save_source_id = g_timeout_add_seconds(file_config.journal_interval,
					       timer_save_journal, nullptr);

	/* run the main loop */

	g_main_loop_run(main_loop);

	/* cleanup */

	g_message("shutting down\n");

	g_source_remove(save_source_id);
	g_main_loop_unref(main_loop);

	g_timer_destroy(timer);

	as_save_cache();
	as_cleanup();
	http_client_finish();
	lmc_disconnect();
	file_cleanup();
	log_deinit();

	daemonize_finish();

	return 0;
}
