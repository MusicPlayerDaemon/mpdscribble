// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "Form.hxx"
#include "lib/curl/Escape.hxx"

#include <fmt/format.h>

void
FormDataBuilder::AppendVerbatim(unsigned value) noexcept
{
	AppendVerbatim(fmt::format_int{value}.c_str());
}

void
FormDataBuilder::AppendEscape(std::string_view value) noexcept
{
	if (auto e = CurlEscape(value))
		s.append(e);
}
