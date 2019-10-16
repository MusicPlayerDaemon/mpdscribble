/* ncmpc (Ncurses MPD Client)
 * (c) 2004-2019 The Music Player Daemon Project
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

#ifndef NCMPC_ASIO_SERVICE_FWD_HXX
#define NCMPC_ASIO_SERVICE_FWD_HXX

/* This header provides a forward declaration for
   boost::asio::io_service */

#include <boost/version.hpp>

#if BOOST_VERSION >= 106600

/* in Boost 1.66, the API has changed for "Networking TS
   compatibility"; the forward declaration above doesn't work because
   boost::asio::io_service is a deprecated typedef to
   boost::asio::io_context; eventually, we'll switch to the new API,
   but this would require dropping support for older Boost versions */

#include <boost/asio/io_service.hpp> // IWYU pragma: export

#else
namespace boost { namespace asio { class io_service; }}
#endif

#endif
