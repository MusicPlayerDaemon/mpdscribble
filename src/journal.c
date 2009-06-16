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

#include "journal.h"
#include "file.h"
#include "record.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int journal_file_empty;

static void
journal_write_song(gpointer data, gpointer user_data)
{
	struct song *song = data;
	FILE *file = user_data;

	fprintf(file,
		"a = %s\nt = %s\nb = %s\nm = %s\n"
		"i = %s\nl = %i\no = %s\n\n", song->artist,
		song->track, song->album, song->mbid, song->time,
		song->length, song->source);
}

bool journal_write(GQueue *queue)
{
	FILE *handle;

	if (g_queue_is_empty(queue) && journal_file_empty)
		return false;

	handle = fopen(file_config.cache, "wb");
	if (!handle) {
		g_warning("failed to open %s: %s\n",
			  file_config.cache, strerror(errno));
		return false;
	}

	g_queue_foreach(queue, journal_write_song, handle);

	fclose(handle);

	return true;
}

static void
journal_commit_song(GQueue *queue, struct song *song)
{
	if (song->artist != NULL && song->track != NULL) {
		/* append song to the queue; reuse allocated strings */

		g_queue_push_tail(queue, g_memdup(song, sizeof(*song)));

		journal_file_empty = false;
	} else {
		/* free and clear the song, it was not used */

		g_free(song->artist);
		g_free(song->track);
		g_free(song->album);
		g_free(song->mbid);
		g_free(song->time);
	}

	clear_song(song);
}

/* g_time_val_from_iso8601() was introduced in GLib 2.12 */
#if GLIB_CHECK_VERSION(2,12,0)

/**
 * Imports an old (protocol v1.2) timestamp, format "%Y-%m-%d
 * %H:%M:%S".
 */
static char *
import_old_timestamp(const char *p)
{
	char *q;
	bool success;
	GTimeVal time_val;

	if (strlen(p) <= 10 || p[10] != ' ')
		return NULL;

	g_debug("importing time stamp '%s'", p);

	/* replace a space with 'T', as expected by
	   g_time_val_from_iso8601() */
	q = g_strdup(p);
	q[10] = 'T';

	success = g_time_val_from_iso8601(q, &time_val);
	g_free(q);
	if (!success) {
		g_debug("import of '%s' failed", p);
		return NULL;
	}

	g_debug("'%s' -> %ld", p, time_val.tv_sec);
	return g_strdup_printf("%ld", time_val.tv_sec);
}

#endif

/**
 * Parses the time stamp.  If needed, converts the time stamp, and
 * returns an allocated string.
 */
static char *
parse_timestamp(const char *p)
{
#if GLIB_CHECK_VERSION(2,12,0)
	char *ret = import_old_timestamp(p);
	if (ret != NULL)
		return ret;
#endif

	return g_strdup(p);
}

void journal_read(GQueue *queue)
{
	FILE *file;
	char line[1024];
	struct song sng;

	journal_file_empty = true;

	file = fopen(file_config.cache, "r");
	if (file == NULL) {
		g_warning("Failed to open %s: %s\n",
			  file_config.cache, strerror(errno));
		return;
	}

	clear_song(&sng);

	while (fgets(line, sizeof(line), file) != NULL) {
		char *key, *value;

		key = g_strchug(line);
		if (*key == 0 || *key == '#')
			continue;

		value = strchr(key, '=');
		if (value == NULL || value == key)
			continue;

		*value++ = 0;

		key = g_strchomp(key);
		value = g_strstrip(value);

		if (!strcmp("a", key)) {
			journal_commit_song(queue, &sng);
			sng.artist = g_strdup(value);
		} else if (!strcmp("t", key))
			sng.track = g_strdup(value);
		else if (!strcmp("b", key))
			sng.album = g_strdup(value);
		else if (!strcmp("m", key))
			sng.mbid = g_strdup(value);
		else if (!strcmp("i", key))
			sng.time = parse_timestamp(value);
		else if (!strcmp("l", key))
			sng.length = atoi(value);
		else if (strcmp("o", key) == 0 && value[0] == 'R')
			sng.source = "R";
	}

	fclose(file);

	journal_commit_song(queue, &sng);
}
