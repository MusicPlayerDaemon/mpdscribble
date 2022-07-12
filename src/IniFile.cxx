/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2019 The Music Player Daemon Project
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

#include "IniFile.hxx"
#include "system/Error.hxx"
#include "util/CharUtil.hxx"
#include "util/RuntimeError.hxx"
#include "util/ScopeExit.hxx"
#include "util/StringStrip.hxx"

#include <string_view>

#include <stdio.h>

static constexpr bool
IsValidSectionNameChar(char ch) noexcept
{
	return IsAlphaNumericASCII(ch) || ch == '_' || ch == '-' || ch == '.';
}

[[gnu::pure]]
static bool
IsValidSectionName(std::string_view name) noexcept
{
	if (name.empty())
		return false;

	for (char ch : name)
		if (!IsValidSectionNameChar(ch))
			return false;

	return true;
}

static constexpr bool
IsValidKeyChar(char ch) noexcept
{
	return IsAlphaNumericASCII(ch) || ch == '_';
}

[[gnu::pure]]
static bool
IsValidKey(std::string_view name) noexcept
{
	if (name.empty())
		return false;

	for (char ch : name)
		if (!IsValidKeyChar(ch))
			return false;

	return true;
}

namespace {

class IniParser {
	IniFile data;

	IniFile::iterator section;

public:
	void ParseLine(char *line);

	auto Commit() noexcept {
		return std::move(data);
	}
};

void
IniParser::ParseLine(char *line)
{
	line = Strip(line);
	if (*line == 0 || *line == '#')
		/* ignore empty lines and comments */
		return;

	if (*line == '[') {
		/* a new section */

		line = StripLeft(line + 1);
		char *end = strchr(line, ']');
		if (end == nullptr)
			throw std::runtime_error("Missing ']'");

		std::string_view name{line, std::size_t(end - line)};
		name = StripRight(name);

		if (!IsValidSectionName(name))
			throw std::runtime_error("Invalid section name");

		line = end + 1;
		if (*line != 0)
			throw std::runtime_error("Garbage after section");

		auto i = data.emplace(name, IniSection{});
		if (!i.second)
			throw FormatRuntimeError("Duplicate section name: %.*s",
						 int(name.size()), name.data());

		section = i.first;
	} else if (IsValidKeyChar(*line)) {
		char *eq = strchr(line, '=');
		if (eq == nullptr)
			throw std::runtime_error("Missing '='");

		std::string_view key{line, std::size_t(eq - line)};
		key = StripRight(key);

		if (!IsValidKey(key))
			throw std::runtime_error("Invalid key");

		std::string_view value{eq + 1};
		value = StripLeft(value);

		// TODO: support quoted values

		if (data.empty())
			section = data.emplace(std::string(),
					       IniSection()).first;

		auto i = section->second.emplace(key, value);
		if (!i.second)
			throw FormatRuntimeError("Duplicate key: %.*s",
						 int(key.size()), key.data());
	} else
		throw std::runtime_error("Syntax error");
}

}

static IniFile
ReadIniFile(const char *path, FILE *file)
{
	IniParser parser;

	unsigned no = 1;
	char buffer[4096];
	while (auto line = fgets(buffer, sizeof(buffer), file)) {
		try {
			parser.ParseLine(line);
		} catch (...) {
			std::throw_with_nested(FormatRuntimeError("Error on %s line %u",
								  path, no));
		}

		++no;
	}

	return parser.Commit();
}

IniFile
ReadIniFile(const char *path)
{
	FILE *file = fopen(path, "r");
	if (file == nullptr)
		throw FormatErrno("Failed to open %s", path);

	AtScopeExit(file) { fclose(file); };

	return ReadIniFile(path, file);
}
