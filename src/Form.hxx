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

#ifndef FORM_HXX
#define FORM_HXX

#include <string>

void
add_var_internal(std::string &dest, char sep, const char *key,
		 signed char idx, const char *val) noexcept;

inline void
first_var(std::string &s, const char *key, const char *val)
{
	add_var_internal(s, '?', key, -1, val);
}

inline void
add_var(std::string &s, const char *key, const char *val)
{
	add_var_internal(s, '&', key, -1, val);
}

inline void
add_var(std::string &s, const char *key, const std::string &val)
{
	add_var(s, key, val.c_str());
}

inline void
add_var_i(std::string &s, const char *key, signed char idx, const char *val)
{
	add_var_internal(s, '&', key, idx, val);
}

inline void
add_var_i(std::string &s, const char *key, signed char idx,
	  const std::string &val)
{
	add_var_i(s, key, idx, val.c_str());
}

#endif
