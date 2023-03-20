// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef CURL_HANDLER_HXX
#define CURL_HANDLER_HXX

#include <exception>
#include <string>

class HttpResponseHandler {
public:
	virtual void OnHttpResponse(std::string body) noexcept = 0;
	virtual void OnHttpError(std::exception_ptr e) noexcept = 0;
};

#endif
