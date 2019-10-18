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

#include "Daemon.hxx"
#include "system/Error.hxx"
#include "util/RuntimeError.hxx"
#include "Log.hxx"

#include <glib.h>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <pwd.h>
#endif

#ifndef _WIN32

/** the Unix user name which MPD runs as */
static char *user_name;

/** the Unix user id which MPD runs as */
static uid_t user_uid;

/** the Unix group id which MPD runs as */
static gid_t user_gid;

/** the absolute path of the pidfile */
static char *pidfile;

#endif

void
daemonize_close_stdin() noexcept
{
#ifndef _WIN32
	int fd = open("/dev/null", O_RDONLY);

	if (fd < 0)
		close(STDIN_FILENO);
	else if (fd != STDIN_FILENO) {
		dup2(fd, STDIN_FILENO);
		close(fd);
	}
#endif
}

void
daemonize_close_stdout_stderr() noexcept
{
#ifndef _WIN32
	int fd = open("/dev/null", O_WRONLY);

	if (fd >= 0) {
		if (fd != STDOUT_FILENO)
			dup2(fd, STDOUT_FILENO);
		if (fd != STDERR_FILENO)
			dup2(fd, STDERR_FILENO);
		if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
			close(fd);
	} else {
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
	}
#endif
}

void
daemonize_set_user()
{
#ifndef _WIN32
	if (user_name == nullptr)
		return;

	/* get uid */
	if (setgid(user_gid) == -1)
		throw FormatErrno("cannot setgid for user \"%s\"",
				  user_name);

#ifdef _BSD_SOURCE
	/* init suplementary groups
	 * (must be done before we change our uid)
	 */
	if (initgroups(user_name, user_gid) == -1)
		FormatErrno("cannot init supplementary groups "
			    "of user \"%s\": %s",
			    user_name, strerror(errno));
#endif

	/* set uid */
	if (setuid(user_uid) == -1)
		throw FormatErrno("cannot change to uid of user \"%s\"",
				  user_name);
#endif
}

void
daemonize_detach()
{
#ifndef _WIN32
	int ret;

	/* detach from parent process */

	ret = fork();
	if (ret < 0)
		throw MakeErrno("fork() failed");

	if (ret > 0)
		/* exit the parent process */
		_exit(EXIT_SUCCESS);

	/* release the current working directory */

	ret = chdir("/");
	if (ret < 0)
		throw MakeErrno("chdir() failed");

	/* detach from the current session */

	setsid();
#endif
}

void
daemonize_write_pidfile()
{
#ifndef _WIN32
	FILE *file;

	if (pidfile == nullptr)
		return;

	unlink(pidfile);

	file = fopen(pidfile, "w");
	if (file == nullptr)
		throw FormatErrno("Failed to create pidfile %s", pidfile);

	fprintf(file, "%d\n", getpid());
	fclose(file);
#endif
}

void
daemonize_init(const char *user, const char *_pidfile)
{
#ifndef _WIN32
	if (user != nullptr && strcmp(user, g_get_user_name()) != 0) {
		struct passwd *pwd;

		user_name = g_strdup(user);

		pwd = getpwnam(user_name);
		if (pwd == nullptr)
			throw FormatRuntimeError("no such user \"%s\"",
						 user_name);

		user_uid = pwd->pw_uid;
		user_gid = pwd->pw_gid;
	}

	pidfile = g_strdup(_pidfile);
#else
	(void)user;
	(void)_pidfile;
#endif
}

void
daemonize_finish() noexcept
{
#ifndef _WIN32
	if (pidfile != nullptr)
		unlink(pidfile);

	g_free(user_name);
	g_free(pidfile);
#endif
}
