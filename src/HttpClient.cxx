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

#include "HttpClient.hxx"
#include "util/Exception.hxx"
#include "util/RuntimeError.hxx"
#include "Config.hxx"
#include "config.h"

#include <curl/curl.h>

#include <forward_list>
#include <stdexcept>

#include <assert.h>

enum {
	/** maximum length of a response body */
	MAX_RESPONSE_BODY = 8192,
};

void
HttpClient::Add(CURL *easy)
{
	CURLMcode mcode = curl_multi_add_handle(multi.Get(), easy);
	if (mcode != CURLM_OK)
		throw std::runtime_error(curl_multi_strerror(mcode));
}

HttpRequest::HttpRequest(HttpClient &_client,
			 const char *url, std::string &&_request_body,
			 const HttpClientHandler &_handler,
			 void *_ctx)
	:client(_client),
	 handler(_handler), handler_ctx(_ctx),
	 curl(url),
	 request_body(std::move(_request_body))
{
	curl.SetPrivate(this);
	curl.SetUserAgent(PACKAGE "/" VERSION);
	curl.SetWriteFunction(WriteFunction, this);
	curl.SetOption(CURLOPT_FAILONERROR, true);
	curl.SetOption(CURLOPT_ERRORBUFFER, error);
	curl.SetOption(CURLOPT_BUFFERSIZE, (long)2048);

	if (file_config.proxy != nullptr)
		curl.SetOption(CURLOPT_PROXY, file_config.proxy);

	if (!request_body.empty()) {
		curl.SetOption(CURLOPT_POST, true);
		curl.SetRequestBody(request_body.data(),
				    request_body.size());
	}

	client.Add(curl.Get());
}

HttpRequest::~HttpRequest() noexcept
{
	if (curl)
		client.Remove(curl.Get());
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
HttpClient::UpdateFDs() noexcept
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

inline void
HttpRequest::CheckResponse(CURLcode result, long status)
{
	if (result == CURLE_WRITE_ERROR &&
	    /* handle the postponed error that was caught in
	       WriteFunction() */
	    response_body.length() > MAX_RESPONSE_BODY)
		throw std::runtime_error("response body is too large");
	else if (result != CURLE_OK)
		throw FormatRuntimeError("CURL failed: %s",
					 error);
	else if (status < 200 || status >= 300)
		throw FormatRuntimeError("got HTTP status %ld",
					 status);
}

void
HttpRequest::Done(CURLcode result, long status) noexcept
{
	/* invoke the handler method */

	try {
		CheckResponse(result, status);
		handler.response(std::move(response_body), handler_ctx);
	} catch (...) {
		handler.error(std::current_exception(), handler_ctx);
	}
}

void
HttpClient::ReadInfo() noexcept
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
HttpClient::Perform() noexcept
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
HttpClient::SourcePrepare(GSource *source, gint *timeout_) noexcept
{
	auto &http_client = *((Source *)source)->client;

	http_client.UpdateFDs();
	http_client.timeout = false;

	long timeout2;
	CURLMcode mcode = curl_multi_timeout(http_client.multi.Get(),
					     &timeout2);
	if (mcode == CURLM_OK) {
		if (timeout2 >= 0 && timeout2 < 10)
			/* CURL 7.21.1 likes to report "timeout=0",
			   which means we're running in a busy loop.
			   Quite a bad idea to waste so much CPU.
			   Let's use a lower limit of 10ms. */
			timeout2 = 10;

		*timeout_ = timeout2;

		http_client.timeout = timeout2 >= 0;
	} else
		g_warning("curl_multi_timeout() failed: %s\n",
			  curl_multi_strerror(mcode));

	return false;
}

/**
 * The GSource check() method implementation.
 */
gboolean
HttpClient::SourceCheck(GSource *source) noexcept
{
	auto &http_client = *((Source *)source)->client;

	if (http_client.timeout) {
		/* when a timeout has expired, we need to call
		   curl_multi_perform(), even if there was no file
		   descriptor event */
		http_client.timeout = false;
		return true;
	}

	for (const auto &i : http_client.fds)
		if (i.revents != 0)
			return true;

	return false;
}

/**
 * The GSource dispatch() method implementation.  The callback isn't
 * used, because we're handling all events directly.
 */
gboolean
HttpClient::SourceDispatch(GSource *source, GSourceFunc, gpointer) noexcept
{
	auto &http_client = *((Source *)source)->client;

	if (http_client.Perform())
		http_client.ReadInfo();

	return true;
}

/**
 * The vtable for our GSource implementation.  Unfortunately, we
 * cannot declare it "const", because g_source_new() takes a non-const
 * pointer, for whatever reason.
 */
static GSourceFuncs curl_source_funcs = {
	.prepare = HttpClient::SourcePrepare,
	.check = HttpClient::SourceCheck,
	.dispatch = HttpClient::SourceDispatch,
};

HttpClient::HttpClient()
{
	source = g_source_new(&curl_source_funcs,
			      sizeof(Source));
	((Source *)source)->client = this;

	source_id = g_source_attach(source, g_main_context_default());
}

HttpClient::~HttpClient() noexcept
{
	/* unregister all GPollFD instances */

	UpdateFDs();

	/* free the GSource object */

	g_source_unref(source);
	g_source_remove(source_id);
}

std::string
http_client_uri_escape(const char *src) noexcept
{
	/* curl_escape() is deprecated, but for some reason,
	   curl_easy_escape() wants to have a CURL object, which we
	   don't have right now */
	char *tmp = curl_escape(src, 0);
	/* call g_strdup(), because the caller expects a pointer which
	   can be freed with g_free() */
	std::string dest(tmp == nullptr ? src : tmp);
	curl_free(tmp);
	return dest;
}

/**
 * Called by curl when new data is available.
 */
size_t
HttpRequest::WriteFunction(char *ptr, size_t size, size_t nmemb,
			   void *stream) noexcept
{
	auto *request = (HttpRequest *)stream;

	request->response_body.append((const char *)ptr, size * nmemb);

	if (request->response_body.length() > MAX_RESPONSE_BODY)
		/* response body too large */
		return 0;

	return size * nmemb;
}
