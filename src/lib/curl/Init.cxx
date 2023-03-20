// SPDX-License-Identifier: BSD-2-Clause
// author: Max Kellermann <max.kellermann@gmail.com>

#include "Init.hxx"
#include "Error.hxx"

#include <curl/curl.h>

void
CurlInit()
{
	CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
	if (code != CURLE_OK)
		throw Curl::MakeError(code, "CURL initialization failed");
}

void
CurlDeinit() noexcept
{
	curl_global_cleanup();
}
