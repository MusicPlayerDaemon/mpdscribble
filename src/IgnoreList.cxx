// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "IgnoreList.hxx"

#include <cassert>
#include <algorithm>

[[gnu::pure]]
static constexpr bool
MatchIgnoreIfSpecified(std::string_view ignore, std::string_view value)
{
	return ignore.empty() || ignore == value;
}

bool
IgnoreListEntry::matches_record(const Record& record) const noexcept
{
	/*
	   The below logic would always return true if the entry is empty.
	   This condition should never be true, as we don't push empty entries.
	*/
	assert(!artist.empty() || !album.empty() || !title.empty());

	/*
	   Note the mismatch of 'title' and 'track' field names with the Record structure.
	   This is not a bug - the Record structure does not use the expected field names.
	*/
	return MatchIgnoreIfSpecified(artist, record.artist) &&
	       MatchIgnoreIfSpecified(album, record.album) &&
	       MatchIgnoreIfSpecified(title, record.track) &&
	       MatchIgnoreIfSpecified(track, record.number);
}

bool
IgnoreList::matches_record(const Record& record) const noexcept
{
	return std::any_of(entries.begin(),
	                   entries.end(),
	                   [&record](const auto& entry) { return entry.matches_record(record); });
}
