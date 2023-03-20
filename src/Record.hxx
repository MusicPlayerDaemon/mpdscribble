// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef RECORD_HXX
#define RECORD_HXX

#include <chrono>
#include <string>

struct Record {
	std::string artist;
	std::string track;
	std::string album;
	std::string number;
	std::string mbid;
	std::string time;
	std::chrono::steady_clock::duration length{};
	bool love = false;
	const char *source = "P";
};

/**
 * Does this record object have a defined and usable value?
 */
static inline bool
record_is_defined(const Record *record)
{
	return !record->artist.empty() && !record->track.empty();
}

#endif
