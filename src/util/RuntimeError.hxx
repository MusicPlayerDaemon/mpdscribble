// SPDX-License-Identifier: BSD-2-Clause
// author: Max Kellermann <max.kellermann@gmail.com>

#ifndef RUNTIME_ERROR_HXX
#define RUNTIME_ERROR_HXX

#include <stdexcept> // IWYU pragma: export
#include <utility>

#include <stdio.h>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
// TODO: fix this warning properly
#pragma GCC diagnostic ignored "-Wformat-security"
#endif

template<typename... Args>
static inline std::runtime_error
FormatRuntimeError(const char *fmt, Args&&... args) noexcept
{
	char buffer[1024];
	snprintf(buffer, sizeof(buffer), fmt, std::forward<Args>(args)...);
	return std::runtime_error(buffer);
}

template<typename... Args>
inline std::invalid_argument
FormatInvalidArgument(const char *fmt, Args&&... args) noexcept
{
	char buffer[1024];
	snprintf(buffer, sizeof(buffer), fmt, std::forward<Args>(args)...);
	return std::invalid_argument(buffer);
}

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif
