// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "Form.hxx"
#include "lib/curl/Escape.hxx"

#include <stdio.h>

void
FormDataBuilder::AppendVerbatim(unsigned value) noexcept
{
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "%u", value);
	AppendVerbatim(buffer);
}

void
FormDataBuilder::AppendEscape(std::string_view value) noexcept
{
	if (auto e = CurlEscape(value))
		s.append(e);
}
