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

#include "Instance.hxx"
#include "Config.hxx"
#include "SdDaemon.hxx"
#include "event/SignalMonitor.hxx"

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
