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
#include "Config.hxx"
#include "config.h"

#include <curl/curl.h>

#include <boost/intrusive/list.hpp>

#include <forward_list>

#include <assert.h>

enum {
	/** maximum length of a response body */
	MAX_RESPONSE_BODY = 8192,
};

struct HttpRequest
	: boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
{
	const HttpClientHandler &handler;
	void *handler_ctx;

	/** the CURL easy handle */
	CURL *curl = nullptr;

	/** the POST request body */
	std::string request_body;

	/** the response body */
	std::string response_body;

	/** error message provided by libcurl */
	char error[CURL_ERROR_SIZE];

	HttpRequest(std::string &&_request_body,
		    const HttpClientHandler &_handler, void *_ctx) noexcept;
	~HttpRequest() noexcept;
};

static struct {
	/** the CURL multi handle */
	CURLM *multi;

	/** the GMainLoop source used to poll all CURL file
	    descriptors */
	GSource *source;

	/** the source id of #source */
	guint source_id;

	/** a linked list of all registered GPollFD objects */
	std::forward_list<GPollFD> fds;

	/** a linked list of all active HTTP requests */
	boost::intrusive::list<HttpRequest,
			       boost::intrusive::constant_time_size<false>> requests;

	/**
	 * Did CURL give us a timeout?  If yes, then we need to call
	 * curl_multi_perform(), even if there was no event on any
	 * file descriptor.
	 */
	bool timeout;
} http_client;

static inline GQuark
curl_quark() noexcept
{
    return g_quark_from_static_string("curl");
}

static size_t
http_request_writefunction(void *ptr, size_t size, size_t nmemb,
			   void *stream) noexcept;

HttpRequest::HttpRequest(std::string &&_request_body,
			 const HttpClientHandler &_handler,
			 void *_ctx) noexcept
	:handler(_handler), handler_ctx(_ctx),
	 request_body(std::move(_request_body))
{
	curl_easy_setopt(curl, CURLOPT_USERAGENT, PACKAGE "/" VERSION);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
			 http_request_writefunction);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, true);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 2048);

	if (file_config.proxy != nullptr)
		curl_easy_setopt(curl, CURLOPT_PROXY, file_config.proxy);

	if (!request_body.empty()) {
		curl_easy_setopt(curl, CURLOPT_POST, true);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS,
				 request_body.data());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
				 (long)request_body.size());
	}
}

HttpRequest::~HttpRequest() noexcept
{
	if (curl != nullptr) {
		curl_multi_remove_handle(http_client.multi, curl);
		curl_easy_cleanup(curl);
	}
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

/**
 * Updates all registered GPollFD objects, unregisters old ones,
 * registers new ones.
 */
static void
http_client_update_fds() noexcept
{
	fd_set rfds, wfds, efds;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&efds);

	int max_fd;
	CURLMcode mcode = curl_multi_fdset(http_client.multi, &rfds, &wfds,
					   &efds, &max_fd);
	if (mcode != CURLM_OK) {
		g_warning("curl_multi_fdset() failed: %s\n",
			  curl_multi_strerror(mcode));
		return;
	}

	for (auto prev = http_client.fds.before_begin(), i = std::next(prev);
	     i != http_client.fds.end();) {
		GPollFD *poll_fd = &*i;
		gushort events = http_client_fd_events(poll_fd->fd, &rfds,
						       &wfds, &efds);

		assert(poll_fd->events != 0);

		if (events != poll_fd->events)
			g_source_remove_poll(http_client.source, poll_fd);

		if (events != 0) {
			if (events != poll_fd->events) {
				poll_fd->events = events;
				g_source_add_poll(http_client.source, poll_fd);
			}

			prev = i++;
		} else {
			i = http_client.fds.erase_after(prev);
		}
	}

	for (int fd = 0; fd <= max_fd; ++fd) {
		gushort events = http_client_fd_events(fd, &rfds,
						       &wfds, &efds);
		if (events != 0) {
			http_client.fds.emplace_front();
			GPollFD *poll_fd = &http_client.fds.front();
			poll_fd->fd = fd;
			poll_fd->events = events;
			g_source_add_poll(http_client.source, poll_fd);
		}
	}
}

/**
 * Aborts and frees a running HTTP request and report an error to its
 * handler.
 */
static void
http_request_abort(HttpRequest *request, GError *error) noexcept
{
	request->handler.error(error, request->handler_ctx);
	delete request;
}

/**
 * Abort and free all HTTP requests, but don't invoke their handler
 * methods.
 */
static void
http_client_abort_all_requests(GError *error) noexcept
{
	http_client.requests.clear_and_dispose([error](auto *request){
		http_request_abort(request, g_error_copy(error));
	});

	g_error_free(error);
}

/**
 * Find a request by its CURL "easy" handle.
 */
static HttpRequest *
http_client_find_request(CURL *curl) noexcept
{
	for (auto &i : http_client.requests)
		if (i.curl == curl)
			return &i;

	return nullptr;
}

/**
 * A HTTP request is finished: invoke its callback and free it.
 */
static void
http_request_done(HttpRequest *request, CURLcode result, long status) noexcept
{
	/* invoke the handler method */

	if (result == CURLE_WRITE_ERROR &&
	    /* handle the postponed error that was caught in
	       http_request_writefunction() */
	    request->response_body.length() > MAX_RESPONSE_BODY) {
		GError *error =
			g_error_new_literal(curl_quark(), 0,
					    "response body is too large");
		request->handler.error(error, request->handler_ctx);
	} else if (result != CURLE_OK) {
		GError *error = g_error_new(curl_quark(), result,
					    "curl failed: %s",
					    request->error);
		request->handler.error(error, request->handler_ctx);
	} else if (status < 200 || status >= 300) {
		GError *error = g_error_new(curl_quark(), 0,
					    "got HTTP status %ld",
					    status);
		request->handler.error(error, request->handler_ctx);
	} else
		request->handler.response(std::move(request->response_body),
					   request->handler_ctx);

	delete request;
}

/**
 * Check for finished HTTP responses.
 */
static void
http_multi_info_read() noexcept
{
	CURLMsg *msg;
	int msgs_in_queue;

	while ((msg = curl_multi_info_read(http_client.multi,
					   &msgs_in_queue)) != nullptr) {
		if (msg->msg == CURLMSG_DONE) {
			HttpRequest *request =
				http_client_find_request(msg->easy_handle);
			assert(request != nullptr);

			long status = 0;
			curl_easy_getinfo(msg->easy_handle,
					  CURLINFO_RESPONSE_CODE, &status);

			http_request_done(request, msg->data.result, status);
		}
	}
}

/**
 * Give control to CURL.
 */
static bool
http_multi_perform() noexcept
{
	CURLMcode mcode;

	do {
		int running_handles;
		mcode = curl_multi_perform(http_client.multi,
					   &running_handles);
	} while (mcode == CURLM_CALL_MULTI_PERFORM);

	if (mcode != CURLM_OK && mcode != CURLM_CALL_MULTI_PERFORM) {
		GError *error = g_error_new(curl_quark(), mcode,
					    "curl_multi_perform() failed: %s",
					    curl_multi_strerror(mcode));
		http_client_abort_all_requests(error);
		return false;
	}

	return true;
}

/**
 * The GSource prepare() method implementation.
 */
static gboolean
curl_source_prepare(GSource *, gint *timeout_) noexcept
{
	http_client_update_fds();

	http_client.timeout = false;

	long timeout2;
	CURLMcode mcode = curl_multi_timeout(http_client.multi, &timeout2);
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
static gboolean
curl_source_check(GSource *) noexcept
{
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
static gboolean
curl_source_dispatch(GSource *, GSourceFunc, gpointer) noexcept
{
	if (http_multi_perform())
		http_multi_info_read();

	return true;
}

/**
 * The vtable for our GSource implementation.  Unfortunately, we
 * cannot declare it "const", because g_source_new() takes a non-const
 * pointer, for whatever reason.
 */
static GSourceFuncs curl_source_funcs = {
	.prepare = curl_source_prepare,
	.check = curl_source_check,
	.dispatch = curl_source_dispatch,
};

void
http_client_init()
{
	CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
	if (code != CURLE_OK)
		g_error("curl_global_init() failed: %s",
			curl_easy_strerror(code));

	http_client.multi = curl_multi_init();
	if (http_client.multi == nullptr)
		g_error("curl_multi_init() failed");

	http_client.source = g_source_new(&curl_source_funcs,
					  sizeof(*http_client.source));
	http_client.source_id = g_source_attach(http_client.source,
						g_main_context_default());
}

void
http_client_finish() noexcept
{
	/* free all requests */

	http_client.requests.clear_and_dispose([](auto *request){ delete request; });

	/* unregister all GPollFD instances */

	http_client_update_fds();

	/* free the GSource object */

	g_source_unref(http_client.source);
	g_source_remove(http_client.source_id);

	/* clean up CURL */

	curl_multi_cleanup(http_client.multi);
	curl_global_cleanup();
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
static size_t
http_request_writefunction(void *ptr, size_t size, size_t nmemb,
			   void *stream) noexcept
{
	auto *request = (HttpRequest *)stream;

	request->response_body.append((const char *)ptr, size * nmemb);

	if (request->response_body.length() > MAX_RESPONSE_BODY)
		/* response body too large */
		return 0;

	return size * nmemb;
}

void
http_client_request(const char *url, std::string &&post_data,
		    const HttpClientHandler &handler, void *ctx) noexcept
{
	HttpRequest *request = new HttpRequest(std::move(post_data),
					       handler, ctx);

	/* create a CURL request */

	request->curl = curl_easy_init();
	if (request->curl == nullptr) {
		delete request;

		GError *error = g_error_new_literal(curl_quark(), 0,
						    "curl_easy_init() failed");
		handler.error(error, ctx);
		return;
	}

	CURLMcode mcode = curl_multi_add_handle(http_client.multi, request->curl);
	if (mcode != CURLM_OK) {
		curl_easy_cleanup(request->curl);
		delete request;

		GError *error = g_error_new_literal(curl_quark(), 0,
						    "curl_multi_add_handle() failed");
		handler.error(error, ctx);
		return;
	}

	/* .. and set it up */

	CURLcode code = curl_easy_setopt(request->curl, CURLOPT_URL, url);
	if (code != CURLE_OK) {
		curl_multi_remove_handle(http_client.multi, request->curl);
		curl_easy_cleanup(request->curl);
		delete request;

		GError *error = g_error_new_literal(curl_quark(), code,
						    "curl_easy_setopt() failed");
		handler.error(error, ctx);
		return;
	}

	http_client.requests.push_front(*request);
}
