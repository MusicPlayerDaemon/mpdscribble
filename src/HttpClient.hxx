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

#include <boost/intrusive/list_hook.hpp>

#include <exception>
#include <string>

#include <stddef.h>

struct HttpClientHandler {
	void (*response)(std::string &&body, void *ctx);
	void (*error)(std::exception_ptr e, void *ctx);
};

class HttpRequest final
	: public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
{
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
	HttpRequest(const char *url, std::string &&_request_body,
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

/**
 * Perform global initialization on the HTTP client library.
 */
void
http_client_init();

/**
 * Global deinitializaton.
 */
void
http_client_finish() noexcept;

class HttpClientInit final {
public:
	HttpClientInit() {
		http_client_init();
	}

	~HttpClientInit() noexcept {
		http_client_finish();
	}

	HttpClientInit(const HttpClientInit &) = delete;
	HttpClientInit &operator=(const HttpClientInit &) = delete;
};

/**
 * Escapes URI parameters with '%'.  Free the return value with
 * g_free().
 */
std::string
http_client_uri_escape(const char *src) noexcept;

#endif
