// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "Log.hxx"
#include "lib/fmt/SystemError.hxx"
#include "util/Compiler.h"
#include "util/StringStrip.hxx"
#include "config.h"

#include <cassert>

#include <string.h>
#include <errno.h>
#include <time.h>

#ifdef HAVE_SYSLOG
#include <fmt/format.h> // for fmt::memory_buffer
#include <iterator> // for std::back_inserter
#include <syslog.h>
#endif

static LogLevel log_threshold = LogLevel::INFO;

static FILE *log_file;

const char *
log_date() noexcept
{
	static char buf[32];
	time_t t;
	struct tm *tmp;

	t = time(nullptr);
	tmp = localtime(&t);
	if (tmp == nullptr) {
		buf[0] = 0;
		return buf;
	}

	if (!strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", tmp)) {
		buf[0] = 0;
		return buf;
	}
	return buf;
}

static void
log_init_file(const char *path)
{
	assert(path != nullptr);
	assert(log_file == nullptr);

	if (strcmp(path, "-") == 0) {
		log_file = stderr;
	} else {
		log_file = fopen(path, "ab");
		if (log_file == nullptr)
			throw FmtErrno("cannot open {:?}", path);
	}

	setvbuf(log_file, nullptr, _IONBF, 0);
}

#ifdef HAVE_SYSLOG

static constexpr int
ToSyslog(LogLevel log_level) noexcept
{
	switch (log_level) {
	case LogLevel::ERROR:
		return LOG_ERR;

	case LogLevel::WARNING:
		return LOG_WARNING;

	case LogLevel::INFO:
		return LOG_INFO;

	case LogLevel::DEBUG:
		return LOG_DEBUG;
	}

	gcc_unreachable();
}

static void
log_init_syslog() noexcept
{
	assert(log_file == nullptr);

	openlog(PACKAGE, 0, LOG_DAEMON);
}

#endif

void
log_init(const char *path, int verbose)
{
	assert(path != nullptr);
	assert(verbose >= 0);
	assert(log_file == nullptr);

	if (verbose == 0)
		log_threshold = LogLevel::ERROR;
	else if (verbose == 1)
		log_threshold = LogLevel::WARNING;
	else if (verbose == 2)
		log_threshold = LogLevel::INFO;
	else
		log_threshold = LogLevel::DEBUG;

#ifdef HAVE_SYSLOG
	if (strcmp(path, "syslog") == 0)
		log_init_syslog();
	else
#endif
		log_init_file(path);
}

void
log_deinit() noexcept
{
#ifndef HAVE_SYSLOG
	assert(log_file != nullptr);

#else
	if (log_file == nullptr)
		closelog();
	else
#endif
		fclose(log_file);
}

void
Log(LogLevel level, const char *msg) noexcept
{
	if (level < log_threshold)
		return;

#ifdef HAVE_SYSLOG
	if (log_file == nullptr)
		syslog(ToSyslog(level), "%s", msg);
	else
#endif
		fmt::print(log_file, "{} {}\n", log_date(), msg);
}

void
LogVFmt(LogLevel level, fmt::string_view format_str, fmt::format_args args) noexcept
{
	if (level < log_threshold)
		return;

#ifdef HAVE_SYSLOG
	if (log_file == nullptr) {
		fmt::memory_buffer buffer;
		fmt::vformat_to(std::back_inserter(buffer), format_str, args);
		syslog(ToSyslog(level), "%.*s", (int)buffer.size(), buffer.data());
	} else {
#endif
		fmt::vprint(log_file, format_str, args);
#ifdef HAVE_SYSLOG
	}
#endif
}
