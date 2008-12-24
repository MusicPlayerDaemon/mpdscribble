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

#include "log.h"

#include <glib.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static FILE *log_file;
static GLogLevelFlags log_threshold = G_LOG_LEVEL_MESSAGE;

static const char *log_date(void)
{
	static char buf[20];
	time_t t;
	struct tm *tmp;

	t = time(NULL);
	tmp = gmtime(&t);
	if (tmp == NULL) {
		buf[0] = 0;
		return buf;
	}

	if (!strftime(buf, sizeof(buf), "%Y/%m/%d %H:%M:%S", tmp)) {
		buf[0] = 0;
		return buf;
	}
	return buf;
}

static void
mpdscribble_log_func(const gchar *log_domain, GLogLevelFlags log_level,
		     const gchar *message, G_GNUC_UNUSED gpointer user_data)
{
	if (log_level > log_threshold)
		return;

	if (log_domain == NULL)
		log_domain = "";

	fprintf(log_file, "%s %s%s%s",
		log_date(),
		log_domain, *log_domain == 0 ? "" : ": ",
		message);
}

void
log_init(const char *path, int verbose)
{
	assert(path != NULL);
	assert(verbose >= 0);
	assert(log_file == NULL);

	log_file = fopen(path, "ab");
	if (log_file == NULL)
		g_error("cannot open %s: %s\n", path, strerror(errno));

	setvbuf(log_file, NULL, _IONBF, 0);

	if (verbose == 0)
		log_threshold = G_LOG_LEVEL_ERROR;
	else if (verbose == 1)
		log_threshold = G_LOG_LEVEL_WARNING;
	else
		log_threshold = G_LOG_LEVEL_DEBUG;

	g_log_set_default_handler(mpdscribble_log_func, NULL);
}

void
log_deinit(void)
{
	assert(log_file != NULL);

	fclose(log_file);
}
