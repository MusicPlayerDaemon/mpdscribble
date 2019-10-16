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

#include <glib.h>

class CurlGlobal final {
	class Socket;

	const ScopeCurlInit init;

	/** the CURL multi handle */
	CurlMulti multi;

	guint timeout_source_id = 0, read_info_source_id = 0;

public:
	CurlGlobal();
	~CurlGlobal() noexcept;

	CurlGlobal(const CurlGlobal &) = delete;
	CurlGlobal &operator=(const CurlGlobal &) = delete;

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

	static gboolean DeferredReadInfo(gpointer user_data) noexcept;

	void ScheduleReadInfo() noexcept {
		if (read_info_source_id == 0)
			read_info_source_id = g_idle_add(DeferredReadInfo,
							 this);
	}

	void OnTimeout() noexcept;
	static gboolean TimeoutCallback(gpointer user_data) noexcept;

	void UpdateTimeout(long timeout_ms) noexcept;
	static int TimerFunction(CURLM *multi, long timeout_ms,
				 void *userp) noexcept;
};

#endif
