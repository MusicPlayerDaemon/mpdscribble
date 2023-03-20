// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef DAEMON_HXX
#define DAEMON_HXX

/**
 * Throws on error.
 */
void
daemonize_init(const char *user, const char *pidfile);

void
daemonize_finish() noexcept;

/**
 * Close stdin (fd 0) and re-open it as /dev/null.
 */
void
daemonize_close_stdin() noexcept;

/**
 * Close stdout and stderr and re-open it as /dev/null.
 */
void
daemonize_close_stdout_stderr() noexcept;

/**
 * Throws on error.
 */
void
daemonize_set_user();

/**
 * Daemonize the process: detach it from the parent process and the
 * session.
 *
 * Throws on error.
 */
void
daemonize_detach();

/**
 * Writes the id of the current process to the configured pidfile.
 *
 * Throws on error.
 */
void
daemonize_write_pidfile();

#endif
