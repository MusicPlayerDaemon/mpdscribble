/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2019 The Music Player Daemon Project
 * Copyright (C) 2005-2008 Kuno Woudt <kuno@frob.nl>
 * Project homepage: http://musicpd.org
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef CURL_GLOBAL_HXX
#define CURL_GLOBAL_HXX

#include "lib/curl/Init.hxx"
#include "lib/curl/Multi.hxx"
#include "AsioServiceFwd.hxx"
#include "AsioGetIoService.hxx"

#include <boost/asio/steady_timer.hpp>

class CurlEasy;

class CurlGlobal final {
	class Socket;

	const char *const proxy;

	const ScopeCurlInit init;

	/** the CURL multi handle */
	CurlMulti multi;

	boost::asio::steady_timer timeout_timer, read_info_timer;

public:
	CurlGlobal(boost::asio::io_service &io_service,
		   const char *_proxy);
	~CurlGlobal() noexcept;

	CurlGlobal(const CurlGlobal &) = delete;
	CurlGlobal &operator=(const CurlGlobal &) = delete;

	auto &get_io_service() noexcept {
		return ::get_io_service(timeout_timer);
	}

	void Configure(CurlEasy &easy);

	void Add(CURL *easy);

	void Remove(CURL *easy) noexcept {
		curl_multi_remove_handle(multi.Get(), easy);
	}

	void Assign(curl_socket_t fd, Socket &s) noexcept {
		curl_multi_assign(multi.Get(), fd, &s);
	}

	void SocketAction(curl_socket_t fd, int ev_bitmask) noexcept;

private:
	/**
	 * Check for finished HTTP responses.
	 */
	void ReadInfo() noexcept;

	void ScheduleReadInfo() noexcept {
		read_info_timer.cancel();
		read_info_timer.expires_from_now(std::chrono::seconds(0));
		read_info_timer.async_wait([this](const boost::system::error_code &error){
			if (!error)
				ReadInfo();
		});
	}

	void UpdateTimeout(long timeout_ms) noexcept;
	static int TimerFunction(CURLM *multi, long timeout_ms,
				 void *userp) noexcept;
};

#endif
