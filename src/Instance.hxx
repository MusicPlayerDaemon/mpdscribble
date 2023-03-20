// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef INSTANCE_HXX
#define INSTANCE_HXX

#include "event/Loop.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "lib/curl/Global.hxx"
#include "time/Stopwatch.hxx"
#include "MpdObserver.hxx"
#include "MultiScrobbler.hxx"

struct Config;

struct Instance final : MpdObserverListener {
	EventLoop event_loop;

	Stopwatch stopwatch;

	CurlGlobal curl_global;

	MpdObserver mpd_observer;

	MultiScrobbler scrobblers;

	const Event::Duration save_journal_interval;
	CoarseTimerEvent save_journal_timer;

	Instance(const Config &config);
	~Instance() noexcept;

	void Run() noexcept {
		event_loop.Run();
	}

	void Stop() noexcept;

	void OnMpdSongChanged(const struct mpd_song *song) noexcept;

	/* virtual methods from MpdObserverListener */
	void OnMpdStarted(const struct mpd_song *song) noexcept override;
	void OnMpdPlaying(const struct mpd_song *song,
			  std::chrono::steady_clock::duration elapsed) noexcept override;
	void OnMpdEnded(const struct mpd_song *song,
			bool love) noexcept override;
	void OnMpdPaused() noexcept override;
	void OnMpdResumed() noexcept override;

private:
#ifndef _WIN32
	void OnSubmitSignal() noexcept;
#endif

	void OnSaveJournalTimer() noexcept;
	void ScheduleSaveJournalTimer() noexcept;
};

#endif
