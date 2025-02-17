// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "MpdObserver.hxx"
#include "Log.hxx"

#include <cassert>
#include <string>

#include <string.h>
#include <stdio.h>

void
MpdObserver::HandleError() noexcept
{
	FmtWarning("mpd error ({}): {:?}",
		   (int)mpd_connection_get_error(connection),
		   mpd_connection_get_error_message(connection));

	mpd_connection_free(connection);
	connection = nullptr;

	socket.Abandon();
}

static std::string
settings_name(const struct mpd_settings *settings) noexcept
{
	const char *host = mpd_settings_get_host(settings);
	if (host == nullptr)
		host = "unknown";

	if (host[0] == '/' || host[0] == '@')
		return host;

	unsigned port = mpd_settings_get_port(settings);
	if (port == 0 || port == 6600)
		return host;

	char buffer[256];
	snprintf(buffer, sizeof(buffer), "%s:%u", host, port);
	return buffer;
}

static std::string
connection_settings_name(const struct mpd_connection *connection) noexcept
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
		FmtWarning("Error: MPD version {}.{}.{} is too old ({} needed)",
			   version[0], version[1], version[2],
			   "0.16.0");
		mpd_connection_free(connection);
		connection = nullptr;
		return false;
	}

	const auto name = connection_settings_name(connection);
	FmtInfo("connected to mpd {}.{}.{} at {}",
		version[0], version[1], version[2],
		name);

	socket.Open(SocketDescriptor(mpd_connection_get_fd(connection)));
	socket.ScheduleRead();

	subscribed = mpd_run_subscribe(connection, "mpdscribble");
	if (!subscribed && !mpd_connection_clear_error(connection)) {
		HandleError();
		return false;
	}

	return true;
}

void
MpdObserver::OnConnectTimer() noexcept
{
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

	LogInfo("waiting 15 seconds before reconnecting");

	connect_timer.Schedule(std::chrono::seconds{15});
}

MpdObserver::MpdObserver(EventLoop &event_loop,
			 MpdObserverListener &_listener,
			 const char *_host, int _port) noexcept
	:listener(_listener),
	 host(_host), port(_port),
	 connect_timer(event_loop, BIND_THIS_METHOD(OnConnectTimer)),
	 update_timer(event_loop, BIND_THIS_METHOD(OnUpdateTimer)),
	 socket(event_loop, BIND_THIS_METHOD(OnSocketReady))
{
	connect_timer.Schedule(std::chrono::seconds{0});
}

MpdObserver::~MpdObserver() noexcept
{
	if (connection != nullptr)
		mpd_connection_free(connection);

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
	} else if ((mpd_song_get_tag(current_song, MPD_TAG_ARTIST, 0) == nullptr &&
		   mpd_song_get_tag(current_song, MPD_TAG_ALBUM_ARTIST, 0) == nullptr) ||
		   mpd_song_get_tag(current_song, MPD_TAG_TITLE, 0) == nullptr) {
		if (mpd_song_get_id(current_song) != last_id) {
			FmtInfo("new song detected with tags missing ({})",
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
MpdObserver::OnUpdateTimer() noexcept
{
	Update();
}

inline void
MpdObserver::ScheduleUpdate() noexcept
{
	update_timer.Schedule();
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
			FmtInfo("Unrecognized client-to-client message: {:?}",
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
MpdObserver::OnSocketReady(unsigned) noexcept
{
	OnIdleResponse();
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
}
