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

*/

#ifndef MISC_H
#define MISC_H

#include <stdlib.h>

long now (void);

void set_logfile (FILE *, int verbose);

void fatal (const char *template, ...);
void fatal_errno (const char *template, ...);

void warning (const char *template, ...);
void warning_errno (const char *template, ...);

void notice (const char *template, ...);

char *strdup2 (const char *const s);
char *strndup2 (const char *const s, const size_t size);

char *concat (const char *str, ...);

/* concat originally from gnu libc manual,
   this version free()s everything submitted to it. */
char *concatDX (char *str, ...);

#endif /* MISC_H */
