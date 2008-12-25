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

#include "as.h"
#include "file.h"
#include "journal.h"
#include "conn.h"
#include "config.h"

#include <glib.h>

#if GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 16
#include <gcrypt.h>
#include <libsoup/soup.h>
#endif

#include <assert.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#define MAX_VAR_SIZE 8192
#define MAX_TIMESTAMP_SIZE 64

#define AS_HOST "http://post.audioscrobbler.com/"

/* don't submit more than this amount of songs in a batch. */
#define MAX_SUBMIT_COUNT 10

static const char OK[] = "OK";
static const char BADSESSION[] = "BADSESSION";
static const char FAILED[] = "FAILED";

typedef enum {
	AS_NOTHING,
	AS_HANDSHAKING,
	AS_READY,
	AS_SUBMITTING,
	AS_BADAUTH,
} as_state;

typedef enum {
	AS_COMMAND,
	AS_SESSION,
	AS_NOWPLAY,
	AS_SUBMIT,
} as_handshaking;

typedef enum {
	AS_SUBMIT_OK,
	AS_SUBMIT_FAILED,
	AS_SUBMIT_HANDSHAKE,
} as_submitting;

static char *g_session = NULL;
static char *g_nowplay_url = NULL;
static char *g_submit_url = NULL;
static char *g_md5_response = NULL;
static int g_interval = 1;
static as_state g_state = AS_NOTHING;

static struct song g_now_playing;
static GQueue *queue;
static unsigned g_submit_pending;

static guint as_handshake_id, as_submit_id;

static void
add_var_internal(GString * s, char sep, const char *key,
		 signed char idx, const char *val)
{
	g_string_append_c(s, sep);
	g_string_append(s, key);

	if (idx >= 0)
		g_string_append_printf(s, "[%i]", idx);

	g_string_append_c(s, '=');

	if (val != NULL) {
#if GLIB_MAJOR_VERSION > 2 || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION >= 16)
		char *escaped = g_uri_escape_string(val, NULL, false);
#else
		char *escaped = soup_uri_encode(val, NULL);
#endif
		g_string_append(s, escaped);
		g_free(escaped);
	}
}

static void first_var(GString * s, const char *key, const char *val)
{
	add_var_internal(s, '?', key, -1, val);
}

static void add_var(GString * s, const char *key, const char *val)
{
	add_var_internal(s, '&', key, -1, val);
}

static void
add_var_i(GString * s, const char *key, signed char idx, const char *val)
{
	add_var_internal(s, '&', key, idx, val);
}

static void
as_schedule_handshake(void);

static void
as_submit(void);

static void
as_schedule_submit(void);

static void as_increase_interval(void)
{
	if (g_interval < 60)
		g_interval = 60;
	else
		g_interval <<= 1;

	if (g_interval > 60 * 60 * 2)
		g_interval = 60 * 60 * 2;

	g_warning("waiting %i seconds before trying again\n", g_interval);
}

static int as_parse_submit_response(const char *line, size_t length)
{
	if (length == sizeof(OK) - 1 && memcmp(line, OK, length) == 0) {
		g_message("OK\n");
		return AS_SUBMIT_OK;
	} else if (length == sizeof(BADSESSION) - 1 &&
		   memcmp(line, BADSESSION, length) == 0) {
		g_warning("invalid session\n");

		return AS_SUBMIT_HANDSHAKE;
	} else if (length == sizeof(FAILED) - 1 &&
		   memcmp(line, FAILED, length) == 0) {
		const char *start = line + strlen(FAILED);
		if (*start)
			g_warning("submission rejected: %s\n", start);
		else
			g_warning("submission rejected\n");
	} else {
		g_warning("unknown response\n");
	}

	return AS_SUBMIT_FAILED;
}

static bool
as_parse_handshake_response(const char *line)
{
	static const char *BANNED = "BANNED";
	static const char *BADAUTH = "BADAUTH";
	static const char *BADTIME = "BADTIME";

	/* FIXME: some code duplication between this
	   and as_parse_submit_response. */
	if (!strncmp(line, OK, strlen(OK))) {
		g_message("handshake ok\n");
		return true;
	} else if (!strncmp(line, BANNED, strlen(BANNED))) {
		g_warning("handshake failed, we're banned (%s)\n", line);
		g_state = AS_BADAUTH;
	} else if (!strncmp(line, BADAUTH, strlen(BADAUTH))) {
		g_warning("handshake failed, username or password incorrect (%s)\n",
			  line);
		g_state = AS_BADAUTH;
	} else if (!strncmp(line, BADTIME, strlen(BADTIME))) {
		g_warning("handshake failed, clock not synchronized (%s)\n",
			  line);
		g_state = AS_BADAUTH;
	} else if (!strncmp(line, FAILED, strlen(FAILED))) {
		g_warning("handshake failed (%s)\n", line);
	} else {
		g_warning("error parsing handshake response (%s)\n", line);
	}

	return false;
}

static void as_song_cleanup(struct song *s, int free_struct)
{
	g_free(s->artist);
	g_free(s->track);
	g_free(s->album);
	g_free(s->mbid);
	g_free(s->time);
	if (free_struct)
		free(s);
}

static void as_handshake_callback(size_t length, const char *response)
{
	as_handshaking state = AS_COMMAND;
	char *newline;
	char *next;
	bool ret;

	assert(g_state == AS_HANDSHAKING);
	g_state = AS_NOTHING;

	if (!length) {
		g_warning("handshake timed out\n");
		as_increase_interval();
		as_schedule_handshake();
		return;
	}

	while ((newline = memchr(response, '\n', length)) != NULL) {
		next = g_strndup(response, newline - response);
		switch (state) {
		case AS_COMMAND:
			ret = as_parse_handshake_response(next);
			if (!ret) {
				g_free(next);
				as_increase_interval();
				as_schedule_handshake();
				return;
			}

			state = AS_SESSION;
			break;
		case AS_SESSION:
			g_session = next;
			next = NULL;
			g_debug("session: %s\n", g_session);
			state = AS_NOWPLAY;
			break;
		case AS_NOWPLAY:
			g_nowplay_url = next;
			next = NULL;
			g_debug("now playing url: %s\n", g_nowplay_url);
			state = AS_SUBMIT;
			break;
		case AS_SUBMIT:
			g_submit_url = next;
			g_debug("submit url: %s\n", g_submit_url);
			g_state = AS_READY;
			g_interval = 1;

			/* handshake was successful: see if we have
			   songs to submit */
			as_submit();
			return;
		}

		g_free(next);
		length = response + length - (newline + 1);
		response = newline + 1;

	}

	as_increase_interval();
	as_schedule_handshake();
}

static void as_queue_remove_oldest(unsigned count)
{
	assert(count > 0);

	while (count--) {
		struct song *tmp = g_queue_pop_head(queue);
		as_song_cleanup(tmp, 1);
	}
}

static void as_submit_callback(size_t length, const char *response)
{
	char *newline;

	assert(g_state == AS_SUBMITTING);
	g_state = AS_READY;

	if (!length) {
		g_submit_pending = 0;
		g_warning("submit timed out\n");
		as_increase_interval();
		as_schedule_submit();
		return;
	}

	newline = memchr(response, '\n', length);
	if (newline != NULL)
		length = newline - response;

	switch (as_parse_submit_response(response, length)) {
	case AS_SUBMIT_OK:
		g_interval = 1;

		/* submission was accepted, so clean up the cache. */
		if (g_submit_pending > 0) {
			as_queue_remove_oldest(g_submit_pending);
			g_submit_pending = 0;
		} else {
			assert(g_now_playing.artist != NULL &&
			       g_now_playing.track != NULL);

			as_song_cleanup(&g_now_playing, false);
			memset(&g_now_playing, 0, sizeof(g_now_playing));
		}


		/* submit the next chunk (if there is some left) */
		as_submit();
		break;
	case AS_SUBMIT_FAILED:
		as_increase_interval();
		as_schedule_submit();
		break;
	case AS_SUBMIT_HANDSHAKE:
		g_state = AS_NOTHING;
		as_schedule_handshake();
		break;
	}
}

char *as_timestamp(void)
{
	/* create timestamp for 1.2 protocol. */
	time_t timestamp = time(NULL);
	char timestr[12];
	snprintf(timestr, 12, "%ld", timestamp);

	return g_strdup(timestr);
}

static char *as_md5(const char *password, const char *timestamp)
{
	char *cat, *result;

	cat = g_strconcat(password, timestamp, NULL);
#if GLIB_MAJOR_VERSION > 2 || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION >= 16)
	result = g_compute_checksum_for_string(G_CHECKSUM_MD5, cat, -1);
#else
	/* fall back to libgcrypt on GLib < 2.16 */
	{
		gcry_md_hd_t hd;
		unsigned char *p;
		size_t i;

		if (gcry_md_open(&hd, GCRY_MD_MD5, 0) != GPG_ERR_NO_ERROR)
			g_error("gcry_md_open() failed\n");
		gcry_md_write(hd, cat, strlen(cat));
		p = gcry_md_read(hd, GCRY_MD_MD5);
		if (p == NULL)
			g_error("gcry_md_read() failed\n");
		result = g_malloc(gcry_md_get_algo_dlen(GCRY_MD_MD5) * 2 + 1);
		for (i = 0; i < gcry_md_get_algo_dlen(GCRY_MD_MD5); ++i)
			snprintf(result + i * 2, 3, "%02x", p[i]);
		gcry_md_close(hd);
	}
#endif
	g_free(cat);

	return result;
}

static void as_handshake(void)
{
	GString *url;
	char *timestr, *md5;

	g_state = AS_HANDSHAKING;

	timestr = as_timestamp();
	md5 = as_md5(file_config.password, timestr);

	/* construct the handshake url. */
	url = g_string_new(AS_HOST);
	first_var(url, "hs", "true");
	add_var(url, "p", "1.2");
	add_var(url, "c", AS_CLIENT_ID);
	add_var(url, "v", AS_CLIENT_VERSION);
	add_var(url, "u", file_config.username);
	add_var(url, "t", timestr);
	add_var(url, "a", md5);

	g_free(timestr);
	g_free(md5);

	//  notice ("handshake url:\n%s", url);

	if (!conn_initiate(url->str, &as_handshake_callback, NULL)) {
		g_warning("something went wrong when trying to connect, "
			  "probably a bug\n");

		g_state = AS_NOTHING;
		as_increase_interval();
		as_schedule_handshake();
	}

	g_string_free(url, true);
}

static gboolean
as_handshake_timer(G_GNUC_UNUSED gpointer data)
{
	assert(g_state == AS_NOTHING);

	as_handshake_id = 0;

	as_handshake();
	return false;
}

static void
as_schedule_handshake(void)
{
	assert(g_state == AS_NOTHING);
	assert(as_handshake_id == 0);

	as_handshake_id = g_timeout_add(g_interval * 1000,
					as_handshake_timer, NULL);
}

static void
as_send_now_playing(const char *artist, const char *track,
		    const char *album, const char *mbid, const int length)
{
	GString *post_data;
	char len[MAX_VAR_SIZE];

	assert(g_state == AS_READY);
	assert(as_submit_id == 0);

	g_state = AS_SUBMITTING;

	snprintf(len, MAX_VAR_SIZE, "%i", length);

	post_data = g_string_new(NULL);
	add_var(post_data, "s", g_session);
	add_var(post_data, "a", artist);
	add_var(post_data, "t", track);
	add_var(post_data, "b", album);
	add_var(post_data, "l", len);
	add_var(post_data, "n", "");
	add_var(post_data, "m", mbid);

	g_message("sending 'now playing' notification\n");

	if (!conn_initiate(g_nowplay_url, as_submit_callback,
			   post_data->str)) {
		g_warning("failed to POST to %s\n", g_nowplay_url);

		g_state = AS_READY;
		as_increase_interval();
		as_schedule_submit();
	}

	g_string_free(post_data, true);
}

void
as_now_playing(const char *artist, const char *track,
	       const char *album, const char *mbid, const int length)
{
	as_song_cleanup(&g_now_playing, false);

	g_now_playing.artist = g_strdup(artist);
	g_now_playing.track = g_strdup(track);
	g_now_playing.album = g_strdup(album);
	g_now_playing.mbid = g_strdup(mbid);
	g_now_playing.length = length;

	if (g_state == AS_READY && as_submit_id == 0)
		as_schedule_submit();
}

static void as_submit(void)
{
	//MAX_SUBMIT_COUNT
	unsigned count = 0;
	GString *post_data;
	char len[MAX_VAR_SIZE];

	assert(g_state == AS_READY);
	assert(as_submit_id == 0);

	if (g_queue_is_empty(queue)) {
		/* the submission queue is empty.  See if a "now playing" song is
		   scheduled - these should be sent after song submissions */
		if (g_now_playing.artist != NULL && g_now_playing.track != NULL) {
			as_send_now_playing(g_now_playing.artist,
					    g_now_playing.track,
					    g_now_playing.album,
					    g_now_playing.mbid,
					    g_now_playing.length);
		}

		return;
	}

	g_state = AS_SUBMITTING;

	/* construct the handshake url. */
	post_data = g_string_new(NULL);
	add_var(post_data, "s", g_session);

	for (GList *list = g_queue_peek_head_link(queue);
	     list != NULL && count < MAX_SUBMIT_COUNT;
	     list = g_list_next(list)) {
		struct song *song = list->data;

		snprintf(len, MAX_VAR_SIZE, "%i", song->length);

		add_var_i(post_data, "a", count, song->artist);
		add_var_i(post_data, "t", count, song->track);
		add_var_i(post_data, "l", count, len);
		add_var_i(post_data, "i", count, song->time);
		add_var_i(post_data, "o", count, song->source);
		add_var_i(post_data, "r", count, "");
		add_var_i(post_data, "b", count, song->album);
		add_var_i(post_data, "n", count, "");
		add_var_i(post_data, "m", count, song->mbid);

		count++;
	}

	g_message("submitting %i song%s\n", count, count == 1 ? "" : "s");
	g_debug("post data: %s\n", post_data->str);
	g_debug("url: %s\n", g_submit_url);

	g_submit_pending = count;
	if (!conn_initiate(g_submit_url, &as_submit_callback,
			   post_data->str)) {
		g_warning("something went wrong when trying to connect,"
			  " probably a bug\n");

		g_state = AS_READY;
		as_increase_interval();
		as_schedule_submit();
	}

	g_string_free(post_data, true);
}

int
as_songchange(const char *file, const char *artist, const char *track,
	      const char *album, const char *mbid, const int length,
	      const char *time2)
{
	struct song *current;

	/* from the 1.2 protocol draft:

	   You may still submit if there is no album title (variable b)
	   You may still submit if there is no musicbrainz id available (variable m)

	   everything else is mandatory.
	 */
	if (!(artist && strlen(artist))) {
		g_warning("empty artist, not submitting; "
			  "please check the tags on %s\n", file);
		return -1;
	}

	if (!(track && strlen(track))) {
		g_warning("empty title, not submitting; "
			  "please check the tags on %s", file);
		return -1;
	}

	current = g_new(struct song, 1);
	current->artist = g_strdup(artist);
	current->track = g_strdup(track);
	current->album = g_strdup(album);
	current->mbid = g_strdup(mbid);
	current->length = length;
	current->time = time2 ? g_strdup(time2) : as_timestamp();
	current->source = strstr(file, "://") == NULL ? "P" : "R";

	g_message("%s, songchange: %s - %s (%i)\n",
		  current->time, current->artist,
		  current->track, current->length);

	g_queue_push_tail(queue, current);

	if (g_state == AS_READY && as_submit_id == 0)
		as_schedule_submit();

	return g_queue_get_length(queue);
}

void as_init(void)
{
	guint queue_length;

	assert(g_state == AS_NOTHING);

	g_message("starting mpdscribble (" AS_CLIENT_ID " " AS_CLIENT_VERSION ")\n");

	queue = g_queue_new();
	journal_read(queue);

	queue_length = g_queue_get_length(queue);
	g_message("loaded %i song%s from cache\n",
		  queue_length, queue_length == 1 ? "" : "s");

	conn_setup();

	as_schedule_handshake();
}

static gboolean
as_submit_timer(G_GNUC_UNUSED gpointer data)
{
	assert(g_state == AS_READY);

	as_submit_id = 0;

	as_submit();
	return false;
}

static void
as_schedule_submit(void)
{
	assert(as_submit_id == 0);
	assert(!g_queue_is_empty(queue) ||
	       (g_now_playing.artist != NULL && g_now_playing.track != NULL));

	as_submit_id = g_timeout_add(g_interval * 1000,
				     as_submit_timer, NULL);
}

void as_save_cache(void)
{
	if (journal_write(queue)) {
		guint queue_length = g_queue_get_length(queue);
		g_message("saved %i song%s to cache\n",
			  queue_length, queue_length == 1 ? "" : "s");
	}
}

static void
free_queue_song(gpointer data, G_GNUC_UNUSED gpointer user_data)
{
	struct song *song = data;
	as_song_cleanup(song, true);
}

void as_cleanup(void)
{
	as_save_cache();

	as_song_cleanup(&g_now_playing, false);

	g_queue_foreach(queue, free_queue_song, NULL);
	g_queue_free(queue);

	g_free(g_submit_url);
	g_free(g_md5_response);

	conn_cleanup();
}
