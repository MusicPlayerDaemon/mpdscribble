/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2019 The Music Player Daemon Project
 * Copyright (C) 2005-2008 Kuno Woudt <kuno@frob.nl>
 * Project homepage: http://musicpd.org
 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

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
