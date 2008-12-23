/*
    This file is part of mpdscribble,
    another audioscrobbler plugin for music player daemon.
    Copyright © 2005,2006 Kuno Woudt <kuno@frob.nl>

    mpdscribble is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    mpdscribble is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with mpdscribble; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "journal.h"
#include "file.h"
#include "as.h"
#include "misc.h"

#include <glib.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int file_saved_count = 0;

bool journal_write(struct song *sng)
{
	struct song *tmp = sng;
	int count = 0;
	FILE *handle;

	if (!tmp && file_saved_count == 0)
		return false;

	handle = fopen(file_config.cache, "wb");
	if (!handle) {
		warning_errno("error opening %s", file_config.cache);
		return false;
	}

	while (tmp) {
		fprintf(handle,
			"# song %i in queue\na = %s\nt = %s\nb = %s\nm = %s\n"
			"i = %s\nl = %i\no = %s\n\n", ++count, tmp->artist,
			tmp->track, tmp->album, tmp->mbid, tmp->time,
			tmp->length, tmp->source);

		tmp = tmp->next;
	}

	fclose(handle);

	file_saved_count = count;

	return true;
}

static void clear_song(struct song *s)
{
	s->artist = NULL;
	s->track = NULL;
	s->album = NULL;
	s->mbid = NULL;
	s->time = NULL;
	s->length = 0;
	s->source = "P";
}

void journal_read(void)
{
	FILE *file;
	char line[1024];
	int count = 0;
	struct song sng;

	file = fopen(file_config.cache, "r");
	if (file == NULL) {
		warning("Failed to open %s: %s",
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

		if (!strcmp("a", key))
			sng.artist = g_strdup(value);
		else if (!strcmp("t", key))
			sng.track = g_strdup(value);
		else if (!strcmp("b", key))
			sng.album = g_strdup(value);
		else if (!strcmp("m", key))
			sng.mbid = g_strdup(value);
		else if (!strcmp("i", key))
			sng.time = g_strdup(value);
		else if (!strcmp("l", key)) {
			sng.length = atoi(value);

			as_songchange("", sng.artist, sng.track,
				      sng.album, sng.mbid, sng.length,
				      sng.time);

			count++;

			if (sng.artist) {
				free(sng.artist);
				sng.artist = NULL;
			}
			if (sng.track) {
				free(sng.track);
				sng.track = NULL;
			}
			if (sng.album) {
				free(sng.album);
				sng.album = NULL;
			}
			if (sng.mbid) {
				free(sng.mbid);
				sng.mbid = NULL;
			}
			if (sng.time) {
				free(sng.time);
				sng.time = NULL;
			}

			clear_song(&sng);
		} else if (strcmp("o", key) == 0 && value[0] == 'R')
			sng.source = "R";
	}

	fclose(file);

	file_saved_count = count;
}
