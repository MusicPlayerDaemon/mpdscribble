// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "MultiScrobbler.hxx"
#include "Scrobbler.hxx"
#include "ScrobblerConfig.hxx"
#include "Protocol.hxx"
#include "Record.hxx"
#include "Log.hxx"

#include <string.h>

MultiScrobbler::MultiScrobbler(const std::forward_list<ScrobblerConfig> &configs,
			       EventLoop &event_loop,
			       CurlGlobal &curl_global)
{
	LogInfo("starting mpdscribble (" AS_CLIENT_ID " " AS_CLIENT_VERSION ")");

	for (const auto &i : configs)
		scrobblers.emplace_front(i, event_loop, curl_global);
}

MultiScrobbler::~MultiScrobbler() noexcept = default;

void
MultiScrobbler::WriteJournal() noexcept
{
	for (auto &i : scrobblers)
		i.WriteJournal();
}

void
MultiScrobbler::NowPlaying(const char *artist, const char *track,
			   const char *album, const char *number,
			   const char *mbid,
			   std::chrono::steady_clock::duration length) noexcept
{
	Record record;

	if (artist != nullptr)
		record.artist = artist;

	if (track != nullptr)
		record.track = track;

	if (album != nullptr)
		record.album = album;

	if (number != nullptr)
		record.number = number;

	if (mbid != nullptr)
		record.mbid = mbid;

	record.length = length;

	for (auto &i : scrobblers)
		i.ScheduleNowPlaying(record);
}

void
MultiScrobbler::SongChange(const char *file, const char *artist, const char *track,
			   const char *album, const char *number,
			   const char *mbid,
			   std::chrono::steady_clock::duration length,
			   bool love,
			   const char *time2) noexcept
{
	Record record;

	/* from the 1.2 protocol draft:

	   You may still submit if there is no album title (variable b)
	   You may still submit if there is no musicbrainz id available (variable m)

	   everything else is mandatory.
	 */
	if (!(artist && strlen(artist))) {
		FormatWarning("empty artist, not submitting; "
			      "please check the tags on %s\n", file);
		return;
	}

	if (!(track && strlen(track))) {
		FormatWarning("empty title, not submitting; "
			      "please check the tags on %s", file);
		return;
	}

	record.artist = artist;
	record.track = track;

	if (album != nullptr)
		record.album = album;

	if (number != nullptr)
		record.number = number;

	if (mbid != nullptr)
		record.mbid = mbid;

	record.length = length;
	record.time = time2 ? time2 : as_timestamp();
	record.love = love;
	record.source = strstr(file, "://") == nullptr ? "P" : "R";

	FormatInfo("%s, songchange: %s - %s (%i)\n",
		   record.time.c_str(), record.artist.c_str(),
		   record.track.c_str(),
		   (int)std::chrono::duration_cast<std::chrono::seconds>(record.length).count());

	for (auto &i : scrobblers)
		i.Push(record);
}

void
MultiScrobbler::SubmitNow() noexcept
{
	for (auto &i : scrobblers)
		i.SubmitNow();
}
