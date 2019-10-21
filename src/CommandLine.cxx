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

#include "CommandLine.hxx"
#include "Config.hxx"
#include "config.h"

#include <glib.h>

#include <stdio.h>
#include <stdlib.h>

static const char *blurb =
	PACKAGE " version " VERSION "\n"
	"another audioscrobbler plugin for music player daemon.\n"
	"Copyright 2005,2006 Kuno Woudt <kuno@frob.nl>.\n"
	"Copyright 2008-2019 Max Kellermann <max.kellermann+mpdscribble@gmail.com>\n" "\n";

static const char *summary =
	"A Music Player Daemon (MPD) client which submits information about\n"
	"tracks being played to Last.fm (formerly Audioscrobbler).";

static gboolean option_version;

static void
version() noexcept
{
	fputs(blurb, stdout);

	printf
	    ("mpdscribble comes with NO WARRANTY, to the extent permitted by law.\n"
	     "You may redistribute copies of mpdscribble under the terms of the\n"
	     "GNU General Public License; either version 2 of the License, or\n"
	     "(at your option) any later version.\n"
	     "For more information about these matters, see the file named COPYING.\n"
	     "\n");

	exit(EXIT_SUCCESS);
}

void
parse_cmdline(Config &config, int argc, char **argv) noexcept
{
	const GOptionEntry entries[] = {
		{ "version", 'V', 0, G_OPTION_ARG_NONE, &option_version,
		  "print version number", nullptr },
		{ "no-daemon", 'D', 0, G_OPTION_ARG_NONE, &config.no_daemon,
		  "don't daemonize", nullptr },
		{ "verbose", 'v', 0, G_OPTION_ARG_INT, &config.verbose,
		  "verbosity (0-2, default 2)", nullptr },
		{ "conf", 0, 0, G_OPTION_ARG_STRING, &config.conf,
		  "load configuration from this file", nullptr },
		{ "pidfile", 0, 0, G_OPTION_ARG_STRING, &config.pidfile,
		  "write the process id to this file", nullptr },
		{ "daemon-user", 0, 0, G_OPTION_ARG_STRING, &config.daemon_user,
		  "run daemon as this user", nullptr },
		{ "log", 0, 0, G_OPTION_ARG_STRING, &config.log,
		  "log file or 'syslog'", nullptr },
		{ "host", 0, 0, G_OPTION_ARG_STRING, &config.host,
		  "MPD host name to connect to, or Unix domain socket path", nullptr },
		{ "port", 0, 0, G_OPTION_ARG_INT, &config.port,
		  "MPD port to connect to", nullptr },
		{ "proxy", 0, 0, G_OPTION_ARG_STRING, &config.host,
		  "HTTP proxy URI", nullptr },
		{ .long_name = nullptr }
	};

	GError *error = nullptr;
	GOptionContext *context;
	bool ret;

	context = g_option_context_new(nullptr);
	g_option_context_add_main_entries(context, entries, nullptr);

	g_option_context_set_summary(context, summary);

	ret = g_option_context_parse(context, &argc, &argv, &error);
	g_option_context_free(context);

	if (!ret) {
		g_print ("option parsing failed: %s\n", error->message);
		exit (1);
	}

	if (option_version)
		version();
}
