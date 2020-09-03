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

#ifndef SCROBBLER_HXX
#define SCROBBLER_HXX

#include "lib/curl/Handler.hxx"
#include "AsioServiceFwd.hxx"
#include "Record.hxx"

#include <boost/asio/steady_timer.hpp>

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

	unsigned interval = 1;

	CurlGlobal &curl_global;

	std::unique_ptr<CurlRequest> http_request;

	boost::asio::steady_timer handshake_timer, submit_timer;

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

	bool handshake_timer_scheduled = false, submit_timer_scheduled = false;

public:
	Scrobbler(const ScrobblerConfig &_config,
		  boost::asio::io_service &io_service,
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

	void OnHandshakeTimer(const boost::system::error_code &error) noexcept;
	void OnSubmitTimer(const boost::system::error_code &error) noexcept;

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
