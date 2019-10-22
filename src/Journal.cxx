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

#include "Journal.hxx"
#include "Record.hxx"

#include <glib.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int journal_file_empty;

static void
journal_write_string(FILE *file, char field, const char *value)
{
	if (value != nullptr)
		fprintf(file, "%c = %s\n", field, value);
}

static void
journal_write_string(FILE *file, char field, const std::string &value)
{
	if (!value.empty())
		fprintf(file, "%c = %s\n", field, value.c_str());
}

static void
journal_write_record(FILE *file, const Record *record)
{
	assert(record->source != nullptr);

	journal_write_string(file, 'a', record->artist);
	journal_write_string(file, 't', record->track);
	journal_write_string(file, 'b', record->album);
	journal_write_string(file, 'n', record->number);
	journal_write_string(file, 'm', record->mbid);
	if (record->love)
		journal_write_string(file, 'r', "L");
	journal_write_string(file, 'i', record->time);

	const auto length_s =
		std::chrono::duration_cast<std::chrono::seconds>(record->length);

	fprintf(file,
		"l = %i\no = %s\n\n",
		(int)length_s.count(),
		record->source);
}

bool
journal_write(const char *path, const std::list<Record> &queue)
{
	FILE *handle;

	if (queue.empty() && journal_file_empty)
		return false;

	handle = fopen(path, "wb");
	if (!handle) {
		g_warning("Failed to save %s: %s\n", path, g_strerror(errno));
		return false;
	}

	for (const auto &i : queue)
		journal_write_record(handle, &i);

	fclose(handle);

	return true;
}

static void
journal_commit_record(std::list<Record> &queue, Record &&record)
{
	if (!record.artist.empty() && !record.track.empty()) {
		/* append record to the queue */

		queue.emplace_back(std::move(record));

		journal_file_empty = false;
	}
}

std::list<Record>
journal_read(const char *path)
{
	FILE *file;
	char line[1024];
	Record record;

	journal_file_empty = true;

	file = fopen(path, "r");
	if (file == nullptr) {
		if (errno != ENOENT)
			/* ENOENT is ignored silently, because the
			   user might be starting mpdscribble for the
			   first time */
			g_warning("Failed to load %s: %s",
				  path, g_strerror(errno));
		return {};
	}

	std::list<Record> queue;
	while (fgets(line, sizeof(line), file) != nullptr) {
		char *key, *value;

		key = g_strchug(line);
		if (*key == 0 || *key == '#')
			continue;

		value = strchr(key, '=');
		if (value == nullptr || value == key)
			continue;

		*value++ = 0;

		key = g_strchomp(key);
		value = g_strstrip(value);

		if (!strcmp("a", key)) {
			journal_commit_record(queue, std::move(record));
			record = {};
			record.artist = value;
		} else if (!strcmp("t", key))
			record.track = value;
		else if (!strcmp("b", key))
			record.album = value;
		else if (!strcmp("n", key))
			record.number = value;
		else if (!strcmp("m", key))
			record.mbid = value;
		else if (!strcmp("i", key))
			record.time = value;
		else if (!strcmp("l", key))
			record.length = std::chrono::seconds(atoi(value));
		else if (strcmp("o", key) == 0 && value[0] == 'R')
			record.source = "R";
		else if (strcmp("r", key) == 0 && value[0] == 'L')
			record.love = true;
	}

	fclose(file);

	journal_commit_record(queue, std::move(record));

	return queue;
}
