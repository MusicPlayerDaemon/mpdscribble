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
#include "util/OptionDef.hxx"
#include "util/OptionParser.hxx"
#include "util/RuntimeError.hxx"
#include "Config.hxx"
#include "config.h"

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

gcc_noreturn
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

enum Option {
	OPTION_VERSION,
	OPTION_NO_DAEMON,
	OPTION_VERBOSE,
	OPTION_CONF,
	OPTION_PIDFILE,
	OPTION_DAEMON_USER,
	OPTION_LOG,
	OPTION_HOST,
	OPTION_PORT,
	OPTION_PROXY,
	OPTION_HELP,
};

static constexpr OptionDef option_defs[] = {
	{"version", 'V', "print version number"},
	{"no-daemon", 'D', "don't daemonize"},
	{"verbose", 'v', true, "verbosity (0-2, default 2)"},
	{"conf", 0, true, "load configuration from this file"},
	{"pidfile", 0, true, "write the process id to this file"},
	{"daemon-user", 0, true, "run daemon as this user"},
	{"log", 0, true, "log file or 'syslog'"},
	{"host", 0, true, "MPD host name to connect to, or Unix domain socket path"},
	{"port", 0, true, "MPD port to connect to"},
	{"proxy", 0, true, "HTTP proxy URI"},
	{"help", 'h', "show help options"},
};

static void
PrintOption(const OptionDef &opt) noexcept
{
	if (opt.HasShortOption())
		printf("  -%c, --%-12s%s\n",
		       opt.GetShortOption(),
		       opt.GetLongOption(),
		       opt.GetDescription());
	else
		printf("  --%-16s%s\n",
		       opt.GetLongOption(),
		       opt.GetDescription());
}

gcc_noreturn
static void
help() noexcept
{
	printf("Usage:\n"
	       "  mpdscribble [OPTION...]\n"
	       "\n"
	       "%s\n"
	       "\n"
	       "Options:\n",
	       summary);

	for (const auto &i : option_defs)
		if(i.HasDescription() == true) // hide hidden options from help print
			PrintOption(i);

	exit(EXIT_SUCCESS);
}

void
parse_cmdline(Config &config, int argc, char **argv)
{
	// First pass: handle command line options
	OptionParser parser(option_defs, argc, argv);
	while (auto o = parser.Next()) {
		switch (Option(o.index)) {
		case OPTION_VERSION:
			version();

		case OPTION_NO_DAEMON:
			config.no_daemon = true;
			break;

		case OPTION_VERBOSE:
			config.verbose = atoi(o.value);
			break;

		case OPTION_CONF:
			config.conf = g_strdup(o.value);
			break;

		case OPTION_PIDFILE:
			config.pidfile = g_strdup(o.value);
			break;

		case OPTION_DAEMON_USER:
			config.daemon_user = g_strdup(o.value);
			break;

		case OPTION_LOG:
			config.log = g_strdup(o.value);
			break;

		case OPTION_HOST:
			config.host = g_strdup(o.value);
			break;

		case OPTION_PORT:
			config.port = atoi(o.value);
			break;

		case OPTION_PROXY:
			config.proxy = g_strdup(o.value);
			break;

		case OPTION_HELP:
			help();
		}
	}

	const auto remaining = parser.GetRemaining();
	if (!remaining.empty())
		throw FormatRuntimeError("Unknown option: %s", remaining.front());
}
