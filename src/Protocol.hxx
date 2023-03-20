// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef PROTOCOL_HXX
#define PROTOCOL_HXX

#include "config.h"

#include <string>

#define AS_CLIENT_ID "mdc"
#define AS_CLIENT_VERSION VERSION

std::string
as_timestamp() noexcept;

#endif
