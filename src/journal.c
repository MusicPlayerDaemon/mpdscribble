/*
    This file is part of mpdscribble,
    another audioscrobbler plugin for music player daemon.
    Copyright © 2005,2006 Kuno Woudt <kuno@frob.nl>

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

#include "journal.h"
#include "file.h"
#include "as.h"
#include "misc.h"

#include <glib.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <regex.h>

struct pair {
	char *key;
	char *val;
};

static int file_saved_count = 0;

static void free_pairs(struct pair *p)
{
	free(p->key);
	free(p->val);
	free(p);
}

static struct pair *
make_pair(const char *ptr, int s0, int e0, int s1, int e1)
{
	struct pair *p = g_new(struct pair, 1);

	p->key = g_strndup(ptr + s0, e0 - s0);
	p->val = g_strndup(ptr + s1, e1 - s1);

	return p;
}

static struct pair *get_pair(const char *str)
{
	struct pair *p = NULL;
	regex_t compiled;
	regmatch_t m[4];
	int error = 0;

	if ((error = regcomp(&compiled,
			     "^(#.*|[ \t]*|([A-Za-z_][A-Za-z0-9_]*) = (.*))$",
			     REG_NEWLINE | REG_EXTENDED)))
		fatal("error %i when compiling regexp, this is a bug.\n",
		      error);

	error = regexec(&compiled, str, 4, m, 0);
	if (!error && m[3].rm_eo != -1)
		p = make_pair(str,
			      m[2].rm_so, m[2].rm_eo,
			      m[3].rm_so, m[3].rm_eo);

	regfree(&compiled);

	return p;
}

int journal_write(struct song *sng)
{
	struct song *tmp = sng;
	int count = 0;
	FILE *handle;

	if (!tmp && file_saved_count == 0)
		return -1;

	handle = fopen(file_config.cache, "wb");
	if (!handle) {
		warning_errno("error opening %s", file_config.cache);
		return 0;
	}

	while (tmp) {
		fprintf(handle,
			"# song %i in queue\na = %s\nt = %s\nb = %s\nm = %s\n"
			"i = %s\nl = %i\no = %s\n\n", ++count, tmp->artist,
			tmp->track, tmp->album, tmp->mbid, tmp->time,
			tmp->length, tmp->source);

		tmp = tmp->next;
	}

	fclose(handle);

	file_saved_count = count;
	return count;
}

static void clear_song(struct song *s)
{
	s->artist = NULL;
	s->track = NULL;
	s->album = NULL;
	s->mbid = NULL;
	s->time = NULL;
	s->length = 0;
	s->source = "P";
}

int journal_read(void)
{
	FILE *file;
	char line[1024];
	char *data;
	int count = 0;
	struct pair *p;
	struct song sng;

	file = fopen(file_config.cache, "r");
	if (file == NULL) {
		warning("Failed to open %s: %s",
			file_config.cache, strerror(errno));
		return 0;
	}

	clear_song(&sng);

	while (fgets(line, sizeof(line), file) != NULL) {
		p = get_pair(line);
		if (p == NULL)
			continue;

		if (!strcmp("a", p->key))
			sng.artist = g_strdup(p->val);
		if (!strcmp("t", p->key))
			sng.track = g_strdup(p->val);
		if (!strcmp("b", p->key))
			sng.album = g_strdup(p->val);
		if (!strcmp("m", p->key))
			sng.mbid = g_strdup(p->val);
		if (!strcmp("i", p->key))
			sng.time = g_strdup(p->val);
		if (!strcmp("l", p->key)) {
			sng.length = atoi(p->val);

			as_songchange("", sng.artist, sng.track,
				      sng.album, sng.mbid, sng.length,
				      sng.time);

			count++;

			if (sng.artist) {
				free(sng.artist);
				sng.artist = NULL;
			}
			if (sng.track) {
				free(sng.track);
				sng.track = NULL;
			}
			if (sng.album) {
				free(sng.album);
				sng.album = NULL;
			}
			if (sng.mbid) {
				free(sng.mbid);
				sng.mbid = NULL;
			}
			if (sng.time) {
				free(sng.time);
				sng.time = NULL;
			}

			clear_song(&sng);
		}
		if (strcmp("o", p->key) == 0 && p->val[0] == 'R')
			sng.source = "R";

		free_pairs(p);
	}

	file_saved_count = count;

	free(data);
	return count;
}
