// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef IGNORE_LIST_HXX
#define IGNORE_LIST_HXX

#include <string>
#include <vector>

#include "Record.hxx"

struct IgnoreListEntry {

	std::string artist;
	std::string album;
	std::string title;
	std::string track;

	[[nodiscard]] bool matches_record(const Record& record) const noexcept;
};

struct IgnoreList {
	std::vector<IgnoreListEntry> entries;

	[[nodiscard]] bool matches_record(const Record& record) const noexcept;
};


#endif
