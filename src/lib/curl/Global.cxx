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
#include "HttpClient.hxx"

#include <stdexcept>

#include <assert.h>

static guint
MakeGlibFdSource(int fd, GIOCondition condition,
		 GIOFunc func, gpointer user_data) noexcept
{
	auto *channel = g_io_channel_unix_new(fd);
	guint source_id = g_io_add_watch(channel, condition, func, user_data);
	g_io_channel_unref(channel);
	return source_id;
}

static constexpr GIOCondition
CurlPollToGIOCondition(int action) noexcept
{
	switch (action) {
	case CURL_POLL_NONE:
		break;

	case CURL_POLL_IN:
		return G_IO_IN;

	case CURL_POLL_OUT:
		return G_IO_OUT;

	case CURL_POLL_INOUT:
		return GIOCondition(G_IO_IN|G_IO_OUT);
	}

	return GIOCondition(0);
}

static constexpr int
GIOConditionToCurlCSelect(GIOCondition condition) noexcept
{
	int result = 0;

	if (condition & (G_IO_IN|G_IO_HUP))
		result |= CURL_CSELECT_IN;

	if (condition & G_IO_OUT)
		result |= CURL_CSELECT_OUT;

	if (condition & G_IO_ERR)
		result |= CURL_CSELECT_ERR;

	return result;
}

class CurlGlobal::Socket {
	CurlGlobal &global;

	const int fd;

	const guint source_id;

public:
	Socket(CurlGlobal &_global, int _fd, int action) noexcept
		:global(_global), fd(_fd),
		 source_id(MakeGlibFdSource(fd, CurlPollToGIOCondition(action),
					    Callback, this))
	{
	}

	~Socket() noexcept {
		g_source_remove(source_id);
	}

	/**
	 * Callback function for CURLMOPT_SOCKETFUNCTION.
	 */
	static int SocketFunction(CURL *easy,
				  curl_socket_t s, int action,
				  void *userp, void *socketp) noexcept;

private:
	static gboolean Callback(GIOChannel *channel, GIOCondition condition,
				 gpointer user_data) noexcept;
};

int
CurlGlobal::Socket::SocketFunction(CURL *, curl_socket_t s, int action,
				   void *userp, void *socketp) noexcept
{
	auto &global = *(CurlGlobal *)userp;

	auto *socket = (Socket *)socketp;
	delete socket;

	if (action == CURL_POLL_REMOVE)
		return 0;

	socket = new Socket(global, s, action);
	global.Assign(s, *socket);
	return 0;
}

gboolean
CurlGlobal::Socket::Callback(GIOChannel *, GIOCondition condition,
			     gpointer user_data) noexcept
{
	auto &socket = *(Socket *)user_data;

	socket.global.SocketAction(socket.fd,
				   GIOConditionToCurlCSelect(condition));
	return true;
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
static HttpRequest *
http_client_find_request(CURL *curl) noexcept
{
	void *p;
	CURLcode code = curl_easy_getinfo(curl, CURLINFO_PRIVATE, &p);
	if (code != CURLE_OK)
		return nullptr;

	return (HttpRequest *)p;
}

void
CurlGlobal::ReadInfo() noexcept
{
	CURLMsg *msg;
	int msgs_in_queue;

	while ((msg = curl_multi_info_read(multi.Get(),
					   &msgs_in_queue)) != nullptr) {
		if (msg->msg == CURLMSG_DONE) {
			HttpRequest *request =
				http_client_find_request(msg->easy_handle);
			assert(request != nullptr);

			long status = 0;
			curl_easy_getinfo(msg->easy_handle,
					  CURLINFO_RESPONSE_CODE, &status);

			request->Done(msg->data.result, status);
		}
	}
}

gboolean
CurlGlobal::DeferredReadInfo(gpointer user_data) noexcept
{
	auto &global = *(CurlGlobal *)user_data;

	global.read_info_source_id = 0;
	global.ReadInfo();
	return false;
}

inline void
CurlGlobal::OnTimeout() noexcept
{
	SocketAction(CURL_SOCKET_TIMEOUT, 0);
}

gboolean
CurlGlobal::TimeoutCallback(gpointer user_data) noexcept
{
	auto &global = *(CurlGlobal *)user_data;
	global.OnTimeout();
	return false;
}

void
CurlGlobal::UpdateTimeout(long timeout_ms) noexcept
{
	if (timeout_ms < 0) {
		if (timeout_source_id != 0)
			g_source_remove(std::exchange(timeout_source_id, 0));

		return;
	}

	if (timeout_ms < 10)
		/* CURL 7.21.1 likes to report "timeout=0", which
		   means we're running in a busy loop.  Quite a bad
		   idea to waste so much CPU.  Let's use a lower limit
		   of 10ms. */
		timeout_ms = 10;

	if (timeout_source_id != 0)
		g_source_remove(timeout_source_id);

	timeout_source_id = g_timeout_add(timeout_ms, TimeoutCallback, this);
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
		g_warning("curl_multi_socket_action() failed: %s",
			  curl_multi_strerror(mcode));

	ScheduleReadInfo();
}

CurlGlobal::CurlGlobal()
{
	multi.SetOption(CURLMOPT_SOCKETFUNCTION, Socket::SocketFunction);
	multi.SetOption(CURLMOPT_SOCKETDATA, this);

	multi.SetOption(CURLMOPT_TIMERFUNCTION, TimerFunction);
	multi.SetOption(CURLMOPT_TIMERDATA, this);
}

CurlGlobal::~CurlGlobal() noexcept
{
	if (timeout_source_id != 0)
		g_source_remove(timeout_source_id);
	if (read_info_source_id != 0)
		g_source_remove(read_info_source_id);
}
