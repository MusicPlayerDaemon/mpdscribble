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

#include "Scrobbler.hxx"
#include "Protocol.hxx"
#include "ScrobblerConfig.hxx"
#include "Record.hxx"
#include "Journal.hxx"
#include "HttpClient.hxx"
#include "Form.hxx"
#include "Log.hxx" /* for log_date() */

#include <glib.h>

#include <gcrypt.h>

#include <array>
#include <list>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>

/* don't submit more than this amount of songs in a batch. */
#define MAX_SUBMIT_COUNT 10

static const char OK[] = "OK";
static const char BADSESSION[] = "BADSESSION";
static const char FAILED[] = "FAILED";

enum scrobbler_state {
	/**
	 * mpdscribble has started, and doesn't have a session yet.
	 * Handshake to be submitted.
	 */
	SCROBBLER_STATE_NOTHING,

	/**
	 * Handshake is in progress, waiting for the server's
	 * response.
	 */
	SCROBBLER_STATE_HANDSHAKE,

	/**
	 * We have a session, and we're ready to submit.
	 */
	SCROBBLER_STATE_READY,

	/**
	 * Submission in progress, waiting for the server's response.
	 */
	SCROBBLER_STATE_SUBMITTING,
};

typedef enum {
	AS_SUBMIT_OK,
	AS_SUBMIT_FAILED,
	AS_SUBMIT_HANDSHAKE,
} as_submitting;

class Scrobbler {
	const ScrobblerConfig &config;

	FILE *file = nullptr;

	enum scrobbler_state state = SCROBBLER_STATE_NOTHING;

	unsigned interval = 1;

	guint handshake_source_id = 0;
	guint submit_source_id = 0;

	std::string session;
	std::string nowplay_url;
	std::string submit_url;

	Record now_playing;

	/**
	 * A queue of #record objects.
	 */
	std::list<Record> queue;

	/**
	 * How many songs are we trying to submit right now?  This
	 * many will be shifted from #queue if the submit succeeds.
	 */
	unsigned pending = 0;

public:
	Scrobbler(const ScrobblerConfig &_config) noexcept;
	~Scrobbler() noexcept;

	void Push(const Record &song);
	void ScheduleNowPlaying(const Record &song) noexcept;
	void SubmitNow() noexcept;

	void WriteJournal() const noexcept;

private:
	void ScheduleHandshake() noexcept;
	void Handshake() noexcept;
	bool ParseHandshakeResponse(const char *line) noexcept;

	void SendNowPlaying(const char *artist,
			    const char *track, const char *album,
			    const char *number,
			    const char *mbid, int length) noexcept;

	void ScheduleSubmit() noexcept;
	void Submit() noexcept;
	void IncreaseInterval() noexcept;

	static gboolean OnHandshakeTimer(gpointer data) noexcept;
	static gboolean OnSubmitTimer(gpointer data) noexcept;

public:
	static void OnHandshakeResponse(std::string &&body,
					void *data) noexcept;
	static void OnHandshakeError(GError *error, void *data) noexcept;
	static void OnSubmitResponse(std::string &&body,
					void *data) noexcept;
	static void OnSubmitError(GError *error, void *data) noexcept;
};

static std::forward_list<Scrobbler> scrobblers;

Scrobbler::Scrobbler(const ScrobblerConfig &_config) noexcept
	:config(_config)
{
	if (!config.journal.empty()) {
		guint queue_length;

		queue = journal_read(config.journal.c_str());

		queue_length = queue.size();
		g_message("loaded %i song%s from %s",
			  queue_length, queue_length == 1 ? "" : "s",
			  config.journal.c_str());
	}

	if (!config.file.empty()) {
		file = fopen(config.file.c_str(), "a");
		if (file == nullptr)
			g_error("Failed to open file '%s' of scrobbler '%s': %s\n",
				config.file.c_str(), config.name.c_str(),
				g_strerror(errno));
	} else
		ScheduleHandshake();
}

Scrobbler::~Scrobbler() noexcept
{
	if (handshake_source_id != 0)
		g_source_remove(handshake_source_id);
	if (submit_source_id != 0)
		g_source_remove(submit_source_id);

	if (file != nullptr)
		fclose(file);
}

void
Scrobbler::IncreaseInterval() noexcept
{
	if (interval < 60)
		interval = 60;
	else
		interval <<= 1;

	if (interval > 60 * 60 * 2)
		interval = 60 * 60 * 2;

	g_warning("[%s] waiting %u seconds before trying again",
		  config.name.c_str(), interval);
}

static as_submitting
scrobbler_parse_submit_response(const char *scrobbler_name,
				const char *line, size_t length)
{
	if (length == sizeof(OK) - 1 && memcmp(line, OK, length) == 0) {
		g_message("[%s] OK", scrobbler_name);

		return AS_SUBMIT_OK;
	} else if (length == sizeof(BADSESSION) - 1 &&
		   memcmp(line, BADSESSION, length) == 0) {
		g_warning("[%s] invalid session", scrobbler_name);

		return AS_SUBMIT_HANDSHAKE;
	} else if (length == sizeof(FAILED) - 1 &&
		   memcmp(line, FAILED, length) == 0) {
		if (length > strlen(FAILED))
			g_warning("[%s] submission rejected: %.*s",
				  scrobbler_name,
				  (int)(length - strlen(FAILED)),
				  line + strlen(FAILED));
		else
			g_warning("[%s] submission rejected", scrobbler_name);
	} else {
		g_warning("[%s] unknown response: %.*s",
			  scrobbler_name, (int)length, line);
	}

	return AS_SUBMIT_FAILED;
}

bool
Scrobbler::ParseHandshakeResponse(const char *line)
{
	static const char *BANNED = "BANNED";
	static const char *BADAUTH = "BADAUTH";
	static const char *BADTIME = "BADTIME";

	/* FIXME: some code duplication between this
	   and as_parse_submit_response. */
	if (!strncmp(line, OK, strlen(OK))) {
		g_message("[%s] handshake successful",
			  config.name.c_str());
		return true;
	} else if (!strncmp(line, BANNED, strlen(BANNED))) {
		g_warning("[%s] handshake failed, we're banned (%s)",
			  config.name.c_str(), line);
	} else if (!strncmp(line, BADAUTH, strlen(BADAUTH))) {
		g_warning("[%s] handshake failed, "
			  "username or password incorrect (%s)",
			  config.name.c_str(), line);
	} else if (!strncmp(line, BADTIME, strlen(BADTIME))) {
		g_warning("[%s] handshake failed, clock not synchronized (%s)",
			  config.name.c_str(), line);
	} else if (!strncmp(line, FAILED, strlen(FAILED))) {
		g_warning("[%s] handshake failed (%s)",
			  config.name.c_str(), line);
	} else {
		g_warning("[%s] error parsing handshake response (%s)",
			  config.name.c_str(), line);
	}

	return false;
}

static std::string
next_line(const char **input_r, const char *end)
{
	const char *input = *input_r;
	const char *newline = (const char *)memchr(input, '\n', end - input);

	if (newline == nullptr)
		return {};

	std::string line(input, newline);
	*input_r = newline + 1;

	return line;
}

void
Scrobbler::OnHandshakeResponse(std::string &&body, void *data) noexcept
{
	auto *scrobbler = (Scrobbler *)data;
	const char *response = body.data();
	const char *end = response + body.length();
	bool ret;

	assert(scrobbler != nullptr);
	assert(scrobbler->config.file.empty());
	assert(scrobbler->state == SCROBBLER_STATE_HANDSHAKE);

	scrobbler->state = SCROBBLER_STATE_NOTHING;

	auto line = next_line(&response, end);
	ret = scrobbler->ParseHandshakeResponse(line.c_str());
	if (!ret) {
		scrobbler->IncreaseInterval();
		scrobbler->ScheduleHandshake();
		return;
	}

	scrobbler->session = next_line(&response, end);
	g_debug("[%s] session: %s",
		scrobbler->config.name.c_str(), scrobbler->session.c_str());

	scrobbler->nowplay_url = next_line(&response, end);
	g_debug("[%s] now playing url: %s",
		scrobbler->config.name.c_str(),
		scrobbler->nowplay_url.c_str());

	scrobbler->submit_url = next_line(&response, end);
	g_debug("[%s] submit url: %s",
		scrobbler->config.name.c_str(),
		scrobbler->submit_url.c_str());

	if (scrobbler->nowplay_url.empty() || scrobbler->submit_url.empty()) {
		scrobbler->session.clear();
		scrobbler->nowplay_url.clear();
		scrobbler->submit_url.clear();

		scrobbler->IncreaseInterval();
		scrobbler->ScheduleHandshake();
		return;
	}

	scrobbler->state = SCROBBLER_STATE_READY;
	scrobbler->interval = 1;

	/* handshake was successful: see if we have songs to submit */
	scrobbler->Submit();
}

void
Scrobbler::OnHandshakeError(GError *error, void *data) noexcept
{
	auto *scrobbler = (Scrobbler *)data;

	assert(scrobbler != nullptr);
	assert(scrobbler->config.file.empty());
	assert(scrobbler->state == SCROBBLER_STATE_HANDSHAKE);

	scrobbler->state = SCROBBLER_STATE_NOTHING;

	g_warning("[%s] handshake error: %s",
		  scrobbler->config.name.c_str(), error->message);
	g_error_free(error);

	scrobbler->IncreaseInterval();
	scrobbler->ScheduleHandshake();
}

static constexpr HttpClientHandler scrobbler_handshake_handler = {
	.response = Scrobbler::OnHandshakeResponse,
	.error = Scrobbler::OnHandshakeError,
};

static void
scrobbler_queue_remove_oldest(std::list<Record> &queue, unsigned count)
{
	assert(count > 0);

	while (count--)
		queue.pop_front();
}

void
Scrobbler::OnSubmitResponse(std::string &&body, void *data) noexcept
{
	auto *scrobbler = (Scrobbler *)data;

	assert(scrobbler->config.file.empty());
	assert(scrobbler->state == SCROBBLER_STATE_SUBMITTING);
	scrobbler->state = SCROBBLER_STATE_READY;

	auto newline = body.find('\n');
	if (newline != body.npos)
		body.resize(newline);

	switch (scrobbler_parse_submit_response(scrobbler->config.name.c_str(),
						body.data(), body.length())) {
	case AS_SUBMIT_OK:
		scrobbler->interval = 1;

		/* submission was accepted, so clean up the cache. */
		if (scrobbler->pending > 0) {
			scrobbler_queue_remove_oldest(scrobbler->queue, scrobbler->pending);
			scrobbler->pending = 0;
		} else {
			assert(record_is_defined(&scrobbler->now_playing));

			scrobbler->now_playing = {};
		}


		/* submit the next chunk (if there is some left) */
		scrobbler->Submit();
		break;
	case AS_SUBMIT_FAILED:
		scrobbler->IncreaseInterval();
		scrobbler->ScheduleSubmit();
		break;
	case AS_SUBMIT_HANDSHAKE:
		scrobbler->state = SCROBBLER_STATE_NOTHING;
		scrobbler->ScheduleHandshake();
		break;
	}
}

void
Scrobbler::OnSubmitError(GError *error, void *data) noexcept
{
	auto *scrobbler = (Scrobbler *)data;

	assert(scrobbler->config.file.empty());
	assert(scrobbler->state == SCROBBLER_STATE_SUBMITTING);

	scrobbler->state = SCROBBLER_STATE_READY;

	g_warning("[%s] submit error: %s",
		  scrobbler->config.name.c_str(), error->message);
	g_error_free(error);

	scrobbler->IncreaseInterval();
	scrobbler->ScheduleSubmit();
}

static constexpr HttpClientHandler scrobbler_submit_handler = {
	.response = Scrobbler::OnSubmitResponse,
	.error = Scrobbler::OnSubmitError,
};

static constexpr size_t MD5_SIZE = 16;
static constexpr size_t MD5_HEX_SIZE = MD5_SIZE * 2;

/**
 * Calculate the MD5 checksum of the specified string.  The return
 * value is a newly allocated string containing the hexadecimal
 * checksum.
 */
static std::array<char, MD5_HEX_SIZE + 1>
md5_hex(const char *p, int len)
{
	if (len == -1)
		len = strlen(p);

	std::array<uint8_t, MD5_SIZE> binary;
	gcry_md_hash_buffer(GCRY_MD_MD5, &binary.front(), p, len);

	std::array<char, MD5_HEX_SIZE + 1> result;
	for (size_t i = 0; i < MD5_SIZE; ++i)
		snprintf(&result[i * 2], 3, "%02x", binary[i]);

	return result;
}

static auto
md5_hex(const std::string &s) noexcept
{
	return md5_hex(s.data(), s.size());
}

static auto
as_md5(const std::string &password, const std::string &timestamp)
{
	std::array<char, MD5_HEX_SIZE + 1> buffer;

	const char *password_md5 = password.c_str();
	if (password.length() != 32) {
		/* assume it's not hashed yet */
		buffer = md5_hex(password);
		password_md5 = &buffer.front();
	}

	return md5_hex(password_md5 + timestamp);
}

void
Scrobbler::Handshake()
{
	assert(config.file.empty());

	state = SCROBBLER_STATE_HANDSHAKE;

	const auto timestr = as_timestamp();
	const auto md5 = as_md5(config.password, timestr);

	/* construct the handshake url. */
	std::string url(config.url);
	first_var(url, "hs", "true");
	add_var(url, "p", "1.2");
	add_var(url, "c", AS_CLIENT_ID);
	add_var(url, "v", AS_CLIENT_VERSION);
	add_var(url, "u", config.username.c_str());
	add_var(url, "t", timestr.c_str());
	add_var(url, "a", &md5.front());

	//  notice ("handshake url:\n%s", url);

	http_client_request(url.c_str(), {},
			    scrobbler_handshake_handler, this);
}

gboolean
Scrobbler::OnHandshakeTimer(gpointer data) noexcept
{
	auto *scrobbler = (Scrobbler *)data;

	assert(scrobbler->config.file.empty());
	assert(scrobbler->state == SCROBBLER_STATE_NOTHING);

	scrobbler->handshake_source_id = 0;

	scrobbler->Handshake();
	return false;
}

void
Scrobbler::ScheduleHandshake() noexcept
{
	assert(config.file.empty());
	assert(state == SCROBBLER_STATE_NOTHING);
	assert(handshake_source_id == 0);

	handshake_source_id =
		g_timeout_add_seconds(interval,
				      OnHandshakeTimer, this);
}

void
Scrobbler::SendNowPlaying(const char *artist,
			  const char *track, const char *album,
			  const char *number,
			  const char *mbid, const int length) noexcept
{
	char len[16];

	assert(config.file.empty());
	assert(state == SCROBBLER_STATE_READY);
	assert(submit_source_id == 0);

	state = SCROBBLER_STATE_SUBMITTING;

	snprintf(len, sizeof(len), "%i", length);

	std::string post_data;
	add_var(post_data, "s", session);
	add_var(post_data, "a", artist);
	add_var(post_data, "t", track);
	add_var(post_data, "b", album);
	add_var(post_data, "l", len);
	add_var(post_data, "n", number);
	add_var(post_data, "m", mbid);

	g_message("[%s] sending 'now playing' notification",
		  config.name.c_str());

	http_client_request(nowplay_url.c_str(),
			    std::move(post_data),
			    scrobbler_submit_handler, this);
}

void
Scrobbler::ScheduleNowPlaying(const Record &song) noexcept
{
	if (file != nullptr)
		/* there's no "now playing" support for files */
		return;

	now_playing = song;

	if (state == SCROBBLER_STATE_READY && submit_source_id == 0)
		ScheduleSubmit();
}

void
as_now_playing(const char *artist, const char *track,
	       const char *album, const char *number,
	       const char *mbid, const int length)
{
	Record record;

	if (artist != nullptr)
		record.artist = artist;

	if (track != nullptr)
		record.track = track;

	if (album != nullptr)
		record.album = album;

	if (number != nullptr)
		record.number = number;

	if (mbid != nullptr)
		record.mbid = mbid;

	record.length = length;

	for (auto &i : scrobblers)
		i.ScheduleNowPlaying(record);
}

void
Scrobbler::Submit() noexcept
{
	//MAX_SUBMIT_COUNT
	unsigned count = 0;

	assert(config.file.empty());
	assert(state == SCROBBLER_STATE_READY);
	assert(submit_source_id == 0);

	if (queue.empty()) {
		/* the submission queue is empty.  See if a "now playing" song is
		   scheduled - these should be sent after song submissions */
		if (record_is_defined(&now_playing))
			SendNowPlaying(now_playing.artist.c_str(),
				       now_playing.track.c_str(),
				       now_playing.album.c_str(),
				       now_playing.number.c_str(),
				       now_playing.mbid.c_str(),
				       now_playing.length);

		return;
	}

	state = SCROBBLER_STATE_SUBMITTING;

	/* construct the handshake url. */
	std::string post_data;
	add_var(post_data, "s", session);

	for (const auto &i : queue) {
		if (count >= MAX_SUBMIT_COUNT)
			break;

		const auto *song = &i;
		char len[16];

		snprintf(len, sizeof(len), "%i", song->length);

		add_var_i(post_data, "a", count, song->artist);
		add_var_i(post_data, "t", count, song->track);
		add_var_i(post_data, "l", count, len);
		add_var_i(post_data, "i", count, song->time);
		add_var_i(post_data, "o", count, song->source);
		add_var_i(post_data, "r", count, "");
		add_var_i(post_data, "b", count, song->album);
		add_var_i(post_data, "n", count, song->number);
		add_var_i(post_data, "m", count, song->mbid);

		if (song->love)
			add_var_i(post_data, "r", count, "L");

		count++;
	}

	g_message("[%s] submitting %i song%s",
		  config.name.c_str(), count, count == 1 ? "" : "s");
	g_debug("[%s] post data: %s",
		config.name.c_str(), post_data.c_str());
	g_debug("[%s] url: %s",
		config.name.c_str(),
		submit_url.c_str());

	pending = count;
	http_client_request(submit_url.c_str(),
			    std::move(post_data),
			    scrobbler_submit_handler, this);
}

void
Scrobbler::Push(const Record &song) noexcept
{
	if (file != nullptr) {
		fprintf(file, "%s %s - %s\n",
			log_date(),
			song.artist.c_str(), song.track.c_str());
		fflush(file);
		return;
	}

	queue.emplace_back(song);

	if (state == SCROBBLER_STATE_READY && submit_source_id == 0)
		ScheduleSubmit();
}

void
as_songchange(const char *file, const char *artist, const char *track,
	      const char *album, const char *number,
	      const char *mbid, const int length,
	      bool love,
	      const char *time2)
{
	Record record;

	/* from the 1.2 protocol draft:

	   You may still submit if there is no album title (variable b)
	   You may still submit if there is no musicbrainz id available (variable m)

	   everything else is mandatory.
	 */
	if (!(artist && strlen(artist))) {
		g_warning("empty artist, not submitting; "
			  "please check the tags on %s\n", file);
		return;
	}

	if (!(track && strlen(track))) {
		g_warning("empty title, not submitting; "
			  "please check the tags on %s", file);
		return;
	}

	record.artist = artist;
	record.track = track;

	if (album != nullptr)
		record.album = album;

	if (number != nullptr)
		record.number = number;

	if (mbid != nullptr)
		record.mbid = mbid;

	record.length = length;
	record.time = time2 ? time2 : as_timestamp();
	record.love = love;
	record.source = strstr(file, "://") == nullptr ? "P" : "R";

	g_message("%s, songchange: %s - %s (%i)\n",
		  record.time.c_str(), record.artist.c_str(),
		  record.track.c_str(), record.length);

	for (auto &i : scrobblers)
		i.Push(record);
}

static void
AddScrobbler(const ScrobblerConfig &config)
{
	scrobblers.emplace_front(config);
}

void
as_init(const std::forward_list<ScrobblerConfig> &scrobbler_configs)
{
	g_message("starting mpdscribble (" AS_CLIENT_ID " " AS_CLIENT_VERSION ")\n");

	for (const auto &i : scrobbler_configs)
		AddScrobbler(i);
}

gboolean
Scrobbler::OnSubmitTimer(gpointer data) noexcept
{
	auto *scrobbler = (Scrobbler *)data;

	assert(scrobbler->state == SCROBBLER_STATE_READY);

	scrobbler->submit_source_id = 0;

	scrobbler->Submit();
	return false;
}

void
Scrobbler::ScheduleSubmit() noexcept
{
	assert(submit_source_id == 0);
	assert(!queue.empty() || record_is_defined(&now_playing));

	submit_source_id = g_timeout_add_seconds(interval,
						 OnSubmitTimer, this);
}

void
Scrobbler::WriteJournal() const noexcept
{
	if (file != nullptr || config.journal.empty())
		return;

	if (journal_write(config.journal.c_str(), queue)) {
		unsigned queue_length = queue.size();
		g_message("[%s] saved %i song%s to %s",
			  config.name.c_str(),
			  queue_length, queue_length == 1 ? "" : "s",
			  config.journal.c_str());
	}
}

void as_save_cache()
{
	for (auto &i : scrobblers)
		i.WriteJournal();
}

void
Scrobbler::SubmitNow() noexcept
{
	interval = 1;

	if (handshake_source_id != 0) {
		g_source_remove(handshake_source_id);
		handshake_source_id = 0;
		ScheduleHandshake();
	}

	if (submit_source_id != 0) {
		g_source_remove(submit_source_id);
		submit_source_id = 0;
		ScheduleSubmit();
	}
}

void as_submit_now()
{
	for (auto &i : scrobblers)
		i.SubmitNow();
}

void as_cleanup()
{
	scrobblers.clear();
}
