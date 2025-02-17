// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "Instance.hxx"
#include "Daemon.hxx"
#include "CommandLine.hxx"
#include "ReadConfig.hxx"
#include "Config.hxx"
#include "Log.hxx"
#include "lib/curl/Init.hxx"
#include "util/PrintException.hxx"
#include "SdDaemon.hxx"

#ifndef _WIN32
#include "lib/gcrypt/Init.hxx"
#endif

#include <stdlib.h>

static std::chrono::steady_clock::duration
GetSongDuration(const struct mpd_song *song) noexcept
{
	return std::chrono::milliseconds(mpd_song_get_duration_ms(song));
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

static const char *
artist(const struct mpd_song *song) noexcept
{
	if (mpd_song_get_tag(song, MPD_TAG_ARTIST, 0) != nullptr) {
		return mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
	} else {
		return mpd_song_get_tag(song, MPD_TAG_ALBUM_ARTIST, 0);
	}
}

void
Instance::OnMpdSongChanged(const struct mpd_song *song) noexcept
{
	FmtInfo("new song detected ({} - {}), id: {}, pos: {}\n",
		artist(song),
		mpd_song_get_tag(song, MPD_TAG_TITLE, 0),
		mpd_song_get_id(song), mpd_song_get_pos(song));

	stopwatch.Start();

	scrobblers.NowPlaying(artist(song),
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
	stopwatch.Stop();
}

/**
 * The current song continues to play (after pause).
 */
void
Instance::OnMpdResumed() noexcept
{
	stopwatch.Resume();
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
	const auto prev_elapsed = stopwatch.GetDuration();

	if (song_repeated(song, elapsed, prev_elapsed)) {
		/* the song is playing repeatedly: make it virtually
		   stop and re-start */
		LogDebug("repeated song detected");

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
	const auto elapsed = stopwatch.GetDuration();
	const auto length = GetSongDuration(song);

	if (!played_long_enough(elapsed, length))
		return;

	/* FIXME:
	   libmpdclient doesn't have any way to fetch the musicbrainz id. */
	scrobblers.SongChange(mpd_song_get_uri(song),
			      artist(song),
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

	Config config;
	parse_cmdline(config, argc, argv);
	file_read_config(config);

	log_init(NullableString(config.log), config.verbose);

	daemonize_init(NullableString(config.daemon_user),
		       NullableString(config.pidfile));

	if (!config.no_daemon)
		daemonize_detach();

	daemonize_write_pidfile();
	daemonize_set_user();

#ifndef NDEBUG
	if (!config.no_daemon)
#endif
		daemonize_close_stdout_stderr();

#ifndef _WIN32
	Gcrypt::Init();
#endif
	const ScopeCurlInit init;

	Instance instance(config);

	/* run the main loop */

	sd_notify(0, "READY=1");

	instance.Run();

	/* cleanup */

	LogInfo("shutting down");

	instance.scrobblers.WriteJournal();

	log_deinit();

	daemonize_finish();

	return EXIT_SUCCESS;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
