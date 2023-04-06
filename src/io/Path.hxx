// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef IO_PATH_HXX
#define IO_PATH_HXX

#include <string>
#include <string_view>

namespace PathDetail {

#ifdef _WIN32
static constexpr char SEPARATOR = '\\';
#else
static constexpr char SEPARATOR = '/';
#endif

inline void
AppendWithSeparator(std::string &dest, std::string_view src) noexcept
{
	dest.push_back(SEPARATOR);
	dest.append(src);
}

template<typename... Args>
std::string
BuildPath(std::string_view first, Args&&... args) noexcept
{
	constexpr size_t n = sizeof...(args);

	const std::size_t total = first.size() + (args.size() + ...);

	std::string result;
	result.reserve(total + n);

	result.append(first);
	(AppendWithSeparator(result, args), ...);

	return result;
}

} // namespace PathDetail

template<typename... Args>
std::string
BuildPath(std::string_view first, Args&&... args) noexcept
{
	return PathDetail::BuildPath(first, static_cast<std::string_view>(args)...);
}

#endif
