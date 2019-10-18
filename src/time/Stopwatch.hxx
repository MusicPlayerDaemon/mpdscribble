/*
 * Copyright 2019 Max Kellermann <max.kellermann@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
