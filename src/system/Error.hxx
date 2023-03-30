// SPDX-License-Identifier: BSD-2-Clause
// author: Max Kellermann <max.kellermann@gmail.com>

#pragma once

#include <system_error> // IWYU pragma: export
#include <utility>

#include <stdio.h>

template<typename... Args>
static inline std::system_error
FormatSystemError(std::error_code code, const char *fmt,
		  Args&&... args) noexcept
{
	char buffer[1024];
	snprintf(buffer, sizeof(buffer), fmt, std::forward<Args>(args)...);
	return std::system_error(code, buffer);
}

#ifdef _WIN32

#include <Windows.h>

/**
 * Returns the error_category to be used to wrap WIN32 GetLastError()
 * values.  The C++ standard does not define this well, and this value
 * is mostly guessed.
 *
 * TODO: verify
 */
[[gnu::const]]
static inline const std::error_category &
LastErrorCategory() noexcept
{
	return std::system_category();
}

[[gnu::pure]]
inline bool
IsLastError(const std::system_error &e, DWORD code) noexcept
{
	return e.code().category() == LastErrorCategory() &&
		(DWORD)e.code().value() == code;
}

static inline std::system_error
MakeLastError(DWORD code, const char *msg) noexcept
{
	return std::system_error(std::error_code(code, LastErrorCategory()),
				 msg);
}

static inline std::system_error
MakeLastError(const char *msg) noexcept
{
	return MakeLastError(GetLastError(), msg);
}

template<typename... Args>
static inline std::system_error
FormatLastError(DWORD code, const char *fmt, Args&&... args) noexcept
{
	char buffer[512];
	const auto end = buffer + sizeof(buffer);
	constexpr std::size_t max_prefix = sizeof(buffer) - 128;
	size_t length = snprintf(buffer, max_prefix,
				 fmt, std::forward<Args>(args)...);
	if (length >= max_prefix)
		length = max_prefix - 1;
	char *p = buffer + length;
	*p++ = ':';
	*p++ = ' ';

	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
		       FORMAT_MESSAGE_IGNORE_INSERTS,
		       nullptr, code, 0, p, end - p, nullptr);
	return MakeLastError(code, buffer);
}

template<typename... Args>
static inline std::system_error
FormatLastError(const char *fmt, Args&&... args) noexcept
{
	return FormatLastError(GetLastError(), fmt,
			       std::forward<Args>(args)...);
}

#endif /* _WIN32 */

#include <cerrno> // IWYU pragma: export
#include <string.h>

/**
 * Returns the error_category to be used to wrap errno values.  The
 * C++ standard does not define this well, so this code is based on
 * observations what C++ standard library implementations actually
 * use.
 *
 * @see https://stackoverflow.com/questions/28746372/system-error-categories-and-standard-system-error-codes
 */
[[gnu::const]]
static inline const std::error_category &
ErrnoCategory() noexcept
{
#ifdef _WIN32
	/* on Windows, the generic_category() is used for errno
	   values */
	return std::generic_category();
#else
	/* on POSIX, system_category() appears to be the best
	   choice */
	return std::system_category();
#endif
}

static inline std::system_error
MakeErrno(int code, const char *msg) noexcept
{
	return std::system_error(std::error_code(code, ErrnoCategory()),
				 msg);
}

static inline std::system_error
MakeErrno(const char *msg) noexcept
{
	return MakeErrno(errno, msg);
}

template<typename... Args>
static inline std::system_error
FormatErrno(int code, const char *fmt, Args&&... args) noexcept
{
	char buffer[512];
	snprintf(buffer, sizeof(buffer),
		 fmt, std::forward<Args>(args)...);
	return MakeErrno(code, buffer);
}

template<typename... Args>
static inline std::system_error
FormatErrno(const char *fmt, Args&&... args) noexcept
{
	return FormatErrno(errno, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
static inline std::system_error
FormatFileNotFound(const char *fmt, Args&&... args) noexcept
{
#ifdef _WIN32
	return FormatLastError(ERROR_FILE_NOT_FOUND, fmt,
			       std::forward<Args>(args)...);
#else
	return FormatErrno(ENOENT, fmt, std::forward<Args>(args)...);
#endif
}

[[gnu::pure]]
inline bool
IsErrno(const std::system_error &e, int code) noexcept
{
	return e.code().category() == ErrnoCategory() &&
		e.code().value() == code;
}

[[gnu::pure]]
static inline bool
IsFileNotFound(const std::system_error &e) noexcept
{
#ifdef _WIN32
	return IsLastError(e, ERROR_FILE_NOT_FOUND);
#else
	return IsErrno(e, ENOENT);
#endif
}

[[gnu::pure]]
static inline bool
IsPathNotFound(const std::system_error &e) noexcept
{
#ifdef _WIN32
	return IsLastError(e, ERROR_PATH_NOT_FOUND);
#else
	return IsErrno(e, ENOTDIR);
#endif
}

[[gnu::pure]]
static inline bool
IsAccessDenied(const std::system_error &e) noexcept
{
#ifdef _WIN32
	return IsLastError(e, ERROR_ACCESS_DENIED);
#else
	return IsErrno(e, EACCES);
#endif
}
