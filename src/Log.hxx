// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#pragma once

#include <fmt/core.h>

#include <utility>

#ifdef _WIN32
#include <windows.h>
/* damn you, windows.h! */
#ifdef ERROR
#undef ERROR
#endif
#endif

enum class LogLevel {
	DEBUG,
	INFO,
	WARNING,
	ERROR,
};

/**
 * Throws on error.
 */
void
log_init(const char *path, int verbose);

void
log_deinit() noexcept;

const char *
log_date() noexcept;

void
Log(LogLevel level, const char *msg) noexcept;

inline void
LogDebug(const char *msg) noexcept
{
	Log(LogLevel::DEBUG, msg);
}

inline void
LogInfo(const char *msg) noexcept
{
	Log(LogLevel::INFO, msg);
}

void
LogVFmt(LogLevel level, fmt::string_view format_str, fmt::format_args args) noexcept;

template<typename S, typename... Args>
inline void
FmtDebug(const S &format_str, Args&&... args) noexcept
{
	LogVFmt(LogLevel::DEBUG, format_str, fmt::make_format_args(args...));
}

template<typename S, typename... Args>
inline void
FmtInfo(const S &format_str, Args&&... args) noexcept
{
	LogVFmt(LogLevel::INFO, format_str, fmt::make_format_args(args...));
}

template<typename S, typename... Args>
inline void
FmtWarning(const S &format_str, Args&&... args) noexcept
{
	LogVFmt(LogLevel::WARNING, format_str, fmt::make_format_args(args...));
}

template<typename S, typename... Args>
inline void
FmtError(const S &format_str, Args&&... args) noexcept
{
	LogVFmt(LogLevel::ERROR, format_str, fmt::make_format_args(args...));
}
