// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "XdgBaseDirectory.hxx"
#include "io/Path.hxx"

#include <stdlib.h>

[[gnu::const]]
static const char *
GetUserDirectory() noexcept
{
	return getenv("HOME");
}

[[gnu::const]]
static std::string
GetUserConfigDirectory() noexcept
{
	const char *config_home = getenv("XDG_CONFIG_HOME");
	if (config_home != nullptr && *config_home != 0)
		return config_home;

	const char *home = GetUserDirectory();
	if (home != nullptr)
		return BuildPath(home, ".config");

	return {};
}

std::string
GetUserConfigDirectory(std::string_view package) noexcept
{
	const auto dir = GetUserConfigDirectory();
	if (dir.empty())
		return {};

	return BuildPath(dir, package);
}

[[gnu::const]]
static std::string
GetUserCacheDirectory() noexcept
{
	const char *cache_home = getenv("XDG_CACHE_HOME");
	if (cache_home != nullptr && *cache_home != 0)
		return cache_home;

	const char *home = GetUserDirectory();
	if (home != nullptr)
		return BuildPath(home, ".cache");

	return {};
}

std::string
GetUserCacheDirectory(std::string_view package) noexcept
{
	const auto dir = GetUserCacheDirectory();
	if (dir.empty())
		return {};

	return BuildPath(dir, package);
}
