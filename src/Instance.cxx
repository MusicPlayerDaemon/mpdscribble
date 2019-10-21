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

Instance::Instance(const struct config &config) noexcept
	:io_service(),
#ifndef _WIN32
	 quit_signal(io_service, SIGTERM, SIGINT),
	 submit_signal(io_service, SIGUSR1),
#endif
	 curl_global(io_service),
	 mpd_observer(io_service, *this, config.host, config.port),
	 scrobblers(config.scrobblers, io_service, curl_global),
	 save_journal_interval(config.journal_interval),
	 save_journal_timer(io_service)
{
#ifndef _WIN32
	quit_signal.async_wait(std::bind(&Instance::Stop, this));

	AsyncWaitSubmitSignal();
#endif

	ScheduleSaveJournalTimer();
}

Instance::~Instance() noexcept = default;

inline void
Instance::Stop() noexcept
{
	this->io_service.stop();
}

#ifndef _WIN32

void
Instance::AsyncWaitSubmitSignal() noexcept
{
	submit_signal.async_wait([this](const auto &error, int){
			if (error)
				return;

			scrobblers.SubmitNow();
			this->AsyncWaitSubmitSignal();
		});
}

#endif

void
Instance::ScheduleSaveJournalTimer() noexcept
{
	save_journal_timer.expires_from_now(save_journal_interval);
	save_journal_timer.async_wait([this](const boost::system::error_code &error){
		if (error)
			return;

		scrobblers.WriteJournal();
		ScheduleSaveJournalTimer();
	});
}
