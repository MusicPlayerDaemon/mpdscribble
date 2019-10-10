/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2019 The Music Player Daemon Project
 * Copyright (C) 2005-2008 Kuno Woudt <kuno@frob.nl>
 * Project homepage: http://musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "Form.hxx"
#include "HttpClient.hxx"

void
add_var_internal(std::string &dest, char sep, const char *key,
		 signed char idx, const char *val) noexcept
{
	dest.push_back(sep);
	dest.append(key);

	if (idx >= 0) {
		char buffer[16];
		snprintf(buffer, sizeof(buffer), "[%i]", idx);
		dest.append(buffer);
	}

	dest.push_back('=');

	if (val != nullptr)
		dest.append(http_client_uri_escape(val));
}
