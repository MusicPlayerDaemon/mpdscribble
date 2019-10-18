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
#include "lib/curl/Global.hxx"
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

HttpRequest::HttpRequest(CurlGlobal &_global,
			 const char *url, std::string &&_request_body,
			 const HttpClientHandler &_handler,
			 void *_ctx)
	:global(_global),
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

	global.Add(curl.Get());
}

HttpRequest::~HttpRequest() noexcept
{
	if (curl)
		global.Remove(curl.Get());
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

std::string
http_client_uri_escape(const char *src) noexcept
{
	/* curl_escape() is deprecated, but for some reason,
	   curl_easy_escape() wants to have a CURL object, which we
	   don't have right now */
	char *tmp = curl_escape(src, 0);
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
