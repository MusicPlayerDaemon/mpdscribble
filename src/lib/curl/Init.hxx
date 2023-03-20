// SPDX-License-Identifier: BSD-2-Clause
// author: Max Kellermann <max.kellermann@gmail.com>

#ifndef CURL_INIT_HXX
#define CURL_INIT_HXX

void
CurlInit();

void
CurlDeinit() noexcept;

class ScopeCurlInit {
public:
	ScopeCurlInit() {
		CurlInit();
	}

	~ScopeCurlInit() noexcept {
		CurlDeinit();
	}
};

#endif
