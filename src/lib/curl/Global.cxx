/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2019 The Music Player Daemon Project
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

#include "Global.hxx"
#include "Request.hxx"

#include <boost/asio/posix/stream_descriptor.hpp>

#include <cassert>
#include <stdexcept>

#include <stdio.h>

class CurlGlobal::Socket {
	CurlGlobal &global;

	boost::asio::posix::stream_descriptor fd;

	const int action;

	bool *destroyed_p = nullptr;

public:
	Socket(CurlGlobal &_global, boost::asio::io_service &io_service,
	       int _fd, int _action) noexcept
		:global(_global), fd(io_service, _fd), action(_action)
	{
		AsyncWait();
	}

	~Socket() noexcept {
		/* CURL closes the socket */
		fd.release();

		if (destroyed_p != nullptr)
			*destroyed_p = true;
	}

	/**
	 * Callback function for CURLMOPT_SOCKETFUNCTION.
	 */
	static int SocketFunction(CURL *easy,
				  curl_socket_t s, int action,
				  void *userp, void *socketp) noexcept;

private:
	void Callback(const boost::system::error_code &error,
		      int ev_bitmask) noexcept;

	void AsyncRead() noexcept {
		fd.async_read_some(boost::asio::null_buffers(),
				   std::bind(&Socket::Callback, this, std::placeholders::_1,
					     CURL_CSELECT_IN));
	}

	void AsyncWrite() noexcept {
		fd.async_write_some(boost::asio::null_buffers(),
				    std::bind(&Socket::Callback, this, std::placeholders::_1,
					      CURL_CSELECT_OUT));
	}

	void AsyncWait() noexcept;
};

void
CurlGlobal::Socket::AsyncWait() noexcept
{
	switch (action) {
	case CURL_POLL_NONE:
		break;

	case CURL_POLL_IN:
		AsyncRead();
		break;

	case CURL_POLL_OUT:
		AsyncWrite();
		break;

	case CURL_POLL_INOUT:
		AsyncRead();
		AsyncWrite();
		break;
	}
}

int
CurlGlobal::Socket::SocketFunction(CURL *, curl_socket_t s, int action,
				   void *userp, void *socketp) noexcept
{
	auto &global = *(CurlGlobal *)userp;

	auto *socket = (Socket *)socketp;
	delete socket;

	if (action == CURL_POLL_REMOVE)
		return 0;

	socket = new Socket(global, global.get_io_service(), s, action);
	global.Assign(s, *socket);
	return 0;
}

inline void
CurlGlobal::Socket::Callback(const boost::system::error_code &error,
			     int ev_bitmask) noexcept
{
	if (error)
		return;

	assert(destroyed_p == nullptr);
	bool destroyed = false;
	destroyed_p = &destroyed;

	global.SocketAction(fd.native_handle(), ev_bitmask);

	if (!destroyed) {
		destroyed_p = nullptr;
		AsyncWait();
	}
}

void
CurlGlobal::Add(CURL *easy)
{
	CURLMcode mcode = curl_multi_add_handle(multi.Get(), easy);
	if (mcode != CURLM_OK)
		throw std::runtime_error(curl_multi_strerror(mcode));
}

/**
 * Find a request by its CURL "easy" handle.
 */
static CurlRequest *
http_client_find_request(CURL *curl) noexcept
{
	void *p;
	CURLcode code = curl_easy_getinfo(curl, CURLINFO_PRIVATE, &p);
	if (code != CURLE_OK)
		return nullptr;

	return (CurlRequest *)p;
}

void
CurlGlobal::ReadInfo() noexcept
{
	CURLMsg *msg;
	int msgs_in_queue;

	while ((msg = curl_multi_info_read(multi.Get(),
					   &msgs_in_queue)) != nullptr) {
		if (msg->msg == CURLMSG_DONE) {
			CurlRequest *request =
				http_client_find_request(msg->easy_handle);
			assert(request != nullptr);

			long status = 0;
			curl_easy_getinfo(msg->easy_handle,
					  CURLINFO_RESPONSE_CODE, &status);

			request->Done(msg->data.result, status);
		}
	}
}

void
CurlGlobal::UpdateTimeout(long timeout_ms) noexcept
{
	timeout_timer.cancel();

	if (timeout_ms < 0)
		return;

	if (timeout_ms < 10)
		/* CURL 7.21.1 likes to report "timeout=0", which
		   means we're running in a busy loop.  Quite a bad
		   idea to waste so much CPU.  Let's use a lower limit
		   of 10ms. */
		timeout_ms = 10;

	timeout_timer.expires_from_now(std::chrono::milliseconds(timeout_ms));
	timeout_timer.async_wait([this](const boost::system::error_code &error){
		if (!error)
			SocketAction(CURL_SOCKET_TIMEOUT, 0);
	});
}

int
CurlGlobal::TimerFunction(CURLM *_multi, long timeout_ms, void *userp) noexcept
{
	auto &global = *(CurlGlobal *)userp;
	assert(_multi == global.multi.Get());
	(void)_multi;

	global.UpdateTimeout(timeout_ms);
	return 0;
}

void
CurlGlobal::SocketAction(curl_socket_t fd, int ev_bitmask) noexcept
{
	int running_handles;
	CURLMcode mcode = curl_multi_socket_action(multi.Get(), fd, ev_bitmask,
						   &running_handles);
	if (mcode != CURLM_OK)
		fprintf(stderr, "curl_multi_socket_action() failed: %s",
			curl_multi_strerror(mcode));

	ScheduleReadInfo();
}

CurlGlobal::CurlGlobal(boost::asio::io_service &io_service,
		       const char *_proxy)
	:proxy(_proxy),
	 timeout_timer(io_service), read_info_timer(io_service)
{
	multi.SetOption(CURLMOPT_SOCKETFUNCTION, Socket::SocketFunction);
	multi.SetOption(CURLMOPT_SOCKETDATA, this);

	multi.SetOption(CURLMOPT_TIMERFUNCTION, TimerFunction);
	multi.SetOption(CURLMOPT_TIMERDATA, this);
}

CurlGlobal::~CurlGlobal() noexcept = default;

void
CurlGlobal::Configure(CurlEasy &easy)
{
	if (proxy != nullptr)
		easy.SetOption(CURLOPT_PROXY, proxy);
}
