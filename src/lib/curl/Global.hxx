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

#ifndef CURL_GLOBAL_HXX
#define CURL_GLOBAL_HXX

#include "lib/curl/Init.hxx"
#include "lib/curl/Multi.hxx"

#include <glib.h>

#include <forward_list>

class CurlGlobal final {
	struct Source {
		GSource base;

		CurlGlobal *global;
	};

	const ScopeCurlInit init;

	/** the CURL multi handle */
	CurlMulti multi;

	/** the GMainLoop source used to poll all CURL file
	    descriptors */
	GSource *source;

	/** the source id of #source */
	guint source_id;

	/** a linked list of all registered GPollFD objects */
	std::forward_list<GPollFD> fds;

	/**
	 * Did CURL give us a timeout?  If yes, then we need to call
	 * curl_multi_perform(), even if there was no event on any
	 * file descriptor.
	 */
	bool timeout;

public:
	CurlGlobal();
	~CurlGlobal() noexcept;

	CurlGlobal(const CurlGlobal &) = delete;
	CurlGlobal &operator=(const CurlGlobal &) = delete;

	void Add(CURL *easy);

	void Remove(CURL *easy) noexcept {
		curl_multi_remove_handle(multi.Get(), easy);
	}

	static gboolean SourcePrepare(GSource *source, gint *timeout) noexcept;
	static gboolean SourceCheck(GSource *source) noexcept;
	static gboolean SourceDispatch(GSource *source,
				       GSourceFunc, gpointer) noexcept;

private:
	/**
	 * Updates all registered GPollFD objects, unregisters old
	 * ones, registers new ones.
	 */
	void UpdateFDs() noexcept;

	/**
	 * Check for finished HTTP responses.
	 */
	void ReadInfo() noexcept;

	/**
	 * Give control to CURL.
	 */
	bool Perform() noexcept;
};

#endif
