// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "Journal.hxx"
#include "Record.hxx"
#include "lib/fmt/ExceptionFormatter.hxx"
#include "io/BufferedReader.hxx"
#include "io/FileReader.hxx"
#include "system/Error.hxx"
#include "util/StringStrip.hxx"
#include "Log.hxx"

#include <fmt/core.h>

#include <cassert>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int journal_file_empty;

static void
journal_write_string(FILE *file, char field, const char *value)
{
	if (value != nullptr)
		fmt::print(file, "{} = {}\n", field, value);
}

static void
journal_write_string(FILE *file, char field, const std::string &value)
{
	if (!value.empty())
		fmt::print(file, "{} = {}\n", field, value);
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

	fmt::print(file, "l = {}\no = {}\n\n",
		   length_s.count(),
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
		FmtError("Failed to save {:?}: {}", path, strerror(errno));
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
try {
	FileReader file{path};
	BufferedReader reader{file};

	Record record;

	journal_file_empty = true;

	std::list<Record> queue;
	while (char *line = reader.ReadLine()) {
		char *key, *value;

		key = StripLeft(line);
		if (*key == 0 || *key == '#')
			continue;

		value = strchr(key, '=');
		if (value == nullptr || value == key)
			continue;

		*value++ = 0;

		StripRight(key);
		value = Strip(value);

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

	journal_commit_record(queue, std::move(record));

	return queue;
} catch (const std::system_error &e) {
	if (!IsFileNotFound(e))
		/* ENOENT is ignored silently, because the user might
		   be starting mpdscribble for the first time */
		FmtWarning("Failed to load {:?}: {}", path, std::current_exception());

	return {};
}
