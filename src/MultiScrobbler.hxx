// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef MULTI_SCROBBLER_HXX
#define MULTI_SCROBBLER_HXX

#include <chrono>
#include <forward_list>

struct ScrobblerConfig;
class CurlGlobal;
class Scrobbler;
class EventLoop;

class MultiScrobbler {
	std::forward_list<Scrobbler> scrobblers;

public:
	explicit MultiScrobbler(const std::forward_list<ScrobblerConfig> &configs,
				EventLoop &event_loop,
				CurlGlobal &curl_global);
	~MultiScrobbler() noexcept;

	void WriteJournal() noexcept;

	void NowPlaying(const char *artist, const char *track,
			const char *album, const char *number,
			const char *mbid,
			std::chrono::steady_clock::duration length) noexcept;

	void SongChange(const char *file, const char *artist, const char *track,
			const char *album, const char *number,
			const char *mbid,
			std::chrono::steady_clock::duration length,
			bool love,
			const char *time) noexcept;

	void SubmitNow() noexcept;
};

#endif
