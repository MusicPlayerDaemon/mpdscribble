/*
    This file is part of mpdscribble.
    another audioscrobbler plugin for music player daemon.
    Copyright © 2005 Kuno Woudt <kuno@frob.nl>

    mpdscribble is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    mpdscribble is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with mpdscribble; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


    in case you're wondering, lmc is short for libmpdclient. :)
*/

#ifndef LMC_H
#define LMC_H

#include <libmpdclient.h>
#include <stdio.h>

void lmc_connect (char *host, int port);
void lmc_disconnect (void);
int lmc_current (struct mpd_song *);

#endif /* LMC_H */
