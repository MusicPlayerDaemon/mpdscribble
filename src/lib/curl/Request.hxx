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

#ifndef CURL_REQUEST_HXX
#define CURL_REQUEST_HXX

#include "Easy.hxx"

#include <exception>
#include <string>

class CurlGlobal;

struct HttpResponseHandler {
	void (*response)(std::string body, void *ctx);
	void (*error)(std::exception_ptr e, void *ctx);
};

class CurlRequest final
{
	CurlGlobal &global;

	const HttpResponseHandler &handler;
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
	CurlRequest(CurlGlobal &global,
		    const char *url, std::string &&_request_body,
		    const HttpResponseHandler &_handler, void *_ctx);
	~CurlRequest() noexcept;

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

#endif
