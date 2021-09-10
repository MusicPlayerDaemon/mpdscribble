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

#include "ReadConfig.hxx"
#include "util/Compiler.h"
#include "util/RuntimeError.hxx"
#include "util/ScopeExit.hxx"
#include "util/StringStrip.hxx"
#include "Config.hxx"
#include "IniFile.hxx"
#include "SdDaemon.hxx"
#include "config.h"

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include <cassert>

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
  default locations for files.

  FILE_ETC_* are paths for a system-wide install.
  FILE_USR_* will be used instead if FILE_USR_CONF exists.
*/

#ifndef _WIN32

#define FILE_CACHE "/var/cache/mpdscribble/mpdscribble.cache"
#define FILE_HOME_CONF "~/.mpdscribble/mpdscribble.conf"
#define FILE_HOME_CACHE "~/.mpdscribble/mpdscribble.cache"

#endif

#define AS_HOST "http://post.audioscrobbler.com/"

gcc_pure
static bool
file_exists(const char *filename) noexcept
{
	struct stat st;
	return stat(filename, &st) == 0 && S_ISREG(st.st_mode);
}

static std::string
file_expand_tilde(const char *path)
{
	const char *home;

	if (path[0] != '~')
		return path;

	home = getenv("HOME");
	if (!home)
		home = "./";

	return std::string(home) + (path + 1);
}

static std::string
get_default_config_path(Config &config)
{
#ifndef _WIN32
	auto file = file_expand_tilde(FILE_HOME_CONF);
	if (file_exists(file.c_str())) {
		config.loc = file_home;
		return file;
	} else {
		if (!file_exists(FILE_CONF))
			return {};

		config.loc = file_etc;
		return FILE_CONF;
	}
#else
	(void)config;
	return "mpdscribble.conf";
#endif
}

static const char *
get_default_log_path() noexcept
{
#ifndef _WIN32
	return sd_booted()
		? "-" /* log to journal if systemd is used */
		: "syslog";
#else
	return "-";
#endif
}

static std::string
get_default_cache_path(const Config &config)
{
#ifndef _WIN32
	switch (config.loc) {
	case file_home:
		return file_expand_tilde(FILE_HOME_CACHE);

	case file_etc:
		return FILE_CACHE;

	case file_unknown:
		return {};
	}

	assert(false);
	return {};
#else
	(void)config;
	return "mpdscribble.cache";
#endif
}

static const char *
GetString(const IniSection &section, const std::string &key) noexcept
{
	auto i = section.find(key);
	if (i == section.end())
		return nullptr;
	return i->second.c_str();
}

static std::string
GetStdString(const IniSection &section, const std::string &key) noexcept
{
	auto i = section.find(key);
	if (i == section.end())
		return {};
	return i->second;
}

static bool
load_string(const IniFile &file, const char *name, std::string &value) noexcept
{
	if (!value.empty())
		/* already set by command line */
		return false;

	auto section = file.find(std::string());
	if (section == file.end())
		return false;

	value = GetStdString(section->second, name);
	return !value.empty();
}

static bool
load_integer(const IniFile &file, const char *name, int *value_r)
{
	if (*value_r != -1)
		/* already set by command line */
		return false;

	auto section = file.find(std::string());
	if (section == file.end())
		return false;

	const char *s = GetString(section->second, name);
	if (s == nullptr)
		return false;

	char *endptr;
	auto value = strtol(s, &endptr, 10);
	if (endptr == s || *endptr != 0)
		throw FormatRuntimeError("Not a number: '%s'", s);

	*value_r = value;
	return true;
}

static bool
load_unsigned(const IniFile &file, const char *name, unsigned *value_r)
{
	int value = -1;

	if (!load_integer(file, name, &value))
		return false;

	if (value < 0)
		throw FormatRuntimeError("Setting '%s' must not be negative", name);

	*value_r = (unsigned)value;
	return true;
}

static ScrobblerConfig
load_scrobbler_config(const Config &config,
		      const std::string &section_name,
		      const IniSection &section)
{
	ScrobblerConfig scrobbler;

	/* Use default host for mpdscribble group, for backward compatability */
	if (section_name.empty()) {
		scrobbler.name = "last.fm";
		scrobbler.url = AS_HOST;
	} else {
		scrobbler.name = section_name;
		scrobbler.file = GetStdString(section, "file");
		if (scrobbler.file.empty()) {
			scrobbler.url = GetStdString(section, "url");
			if (scrobbler.url.empty())
				throw FormatRuntimeError("Section '%s' has neither 'file' nor 'url'", section_name.c_str());
		}
	}

	if (scrobbler.file.empty()) {
		scrobbler.username = GetStdString(section, "username");
		if (scrobbler.username.empty())
			throw std::runtime_error("No 'username'");

		scrobbler.password = GetStdString(section, "password");
		if (scrobbler.password.empty())
			throw std::runtime_error("No 'password'");
	}

	scrobbler.journal = GetStdString(section, "journal");
	if (scrobbler.journal.empty() && section_name.empty()) {
		/* mpdscribble <= 0.17 compatibility */
		scrobbler.journal = GetStdString(section, "cache");
		if (scrobbler.journal.empty())
			scrobbler.journal = get_default_cache_path(config);
	}

	return scrobbler;
}

static void
load_config_file(Config &config, const char *path)
{
	const auto file = ReadIniFile(path);

	load_string(file, "pidfile", config.pidfile);
	load_string(file, "daemon_user", config.daemon_user);
	load_string(file, "log", config.log);
	load_string(file, "host", config.host);
	load_unsigned(file, "port", &config.port);
	load_string(file, "proxy", config.proxy);
	if (!load_unsigned(file, "journal_interval",
			   &config.journal_interval))
		load_unsigned(file, "cache_interval",
			      &config.journal_interval);
	load_integer(file, "verbose", &config.verbose);

	for (const auto &section : file) {
		if (section.first.empty() &&
		    section.second.find("username") == section.second.end())
			/* the default section does not contain a
			   username: don't set up the last.fm default
			   scrobbler */
			continue;

		config.scrobblers.emplace_front(load_scrobbler_config(config,
								      section.first,
								      section.second));
	}
}

void
file_read_config(Config &config)
{
	if (config.conf.empty())
		config.conf = get_default_config_path(config);

	/* parse config file options. */

	if (!config.conf.empty())
		load_config_file(config, config.conf.c_str());

	if (config.conf.empty())
		throw std::runtime_error("cannot find configuration file");

	if (config.scrobblers.empty())
		throw FormatRuntimeError("No audioscrobbler host configured in %s",
					 config.conf.c_str());

	if (config.log.empty())
		config.log = get_default_log_path();

	if (config.proxy.empty()) {
		const char *proxy = getenv("http_proxy");
		if (proxy != nullptr)
			config.proxy = proxy;
	}

	if (config.verbose == -1)
		config.verbose = 1;
}
