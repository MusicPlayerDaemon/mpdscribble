// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef SCROBBLER_CONFIG_HXX
#define SCROBBLER_CONFIG_HXX

#include "IgnoreList.hxx"

#include <string>

struct ScrobblerConfig {
	/**
	 * The name of the mpdscribble.conf section.  It is used in
	 * log messages.
	 */
	std::string name;

	std::string url;
	std::string username;
	std::string password;

	/**
	 * The path of the journal file.  It contains records which
	 * have not been submitted yet.
	 */
	std::string journal;

	/**
	 * The path of the log file.  This is set when logging to a
	 * file is configured instead of submission to an
	 * AudioScrobbler server.
	 */
	std::string file;

	IgnoreList* ignore_list;
};

#endif
