// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef SCROBBLER_HXX
#define SCROBBLER_HXX

#include "lib/curl/Handler.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "Record.hxx"

#include <list>
#include <memory>
#include <string>

#include <stdio.h>

struct ScrobblerConfig;
class CurlGlobal;
class CurlRequest;

class Scrobbler final : HttpResponseHandler {
	const ScrobblerConfig &config;

	FILE *file = nullptr;

	enum class State {
		/**
		 * mpdscribble has started, and doesn't have a session yet.
		 * Handshake to be submitted.
		 */
		NOTHING,

		/**
		 * Handshake is in progress, waiting for the server's
		 * response.
		 */
		HANDSHAKE,

		/**
		 * We have a session, and we're ready to submit.
		 */
		READY,

		/**
		 * Submission in progress, waiting for the server's response.
		 */
		SUBMITTING,
	} state = State::NOTHING;

	static constexpr Event::Duration MIN_INTERVAL = std::chrono::minutes{1};

	/**
	 * Maximum exponential backoff delay.
	 */
	static constexpr Event::Duration MAX_INTERVAL = std::chrono::minutes{8};

	Event::Duration interval = std::chrono::seconds{1};

	CurlGlobal &curl_global;

	std::unique_ptr<CurlRequest> http_request;

	CoarseTimerEvent handshake_timer, submit_timer;

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
	Scrobbler(const ScrobblerConfig &_config,
		  EventLoop &event_loop,
		  CurlGlobal &_curl_global);
	~Scrobbler() noexcept;

	void Push(const Record &song) noexcept;
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
			    const char *mbid,
			    std::chrono::steady_clock::duration length) noexcept;

	void ScheduleSubmit() noexcept;
	void Submit() noexcept;
	void IncreaseInterval() noexcept;

	void OnHandshakeTimer() noexcept;
	void OnSubmitTimer() noexcept;

public:
	void OnHandshakeResponse(std::string body) noexcept;
	void OnHandshakeError(std::exception_ptr e) noexcept;
	void OnSubmitResponse(std::string body) noexcept;
	void OnSubmitError(std::exception_ptr e) noexcept;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(std::string body) noexcept override;
	void OnHttpError(std::exception_ptr e) noexcept override;
};

#endif /* SCROBBLER_H */
