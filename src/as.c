/* mpdscribble (MPD Client)
 * Copyright (C) 2008-2009 The Music Player Daemon Project
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

#include "as.h"
#include "file.h"
#include "journal.h"
#include "conn.h"
#include "config.h"
#include "compat.h"

#include <glib.h>

#if !GLIB_CHECK_VERSION(2,16,0)
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

#define MAX_VAR_SIZE 8192
#define MAX_TIMESTAMP_SIZE 64

/* don't submit more than this amount of songs in a batch. */
#define MAX_SUBMIT_COUNT 10

static const char OK[] = "OK";
static const char BADSESSION[] = "BADSESSION";
static const char FAILED[] = "FAILED";

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


static struct song g_now_playing;
static GQueue *queue;
static unsigned g_submit_pending;

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
#if GLIB_CHECK_VERSION(2,16,0)
		char *escaped = g_uri_escape_string(val, NULL, false);
#else
		char *escaped = soup_uri_encode(val, "&");
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
as_schedule_handshake(struct config_as_host *as_host);

static void
as_submit(struct config_as_host *as_host);

static void
as_schedule_submit(struct config_as_host *as_host);

static void as_increase_interval(struct config_as_host *as_host)
{
	if (as_host->g_interval < 60)
		as_host->g_interval = 60;
	else
		as_host->g_interval <<= 1;

	if (as_host->g_interval > 60 * 60 * 2)
		as_host->g_interval = 60 * 60 * 2;

	g_warning("waiting %i seconds before trying again\n", as_host->g_interval);
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
		if (length > strlen(FAILED))
			g_warning("submission rejected: %.*s\n",
				  (int)(length - strlen(FAILED)),
				  line + strlen(FAILED));
		else
			g_warning("submission rejected\n");
	} else {
		g_warning("unknown response: %.*s", (int)length, line);
	}

	return AS_SUBMIT_FAILED;
}

static bool
as_parse_handshake_response(const char *line, struct config_as_host *as_host)
{
	static const char *BANNED = "BANNED";
	static const char *BADAUTH = "BADAUTH";
	static const char *BADTIME = "BADTIME";

	/* FIXME: some code duplication between this
	   and as_parse_submit_response. */
	if (!strncmp(line, OK, strlen(OK))) {
		g_message("handshake ok for '%s'\n", as_host->url);
		return true;
	} else if (!strncmp(line, BANNED, strlen(BANNED))) {
		g_warning("handshake failed, we're banned (%s)\n", line);
		as_host->g_state = AS_BADAUTH;
	} else if (!strncmp(line, BADAUTH, strlen(BADAUTH))) {
		g_warning("handshake failed, username or password incorrect (%s)\n",
			  line);
		as_host->g_state = AS_BADAUTH;
	} else if (!strncmp(line, BADTIME, strlen(BADTIME))) {
		g_warning("handshake failed, clock not synchronized (%s)\n",
			  line);
		as_host->g_state = AS_BADAUTH;
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

static void as_handshake_callback(size_t length, const char *response, void *data)
{
	as_handshaking state = AS_COMMAND;
	char *newline;
	char *next;
	bool ret;
	struct config_as_host *as_host = data;

	assert(as_host->g_state == AS_HANDSHAKING);
	as_host->g_state = AS_NOTHING;

	if (!length) {
		g_warning("handshake timed out\n");
		as_increase_interval(data);
		as_schedule_handshake(as_host);
		return;
	}

	while ((newline = memchr(response, '\n', length)) != NULL) {
		next = g_strndup(response, newline - response);
		switch (state) {
		case AS_COMMAND:
			ret = as_parse_handshake_response(next, as_host);
			if (!ret) {
				g_free(next);
				as_increase_interval(data);
				as_schedule_handshake(as_host);
				return;
			}

			state = AS_SESSION;
			break;
		case AS_SESSION:
			as_host->g_session = next;
			next = NULL;
			g_debug("session: %s\n", as_host->g_session);
			state = AS_NOWPLAY;
			break;
		case AS_NOWPLAY:
			as_host->g_nowplay_url = next;
			next = NULL;
			g_debug("now playing url: %s\n", as_host->g_nowplay_url);
			state = AS_SUBMIT;
			break;
		case AS_SUBMIT:
			as_host->g_submit_url = next;
			g_debug("submit url: %s\n", as_host->g_submit_url);
			as_host->g_state = AS_READY;
			as_host->g_interval = 1;

			/* handshake was successful: see if we have
			   songs to submit */
			as_submit(as_host);
			return;
		}

		g_free(next);
		length = response + length - (newline + 1);
		response = newline + 1;

	}

	as_increase_interval(as_host);
	as_schedule_handshake(as_host);
}

static void as_queue_remove_oldest(unsigned count)
{
	assert(count > 0);

	while (count--) {
		struct song *tmp = g_queue_pop_head(queue);
		as_song_cleanup(tmp, 1);
	}
}

static void as_submit_callback(size_t length, const char *response, void *data)
{
	char *newline;
	struct config_as_host *as_host = data;

	assert(as_host->g_state == AS_SUBMITTING);
	as_host->g_state = AS_READY;

	if (!length) {
		g_submit_pending = 0;
		g_warning("submit timed out\n");
		as_increase_interval(as_host);
		as_schedule_submit(as_host);
		return;
	}

	newline = memchr(response, '\n', length);
	if (newline != NULL)
		length = newline - response;

	switch (as_parse_submit_response(response, length)) {
	case AS_SUBMIT_OK:
		as_host->g_interval = 1;

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
		as_submit(as_host);
		break;
	case AS_SUBMIT_FAILED:
		as_increase_interval(as_host);
		as_schedule_submit(as_host);
		break;
	case AS_SUBMIT_HANDSHAKE:
		as_host->g_state = AS_NOTHING;
		as_schedule_handshake(as_host);
		break;
	}
}

char *as_timestamp(void)
{
	/* create timestamp for 1.2 protocol. */
	GTimeVal time_val;

	g_get_current_time(&time_val);
	return g_strdup_printf("%ld", (glong)time_val.tv_sec);
}

/**
 * Calculate the MD5 checksum of the specified string.  The return
 * value is a newly allocated string containing the hexadecimal
 * checksum.
 */
static char *md5_hex(const char *p, int len)
{
#if GLIB_CHECK_VERSION(2,16,0)
	return g_compute_checksum_for_string(G_CHECKSUM_MD5, p, len);
#else
	/* fall back to libgcrypt on GLib < 2.16 */
	gcry_md_hd_t hd;
	unsigned char *binary;
	char *result;

	if (len == -1)
		len = strlen(p);

	if (gcry_md_open(&hd, GCRY_MD_MD5, 0) != GPG_ERR_NO_ERROR)
		g_error("gcry_md_open() failed\n");
	gcry_md_write(hd, p, len);
	binary = gcry_md_read(hd, GCRY_MD_MD5);
	if (binary == NULL)
		g_error("gcry_md_read() failed\n");
	result = g_malloc(gcry_md_get_algo_dlen(GCRY_MD_MD5) * 2 + 1);
	for (size_t i = 0; i < gcry_md_get_algo_dlen(GCRY_MD_MD5); ++i)
		snprintf(result + i * 2, 3, "%02x", binary[i]);
	gcry_md_close(hd);

	return result;
#endif
}

static char *as_md5(const char *password, const char *timestamp)
{
	char *password_md5, *cat, *result;

	if (strlen(password) != 32)
		/* assume it's not hashed yet */
		password = password_md5 = md5_hex(password, -1);
	else
		password_md5 = NULL;

	cat = g_strconcat(password, timestamp, NULL);
	g_free(password_md5);

	result = md5_hex(cat, -1);
	g_free(cat);

	return result;
}

static void as_handshake(struct config_as_host *as_host)
{
	GString *url;
	char *timestr, *md5;

	as_host->g_state = AS_HANDSHAKING;

	timestr = as_timestamp();
	md5 = as_md5(as_host->password, timestr);

	/* construct the handshake url. */
	url = g_string_new(as_host->url);
	first_var(url, "hs", "true");
	add_var(url, "p", "1.2");
	add_var(url, "c", AS_CLIENT_ID);
	add_var(url, "v", AS_CLIENT_VERSION);
	add_var(url, "u", as_host->username);
	add_var(url, "t", timestr);
	add_var(url, "a", md5);

	g_free(timestr);
	g_free(md5);

	//  notice ("handshake url:\n%s", url);

	if (!conn_initiate(url->str, &as_handshake_callback, NULL, as_host)) {
		g_warning("something went wrong when trying to connect, "
			  "probably a bug\n");

		as_host->g_state = AS_NOTHING;
		as_increase_interval(as_host);
		as_schedule_handshake(as_host);
	}

	g_string_free(url, true);
}

static gboolean
as_handshake_timer(gpointer data)
{
	struct config_as_host *as_host = data;

	assert(as_host->g_state == AS_NOTHING);

	((struct config_as_host*)data)->as_handshake_id = 0;

	as_handshake(data);
	return false;
}

static void
as_schedule_handshake(struct config_as_host *as_host)
{
	assert(as_host->g_state == AS_NOTHING);
	assert(as_host->as_handshake_id == 0);

	as_host->as_handshake_id = g_timeout_add_seconds(as_host->g_interval,
						as_handshake_timer, as_host);
}

static void
as_send_now_playing(const char *artist, const char *track,
		    const char *album, const char *mbid, const int length, struct config_as_host *as_host)
{
	GString *post_data;
	char len[MAX_VAR_SIZE];

	assert(as_host->g_state == AS_READY);
	assert(as_host->as_submit_id == 0);

	as_host->g_state = AS_SUBMITTING;

	snprintf(len, MAX_VAR_SIZE, "%i", length);

	post_data = g_string_new(NULL);
	add_var(post_data, "s", as_host->g_session);
	add_var(post_data, "a", artist);
	add_var(post_data, "t", track);
	add_var(post_data, "b", album);
	add_var(post_data, "l", len);
	add_var(post_data, "n", "");
	add_var(post_data, "m", mbid);

	g_message("sending 'now playing' notification to '%s'\n", as_host->url);

	if (!conn_initiate(as_host->g_nowplay_url, as_submit_callback,
			   post_data->str, as_host)) {
		g_warning("failed to POST to %s\n", as_host->g_nowplay_url);

		as_host->g_state = AS_READY;
		as_increase_interval(as_host);
		as_schedule_submit(as_host);
	}

	g_string_free(post_data, true);
}

void
as_now_playing(const char *artist, const char *track,
	       const char *album, const char *mbid, const int length)
{
	struct config_as_host *current_host = &file_config.as_hosts;

	as_song_cleanup(&g_now_playing, false);

	g_now_playing.artist = g_strdup(artist);
	g_now_playing.track = g_strdup(track);
	g_now_playing.album = g_strdup(album);
	g_now_playing.mbid = g_strdup(mbid);
	g_now_playing.length = length;

	do {
		if (current_host->g_state == AS_READY && current_host->as_submit_id == 0)
			as_schedule_submit(current_host);
	} while((current_host = current_host->next));
}

static void as_submit(struct config_as_host *as_host)
{
	//MAX_SUBMIT_COUNT
	unsigned count = 0;
	GString *post_data;
	char len[MAX_VAR_SIZE];

	assert(as_host->g_state == AS_READY);
	assert(as_host->as_submit_id == 0);

	if (g_queue_is_empty(queue)) {
		/* the submission queue is empty.  See if a "now playing" song is
		   scheduled - these should be sent after song submissions */
		if (g_now_playing.artist != NULL && g_now_playing.track != NULL) {
			as_send_now_playing(g_now_playing.artist,
					    g_now_playing.track,
					    g_now_playing.album,
					    g_now_playing.mbid,
					    g_now_playing.length,
					    as_host);
		}

		return;
	}

	as_host->g_state = AS_SUBMITTING;

	/* construct the handshake url. */
	post_data = g_string_new(NULL);
	add_var(post_data, "s", as_host->g_session);

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
	g_debug("url: %s\n", as_host->g_submit_url);

	g_submit_pending = count;
	if (!conn_initiate(as_host->g_submit_url, &as_submit_callback,
			   post_data->str, as_host)) {
		g_warning("something went wrong when trying to connect,"
			  " probably a bug\n");

		as_host->g_state = AS_READY;
		as_increase_interval(as_host);
		as_schedule_submit(as_host);
	}

	g_string_free(post_data, true);
}

int
as_songchange(const char *file, const char *artist, const char *track,
	      const char *album, const char *mbid, const int length,
	      const char *time2)
{
	struct song *current;
	struct config_as_host *current_host = &file_config.as_hosts;

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

	do {
		if (current_host->g_state == AS_READY && current_host->as_submit_id == 0)
			as_schedule_submit(current_host);
	} while((current_host = current_host->next));

	return g_queue_get_length(queue);
}

void as_init(void)
{
	guint queue_length;
	struct config_as_host *current_host = &file_config.as_hosts;

	g_message("starting mpdscribble (" AS_CLIENT_ID " " AS_CLIENT_VERSION ")\n");

	queue = g_queue_new();
	journal_read(queue);

	queue_length = g_queue_get_length(queue);
	g_message("loaded %i song%s from cache\n",
		  queue_length, queue_length == 1 ? "" : "s");

	conn_setup();

	do {
		current_host->g_session = NULL;
		current_host->g_nowplay_url = NULL;
		current_host->g_submit_url = NULL;
		current_host->g_interval = 1;
		current_host->g_state = AS_NOTHING;
		current_host->as_submit_id = 0;
		current_host->as_handshake_id = 0;
		as_schedule_handshake(current_host);
	} while((current_host = current_host->next));
}

static gboolean
as_submit_timer(gpointer data)
{
	assert(((struct config_as_host *)data)->g_state == AS_READY);

	((struct config_as_host *)data)->as_submit_id = 0;

	as_submit(data);
	return false;
}

static void
as_schedule_submit(struct config_as_host *as_host)
{
	assert(as_host->as_submit_id == 0);
	assert(!g_queue_is_empty(queue) ||
	       (g_now_playing.artist != NULL && g_now_playing.track != NULL));

	as_host->as_submit_id = g_timeout_add_seconds(as_host->g_interval,
					     as_submit_timer, as_host);
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

	conn_cleanup();
}
