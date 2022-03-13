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
#include "Journal.hxx"
#include "lib/curl/Request.hxx"
#include "Form.hxx"
#include "Log.hxx" /* for log_date() */
#include "system/Error.hxx"
#include "util/Exception.hxx"

#include <gcrypt.h>

#include <array>
#include <cassert>

#include <errno.h>
#include <string.h>

/* don't submit more than this amount of songs in a batch. */
#define MAX_SUBMIT_COUNT 10

/* maximum exponential backoff delay */
#define MAX_INTERVAL (60 << 3)

namespace ResponseStrings {
static constexpr char OK[] = "OK";
static constexpr char BADSESSION[] = "BADSESSION";
static constexpr char FAILED[] = "FAILED";
static constexpr char BANNED[] = "BANNED";
static constexpr char BADAUTH[] = "BADAUTH";
static constexpr char BADTIME[] = "BADTIME";
}

Scrobbler::Scrobbler(const ScrobblerConfig &_config,
		     EventLoop &event_loop,
		     CurlGlobal &_curl_global)
	:config(_config), curl_global(_curl_global),
	 handshake_timer(event_loop, BIND_THIS_METHOD(OnHandshakeTimer)),
	 submit_timer(event_loop, BIND_THIS_METHOD(OnSubmitTimer))
{
	if (!config.journal.empty()) {
		queue = journal_read(config.journal.c_str());

		const unsigned queue_length = queue.size();
		FormatInfo("loaded %u song%s from %s",
			   queue_length, queue_length == 1 ? "" : "s",
			   config.journal.c_str());
	}

	if (!config.file.empty()) {
		file = fopen(config.file.c_str(), "a");
		if (file == nullptr)
			throw FormatErrno("Failed to open file '%s' of scrobbler '%s'",
					  config.file.c_str(),
					  config.name.c_str());
	} else
		ScheduleHandshake();
}

Scrobbler::~Scrobbler() noexcept
{
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

	if (interval > MAX_INTERVAL)
		interval = MAX_INTERVAL;

	FormatWarning("[%s] waiting %u seconds before trying again",
		      config.name.c_str(), interval);
}

enum class SubmitResponseType {
	OK,
	FAILED,
	HANDSHAKE,
};

static SubmitResponseType
scrobbler_parse_submit_response(const char *scrobbler_name,
				const char *line, size_t length)
{
	using namespace ResponseStrings;

	if (length == sizeof(OK) - 1 && memcmp(line, OK, length) == 0) {
		FormatInfo("[%s] OK", scrobbler_name);

		return SubmitResponseType::OK;
	} else if (length == sizeof(BADSESSION) - 1 &&
		   memcmp(line, BADSESSION, length) == 0) {
		FormatWarning("[%s] invalid session", scrobbler_name);

		return SubmitResponseType::HANDSHAKE;
	} else if (length == sizeof(FAILED) - 1 &&
		   memcmp(line, FAILED, length) == 0) {
		if (length > strlen(FAILED))
			FormatError("[%s] submission rejected: %.*s",
				    scrobbler_name,
				    (int)(length - strlen(FAILED)),
				    line + strlen(FAILED));
		else
			FormatError("[%s] submission rejected",
				    scrobbler_name);
	} else {
		FormatError("[%s] unknown response: %.*s",
			    scrobbler_name, (int)length, line);
	}

	return SubmitResponseType::FAILED;
}

bool
Scrobbler::ParseHandshakeResponse(const char *line) noexcept
{
	using namespace ResponseStrings;

	/* FIXME: some code duplication between this
	   and as_parse_submit_response. */
	if (!strncmp(line, OK, strlen(OK))) {
		FormatInfo("[%s] handshake successful",
			   config.name.c_str());
		return true;
	} else if (!strncmp(line, BANNED, strlen(BANNED))) {
		FormatError("[%s] handshake failed, we're banned (%s)",
			    config.name.c_str(), line);
	} else if (!strncmp(line, BADAUTH, strlen(BADAUTH))) {
		FormatError("[%s] handshake failed, "
			    "username or password incorrect (%s)",
			    config.name.c_str(), line);
	} else if (!strncmp(line, BADTIME, strlen(BADTIME))) {
		FormatError("[%s] handshake failed, clock not synchronized (%s)",
			    config.name.c_str(), line);
	} else if (!strncmp(line, FAILED, strlen(FAILED))) {
		FormatError("[%s] handshake failed (%s)",
			    config.name.c_str(), line);
	} else {
		FormatError("[%s] error parsing handshake response (%s)",
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

inline void
Scrobbler::OnHandshakeResponse(std::string body) noexcept
{
	const char *response = body.data();
	const char *end = response + body.length();
	bool ret;

	assert(config.file.empty());
	assert(state == State::HANDSHAKE);

	http_request.reset();
	state = State::NOTHING;

	auto line = next_line(&response, end);
	ret = ParseHandshakeResponse(line.c_str());
	if (!ret) {
		IncreaseInterval();
		ScheduleHandshake();
		return;
	}

	session = next_line(&response, end);
	FormatDebug("[%s] session: %s",
		    config.name.c_str(),
		    session.c_str());

	nowplay_url = next_line(&response, end);
	FormatDebug("[%s] now playing url: %s",
		    config.name.c_str(),
		    nowplay_url.c_str());

	submit_url = next_line(&response, end);
	FormatDebug("[%s] submit url: %s",
		    config.name.c_str(),
		    submit_url.c_str());

	if (nowplay_url.empty() || submit_url.empty()) {
		session.clear();
		nowplay_url.clear();
		submit_url.clear();

		IncreaseInterval();
		ScheduleHandshake();
		return;
	}

	state = State::READY;
	interval = 1;

	/* handshake was successful: see if we have songs to submit */
	Submit();
}

inline void
Scrobbler::OnHandshakeError(std::exception_ptr e) noexcept
{
	assert(config.file.empty());
	assert(state == State::HANDSHAKE);

	http_request.reset();
	state = State::NOTHING;

	FormatError("[%s] handshake error: %s",
		    config.name.c_str(),
		    GetFullMessage(e).c_str());

	IncreaseInterval();
	ScheduleHandshake();
}

static void
scrobbler_queue_remove_oldest(std::list<Record> &queue, unsigned count)
{
	assert(count > 0);

	while (count--)
		queue.pop_front();
}

inline void
Scrobbler::OnSubmitResponse(std::string body) noexcept
{
	assert(config.file.empty());
	assert(state == State::SUBMITTING);

	http_request.reset();
	state = State::READY;

	auto newline = body.find('\n');
	if (newline != body.npos)
		body.resize(newline);

	switch (scrobbler_parse_submit_response(config.name.c_str(),
						body.data(), body.length())) {
	case SubmitResponseType::OK:
		interval = 1;

		/* submission was accepted, so clean up the cache. */
		if (pending > 0) {
			scrobbler_queue_remove_oldest(queue, pending);
			pending = 0;
		} else {
			assert(record_is_defined(&now_playing));

			now_playing = {};
		}


		/* submit the next chunk (if there is some left) */
		Submit();
		break;

	case SubmitResponseType::FAILED:
		IncreaseInterval();
		ScheduleSubmit();
		break;

	case SubmitResponseType::HANDSHAKE:
		state = State::NOTHING;
		ScheduleHandshake();
		break;
	}
}

inline void
Scrobbler::OnSubmitError(std::exception_ptr e) noexcept
{
	assert(config.file.empty());
	assert(state == State::SUBMITTING);

	http_request.reset();
	state = State::READY;

	FormatError("[%s] submit error: %s",
		    config.name.c_str(),
		    GetFullMessage(e).c_str());

	IncreaseInterval();
	ScheduleSubmit();
}

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
Scrobbler::Handshake() noexcept
{
	assert(config.file.empty());

	state = State::HANDSHAKE;

	const auto timestr = as_timestamp();
	const auto md5 = as_md5(config.password, timestr);

	/* construct the handshake url. */
	FormDataBuilder url(config.url);
	url.Append("hs", "true");
	url.Append("p", "1.2");
	url.Append("c", AS_CLIENT_ID);
	url.Append("v", AS_CLIENT_VERSION);
	url.Append("u", config.username.c_str());
	url.Append("t", timestr.c_str());
	url.Append("a", &md5.front());

	//  notice ("handshake url:\n%s", url);

	HttpResponseHandler &handler = *this;
	http_request = std::make_unique<CurlRequest>(curl_global,
						     url.c_str(), std::string(),
						     handler);
}

void
Scrobbler::OnHandshakeTimer() noexcept
{
	assert(config.file.empty());
	assert(state == State::NOTHING);

	Handshake();
}

void
Scrobbler::ScheduleHandshake() noexcept
{
	assert(config.file.empty());
	assert(state == State::NOTHING);
	assert(!handshake_timer.IsPending());

	handshake_timer.Schedule(std::chrono::seconds{interval});
}

void
Scrobbler::SendNowPlaying(const char *artist,
			  const char *track, const char *album,
			  const char *number,
			  const char *mbid,
			  std::chrono::steady_clock::duration length) noexcept
{
	assert(config.file.empty());
	assert(state == State::READY);

	state = State::SUBMITTING;

	FormDataBuilder post_data;
	post_data.Append("s", session);
	post_data.Append("a", artist);
	post_data.Append("t", track);
	post_data.Append("b", album);
	post_data.Append("l",
			 std::chrono::duration_cast<std::chrono::seconds>(length).count());
	post_data.Append("n", number);
	post_data.Append("m", mbid);

	FormatInfo("[%s] sending 'now playing' notification",
		   config.name.c_str());

	HttpResponseHandler &handler = *this;
	http_request = std::make_unique<CurlRequest>(curl_global,
						     nowplay_url.c_str(),
						     std::move(post_data),
						     handler);
}

void
Scrobbler::ScheduleNowPlaying(const Record &song) noexcept
{
	if (file != nullptr)
		/* there's no "now playing" support for files */
		return;

	now_playing = song;

	if (state == State::READY && !submit_timer.IsPending())
		ScheduleSubmit();
}

void
Scrobbler::Submit() noexcept
{
	//MAX_SUBMIT_COUNT
	unsigned count = 0;

	assert(config.file.empty());
	assert(state == State::READY);
	assert(!submit_timer.IsPending());

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

	state = State::SUBMITTING;

	/* construct the handshake url. */
	FormDataBuilder post_data;
	post_data.Append("s", session);

	for (const auto &i : queue) {
		if (count >= MAX_SUBMIT_COUNT)
			break;

		const auto *song = &i;

		post_data.AppendIndexed("a", count, song->artist);
		post_data.AppendIndexed("t", count, song->track);
		post_data.AppendIndexed("l", count,
					std::chrono::duration_cast<std::chrono::seconds>(song->length).count());
		post_data.AppendIndexed("i", count, song->time);
		post_data.AppendIndexed("o", count, song->source);
		post_data.AppendIndexed("r", count, "");
		post_data.AppendIndexed("b", count, song->album);
		post_data.AppendIndexed("n", count, song->number);
		post_data.AppendIndexed("m", count, song->mbid);

		if (song->love)
			post_data.AppendIndexed("r", count, "L");

		count++;
	}

	FormatInfo("[%s] submitting %i song%s",
		   config.name.c_str(), count, count == 1 ? "" : "s");
	FormatDebug("[%s] post data: %s",
		    config.name.c_str(), post_data.c_str());
	FormatDebug("[%s] url: %s",
		    config.name.c_str(),
		    submit_url.c_str());

	pending = count;

	HttpResponseHandler &handler = *this;
	http_request = std::make_unique<CurlRequest>(curl_global,
						     submit_url.c_str(),
						     std::move(post_data),
						     handler);
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

	if (state == State::READY && !submit_timer.IsPending())
		ScheduleSubmit();
}

void
Scrobbler::OnSubmitTimer() noexcept
{
	assert(state == State::READY);

	Submit();
}

void
Scrobbler::ScheduleSubmit() noexcept
{
	assert(!submit_timer.IsPending());
	assert(!queue.empty() || record_is_defined(&now_playing));

	submit_timer.Schedule(std::chrono::seconds{interval});
}

void
Scrobbler::WriteJournal() const noexcept
{
	if (file != nullptr || config.journal.empty())
		return;

	if (journal_write(config.journal.c_str(), queue)) {
		unsigned queue_length = queue.size();
		FormatInfo("[%s] saved %i song%s to %s",
			   config.name.c_str(),
			   queue_length, queue_length == 1 ? "" : "s",
			   config.journal.c_str());
	}
}

void
Scrobbler::SubmitNow() noexcept
{
	interval = 1;

	if (handshake_timer.IsPending()) {
		handshake_timer.Cancel();
		ScheduleHandshake();
	}

	if (submit_timer.IsPending()) {
		submit_timer.Cancel();
		ScheduleSubmit();
	}
}

void
Scrobbler::OnHttpResponse(std::string body) noexcept
{
	switch (state) {
	case State::NOTHING:
	case State::READY:
		assert(false);
		break;

	case State::HANDSHAKE:
		OnHandshakeResponse(std::move(body));
		break;

	case State::SUBMITTING:
		OnSubmitResponse(std::move(body));
		break;
	}
}

void
Scrobbler::OnHttpError(std::exception_ptr e) noexcept
{
	switch (state) {
	case State::NOTHING:
	case State::READY:
		assert(false);
		break;

	case State::HANDSHAKE:
		OnHandshakeError(std::move(e));
		break;

	case State::SUBMITTING:
		OnSubmitError(std::move(e));
		break;
	}
}
