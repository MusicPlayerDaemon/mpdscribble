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

#include <regex.h>

struct pair {
	char *key;
	char *val;
	struct pair *next;
};

static int file_saved_count = 0;

static void free_pairs(struct pair *p)
{
	struct pair *n;

	if (!p)
		return;

	do {
		n = p->next;
		free(p->key);
		free(p->val);
		free(p);
		p = n;
	}
	while (p);
}

static void
add_pair(struct pair **stack, const char *ptr, int s0, int e0, int s1, int e1)
{
	struct pair *p = g_new(struct pair, 1);
	struct pair *last;

	p->key = g_strndup(ptr + s0, e0 - s0);
	p->val = g_strndup(ptr + s1, e1 - s1);
	p->next = NULL;

	if (!*stack) {
		*stack = p;
		return;
	}

	last = *stack;
	while (last->next)
		last = last->next;
	last->next = p;
}

static struct pair *get_pair(const char *str)
{
	struct pair *p = NULL;
	const char *ptr;
	regex_t compiled;
	regmatch_t m[4];
	int error = 0;

	if ((error = regcomp(&compiled,
			     "^(#.*|[ \t]*|([A-Za-z_][A-Za-z0-9_]*) = (.*))$",
			     REG_NEWLINE | REG_EXTENDED)))
		fatal("error %i when compiling regexp, this is a bug.\n",
		      error);

	m[0].rm_eo = 0;
	ptr = str - 1;
	do {
		ptr += m[0].rm_eo + 1;
		if (*ptr == '\0') {
			break;
		}
		error = regexec(&compiled, ptr, 4, m, 0);
		if (!error && m[3].rm_eo != -1)
			add_pair(&p, ptr,
				 m[2].rm_so, m[2].rm_eo,
				 m[3].rm_so, m[3].rm_eo);
	}
	while (!error);

	regfree(&compiled);

	return p;
}

static char *read_file(const char *filename)
{
	bool ret;
	gchar *contents;
	GError *error = NULL;

	ret = g_file_get_contents(filename, &contents, NULL, &error);
	if (!ret) {
		warning("%s", error->message);
		g_error_free(error);
		return NULL;
	}

	return contents;
}

int file_write_cache(struct song *sng)
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

int file_read_cache(void)
{
	char *data;
	int count = 0;

	if ((data = read_file(file_config.cache))) {
		struct pair *root = get_pair(data);
		struct pair *p = root;
		struct song sng;

		clear_song(&sng);

		while (p) {
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

			p = p->next;
		}

		free_pairs(p);
	}

	file_saved_count = count;

	free(data);
	return count;
}
