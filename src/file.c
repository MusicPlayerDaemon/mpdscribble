/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2009 The Music Player Daemon Project
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

#include "file.h"
#include "cmdline.h"
#include "config.h"

#include <glib.h>

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*
  default locations for files.

  FILE_ETC_* are paths for a system-wide install.
  FILE_USR_* will be used instead if FILE_USR_CONF exists.
*/

#ifndef G_OS_WIN32

#define FILE_CACHE "/var/cache/mpdscribble/mpdscribble.cache"
#define FILE_LOG "/var/log/mpdscribble/mpdscribble.log"
#define FILE_HOME_CONF "~/.mpdscribble/mpdscribble.conf"
#define FILE_HOME_CACHE "~/.mpdscribble/mpdscribble.cache"
#define FILE_HOME_LOG "~/.mpdscribble/mpdscribble.log"

#endif

#define FILE_DEFAULT_PORT 6600
#define FILE_DEFAULT_HOST "localhost"

#define AS_HOST "http://post.audioscrobbler.com/"

struct config file_config = {
	.port = -1,
	.sleep = -1,
	.cache_interval = -1,
	.verbose = -1,
	.loc = file_unknown,
};

static int file_atoi(const char *s)
{
	if (!s)
		return 0;

	return atoi(s);
}

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

	return g_strconcat(home, path + 1, NULL);
}

static char *
get_default_config_path(void)
{
#ifndef G_OS_WIN32
	char *file = file_expand_tilde(FILE_HOME_CONF);
	if (file_exists(file)) {
		file_config.loc = file_home;
		return file;
	} else {
		free(file);

		if (!file_exists(FILE_CONF))
			return NULL;

		file_config.loc = file_etc;
		return g_strdup(FILE_CONF);
	}
#else
	return g_strdup("mpdscribble.conf");
#endif
}

static char *
get_default_log_path(void)
{
#ifndef G_OS_WIN32
	switch (file_config.loc) {
	case file_home:
		return file_expand_tilde(FILE_HOME_LOG);

	case file_etc:
		return g_strdup(FILE_LOG);

	case file_unknown:
		g_error("please specify where to put the log file\n");
	}

	assert(false);
	return NULL;
#else
	return g_strdup("-");
#endif
}

static char *
get_default_cache_path(void)
{
#ifndef G_OS_WIN32
	switch (file_config.loc) {
	case file_home:
		return file_expand_tilde(FILE_HOME_CACHE);

	case file_etc:
		return g_strdup(FILE_CACHE);

	case file_unknown:
		g_error("please specify where to put the cache file\n");
	}

	assert(false);
	return NULL;
#else
	return g_strdup("mpdscribble.cache");
#endif
}

static void load_string(GKeyFile * file, const char *name, char **value_r)
{
	GError *error = NULL;
	char *value;

	if (*value_r != NULL)
		/* already set by command line */
		return;

	value = g_key_file_get_string(file, PACKAGE, name, &error);
	if (error != NULL) {
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
			g_error("%s\n", error->message);
		g_error_free(error);
		return;
	}

	g_free(*value_r);
	*value_r = value;
}

static void load_integer(GKeyFile * file, const char *name, int *value_r)
{
	GError *error = NULL;
	int value;

	if (*value_r != -1)
		/* already set by command line */
		return;

	value = g_key_file_get_integer(file, PACKAGE, name, &error);
	if (error != NULL) {
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
			g_error("%s\n", error->message);
		g_error_free(error);
		return;
	}

	*value_r = value;
}

static void
load_config_file(const char *path)
{
	bool ret;
	char *data1, *data2;
	char **groups;
	struct config_as_host *current_host = &file_config.as_hosts;
	int i = -1;
	GKeyFile *file;
	GError *error = NULL;

	/* initialize host, in case there are none */
	current_host->url = NULL;
	current_host->username = NULL;
	current_host->password = NULL;

	ret = g_file_get_contents(path, &data1, NULL, &error);
	if (!ret)
		g_error("%s\n", error->message);

	/* GKeyFile does not allow values without a section.  Apply a
	   hack here: prepend the string "[mpdscribble]" to have all
	   values in the "mpdscribble" section */

	data2 = g_strconcat("[" PACKAGE "]\n", data1, NULL);
	g_free(data1);

	file = g_key_file_new();
	g_key_file_load_from_data(file, data2, strlen(data2),
				  G_KEY_FILE_NONE, &error);
	g_free(data2);
	if (error != NULL)
		g_error("%s\n", error->message);

	load_string(file, "pidfile", &file_config.pidfile);
	load_string(file, "daemon_user", &file_config.daemon_user);
	load_string(file, "log", &file_config.log);
	load_string(file, "cache", &file_config.cache);
	load_string(file, "host", &file_config.host);
	load_integer(file, "port", &file_config.port);
	load_string(file, "proxy", &file_config.proxy);
	load_integer(file, "sleep", &file_config.sleep);
	load_integer(file, "cache_interval",
		     &file_config.cache_interval);
	load_integer(file, "verbose", &file_config.verbose);

	groups = g_key_file_get_groups(file, NULL);
	while(groups[++i]) {
		/* Use default host for mpdscribble group, for backward compatability */
		if(strcmp(groups[i], "mpdscribble") == 0) {
			current_host->url = strdup(AS_HOST);
		} else {
			current_host->url = strdup(groups[i]);
		}

		current_host->username = g_key_file_get_string(file, groups[i], "username", &error);
		if (error != NULL)
			g_error("%s\n", error->message);
		if(current_host->username)
			current_host->username = strdup(current_host->username);

		current_host->password = g_key_file_get_string(file, groups[i], "password", &error);
		if (error != NULL)
			g_error("%s\n", error->message);
		if(current_host->password)
			current_host->password = strdup(current_host->password);

		/* Only allocate a next element if there are more groups */
		if(groups[i+1]) {
			current_host->next = malloc(sizeof *current_host->next);
			current_host = current_host->next;
		} else {
			current_host->next = NULL;
		}
	}
	g_strfreev(groups);

	g_key_file_free(file);
}

int file_read_config(int argc, char **argv)
{
	char *mpd_host = getenv("MPD_HOST");
	char *mpd_port = getenv("MPD_PORT");
	char *http_proxy = getenv("http_proxy");

	/* parse command-line options. */

	parse_cmdline(argc, argv);

	if (file_config.conf == NULL)
		file_config.conf = get_default_config_path();

	/* parse config file options. */

	if (file_config.conf != NULL)
		load_config_file(file_config.conf);

	if (!file_config.conf)
		g_error("cannot find configuration file\n");

	if (file_config.as_hosts.username == NULL || *file_config.as_hosts.username == 0)
		g_error("no audioscrobbler username specified in %s\n",
			file_config.conf);

	if (file_config.as_hosts.password == NULL || *file_config.as_hosts.password == 0)
		g_error("no audioscrobbler password specified in %s\n",
		      file_config.conf);

	if (!file_config.host)
		file_config.host = g_strdup(mpd_host);
	if (!file_config.host)
		file_config.host = g_strdup(FILE_DEFAULT_HOST);
	if (!file_config.log)
		file_config.log = get_default_log_path();
	if (!file_config.cache)
		file_config.cache = get_default_cache_path();
	if (file_config.port == -1 && mpd_port)
		file_config.port = file_atoi(mpd_port);
	if (file_config.port == -1)
		file_config.port = FILE_DEFAULT_PORT;
	if (!file_config.proxy)
		file_config.proxy = http_proxy;
	if (file_config.sleep <= 0)
		file_config.sleep = 1;
	if (file_config.cache_interval == -1)
		file_config.cache_interval = 600;
	if (file_config.verbose == -1)
		file_config.verbose = 1;

	return 1;
}

static void
free_as_host(struct config_as_host *current_host)
{
	if(current_host->url)
		free(current_host->url);
	if(current_host->username)
		free(current_host->username);
	if(current_host->password)
		free(current_host->password);
	if(current_host->g_session)
		g_free(current_host->g_session);
	if(current_host->g_nowplay_url)
		g_free(current_host->g_nowplay_url);
	if(current_host->g_submit_url)
		g_free(current_host->g_submit_url);
	if(current_host->next) {
		free_as_host(current_host->next);
		free(current_host->next);
	}
}

void file_cleanup(void)
{
	g_free(file_config.host);
	g_free(file_config.log);
	g_free(file_config.conf);
	g_free(file_config.cache);

	free_as_host(&file_config.as_hosts);
}
