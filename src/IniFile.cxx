// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "IniFile.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/BufferedReader.hxx"
#include "io/FileReader.hxx"
#include "util/CharUtil.hxx"
#include "util/ScopeExit.hxx"
#include "util/StringSplit.hxx"
#include "util/StringStrip.hxx"

#include <string_view>

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
	void ParseLine(std::string_view line);

	auto Commit() noexcept {
		return std::move(data);
	}
};

void
IniParser::ParseLine(std::string_view line)
{
	line = StripLeft(line);
	if (line.empty() || line.front() == '#')
		/* ignore empty lines and comments */
		return;

	if (line.front() == '[') {
		/* a new section */

		line = StripLeft(line.substr(1));

		auto [name, rest] = Split(line, ']');
		if (rest.data() == nullptr)
			throw std::runtime_error("Missing ']'");

		name = Strip(name);
		if (!IsValidSectionName(name))
			throw std::runtime_error("Invalid section name");

		if (!StripLeft(rest).empty())
			throw std::runtime_error("Garbage after section");

		auto i = data.emplace(name, IniSection{});
		if (!i.second)
			throw FmtRuntimeError("Duplicate section name: {:?}", name);

		section = i.first;
	} else if (IsValidKeyChar(line.front())) {
		auto [key, value] = Split(line, '=');
		if (value.data() == nullptr)
			throw std::runtime_error("Missing '='");

		key = StripRight(key);
		if (!IsValidKey(key))
			throw std::runtime_error("Invalid key");

		value = Strip(value);

		// TODO: support quoted values

		if (data.empty())
			section = data.emplace(std::string(),
					       IniSection()).first;

		auto i = section->second.emplace(key, value);
		if (!i.second)
			throw FmtRuntimeError("Duplicate key: {:?}", key);
	} else
		throw std::runtime_error("Syntax error");
}

}

static IniFile
ReadIniFile(const char *path, BufferedReader &reader)
{
	IniParser parser;

	while (const char *line = reader.ReadLine()) {
		try {
			parser.ParseLine(line);
		} catch (...) {
			std::throw_with_nested(FmtRuntimeError("Error on {:?} line {}",
							       path, reader.GetLineNumber()));
		}
	}

	return parser.Commit();
}

IniFile
ReadIniFile(const char *path)
{
	FileReader file{path};
	BufferedReader reader{file};

	return ReadIniFile(path, reader);
}
