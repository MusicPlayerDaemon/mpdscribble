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
#include "Config.hxx"
#include "Scrobbler.hxx"
#include "config.h"

#include <glib.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
  default locations for files.

  FILE_ETC_* are paths for a system-wide install.
  FILE_USR_* will be used instead if FILE_USR_CONF exists.
*/

#ifndef G_OS_WIN32

#define FILE_CACHE "/var/cache/mpdscribble/mpdscribble.cache"
#define FILE_HOME_CONF "~/.mpdscribble/mpdscribble.conf"
#define FILE_HOME_CACHE "~/.mpdscribble/mpdscribble.cache"

#endif

#define AS_HOST "http://post.audioscrobbler.com/"

static int file_exists(const char *filename)
{
	return g_file_test(filename, G_FILE_TEST_IS_REGULAR);
}

static char *
file_expand_tilde(const char *path)
{
	const char *home;

	if (path[0] != '~')
		return g_strdup(path);

	home = getenv("HOME");
	if (!home)
		home = "./";

	return g_strconcat(home, path + 1, nullptr);
}

static char *
get_default_config_path()
{
#ifndef G_OS_WIN32
	char *file = file_expand_tilde(FILE_HOME_CONF);
	if (file_exists(file)) {
		file_config.loc = file_home;
		return file;
	} else {
		g_free(file);

		if (!file_exists(FILE_CONF))
			return nullptr;

		file_config.loc = file_etc;
		return g_strdup(FILE_CONF);
	}
#else
	return g_strdup("mpdscribble.conf");
#endif
}

static char *
get_default_log_path()
{
#ifndef G_OS_WIN32
	return g_strdup("syslog");
#else
	return g_strdup("-");
#endif
}

static char *
get_default_cache_path()
{
#ifndef G_OS_WIN32
	switch (file_config.loc) {
	case file_home:
		return file_expand_tilde(FILE_HOME_CACHE);

	case file_etc:
		return g_strdup(FILE_CACHE);

	case file_unknown:
		return nullptr;
	}

	assert(false);
	return nullptr;
#else
	return g_strdup("mpdscribble.cache");
#endif
}

static char *
get_string(GKeyFile *file, const char *group_name, const char *key,
	   GError **error_r)
{
	char *value = g_key_file_get_string(file, group_name, key, error_r);
	if (value != nullptr)
		g_strchomp(value);
	return value;
}

static std::string
get_std_string(GKeyFile *file, const char *group_name, const char *key,
	       GError **error_r)
{
	char *value = get_string(file, group_name, key, error_r);
	if (value == nullptr)
		return {};
	std::string result(value);
	g_free(value);
	return result;
}

static bool
load_string(GKeyFile *file, const char *name, char **value_r)
{
	GError *error = nullptr;
	char *value;

	if (*value_r != nullptr)
		/* already set by command line */
		return false;

	value = get_string(file, PACKAGE, name, &error);
	if (error != nullptr) {
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
			g_error("%s\n", error->message);
		g_error_free(error);
		return false;
	}

	g_free(*value_r);
	*value_r = value;
	return true;
}

static bool
load_integer(GKeyFile * file, const char *name, int *value_r)
{
	GError *error = nullptr;
	int value;

	if (*value_r != -1)
		/* already set by command line */
		return false;

	value = g_key_file_get_integer(file, PACKAGE, name, &error);
	if (error != nullptr) {
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
			g_error("%s\n", error->message);
		g_error_free(error);
		return false;
	}

	*value_r = value;
	return true;
}

static bool
load_unsigned(GKeyFile *file, const char *name, unsigned *value_r)
{
	int value = -1;

	if (!load_integer(file, name, &value))
		return false;

	if (value < 0)
		g_error("Setting '%s' must not be negative", name);

	*value_r = (unsigned)value;
	return true;
}

static ScrobblerConfig *
load_scrobbler_config(GKeyFile *file, const char *group)
{
	ScrobblerConfig *scrobbler = new ScrobblerConfig();
	GError *error = nullptr;

	/* Use default host for mpdscribble group, for backward compatability */
	if(strcmp(group, "mpdscribble") == 0) {
		char *username = get_string(file, group, "username", nullptr);
		if (username == nullptr) {
			/* the default section does not contain a
			   username: don't set up the last.fm default
			   scrobbler */
			delete scrobbler;
			return nullptr;
		}

		g_free(username);

		scrobbler->name = "last.fm";
		scrobbler->url = AS_HOST;
	} else {
		scrobbler->name = group;
		scrobbler->file = get_std_string(file, group,
						 "file", nullptr);

		if (scrobbler->file.empty()) {
			scrobbler->url = get_std_string(file, group, "url",
							&error);
			if (error != nullptr)
				g_error("%s\n", error->message);
		}
	}

	if (scrobbler->file.empty()) {
		scrobbler->username = get_std_string(file, group, "username", &error);

		scrobbler->username = get_std_string(file, group, "username", &error);
		if (error != nullptr)
			g_error("%s\n", error->message);

		scrobbler->password = get_std_string(file, group, "password", &error);
		if (error != nullptr)
			g_error("%s\n", error->message);
	}

	scrobbler->journal = get_std_string(file, group, "journal", nullptr);
	if (scrobbler->journal.empty() && strcmp(group, "mpdscribble") == 0) {
		/* mpdscribble <= 0.17 compatibility */
		scrobbler->journal = get_std_string(file, group, "cache", nullptr);
		if (scrobbler->journal.empty())
			scrobbler->journal = get_default_cache_path();
	}

	return scrobbler;
}

static void
load_config_file(const char *path)
{
	bool ret;
	char *data1, *data2;
	char **groups;
	int i = -1;
	GKeyFile *file;
	GError *error = nullptr;

	ret = g_file_get_contents(path, &data1, nullptr, &error);
	if (!ret)
		g_error("%s\n", error->message);

	/* GKeyFile does not allow values without a section.  Apply a
	   hack here: prepend the string "[mpdscribble]" to have all
	   values in the "mpdscribble" section */

	data2 = g_strconcat("[" PACKAGE "]\n", data1, nullptr);
	g_free(data1);

	file = g_key_file_new();
	g_key_file_load_from_data(file, data2, strlen(data2),
				  G_KEY_FILE_NONE, &error);
	g_free(data2);
	if (error != nullptr)
		g_error("%s\n", error->message);

	load_string(file, "pidfile", &file_config.pidfile);
	load_string(file, "daemon_user", &file_config.daemon_user);
	load_string(file, "log", &file_config.log);
	load_string(file, "host", &file_config.host);
	load_unsigned(file, "port", &file_config.port);
	load_string(file, "proxy", &file_config.proxy);
	if (!load_unsigned(file, "journal_interval",
			   &file_config.journal_interval))
		load_unsigned(file, "cache_interval",
			      &file_config.journal_interval);
	load_integer(file, "verbose", &file_config.verbose);

	groups = g_key_file_get_groups(file, nullptr);
	while(groups[++i]) {
		ScrobblerConfig *scrobbler =
			load_scrobbler_config(file, groups[i]);
		if (scrobbler != nullptr) {
			file_config.scrobblers.emplace_front(std::move(*scrobbler));
			delete scrobbler;
		}
	}
	g_strfreev(groups);

	g_key_file_free(file);
}

int file_read_config()
{
	if (file_config.conf == nullptr)
		file_config.conf = get_default_config_path();

	/* parse config file options. */

	if (file_config.conf != nullptr)
		load_config_file(file_config.conf);

	if (!file_config.conf)
		g_error("cannot find configuration file\n");

	if (file_config.scrobblers.empty())
		g_error("No audioscrobbler host configured in %s",
			file_config.conf);

	if (!file_config.log)
		file_config.log = get_default_log_path();

	if (!file_config.proxy)
		file_config.proxy = getenv("http_proxy");
	if (file_config.verbose == -1)
		file_config.verbose = 1;

	return 1;
}

void file_cleanup()
{
	g_free(file_config.host);
	g_free(file_config.log);
	g_free(file_config.conf);

	file_config.scrobblers.clear();
}
