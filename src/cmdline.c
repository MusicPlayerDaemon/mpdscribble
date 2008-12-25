/*
 * (c) 2004-2008 The Music Player Daemon Project
 * http://www.musicpd.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "cmdline.h"
#include "file.h"
#include "as.h"
#include "config.h"

#include <glib.h>

#include <stdlib.h>
#include <string.h>

static const char *blurb =
	"mpdscribble (" AS_CLIENT_ID " " AS_CLIENT_VERSION ").\n"
	"another audioscrobbler plugin for music player daemon.\n"
	"Copyright 2005,2006 Kuno Woudt <kuno@frob.nl>.\n"
	"Copyright 2008 Max Kellermann <max@duempel.org>\n" "\n";

static void version(void)
{
	printf(blurb);

	printf
	    ("mpdscribble comes with NO WARRANTY, to the extent permitted by law.\n"
	     "You may redistribute copies of mpdscribble under the terms of the\n"
	     "GNU General Public License; either version 2 of the License, or\n"
	     "(at your option) any later version.\n"
	     "For more information about these matters, see the file named COPYING.\n"
	     "\n");

	exit(1);
}

static void help(void)
{
	printf(blurb);

	printf("Usage: mpdscribble [OPTIONS]\n"
	       "\n"
	       "  --help                      \tthis message\n"
	       "  --version                   \tthat message\n"
	       "  --log            <filename> \tlog file\n"
	       "  --cache          <filename> \tcache file\n"
	       "  --conf           <filename> \tconfiguration file\n"
	       "  --host           <host>     \tmpd host\n"
	       "  --port           <port>     \tmpd port\n"
	       "  --proxy          <proxy>    \tHTTP proxy URI\n"
	       "  --sleep          <interval> \tupdate interval (default 1 second)\n"
	       "  --cache-interval <interval> \twrite cache file every i seconds\n"
	       "                              \t(default 600 seconds)\n"
	       "  --verbose <0-2>             \tverbosity (default 2)\n"
	       "\n" "Report bugs to <kuno@frob.nl>.\n");

	exit(1);
}

static int file_atoi(const char *s)
{
	if (!s)
		return 0;

	return atoi(s);
}

static void replace(char **dst, char *src)
{
	if (*dst)
		free(*dst);
	*dst = src;
}

void
parse_cmdline(int argc, char **argv)
{
	for (int i = 0; i < argc; i++) {
		if (!strcmp("--help", argv[i]))
			help();
		else if (!strcmp("--version", argv[i]))
			version();
		else if (!strcmp("--host", argv[i]))
			replace(&file_config.host, g_strdup(argv[++i]));
		else if (!strcmp("--log", argv[i]))
			replace(&file_config.log, g_strdup(argv[++i]));
		else if (!strcmp("--cache", argv[i]))
			replace(&file_config.cache, g_strdup(argv[++i]));
		else if (!strcmp("--port", argv[i]))
			file_config.port = file_atoi(argv[++i]);
		else if (!strcmp("--sleep", argv[i]))
			file_config.sleep = file_atoi(argv[++i]);
		else if (!strcmp("--cache-interval", argv[i]))
			file_config.cache_interval = file_atoi(argv[++i]);
		else if (!strcmp("--verbose", argv[i]))
			file_config.verbose = file_atoi(argv[++i]);
		else if (!strcmp("--proxy", argv[i]))
			file_config.proxy = g_strdup(argv[++i]);
	}
}
