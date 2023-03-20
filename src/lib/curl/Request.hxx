// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef CURL_REQUEST_HXX
#define CURL_REQUEST_HXX

#include "Easy.hxx"

#include <string>

class CurlGlobal;
class HttpResponseHandler;

/**
 * A non-blocking HTTP request integrated via #CurlGlobal into the
 * #EventLoop.
 */
class CurlRequest final
{
	CurlGlobal &global;

	HttpResponseHandler &handler;

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
		    HttpResponseHandler &_handler);
	~CurlRequest() noexcept;

	CURL *Get() noexcept {
		return curl.Get();
	}

	/**
	 * A HTTP request is finished: invoke its callback and free it.
	 */
	void Done(CURLcode result) noexcept;

private:
	void CheckResponse(CURLcode result);

	/**
	 * Called by curl when new data is available.
	 */
	static size_t WriteFunction(char *ptr, size_t size, size_t nmemb,
				    void *stream) noexcept;

};

#endif
