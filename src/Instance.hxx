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

#ifndef INSTANCE_HXX
#define INSTANCE_HXX

#include "lib/curl/Global.hxx"
#include "MpdObserver.hxx"
#include "MultiScrobbler.hxx"

#include <glib.h>

struct config;

struct Instance final : MpdObserverListener {
	GMainLoop *main_loop;
	GTimer *timer;

	CurlGlobal curl_global;

	MpdObserver mpd_observer;

	MultiScrobbler scrobblers;

	Instance(const struct config &config) noexcept;
	~Instance() noexcept;

	void Run() noexcept {
		g_main_loop_run(main_loop);
	}

	void OnMpdSongChanged(const struct mpd_song *song) noexcept;

	/* virtual methods from MpdObserverListener */
	void OnMpdStarted(const struct mpd_song *song) noexcept override;
	void OnMpdPlaying(const struct mpd_song *song,
			  int elapsed) noexcept override;
	void OnMpdEnded(const struct mpd_song *song,
			bool love) noexcept override;
	void OnMpdPaused() noexcept override;
	void OnMpdResumed() noexcept override;
};

#endif
