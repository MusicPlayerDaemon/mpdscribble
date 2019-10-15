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

#include "Record.hxx"

#include <glib.h>

#include <list>
#include <string>

#include <stdio.h>

struct ScrobblerConfig;

class Scrobbler {
	const ScrobblerConfig &config;

	FILE *file = nullptr;

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
	} state = SCROBBLER_STATE_NOTHING;

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

#endif /* SCROBBLER_H */
