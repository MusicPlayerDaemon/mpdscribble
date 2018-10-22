/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2010 The Music Player Daemon Project
 * Copyright (C) 2005-2008 Kuno Woudt <kuno@frob.nl>
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

/*
 * mpdcribble (MPD Client) FM4 Patch
 * Copyright FM4 patch (C) 2011 zapster <oss@zapster.cc>
 *
 * This fork of the original mpdscribble aims to detect streams of the
 * FM4 readio station, correct the scrambled tag and thus enable
 * mpdscribbler to send the correct infos to last.fm/libre.fm.
 *
 * FM4 is a radio station at the Austrian Broadcasting Corporation (ORF).
 * There are tags embedded in the official stream but unfortunatally
 * all information is encoded in the title tag.
 *
 * http://fm4.orf.at (german/english)
*/

#ifndef FM4_H
#define FM4_H

#include <mpd/client.h>

#include <stdbool.h>

bool
fm4_is_fm4_stream(struct mpd_song *song);

bool
fm4_parse_stream(struct mpd_song *song);

#endif /* FM4_H */
