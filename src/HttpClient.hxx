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

#ifndef HTTP_CLIENT_HXX
#define HTTP_CLIENT_HXX

#include "lib/curl/Easy.hxx"

#include <glib.h>

#include <exception>
#include <forward_list>
#include <string>

#include <stddef.h>

class HttpClient;

struct HttpClientHandler {
	void (*response)(std::string body, void *ctx);
	void (*error)(std::exception_ptr e, void *ctx);
};

class HttpRequest final
{
	HttpClient &client;

	const HttpClientHandler &handler;
	void *handler_ctx;

	/** the CURL easy handle */
	CurlEasy curl;

	/** the POST request body */
	std::string request_body;

	/** the response body */
	std::string response_body;

	/** error message provided by libcurl */
	char error[CURL_ERROR_SIZE];

public:
	HttpRequest(HttpClient &client,
		    const char *url, std::string &&_request_body,
		    const HttpClientHandler &_handler, void *_ctx);
	~HttpRequest() noexcept;

	/**
	 * A HTTP request is finished: invoke its callback and free it.
	 */
	void Done(CURLcode result, long status) noexcept;

private:
	void CheckResponse(CURLcode result, long status);

	/**
	 * Called by curl when new data is available.
	 */
	static size_t WriteFunction(char *ptr, size_t size, size_t nmemb,
				    void *stream) noexcept;

};

class HttpClient final {
	struct Source {
		GSource base;

		HttpClient *client;
	};

	/** the CURL multi handle */
	CURLM *multi;

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
	HttpClient();
	~HttpClient() noexcept;

	HttpClient(const HttpClient &) = delete;
	HttpClient &operator=(const HttpClient &) = delete;

	void Add(CURL *easy);

	void Remove(CURL *easy) noexcept {
		curl_multi_remove_handle(multi, easy);
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

/**
 * Escapes URI parameters with '%'.  Free the return value with
 * g_free().
 */
std::string
http_client_uri_escape(const char *src) noexcept;

#endif
