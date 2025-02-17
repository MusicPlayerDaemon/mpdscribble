// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "Request.hxx"
#include "Handler.hxx"
#include "Global.hxx"
#include "lib/fmt/RuntimeError.hxx"
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
	curl.SetOption(CURLOPT_ERRORBUFFER, error);
	curl.SetFailOnError();

	if (!request_body.empty()) {
		curl.SetOption(CURLOPT_POST, true);
		curl.SetRequestBody(request_body.data(),
				    request_body.size());
	}

	global.Configure(curl);
	global.Add(*this);
}

CurlRequest::~CurlRequest() noexcept
{
	if (curl)
		global.Remove(*this);
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
		throw FmtRuntimeError("CURL failed: {}", error);
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
