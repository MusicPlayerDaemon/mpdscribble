// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "Protocol.hxx"

#include <time.h>

std::string
as_timestamp() noexcept
{
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "%ld", (long)time(nullptr));
	return buffer;
}
