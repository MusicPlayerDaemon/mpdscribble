/*
    This file is part of mpdscribble
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

*/

#ifndef AS_H
#define AS_H

#include <stdio.h>

#define AS_CLIENT_ID "mdc"
#define AS_CLIENT_VERSION VERSION

struct song
{
  char *artist;
  char *track;
  char *album;
  char *mbid;
  char *time;
  int length;
  struct song *next;
};

void as_init (unsigned int seconds);
void as_poll (void);
void as_cleanup (void);

void
as_now_playing(const char *artist, const char *track,
               const char *album, const char *mbid, const int length);

int as_songchange (const char *file, const char *artist, const char *track,
                   const char *album, const char *mbid, const int length,
                   const char *time);

unsigned int as_sleep (void);
void as_save_cache (void);


#endif /* AS_H */
