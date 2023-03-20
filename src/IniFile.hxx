// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef INI_FILE_HXX
#define INI_FILE_HXX

#include <map>
#include <string>

using IniSection = std::map<std::string, std::string>;
using IniFile = std::map<std::string, IniSection>;

IniFile
ReadIniFile(const char *path);

#endif
