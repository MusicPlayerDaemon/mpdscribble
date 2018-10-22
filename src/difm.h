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
 * mpdcribble (MPD Client) DI.FM Patch (based on FM4 Patch)
 * Copyright DI.FM patch (C) 2018 AmiGO <amigo.elite@gmail.com>
 * Copyright FM4 patch (C) 2011 zapster <oss@zapster.cc>
 *
 * This fork of the original mpdscribble aims to detect streams of the
 * DI.FM readio station, correct the scrambled tag and thus enable
 * mpdscribbler to send the correct infos to last.fm/libre.fm.
 *
 * DI.FM is a very popular electronic radio with a subscription model.
 *
 * http://di.fm/
*/

#ifndef DIFM_H
#define DIFM_H

#include <mpd/client.h>

#include <stdbool.h>

bool
difm_is_difm_stream(struct mpd_song *song);

bool
difm_parse_stream(struct mpd_song *song);

#endif /* DIFM_H */
