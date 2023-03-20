// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "Instance.hxx"
#include "Config.hxx"
#include "SdDaemon.hxx"
#include "event/SignalMonitor.hxx"

#ifndef _WIN32
#include <signal.h>
#endif

Instance::Instance(const Config &config)
	:curl_global(event_loop, NullableString(config.proxy)),
	 mpd_observer(event_loop, *this,
		      NullableString(config.host), config.port),
	 scrobblers(config.scrobblers, event_loop, curl_global),
	 save_journal_interval(std::chrono::seconds{config.journal_interval}),
	 save_journal_timer(event_loop, BIND_THIS_METHOD(OnSaveJournalTimer))
{
#ifndef _WIN32
	SignalMonitorInit(event_loop);
	SignalMonitorRegister(SIGTERM, BIND_THIS_METHOD(Stop));
	SignalMonitorRegister(SIGINT, BIND_THIS_METHOD(Stop));
	SignalMonitorRegister(SIGUSR1, BIND_THIS_METHOD(OnSubmitSignal));
#endif

	ScheduleSaveJournalTimer();
}

Instance::~Instance() noexcept
{
	SignalMonitorFinish();
}

inline void
Instance::Stop() noexcept
{
	sd_notify(0, "STOPPING=1");

	event_loop.Break();
}

#ifndef _WIN32

void
Instance::OnSubmitSignal() noexcept
{
	scrobblers.SubmitNow();
}

#endif

void
Instance::OnSaveJournalTimer() noexcept
{
	scrobblers.WriteJournal();
	ScheduleSaveJournalTimer();
}

void
Instance::ScheduleSaveJournalTimer() noexcept
{
	save_journal_timer.Schedule(save_journal_interval);
}
