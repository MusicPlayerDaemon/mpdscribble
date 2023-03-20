// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef CMDLINE_HXX
#define CMDLINE_HXX

struct Config;

void
parse_cmdline(Config &config, int argc, char **argv);

#endif
