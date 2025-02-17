// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "Daemon.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/fmt/SystemError.hxx"
#include "system/Error.hxx"
#include "Log.hxx"

#ifndef _WIN32
#include <fmt/core.h>

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
		throw FmtErrno("cannot setgid for user {:?}", user_name);

#ifdef _BSD_SOURCE
	/* init suplementary groups
	 * (must be done before we change our uid)
	 */
	if (initgroups(user_name, user_gid) == -1)
		FmtErrno("cannot init supplementary groups of user {:?}",
			 user_name);
#endif

	/* set uid */
	if (setuid(user_uid) == -1)
		throw FmtErrno("cannot change to uid of user {:?}",
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
		throw FmtErrno("Failed to create pidfile {:?}", pidfile);

	fmt::print(file, "{}\n", getpid());
	fclose(file);
#endif
}

void
daemonize_init(const char *user, const char *_pidfile)
{
#ifndef _WIN32
	if (user != nullptr) {
		const auto *pwd = getpwnam(user);
		if (pwd == nullptr)
			throw FmtRuntimeError("no such user {:?}", user);

		user_uid = pwd->pw_uid;
		user_gid = pwd->pw_gid;

		if (user_uid != getuid())
			user_name = strdup(user);
	}

	if (_pidfile != nullptr)
		pidfile = strdup(_pidfile);
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

	free(user_name);
	free(pidfile);
#endif
}
