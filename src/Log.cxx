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

#include "Log.hxx"
#include "system/Error.hxx"
#include "util/StringStrip.hxx"
#include "config.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_SYSLOG
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
			throw FormatErrno("cannot open %s", path);
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
	if (level > log_threshold)
		return;

#ifdef HAVE_SYSLOG
	if (log_file == nullptr)
		syslog(ToSyslog(level), "%s", msg);
	else
#endif
		fprintf(log_file, "%s %s\n", log_date(), msg);
}

void
LogFormat(LogLevel level, const char *fmt, ...) noexcept
{
	if (level > log_threshold)
		return;

#ifdef HAVE_SYSLOG
	if (log_file == nullptr) {
		va_list ap;
		va_start(ap, fmt);
		vsyslog(ToSyslog(level), fmt, ap);
		va_end(ap);
	} else {
#endif
		char msg[1024];

		{
			va_list ap;
			va_start(ap, fmt);
			vsnprintf(msg, sizeof(msg), fmt, ap);
			va_end(ap);
		}

		fprintf(log_file, "%s %s\n", log_date(), msg);
#ifdef HAVE_SYSLOG
	}
#endif
}
