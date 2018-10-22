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

#include "fm4.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>

#include <glib.h>
#include <mpd/tag.h>

#define FM4_THRESHOLD 10

/*
 * Hack libmpdclient
 *
 * Unfortunatally we can not modify mpd_song
 * (add/remove tags) using libmpdclient. The
 * following code is borrowed from libmpdclient
 * (src/song.c)
*/
static struct mpd_tag_value {
	struct mpd_tag_value *next;

	char *value;
};

static struct mpd_song_hack {
	char *uri;

	struct mpd_tag_value tags[MPD_TAG_COUNT];

	/* ... */
};

/**
 * Removes all values of the specified tag.
 */
static void
mpd_song_clear_tag(struct mpd_song_hack *song, enum mpd_tag_type type)
{
	struct mpd_tag_value *tag = &song->tags[type];

	if ((unsigned)type >= MPD_TAG_COUNT)
		return;

	if (tag->value == NULL)
		/* this tag type is empty */
		return;

	/* free and clear the first value */
	free(tag->value);
	tag->value = NULL;

	/* free all other values; no need to clear the "next" pointer,
	   because it is "undefined" as long as value==NULL */
	while ((tag = tag->next) != NULL)
		free(tag->value);
}

/**
 * Adds a tag value to the song.
 *
 * @return true on success, false if the tag is not supported or if no
 * memory could be allocated
 */
static bool
mpd_song_add_tag(struct mpd_song_hack *song,
                 enum mpd_tag_type type, const char *value)
{
	struct mpd_tag_value *tag = &song->tags[type], *prev;

	if ((int)type < 0 || type >= MPD_TAG_COUNT)
		return false;

	if (tag->value == NULL) {
		tag->next = NULL;
		tag->value = strdup(value);

		if (tag->value == NULL)
			return false;
	} else {
		while (tag->next != NULL)
			tag = tag->next;

		prev = tag;
		tag = malloc(sizeof(*tag));

		if (tag == NULL)
			return NULL;

		tag->value = strdup(value);

		if (tag->value == NULL) {
			free(tag);
			return false;
		}

		tag->next = NULL;
		prev->next = tag;
	}

	return true;
}
/*
 * End Hack libmpdclient
 *
*/


/**
 * Checks if a mpd_song might be an FM4 stream
 *
 * @return true if it looks like an FM4 stream, false otherwise
 */
bool
fm4_is_fm4_stream(struct mpd_song *song)
{
	int rank = 0;
	const char *title = NULL;
	const char *uri = NULL;
	const char *title_prefix_fm4 = "FM4";

	if (song == NULL)
		return false;

	/* Information is contained in the title tag */
	title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
	uri = mpd_song_get_uri(song);

	if (title == NULL)
		return false;

	if (strcmp("http://mp3stream1.apasf.apa.at:8000/", uri) == 0)
		rank += 10;

	if (strncmp(title_prefix_fm4, title, sizeof(title_prefix_fm4)))
		rank += 5;

	if (rank >= FM4_THRESHOLD)
		return true;

	return false;
}


/**
 * Parses the mpd_song and correct the tags
 *
 * @return true if the tags have been corrected, false
 * otherwise (not an FM4 stream?)
 */
bool
fm4_parse_stream(struct mpd_song *song)
{
	const char *title = NULL;
	char *real_tag = NULL;
	char *real_artist = NULL;
	char *real_title = NULL;

	const char *pos_tag = NULL;
	const char *pos_artist = NULL;

	const char **dash_pointer = NULL;

	int n = 0;
	int numDash = 0;

	bool newChar = true;

	g_message("FM4: fm4_parse_stream\n");

	if (song == NULL)
		return false;

	title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);

	if (title == NULL)
		return false;


	g_message("FM4: %s\n", title);
	/* parse TAG (program name)*/
	pos_tag = strchr(title, ':');

	if (pos_tag != NULL) {
		n = pos_tag - title;
		real_tag = strndup(title, n);
	}

	/* parse ARTIST */
	++pos_tag; /* ignore ':' */

	if (isspace(*pos_tag))
		++pos_tag; /* ignore space */

	pos_artist = strchr(pos_tag, '-');

	if (pos_artist == NULL) {
		free(real_tag);
		return false;
	}

	/* check for '-' in artist tg */

	for (const char *pos = pos_artist; pos != NULL; pos = strchr(pos + 1, '-')) {
		numDash++;
	}

	g_message("FM4: #Dashes: %d\n", numDash);

	if (numDash > 1) {
		int i = 0;
		bool allupper = true;

		dash_pointer = calloc(sizeof(const char *), numDash + 1);

		for (const char *pos = pos_artist; pos != NULL; pos = strchr(pos + 1, '-')) {
			dash_pointer[i] = pos;
			g_message("FM4: Dashes: %s\n", pos);
			i++;
		}

		/* Endpointer */
		dash_pointer[i] = pos_artist + strlen(pos_artist);

		i = 1;

		for (const char *ptr = pos_artist + 1,
		     *end = pos_artist + strlen(pos_artist);
		     ptr < end;
		     ptr++) {

			if (ptr >= dash_pointer[i]) {
				if (allupper) {
					/* all upper case chars, must be title */
					break;
				}

				g_message("FM4: next dash: %s\n", dash_pointer[i]);
				pos_artist = dash_pointer[i];
				i++;
				allupper = true;
				continue;
			}

			if (isalpha(*ptr) && islower(*ptr)) {
				allupper = false;
			}
		}

		free(dash_pointer);
	}

	g_message("FM4: pos_artist: %s\n", pos_artist);
	g_message("FM4: pos_tag: %s\n", pos_tag);

	n = pos_artist - pos_tag;
	real_artist = strndup(pos_tag, n);

	if (isspace(real_artist[n-1]))
		real_artist[n-1] = 0; /* ignore space */

	/* parse TITLE */
	++pos_artist; /* ignore '-' */

	if (isspace(*pos_artist))
		++pos_artist; /* ignore space */

	real_title = strndup(pos_artist, strlen(pos_artist));

	if (isspace(real_title[strlen(real_title)-1]))
		real_title[strlen(real_title)-1] = 0; /* ignore space */

	/* Capitalize title string */
	for (int i = 0, e = strlen(real_title); i < e; ++i) {
		char c = real_title[i];

		if (isalpha(c)) {
			if (!islower(c)) {
				/* upper case char */
				if (newChar) {
					/* first letter in the word */
					newChar = false;
				} else {
					real_title[i] = tolower(c);
				}
			}
		} else if (isblank(c)) {
			/* new word */
			newChar = true;
		}
	}

	g_message("FM4: title parsed: [%s] [%s] [%s]\n", real_tag, real_artist, real_title);

	mpd_song_clear_tag((struct mpd_song_hack *)song, MPD_TAG_TITLE);
	mpd_song_clear_tag((struct mpd_song_hack *)song, MPD_TAG_ARTIST);

	mpd_song_add_tag((struct mpd_song_hack *)song,
	                 MPD_TAG_TITLE, real_title);

	mpd_song_add_tag((struct mpd_song_hack *)song,
	                 MPD_TAG_ARTIST, real_artist);

	free(real_tag);
	free(real_artist);
	free(real_title);

	return true;

}
