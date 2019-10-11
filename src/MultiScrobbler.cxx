/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2019 The Music Player Daemon Project
 * Copyright (C) 2005-2008 Kuno Woudt <kuno@frob.nl>
 * Project homepage: http://musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "MultiScrobbler.hxx"
#include "Scrobbler.hxx"
#include "ScrobblerConfig.hxx"
#include "Protocol.hxx"
#include "Record.hxx"

#include <glib.h>

#include <string.h>

MultiScrobbler::MultiScrobbler(const std::forward_list<ScrobblerConfig> &configs) noexcept
{
	g_message("starting mpdscribble (" AS_CLIENT_ID " " AS_CLIENT_VERSION ")\n");

	for (const auto &i : configs)
		scrobblers.emplace_front(i);
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
			   const char *mbid, const int length) noexcept
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
			   const char *mbid, const int length,
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
		g_warning("empty artist, not submitting; "
			  "please check the tags on %s\n", file);
		return;
	}

	if (!(track && strlen(track))) {
		g_warning("empty title, not submitting; "
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

	g_message("%s, songchange: %s - %s (%i)\n",
		  record.time.c_str(), record.artist.c_str(),
		  record.track.c_str(), record.length);

	for (auto &i : scrobblers)
		i.Push(record);
}

void
MultiScrobbler::SubmitNow() noexcept
{
	for (auto &i : scrobblers)
		i.SubmitNow();
}
