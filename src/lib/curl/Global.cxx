/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2019 The Music Player Daemon Project
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

#include "Global.hxx"
#include "util/Exception.hxx"
#include "util/RuntimeError.hxx"
#include "HttpClient.hxx"

#include <stdexcept>

#include <assert.h>

enum {
	/** maximum length of a response body */
	MAX_RESPONSE_BODY = 8192,
};

void
CurlGlobal::Add(CURL *easy)
{
	CURLMcode mcode = curl_multi_add_handle(multi.Get(), easy);
	if (mcode != CURLM_OK)
		throw std::runtime_error(curl_multi_strerror(mcode));
}

/**
 * Calculates the GLib event bit mask for one file descriptor,
 * obtained from three #fd_set objects filled by curl_multi_fdset().
 */
static gushort
http_client_fd_events(int fd, fd_set *rfds,
		      fd_set *wfds, fd_set *efds) noexcept
{
	gushort events = 0;

	if (FD_ISSET(fd, rfds)) {
		events |= G_IO_IN | G_IO_HUP | G_IO_ERR;
		FD_CLR(fd, rfds);
	}

	if (FD_ISSET(fd, wfds)) {
		events |= G_IO_OUT | G_IO_ERR;
		FD_CLR(fd, wfds);
	}

	if (FD_ISSET(fd, efds)) {
		events |= G_IO_HUP | G_IO_ERR;
		FD_CLR(fd, efds);
	}

	return events;
}

void
CurlGlobal::UpdateFDs() noexcept
{
	fd_set rfds, wfds, efds;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&efds);

	int max_fd;
	CURLMcode mcode = curl_multi_fdset(multi.Get(), &rfds, &wfds,
					   &efds, &max_fd);
	if (mcode != CURLM_OK) {
		g_warning("curl_multi_fdset() failed: %s\n",
			  curl_multi_strerror(mcode));
		return;
	}

	for (auto prev = fds.before_begin(), i = std::next(prev);
	     i != fds.end();) {
		GPollFD *poll_fd = &*i;
		gushort events = http_client_fd_events(poll_fd->fd, &rfds,
						       &wfds, &efds);

		assert(poll_fd->events != 0);

		if (events != poll_fd->events)
			g_source_remove_poll(source, poll_fd);

		if (events != 0) {
			if (events != poll_fd->events) {
				poll_fd->events = events;
				g_source_add_poll(source, poll_fd);
			}

			prev = i++;
		} else {
			i = fds.erase_after(prev);
		}
	}

	for (int fd = 0; fd <= max_fd; ++fd) {
		gushort events = http_client_fd_events(fd, &rfds,
						       &wfds, &efds);
		if (events != 0) {
			fds.emplace_front();
			GPollFD *poll_fd = &fds.front();
			poll_fd->fd = fd;
			poll_fd->events = events;
			g_source_add_poll(source, poll_fd);
		}
	}
}

/**
 * Find a request by its CURL "easy" handle.
 */
static HttpRequest *
http_client_find_request(CURL *curl) noexcept
{
	void *p;
	CURLcode code = curl_easy_getinfo(curl, CURLINFO_PRIVATE, &p);
	if (code != CURLE_OK)
		return nullptr;

	return (HttpRequest *)p;
}

void
CurlGlobal::ReadInfo() noexcept
{
	CURLMsg *msg;
	int msgs_in_queue;

	while ((msg = curl_multi_info_read(multi.Get(),
					   &msgs_in_queue)) != nullptr) {
		if (msg->msg == CURLMSG_DONE) {
			HttpRequest *request =
				http_client_find_request(msg->easy_handle);
			assert(request != nullptr);

			long status = 0;
			curl_easy_getinfo(msg->easy_handle,
					  CURLINFO_RESPONSE_CODE, &status);

			request->Done(msg->data.result, status);
		}
	}
}

bool
CurlGlobal::Perform() noexcept
{
	CURLMcode mcode;

	do {
		int running_handles;
		mcode = curl_multi_perform(multi.Get(), &running_handles);
	} while (mcode == CURLM_CALL_MULTI_PERFORM);

	if (mcode != CURLM_OK && mcode != CURLM_CALL_MULTI_PERFORM) {
		g_warning("curl_multi_perform() failed: %s",
			  curl_multi_strerror(mcode));
		return false;
	}

	return true;
}

/**
 * The GSource prepare() method implementation.
 */
gboolean
CurlGlobal::SourcePrepare(GSource *source, gint *timeout_) noexcept
{
	auto &global = *((Source *)source)->global;

	global.UpdateFDs();
	global.timeout = false;

	long timeout2;
	CURLMcode mcode = curl_multi_timeout(global.multi.Get(),
					     &timeout2);
	if (mcode == CURLM_OK) {
		if (timeout2 >= 0 && timeout2 < 10)
			/* CURL 7.21.1 likes to report "timeout=0",
			   which means we're running in a busy loop.
			   Quite a bad idea to waste so much CPU.
			   Let's use a lower limit of 10ms. */
			timeout2 = 10;

		*timeout_ = timeout2;

		global.timeout = timeout2 >= 0;
	} else
		g_warning("curl_multi_timeout() failed: %s\n",
			  curl_multi_strerror(mcode));

	return false;
}

/**
 * The GSource check() method implementation.
 */
gboolean
CurlGlobal::SourceCheck(GSource *source) noexcept
{
	auto &global = *((Source *)source)->global;

	if (global.timeout) {
		/* when a timeout has expired, we need to call
		   curl_multi_perform(), even if there was no file
		   descriptor event */
		global.timeout = false;
		return true;
	}

	for (const auto &i : global.fds)
		if (i.revents != 0)
			return true;

	return false;
}

/**
 * The GSource dispatch() method implementation.  The callback isn't
 * used, because we're handling all events directly.
 */
gboolean
CurlGlobal::SourceDispatch(GSource *source, GSourceFunc, gpointer) noexcept
{
	auto &global = *((Source *)source)->global;

	if (global.Perform())
		global.ReadInfo();

	return true;
}

/**
 * The vtable for our GSource implementation.  Unfortunately, we
 * cannot declare it "const", because g_source_new() takes a non-const
 * pointer, for whatever reason.
 */
static GSourceFuncs curl_source_funcs = {
	.prepare = CurlGlobal::SourcePrepare,
	.check = CurlGlobal::SourceCheck,
	.dispatch = CurlGlobal::SourceDispatch,
};

CurlGlobal::CurlGlobal()
{
	source = g_source_new(&curl_source_funcs,
			      sizeof(Source));
	((Source *)source)->global = this;

	source_id = g_source_attach(source, g_main_context_default());
}

CurlGlobal::~CurlGlobal() noexcept
{
	/* unregister all GPollFD instances */

	UpdateFDs();

	/* free the GSource object */

	g_source_unref(source);
	g_source_remove(source_id);
}
