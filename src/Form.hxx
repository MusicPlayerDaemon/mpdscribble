// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef FORM_HXX
#define FORM_HXX

#include <string>

class FormDataBuilder {
	std::string s;

	enum class Separator {
		NONE,
		QUESTION_MARK,
		AMPERSAND,
	} separator = Separator::NONE;

public:
	FormDataBuilder() = default;

	template<typename S>
	FormDataBuilder(S &&_s) noexcept
		:s(std::forward<S>(_s)) {
		if (!s.empty())
			separator = s.find('?') == s.npos
				? Separator::QUESTION_MARK
				: Separator::AMPERSAND;
	}

	const char *c_str() const noexcept {
		return s.c_str();
	}

	operator std::string &&() && noexcept {
		return std::move(s);
	}

	template<typename K, typename V>
	void Append(K &&key, V &&value) noexcept {
		AppendSeparator();

		AppendVerbatim(std::forward<K>(key));
		s.push_back('=');
		AppendEscape(std::forward<V>(value));
	}

	template<typename K, typename V>
	void AppendIndexed(K &&key, unsigned idx, V &&value) noexcept {
		AppendSeparator();

		AppendVerbatim(std::forward<K>(key));
		s.push_back('[');
		AppendVerbatim(idx);
		s.push_back(']');
		s.push_back('=');
		AppendEscape(std::forward<V>(value));
	}

private:
	void AppendSeparator() noexcept {
		switch (separator) {
		case Separator::NONE:
			break;

		case Separator::QUESTION_MARK:
			s.push_back('?');
			break;

		case Separator::AMPERSAND:
			s.push_back('&');
			break;
		}

		separator = Separator::AMPERSAND;
	}

	template<typename T>
	void AppendVerbatim(T &&value) noexcept {
		s.append(std::forward<T>(value));
	}

	void AppendVerbatim(unsigned value) noexcept;

	void AppendEscape(std::string_view value) noexcept;

	void AppendEscape(unsigned value) noexcept {
		AppendVerbatim(value);
	}
};

#endif
