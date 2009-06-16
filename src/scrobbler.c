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

#include "scrobbler.h"
#include "record.h"
#include "file.h"
#include "journal.h"
#include "http_client.h"
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

enum scrobbler_state {
	SCROBBLER_STATE_NOTHING,
	SCROBBLER_STATE_HANDSHAKE,
	SCROBBLER_STATE_READY,
	SCROBBLER_STATE_SUBMITTING,
	SCROBBLER_STATE_BADAUTH,
};

typedef enum {
	AS_SUBMIT_OK,
	AS_SUBMIT_FAILED,
	AS_SUBMIT_HANDSHAKE,
} as_submitting;

struct scrobbler {
	const struct scrobbler_config *config;

	enum scrobbler_state state;

	unsigned interval;

	guint handshake_source_id;
	guint submit_source_id;

	char *session;
	char *nowplay_url;
	char *submit_url;

	struct song now_playing;
};

static GSList *scrobblers;
static GQueue *queue;
static unsigned g_submit_pending;

/**
 * Creates a new scrobbler object based on the specified
 * configuration.
 */
static struct scrobbler *
scrobbler_new(const struct scrobbler_config *config)
{
	struct scrobbler *scrobbler = g_new(struct scrobbler, 1);

	scrobbler->config = config;
	scrobbler->state = SCROBBLER_STATE_NOTHING;
	scrobbler->interval = 1;
	scrobbler->handshake_source_id = 0;
	scrobbler->submit_source_id = 0;
	scrobbler->session = NULL;
	scrobbler->nowplay_url = NULL;
	scrobbler->submit_url = NULL;

	clear_song(&scrobbler->now_playing);

	return scrobbler;
}

/**
 * Frees a scrobbler object.
 */
static void
scrobbler_free(struct scrobbler *scrobbler)
{
	as_song_cleanup(&scrobbler->now_playing, false);

	if (scrobbler->handshake_source_id != 0)
		g_source_remove(scrobbler->handshake_source_id);
	if (scrobbler->submit_source_id != 0)
		g_source_remove(scrobbler->submit_source_id);

	g_free(scrobbler->session);
	g_free(scrobbler->nowplay_url);
	g_free(scrobbler->submit_url);
	g_free(scrobbler);
}

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
scrobbler_schedule_handshake(struct scrobbler *scrobbler);

static void
scrobbler_submit(struct scrobbler *scrobbler);

static void
scrobbler_schedule_submit(struct scrobbler *scrobbler);

static void
scrobbler_increase_interval(struct scrobbler *scrobbler)
{
	if (scrobbler->interval < 60)
		scrobbler->interval = 60;
	else
		scrobbler->interval <<= 1;

	if (scrobbler->interval > 60 * 60 * 2)
		scrobbler->interval = 60 * 60 * 2;

	g_warning("waiting %u seconds before trying again",
		  scrobbler->interval);
}

static as_submitting
scrobbler_parse_submit_response(const char *line, size_t length)
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
scrobbler_parse_handshake_response(struct scrobbler *scrobbler, const char *line)
{
	static const char *BANNED = "BANNED";
	static const char *BADAUTH = "BADAUTH";
	static const char *BADTIME = "BADTIME";

	/* FIXME: some code duplication between this
	   and as_parse_submit_response. */
	if (!strncmp(line, OK, strlen(OK))) {
		g_message("handshake ok for '%s'\n", scrobbler->config->url);
		return true;
	} else if (!strncmp(line, BANNED, strlen(BANNED))) {
		g_warning("handshake failed, we're banned (%s)\n", line);
		scrobbler->state = SCROBBLER_STATE_BADAUTH;
	} else if (!strncmp(line, BADAUTH, strlen(BADAUTH))) {
		g_warning("handshake failed, username or password incorrect (%s)\n",
			  line);
		scrobbler->state = SCROBBLER_STATE_BADAUTH;
	} else if (!strncmp(line, BADTIME, strlen(BADTIME))) {
		g_warning("handshake failed, clock not synchronized (%s)\n",
			  line);
		scrobbler->state = SCROBBLER_STATE_BADAUTH;
	} else if (!strncmp(line, FAILED, strlen(FAILED))) {
		g_warning("handshake failed (%s)\n", line);
	} else {
		g_warning("error parsing handshake response (%s)\n", line);
	}

	return false;
}

static char *
next_line(const char **input_r, const char *end)
{
	const char *input = *input_r;
	const char *newline = memchr(input, '\n', end - input);
	char *line;

	if (newline == NULL)
		return g_strdup("");

	line = g_strndup(input, newline - input);
	*input_r = newline + 1;

	return line;
}

static void
scrobbler_handshake_callback(size_t length, const char *response, void *data)
{
	struct scrobbler *scrobbler = data;
	const char *end = response + length;
	char *line;
	bool ret;

	assert(scrobbler != NULL);
	assert(scrobbler->state == SCROBBLER_STATE_HANDSHAKE);

	scrobbler->state = SCROBBLER_STATE_NOTHING;

	if (!length) {
		g_warning("handshake timed out\n");
		scrobbler_increase_interval(scrobbler);
		scrobbler_schedule_handshake(scrobbler);
		return;
	}

	line = next_line(&response, end);
	ret = scrobbler_parse_handshake_response(scrobbler, line);
	g_free(line);
	if (!ret) {
		scrobbler_increase_interval(scrobbler);
		scrobbler_schedule_handshake(scrobbler);
		return;
	}

	scrobbler->session = next_line(&response, end);
	g_debug("session: %s", scrobbler->session);

	scrobbler->nowplay_url = next_line(&response, end);
	g_debug("now playing url: %s", scrobbler->nowplay_url);

	scrobbler->submit_url = next_line(&response, end);
	g_debug("submit url: %s", scrobbler->submit_url);

	if (*scrobbler->nowplay_url == 0 || *scrobbler->submit_url == 0) {
		g_free(scrobbler->session);
		scrobbler->session = NULL;

		g_free(scrobbler->nowplay_url);
		scrobbler->nowplay_url = NULL;

		g_free(scrobbler->submit_url);
		scrobbler->submit_url = NULL;

		scrobbler_increase_interval(scrobbler);
		scrobbler_schedule_handshake(scrobbler);
		return;
	}

	scrobbler->state = SCROBBLER_STATE_READY;
	scrobbler->interval = 1;

	/* handshake was successful: see if we have songs to submit */
	scrobbler_submit(scrobbler);
}

static void as_queue_remove_oldest(unsigned count)
{
	assert(count > 0);

	while (count--) {
		struct song *tmp = g_queue_pop_head(queue);
		as_song_cleanup(tmp, 1);
	}
}

static void
scrobbler_submit_callback(size_t length, const char *response, void *data)
{
	struct scrobbler *scrobbler = data;
	char *newline;

	assert(scrobbler->state == SCROBBLER_STATE_SUBMITTING);
	scrobbler->state = SCROBBLER_STATE_READY;

	if (!length) {
		g_submit_pending = 0;
		g_warning("submit timed out\n");
		scrobbler_increase_interval(scrobbler);
		scrobbler_schedule_submit(scrobbler);
		return;
	}

	newline = memchr(response, '\n', length);
	if (newline != NULL)
		length = newline - response;

	switch (scrobbler_parse_submit_response(response, length)) {
	case AS_SUBMIT_OK:
		scrobbler->interval = 1;

		/* submission was accepted, so clean up the cache. */
		if (g_submit_pending > 0) {
			as_queue_remove_oldest(g_submit_pending);
			g_submit_pending = 0;
		} else {
			assert(scrobbler->now_playing.artist != NULL &&
			       scrobbler->now_playing.track != NULL);

			as_song_cleanup(&scrobbler->now_playing, false);
			memset(&scrobbler->now_playing, 0,
			       sizeof(scrobbler->now_playing));
		}


		/* submit the next chunk (if there is some left) */
		scrobbler_submit(scrobbler);
		break;
	case AS_SUBMIT_FAILED:
		scrobbler_increase_interval(scrobbler);
		scrobbler_schedule_submit(scrobbler);
		break;
	case AS_SUBMIT_HANDSHAKE:
		scrobbler->state = SCROBBLER_STATE_NOTHING;
		scrobbler_schedule_handshake(scrobbler);
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

static void
scrobbler_handshake(struct scrobbler *scrobbler)
{
	GString *url;
	char *timestr, *md5;

	scrobbler->state = SCROBBLER_STATE_HANDSHAKE;

	timestr = as_timestamp();
	md5 = as_md5(scrobbler->config->password, timestr);

	/* construct the handshake url. */
	url = g_string_new(scrobbler->config->url);
	first_var(url, "hs", "true");
	add_var(url, "p", "1.2");
	add_var(url, "c", AS_CLIENT_ID);
	add_var(url, "v", AS_CLIENT_VERSION);
	add_var(url, "u", scrobbler->config->username);
	add_var(url, "t", timestr);
	add_var(url, "a", md5);

	g_free(timestr);
	g_free(md5);

	//  notice ("handshake url:\n%s", url);

	http_client_request(url->str, NULL,
			    &scrobbler_handshake_callback, scrobbler);

	g_string_free(url, true);
}

static gboolean
scrobbler_handshake_timer(gpointer data)
{
	struct scrobbler *scrobbler = data;

	assert(scrobbler->state == SCROBBLER_STATE_NOTHING);

	scrobbler->handshake_source_id = 0;

	scrobbler_handshake(data);
	return false;
}

static void
scrobbler_schedule_handshake(struct scrobbler *scrobbler)
{
	assert(scrobbler->state == SCROBBLER_STATE_NOTHING);
	assert(scrobbler->handshake_source_id == 0);

	scrobbler->handshake_source_id =
		g_timeout_add_seconds(scrobbler->interval,
				      scrobbler_handshake_timer, scrobbler);
}

static void
scrobbler_send_now_playing(struct scrobbler *scrobbler, const char *artist,
			   const char *track, const char *album,
			   const char *mbid, const int length)
{
	GString *post_data;
	char len[MAX_VAR_SIZE];

	assert(scrobbler->state == SCROBBLER_STATE_READY);
	assert(scrobbler->submit_source_id == 0);

	scrobbler->state = SCROBBLER_STATE_SUBMITTING;

	snprintf(len, MAX_VAR_SIZE, "%i", length);

	post_data = g_string_new(NULL);
	add_var(post_data, "s", scrobbler->session);
	add_var(post_data, "a", artist);
	add_var(post_data, "t", track);
	add_var(post_data, "b", album);
	add_var(post_data, "l", len);
	add_var(post_data, "n", "");
	add_var(post_data, "m", mbid);

	g_message("sending 'now playing' notification to '%s'",
		  scrobbler->config->url);

	http_client_request(scrobbler->nowplay_url,
			    post_data->str,
			    scrobbler_submit_callback, scrobbler);

	g_string_free(post_data, true);
}

static void
scrobbler_schedule_now_playing_callback(gpointer data, gpointer user_data)
{
	struct scrobbler *scrobbler = data;
	struct song *song = user_data;

	if (scrobbler->state != SCROBBLER_STATE_READY)
		return;

	as_song_cleanup(&scrobbler->now_playing, false);

	scrobbler->now_playing.artist = g_strdup(song->artist);
	scrobbler->now_playing.track = g_strdup(song->track);
	scrobbler->now_playing.album = g_strdup(song->album);
	scrobbler->now_playing.mbid = g_strdup(song->mbid);
	scrobbler->now_playing.length = song->length;

	if (scrobbler->submit_source_id == 0)
		scrobbler_schedule_submit(scrobbler);
}

static void
scrobbler_schedule_submit_callback(gpointer data,
				   G_GNUC_UNUSED gpointer user_data)
{
	struct scrobbler *scrobbler = data;

	if (scrobbler->state == SCROBBLER_STATE_READY &&
	    scrobbler->submit_source_id == 0)
		scrobbler_schedule_submit(scrobbler);
}

void
as_now_playing(const char *artist, const char *track,
	       const char *album, const char *mbid, const int length)
{
	struct song song;

	song.artist = g_strdup(artist);
	song.track = g_strdup(track);
	song.album = g_strdup(album);
	song.mbid = g_strdup(mbid);
	song.length = length;

	g_slist_foreach(scrobblers,
			scrobbler_schedule_now_playing_callback, &song);
}

static void
scrobbler_submit(struct scrobbler *scrobbler)
{
	//MAX_SUBMIT_COUNT
	unsigned count = 0;
	GString *post_data;
	char len[MAX_VAR_SIZE];

	assert(scrobbler->state == SCROBBLER_STATE_READY);
	assert(scrobbler->submit_source_id == 0);

	if (g_queue_is_empty(queue)) {
		/* the submission queue is empty.  See if a "now playing" song is
		   scheduled - these should be sent after song submissions */
		if (scrobbler->now_playing.artist != NULL &&
		    scrobbler->now_playing.track != NULL) {
			scrobbler_send_now_playing(scrobbler,
						   scrobbler->now_playing.artist,
						   scrobbler->now_playing.track,
						   scrobbler->now_playing.album,
						   scrobbler->now_playing.mbid,
						   scrobbler->now_playing.length);
		}

		return;
	}

	scrobbler->state = SCROBBLER_STATE_SUBMITTING;

	/* construct the handshake url. */
	post_data = g_string_new(NULL);
	add_var(post_data, "s", scrobbler->session);

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
	g_debug("url: %s", scrobbler->submit_url);

	g_submit_pending = count;
	http_client_request(scrobbler->submit_url,
			    post_data->str,
			    &scrobbler_submit_callback, scrobbler);

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

	g_slist_foreach(scrobblers, scrobbler_schedule_submit_callback, NULL);

	return g_queue_get_length(queue);
}

void as_init(void)
{
	guint queue_length;
	struct scrobbler_config *current_host = &file_config.as_hosts;

	g_message("starting mpdscribble (" AS_CLIENT_ID " " AS_CLIENT_VERSION ")\n");

	queue = g_queue_new();
	journal_read(queue);

	queue_length = g_queue_get_length(queue);
	g_message("loaded %i song%s from cache\n",
		  queue_length, queue_length == 1 ? "" : "s");

	do {
		struct scrobbler *scrobbler = scrobbler_new(current_host);
		scrobblers = g_slist_prepend(scrobblers, scrobbler);
		scrobbler_schedule_handshake(scrobbler);
	} while((current_host = current_host->next));
}

static gboolean
scrobbler_submit_timer(gpointer data)
{
	struct scrobbler *scrobbler = data;

	assert(scrobbler->state == SCROBBLER_STATE_READY);

	scrobbler->submit_source_id = 0;

	scrobbler_submit(scrobbler);
	return false;
}

static void
scrobbler_schedule_submit(struct scrobbler *scrobbler)
{
	assert(scrobbler->submit_source_id == 0);
	assert(!g_queue_is_empty(queue) ||
	       (scrobbler->now_playing.artist != NULL &&
		scrobbler->now_playing.track != NULL));

	scrobbler->submit_source_id =
		g_timeout_add_seconds(scrobbler->interval,
				      scrobbler_submit_timer, scrobbler);
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

static void
scrobbler_free_callback(gpointer data, G_GNUC_UNUSED gpointer user_data)
{
	struct scrobbler *scrobbler = data;

	scrobbler_free(scrobbler);
}

void as_cleanup(void)
{
	as_save_cache();

	g_slist_foreach(scrobblers, scrobbler_free_callback, NULL);
	g_slist_free(scrobblers);

	g_queue_foreach(queue, free_queue_song, NULL);
	g_queue_free(queue);
}
