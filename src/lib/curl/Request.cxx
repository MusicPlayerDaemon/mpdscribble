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

#include "Request.hxx"
#include "Handler.hxx"
#include "Global.hxx"
#include "util/RuntimeError.hxx"
#include "config.h"

#include <curl/curl.h>

#include <stdexcept>

enum {
	/** maximum length of a response body */
	MAX_RESPONSE_BODY = 8192,
};

CurlRequest::CurlRequest(CurlGlobal &_global,
			 const char *url, std::string &&_request_body,
			 HttpResponseHandler &_handler)
	:global(_global),
	 handler(_handler),
	 curl(url),
	 request_body(std::move(_request_body))
{
	curl.SetPrivate(this);
	curl.SetUserAgent(PACKAGE "/" VERSION);
	curl.SetWriteFunction(WriteFunction, this);
	curl.SetOption(CURLOPT_FAILONERROR, true);
	curl.SetOption(CURLOPT_ERRORBUFFER, error);
	curl.SetOption(CURLOPT_BUFFERSIZE, (long)2048);
	curl.SetFailOnError();

	if (!request_body.empty()) {
		curl.SetOption(CURLOPT_POST, true);
		curl.SetRequestBody(request_body.data(),
				    request_body.size());
	}

	global.Configure(curl);
	global.Add(curl.Get());
}

CurlRequest::~CurlRequest() noexcept
{
	if (curl)
		global.Remove(curl.Get());
}

inline void
CurlRequest::CheckResponse(CURLcode result)
{
	if (result == CURLE_WRITE_ERROR &&
	    /* handle the postponed error that was caught in
	       WriteFunction() */
	    response_body.length() > MAX_RESPONSE_BODY)
		throw std::runtime_error("response body is too large");
	else if (result != CURLE_OK)
		throw FormatRuntimeError("CURL failed: %s",
					 error);
}

void
CurlRequest::Done(CURLcode result) noexcept
{
	/* invoke the handler method */

	try {
		CheckResponse(result);
		handler.OnHttpResponse(std::move(response_body));
	} catch (...) {
		handler.OnHttpError(std::current_exception());
	}
}

/**
 * Called by curl when new data is available.
 */
size_t
CurlRequest::WriteFunction(char *ptr, size_t size, size_t nmemb,
			   void *stream) noexcept
{
	auto *request = (CurlRequest *)stream;

	request->response_body.append((const char *)ptr, size * nmemb);

	if (request->response_body.length() > MAX_RESPONSE_BODY)
		/* response body too large */
		return 0;

	return size * nmemb;
}
