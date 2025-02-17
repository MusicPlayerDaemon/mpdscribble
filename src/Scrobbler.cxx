// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "Scrobbler.hxx"
#include "Protocol.hxx"
#include "ScrobblerConfig.hxx"
#include "Journal.hxx"
#include "lib/curl/Request.hxx"
#include "lib/fmt/ExceptionFormatter.hxx"
#include "lib/fmt/SystemError.hxx"
#include "Form.hxx"
#include "Log.hxx" /* for log_date() */
#include "util/HexFormat.hxx"
#include "util/SpanCast.hxx"

#ifdef _WIN32
#include "lib/wincrypt/MD5.hxx"
#else
#include "lib/gcrypt/MD5.hxx"
#endif

#include <array>
#include <cassert>

#include <errno.h>
#include <string.h>

/* don't submit more than this amount of songs in a batch. */
#define MAX_SUBMIT_COUNT 10

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
		FmtInfo("loaded {} song{} from {:?}",
			queue_length, queue_length == 1 ? "" : "s",
			config.journal);
	}

	if (!config.file.empty()) {
		file = fopen(config.file.c_str(), "a");
		if (file == nullptr)
			throw FmtErrno("Failed to open file {:?} of scrobbler {:?}",
				       config.file, config.name);
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
	if (interval < MIN_INTERVAL)
		interval = MIN_INTERVAL;
	else
		interval *= 2;

	if (interval > MAX_INTERVAL)
		interval = MAX_INTERVAL;

	FmtWarning("[{}] waiting {} seconds before trying again",
		   config.name,
		   std::chrono::duration_cast<std::chrono::duration<unsigned>>(interval).count());
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
		FmtInfo("[{}] OK", scrobbler_name);

		return SubmitResponseType::OK;
	} else if (length == sizeof(BADSESSION) - 1 &&
		   memcmp(line, BADSESSION, length) == 0) {
		FmtWarning("[{}] invalid session", scrobbler_name);

		return SubmitResponseType::HANDSHAKE;
	} else if (length == sizeof(FAILED) - 1 &&
		   memcmp(line, FAILED, length) == 0) {
		if (length > strlen(FAILED))
			FmtError("[{}] submission rejected: {}",
				 scrobbler_name,
				 std::string_view{line + strlen(FAILED), length - strlen(FAILED)});
		else
			FmtError("[{}] submission rejected", scrobbler_name);
	} else {
		FmtError("[{}] unknown response: {}",
			 scrobbler_name, std::string_view{line, length});
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
		FmtInfo("[{}] handshake successful", config.name);
		return true;
	} else if (!strncmp(line, BANNED, strlen(BANNED))) {
		FmtError("[{}] handshake failed, we're banned ({:?})",
			 config.name, line);
	} else if (!strncmp(line, BADAUTH, strlen(BADAUTH))) {
		FmtError("[{}] handshake failed, username or password incorrect ({:?})",
			 config.name, line);
	} else if (!strncmp(line, BADTIME, strlen(BADTIME))) {
		FmtError("[{}] handshake failed, clock not synchronized ({:?})",
			 config.name, line);
	} else if (!strncmp(line, FAILED, strlen(FAILED))) {
		FmtError("[{}] handshake failed ({:?})",
			 config.name, line);
	} else {
		FmtError("[{}] error parsing handshake response ({:?})",
			 config.name, line);
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
	FmtDebug("[{}] session: {:?}", config.name, session);

	nowplay_url = next_line(&response, end);
	FmtDebug("[{}] now playing url: {}", config.name, nowplay_url);

	submit_url = next_line(&response, end);
	FmtDebug("[{}] submit url: {}", config.name, submit_url);

	if (nowplay_url.empty() || submit_url.empty()) {
		session.clear();
		nowplay_url.clear();
		submit_url.clear();

		IncreaseInterval();
		ScheduleHandshake();
		return;
	}

	state = State::READY;
	interval = std::chrono::seconds{1};

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

	FmtError("[{}] handshake error: {}", config.name, e);

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
		interval = std::chrono::seconds{1};

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

	FmtError("[{}] submit error: {}", config.name, e);

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
static auto
md5_hex(std::string_view s)
{
#ifdef _WIN32
	const auto binary = WinCrypt::MD5(AsBytes(s));
#else
	const auto binary = Gcrypt::MD5(AsBytes(s));
#endif
	return HexFormat(std::span{binary});
}

static auto
as_md5(const std::string &password, const std::string &timestamp)
{
	std::array<char, MD5_HEX_SIZE> buffer;

	auto password_md5 = std::string_view(password);
	if (password.length() != 32) {
		/* assume it's not hashed yet */
		buffer = md5_hex(password);
		password_md5 = std::string_view(buffer.data(), MD5_HEX_SIZE);
	}

	std::string md5_with_timestamp;
	md5_with_timestamp.reserve(password_md5.size() + timestamp.size());
	md5_with_timestamp.append(password_md5);
	md5_with_timestamp.append(timestamp);
	return md5_hex(md5_with_timestamp);
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
	url.Append("u", config.username);
	url.Append("t", timestr);
	url.Append("a", ToStringView(md5));

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

	handshake_timer.Schedule(interval);
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

	FmtInfo("[{}] sending 'now playing' notification", config.name);

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

	if (config.ignore_list && config.ignore_list->matches_record(song)) {
		return;
	}

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

	FmtInfo("[{}] submitting {} song{}",
		config.name, count, count == 1 ? "" : "s");
	FmtDebug("[{}] post data: {:?}", config.name, post_data.c_str());
	FmtDebug("[{}] url: {}", config.name, submit_url);

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
	if (config.ignore_list && config.ignore_list->matches_record(song)) {
		return;
	}

	if (file != nullptr) {
		fmt::print(file, "{} {} - {}\n",
			   log_date(),
			   song.artist, song.track);
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

	submit_timer.Schedule(interval);
}

void
Scrobbler::WriteJournal() const noexcept
{
	if (file != nullptr || config.journal.empty())
		return;

	if (journal_write(config.journal.c_str(), queue)) {
		unsigned queue_length = queue.size();
		FmtInfo("[{}] saved {} song{} to {:?}",
			config.name,
			queue_length, queue_length == 1 ? "" : "s",
			config.journal);
	}
}

void
Scrobbler::SubmitNow() noexcept
{
	interval = std::chrono::seconds{1};

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
