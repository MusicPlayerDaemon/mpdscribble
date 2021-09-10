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

#ifndef MPD_OBSERVER_HXX
#define MPD_OBSERVER_HXX

#include "event/CoarseTimerEvent.hxx"
#include "event/DeferEvent.hxx"
#include "event/SocketEvent.hxx"

#include <mpd/client.h>

#include <chrono>

class MpdObserverListener {
public:
	virtual void OnMpdStarted(const struct mpd_song *song) noexcept = 0;
	virtual void OnMpdPlaying(const struct mpd_song *song,
				  std::chrono::steady_clock::duration elapsed) noexcept = 0;
	virtual void OnMpdEnded(const struct mpd_song *song,
				bool love) noexcept = 0;
	virtual void OnMpdPaused() noexcept = 0;
	virtual void OnMpdResumed() noexcept = 0;
};

class MpdObserver {
	MpdObserverListener &listener;

	const char *const host;
	const int port;

	struct mpd_connection *connection = nullptr;

	bool idle_notified = false;
	unsigned last_id = -1;
	struct mpd_song *current_song = nullptr;
	bool was_paused = false;

	/**
	 * Is the current song being "loved"?  That variable gets set when the
	 * client-to-client command "love" is received.
	 */
	bool love = false;

	bool subscribed = false;

	CoarseTimerEvent connect_timer;
	DeferEvent update_timer;
	SocketEvent socket;

public:
	MpdObserver(EventLoop &event_loop,
		    MpdObserverListener &_listener,
		    const char *_host, int _port) noexcept;
	~MpdObserver() noexcept;

private:
	void HandleError() noexcept;

	void ScheduleConnect() noexcept;
	void OnConnectTimer() noexcept;
	bool Connect() noexcept;

	void ScheduleUpdate() noexcept;
	void OnUpdateTimer() noexcept;
	enum mpd_state QueryState(struct mpd_song **song_r,
				  std::chrono::steady_clock::duration &elapsed_r) noexcept;
	/**
	 * Update: determine MPD's current song and enqueue submissions.
	 */
	void Update() noexcept;

	bool ReadMessages() noexcept;

	void OnSocketReady(unsigned events) noexcept;
	void ScheduleIdle() noexcept;
	void OnIdleResponse() noexcept;
};

#endif
