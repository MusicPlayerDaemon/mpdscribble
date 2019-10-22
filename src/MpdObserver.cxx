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

#include "MpdObserver.hxx"
#include "Log.hxx"

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <stdio.h>

void
MpdObserver::HandleError() noexcept
{
	char *msg = g_strescape(mpd_connection_get_error_message(connection),
				nullptr);
	FormatWarning("mpd error (%u): %s",
		      mpd_connection_get_error(connection), msg);
	g_free(msg);

	socket.release();
	mpd_connection_free(connection);
	connection = nullptr;
}

static std::string
settings_name(const struct mpd_settings *settings)
{
	const char *host = mpd_settings_get_host(settings);
	if (host == nullptr)
		host = "unknown";

	if (host[0] == '/')
		return g_strdup(host);

	unsigned port = mpd_settings_get_port(settings);
	if (port == 0 || port == 6600)
		return g_strdup(host);

	char buffer[256];
	snprintf(buffer, sizeof(buffer), "%s:%u", host, port);
	return buffer;
}

static std::string
connection_settings_name(const struct mpd_connection *connection)
{
	const struct mpd_settings *settings =
		mpd_connection_get_settings(connection);
	if (settings == nullptr)
		return "unknown";

	return settings_name(settings);
}

bool
MpdObserver::Connect() noexcept
{
	assert(connection == nullptr);

	connection = mpd_connection_new(host, port, 0);
	if (mpd_connection_get_error(connection) != MPD_ERROR_SUCCESS) {
		HandleError();
		return false;
	}

	const unsigned *version = mpd_connection_get_server_version(connection);

	if (mpd_connection_cmp_server_version(connection, 0, 16, 0) < 0) {
		FormatWarning("Error: MPD version %d.%d.%d is too old (%s needed)",
			      version[0], version[1], version[2],
			      "0.16.0");
		mpd_connection_free(connection);
		connection = nullptr;
		return false;
	}

	const auto name = connection_settings_name(connection);
	FormatInfo("connected to mpd %i.%i.%i at %s",
		   version[0], version[1], version[2],
		   name.c_str());

	socket.assign(mpd_connection_get_fd(connection));

	subscribed = mpd_run_subscribe(connection, "mpdscribble");
	if (!subscribed && !mpd_connection_clear_error(connection)) {
		HandleError();
		return false;
	}

	return true;
}

void
MpdObserver::OnConnectTimer(const boost::system::error_code &error) noexcept
{
	if (error)
		return;

	if (!Connect()) {
		ScheduleConnect();
		return;
	}

	ScheduleUpdate();
}

void
MpdObserver::ScheduleConnect() noexcept
{
	assert(connection == nullptr);

	FormatInfo("waiting 15 seconds before reconnecting");

	connect_timer.expires_from_now(std::chrono::seconds(15));
	connect_timer.async_wait(std::bind(&MpdObserver::OnConnectTimer,
					   this, std::placeholders::_1));
}

MpdObserver::MpdObserver(boost::asio::io_service &io_service,
			 MpdObserverListener &_listener,
			 const char *_host, int _port) noexcept
	:listener(_listener),
	 host(_host), port(_port),
	 connect_timer(io_service),
	 update_timer(io_service),
	 socket(io_service)
{
	connect_timer.expires_from_now(std::chrono::seconds(0));
	connect_timer.async_wait(std::bind(&MpdObserver::OnConnectTimer,
					   this, std::placeholders::_1));
}

MpdObserver::~MpdObserver() noexcept
{
	if (connection != nullptr) {
		socket.release();
		mpd_connection_free(connection);
	}

	if (current_song != nullptr)
		mpd_song_free(current_song);
}

enum mpd_state
MpdObserver::QueryState(struct mpd_song **song_r,
			std::chrono::steady_clock::duration &elapsed_r) noexcept
{
	struct mpd_status *status;
	enum mpd_state state;
	struct mpd_song *song;

	assert(connection != nullptr);

	mpd_command_list_begin(connection, true);
	mpd_send_status(connection);
	mpd_send_current_song(connection);
	mpd_command_list_end(connection);

	status = mpd_recv_status(connection);
	if (!status) {
		HandleError();
		return MPD_STATE_UNKNOWN;
	}

	state = mpd_status_get_state(status);
	elapsed_r = std::chrono::milliseconds(mpd_status_get_elapsed_ms(status));

	mpd_status_free(status);

	if (state != MPD_STATE_PLAY) {
		if (!mpd_response_finish(connection)) {
			HandleError();
			return MPD_STATE_UNKNOWN;
		}

		return state;
	}

	if (!mpd_response_next(connection)) {
		HandleError();
		return MPD_STATE_UNKNOWN;
	}

	song = mpd_recv_song(connection);
	if (song == nullptr) {
		if (!mpd_response_finish(connection)) {
			HandleError();
			return MPD_STATE_UNKNOWN;
		}

		return MPD_STATE_UNKNOWN;
	}

	if (!mpd_response_finish(connection)) {
		mpd_song_free(song);
		HandleError();
		return MPD_STATE_UNKNOWN;
	}

	*song_r = song;
	return MPD_STATE_PLAY;
}

void
MpdObserver::Update() noexcept
{
	struct mpd_song *prev;
	enum mpd_state state;
	std::chrono::steady_clock::duration elapsed{};

	prev = current_song;
	state = QueryState(&current_song, elapsed);

	if (state == MPD_STATE_PAUSE) {
		if (!was_paused)
			listener.OnMpdPaused();
		was_paused = true;

		ScheduleIdle();
		return;
	} else if (state != MPD_STATE_PLAY) {
		current_song = nullptr;
		last_id = -1;
		was_paused = false;
	} else if (mpd_song_get_tag(current_song, MPD_TAG_ARTIST, 0) == nullptr ||
		   mpd_song_get_tag(current_song, MPD_TAG_TITLE, 0) == nullptr) {
		if (mpd_song_get_id(current_song) != last_id) {
			FormatInfo("new song detected with tags missing (%s)",
				   mpd_song_get_uri(current_song));
			last_id = mpd_song_get_id(current_song);
		}

		mpd_song_free(current_song);
		current_song = nullptr;
	}

	if (was_paused) {
		if (current_song != nullptr &&
		    mpd_song_get_id(current_song) == last_id)
			listener.OnMpdResumed();
		was_paused = false;
	}

	/* submit the previous song */
	if (prev != nullptr &&
	    (current_song == nullptr ||
	     mpd_song_get_id(prev) != mpd_song_get_id(current_song))) {
		listener.OnMpdEnded(prev, love);
		love = false;
	}

	if (current_song != nullptr) {
		if (mpd_song_get_id(current_song) != last_id) {
			/* new song. */

			listener.OnMpdStarted(current_song);
			last_id = mpd_song_get_id(current_song);
		} else {
			/* still playing the previous song */

			listener.OnMpdPlaying(current_song, elapsed);
		}
	}

	if (prev != nullptr)
		mpd_song_free(prev);

	if (connection == nullptr) {
		ScheduleConnect();
		return;
	}

	ScheduleIdle();
	return;
}

void
MpdObserver::OnUpdateTimer(const boost::system::error_code &error) noexcept
{
	if (error)
		return;

	Update();
}

void
MpdObserver::ScheduleUpdate() noexcept
{
	update_timer.expires_from_now(std::chrono::seconds(0));
	update_timer.async_wait([this](const boost::system::error_code &error){
		if (!error)
			Update();
	});
}

bool
MpdObserver::ReadMessages() noexcept
{
	assert(subscribed);

	if (!mpd_send_read_messages(connection))
		return mpd_connection_clear_error(connection);

	struct mpd_message *msg;
	while ((msg = mpd_recv_message(connection)) != nullptr) {
		const char *text = mpd_message_get_text(msg);
		if (strcmp(text, "love") == 0)
			love = true;
		else
			FormatInfo("Unrecognized client-to-client message: '%s'",
				   text);

		mpd_message_free(msg);
	}

	return mpd_response_finish(connection);
}

void
MpdObserver::OnIdleResponse() noexcept
{
	bool success;
	enum mpd_idle idle;

	assert(connection != nullptr);
	assert(mpd_connection_get_error(connection) == MPD_ERROR_SUCCESS);

	idle = mpd_recv_idle(connection, false);
	success = mpd_response_finish(connection);

	if (!success) {
		HandleError();
		ScheduleConnect();
		return;
	}

	if (subscribed && (idle & MPD_IDLE_MESSAGE) != 0 &&
	    !ReadMessages()) {
		HandleError();
		ScheduleConnect();
		return;
	}

	if (idle & MPD_IDLE_PLAYER)
		/* there was a change: query MPD */
		ScheduleUpdate();
	else
		/* nothing interesting: re-enter idle */
		ScheduleIdle();
}

void
MpdObserver::ScheduleIdle() noexcept
{
	assert(connection != nullptr);

	idle_notified = false;

	constexpr enum mpd_idle mask = (enum mpd_idle)(MPD_IDLE_PLAYER|MPD_IDLE_MESSAGE);

	if (!mpd_send_idle_mask(connection, mask)) {
		HandleError();
		ScheduleConnect();
		return;
	}

	/* add a GLib watch on the libmpdclient socket */

	socket.async_read_some(boost::asio::null_buffers(),
			       [this](const boost::system::error_code &error, size_t){
				       if (!error)
					       OnIdleResponse();
			       });
}
