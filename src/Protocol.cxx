// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "Protocol.hxx"

#include <fmt/core.h>

#include <time.h>

std::string
as_timestamp() noexcept
{
	return fmt::format("{}", time(nullptr));
}
