// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef CURL_ESCAPE_HXX
#define CURL_ESCAPE_HXX

#include "String.hxx"

#include <string>
#include <string_view>

/**
 * Escapes URI parameters with '%'.
 */
inline CurlString
CurlEscape(std::string_view src) noexcept
{
	return CurlString{curl_escape(src.data(), src.size())};
}

#endif
