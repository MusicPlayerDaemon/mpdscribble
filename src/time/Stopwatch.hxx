// SPDX-License-Identifier: BSD-2-Clause
// author: Max Kellermann <max.kellermann@gmail.com>

#ifndef STOPWATCH_HXX
#define STOPWATCH_HXX

#include <chrono>

class Stopwatch {
	std::chrono::steady_clock::duration duration{};

	std::chrono::steady_clock::time_point start{};

public:
	constexpr bool IsRunning() const noexcept {
		return start > std::chrono::steady_clock::time_point{};
	}

	auto GetDuration() const noexcept {
		auto result = duration;
		if (IsRunning())
			result += std::chrono::steady_clock::now() - start;
		return result;
	}

	void Start() noexcept {
		duration = {};
		start = std::chrono::steady_clock::now();
	}

	void Resume() noexcept {
		if (!IsRunning())
			start = std::chrono::steady_clock::now();
	}

	void Stop() noexcept {
		if (IsRunning()) {
			duration += std::chrono::steady_clock::now() - start;
			start = {};
		}
	}
};

#endif
