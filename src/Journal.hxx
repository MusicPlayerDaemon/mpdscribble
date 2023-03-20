// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef JOURNAL_HXX
#define JOURNAL_HXX

#include <list>

struct Record;

bool
journal_write(const char *path, const std::list<Record> &queue);

std::list<Record>
journal_read(const char *path);

#endif
