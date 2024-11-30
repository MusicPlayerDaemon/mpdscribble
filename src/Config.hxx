// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef CONFIG_HXX
#define CONFIG_HXX

#include "ScrobblerConfig.hxx"

#include <forward_list>
#include <string>
#include <map>

enum file_location { file_etc, file_home, file_unknown, };

static inline const char *
NullableString(const std::string &s) noexcept
{
	return s.empty() ? nullptr : s.c_str();
}


struct Config {
	using IgnoreListMap = std::map<std::string, IgnoreList>;

	/** don't daemonize the mpdscribble process */
	bool no_daemon = false;

	std::string pidfile;

	std::string daemon_user;

	std::string log;
	std::string conf;
	std::string host;
	std::string proxy;

	unsigned port = 0;

	/**
	 * The interval in seconds after which the journal is saved to
	 * the file system.
	 */
	unsigned journal_interval = 600;

	int verbose = -1;
	enum file_location loc = file_unknown;

	// Key=file path, value=loaded ignore list
	IgnoreListMap ignore_lists;
	std::forward_list<ScrobblerConfig> scrobblers;
};

#endif
