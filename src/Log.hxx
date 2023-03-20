// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef LOG_HXX
#define LOG_HXX

#include "util/Compiler.h"

#include <utility>

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

gcc_printf(2, 3)
void
LogFormat(LogLevel level, const char *fmt, ...) noexcept;

template<typename... Args>
inline void
FormatDebug(const char *fmt, Args&&... args) noexcept
{
	LogFormat(LogLevel::DEBUG, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
inline void
FormatInfo(const char *fmt, Args&&... args) noexcept
{
	LogFormat(LogLevel::INFO, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
inline void
FormatWarning(const char *fmt, Args&&... args) noexcept
{
	LogFormat(LogLevel::WARNING, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
inline void
FormatError(const char *fmt, Args&&... args) noexcept
{
	LogFormat(LogLevel::ERROR, fmt, std::forward<Args>(args)...);
}

#endif
