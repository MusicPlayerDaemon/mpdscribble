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

#include "Instance.hxx"
#include "Daemon.hxx"
#include "CommandLine.hxx"
#include "ReadConfig.hxx"
#include "Config.hxx"
#include "Log.hxx"
#include "util/PrintException.hxx"

#include <glib.h>
#ifndef WIN32
#include <glib-unix.h>
#endif

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static std::chrono::steady_clock::duration
GetSongDuration(const struct mpd_song *song) noexcept
{
#if LIBMPDCLIENT_CHECK_VERSION(2,10,0)
	return std::chrono::milliseconds(mpd_song_get_duration_ms(song));
#else
	return std::chrono::seconds(mpd_song_get_duration(song));
#endif
}

static constexpr bool
played_long_enough(std::chrono::steady_clock::duration elapsed,
		   std::chrono::steady_clock::duration length) noexcept
{
	/* http://www.lastfm.de/api/submissions "The track must have been
	   played for a duration of at least 240 seconds or half the track's
	   total length, whichever comes first. Skipping or pausing the
	   track is irrelevant as long as the appropriate amount has been
	   played."
	 */
	return elapsed > std::chrono::minutes(4) ||
		(length >= std::chrono::seconds(30) && elapsed > length / 2);
}

/**
 * This function determines if a song is played repeatedly: according
 * to MPD, the current song hasn't changed, and now we're comparing
 * the "elapsed" value with the previous one.
 */
static bool
song_repeated(const struct mpd_song *song,
	      std::chrono::steady_clock::duration elapsed,
	      std::chrono::steady_clock::duration prev_elapsed) noexcept
{
	return elapsed < std::chrono::minutes(1) && prev_elapsed > elapsed &&
		played_long_enough(prev_elapsed - elapsed,
				   GetSongDuration(song));
}

void
Instance::OnMpdSongChanged(const struct mpd_song *song) noexcept
{
	g_message("new song detected (%s - %s), id: %u, pos: %u\n",
		  mpd_song_get_tag(song, MPD_TAG_ARTIST, 0),
		  mpd_song_get_tag(song, MPD_TAG_TITLE, 0),
		  mpd_song_get_id(song), mpd_song_get_pos(song));

	g_timer_start(timer);

	scrobblers.NowPlaying(mpd_song_get_tag(song, MPD_TAG_ARTIST, 0),
			      mpd_song_get_tag(song, MPD_TAG_TITLE, 0),
			      mpd_song_get_tag(song, MPD_TAG_ALBUM, 0),
			      mpd_song_get_tag(song, MPD_TAG_TRACK, 0),
			      mpd_song_get_tag(song, MPD_TAG_MUSICBRAINZ_TRACKID, 0),
			      GetSongDuration(song));
}

/**
 * Pause mode on the current song was activated.
 */
void
Instance::OnMpdPaused() noexcept
{
	g_timer_stop(timer);
}

/**
 * The current song continues to play (after pause).
 */
void
Instance::OnMpdResumed() noexcept
{
	g_timer_continue(timer);
}

/**
 * MPD started playing this song.
 */
void
Instance::OnMpdStarted(const struct mpd_song *song) noexcept
{
	OnMpdSongChanged(song);
}

/**
 * MPD is still playing the song.
 */
void
Instance::OnMpdPlaying(const struct mpd_song *song,
		       std::chrono::steady_clock::duration elapsed) noexcept
{
	const auto prev_elapsed = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(g_timer_elapsed(timer, nullptr)));

	if (song_repeated(song, elapsed, prev_elapsed)) {
		/* the song is playing repeatedly: make it virtually
		   stop and re-start */
		g_debug("repeated song detected");

		OnMpdEnded(song, false);
		OnMpdStarted(song);
	}
}

/**
 * MPD stopped playing this song.
 */
void
Instance::OnMpdEnded(const struct mpd_song *song, bool love) noexcept
{
	const auto elapsed = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(g_timer_elapsed(timer, nullptr)));
	const auto length = GetSongDuration(song);

	if (!played_long_enough(elapsed, length))
		return;

	/* FIXME:
	   libmpdclient doesn't have any way to fetch the musicbrainz id. */
	scrobblers.SongChange(mpd_song_get_uri(song),
			      mpd_song_get_tag(song, MPD_TAG_ARTIST, 0),
			      mpd_song_get_tag(song, MPD_TAG_TITLE, 0),
			      mpd_song_get_tag(song, MPD_TAG_ALBUM, 0),
			      mpd_song_get_tag(song, MPD_TAG_TRACK, 0),
			      mpd_song_get_tag(song, MPD_TAG_MUSICBRAINZ_TRACKID, 0),
			      length.count() > 0 ? length : elapsed,
			      love,
			      nullptr);
}

int
main(int argc, char **argv) noexcept
try {
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

	Instance instance(file_config);

	/* run the main loop */

	instance.Run();

	/* cleanup */

	g_message("shutting down\n");

	instance.scrobblers.WriteJournal();

	file_cleanup();
	log_deinit();

	daemonize_finish();

	return EXIT_SUCCESS;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
