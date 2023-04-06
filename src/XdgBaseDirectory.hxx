// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#pragma once

#include <string>
#include <string_view>

/**
 * Returns the absolute path of the XDG config directory for the
 * specified package (or an empty string on error).
 */
[[gnu::pure]]
std::string
GetUserConfigDirectory(std::string_view package) noexcept;

/**
 * Returns the absolute path of the XDG cache directory for the
 * specified package (or an empty string on error).
 */
[[gnu::pure]]
std::string
GetUserCacheDirectory(std::string_view package) noexcept;
