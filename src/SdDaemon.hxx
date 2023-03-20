// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef SD_DAEMON_HXX
#define SD_DAEMON_HXX

/* this header provides dummy implementations of some functions in
   libsystemd/sd-daemon.h to reduce the number of #ifdefs */

#include "config.h"
#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#else

static inline void
sd_notify(int, const char *) noexcept
{
}

static constexpr bool
sd_booted() noexcept
{
	return false;
}

#endif

#endif
