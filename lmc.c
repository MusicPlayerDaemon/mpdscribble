/*
    This file is part of mpdscribble,
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

#include "lmc.h"
#include "misc.h"
#include "file.h"

#include <glib.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static mpd_Connection *g_mpd = NULL;
static int last_id = -1;
static struct mpd_song *current_song;
static bool was_paused;

static char *g_host;
static int g_port;

static guint update_source_id;

static void
lmc_schedule_update(void);

static void lmc_failure(void)
{
	char *msg = g_strescape(g_mpd->errorStr, NULL);

	warning("mpd error (%i): %s", g_mpd->error, msg);
	g_free(msg);
	mpd_closeConnection(g_mpd);
	g_mpd = 0;
}

static int lmc_reconnect(void)
{
	char *at = strchr(g_host, '@');
	char *host = g_host;
	char *password = NULL;

	if (at) {
		host = at + 1;
		password = g_strndup(g_host, at - g_host);
	}

	g_mpd = mpd_newConnection(host, g_port, 10);
	if (g_mpd->error) {
		lmc_failure();
		return 0;
	}

	if (password) {
		notice("sending password ... ");

		mpd_sendPasswordCommand(g_mpd, password);
		mpd_finishCommand(g_mpd);
		free(password);
	}

	if (g_mpd->error) {
		lmc_failure();
		return 0;
	}

	notice("connected to mpd %i.%i.%i at %s:%i.",
	       g_mpd->version[0], g_mpd->version[1], g_mpd->version[2],
	       host, g_port);

	return 1;
}

void lmc_connect(char *host, int port)
{
	g_host = host;
	g_port = port;
	lmc_reconnect();
	lmc_schedule_update();
}

void lmc_disconnect(void)
{
	if (g_mpd) {
		mpd_closeConnection(g_mpd);
		g_mpd = 0;
	}
}

int lmc_current(struct mpd_song **song_r)
{
	mpd_Status *status;
	int state;
	struct mpd_InfoEntity *entity;

	if (!g_mpd) {
		warning("waiting 15 seconds before reconnecting.");
		sleep(15);
		warning("attempting to reconnect to mpd... ");
		lmc_reconnect();
		return MPD_STATUS_STATE_UNKNOWN;
	}

	mpd_sendCommandListOkBegin(g_mpd);
	mpd_sendStatusCommand(g_mpd);
	mpd_sendCurrentSongCommand(g_mpd);
	mpd_sendCommandListEnd(g_mpd);

	status = mpd_getStatus(g_mpd);
	if (!status) {
		lmc_failure();
		warning("waiting 15 seconds before reconnecting.");
		sleep(15);
		warning("attempting to reconnect to mpd... ");
		lmc_reconnect();
		return MPD_STATUS_STATE_UNKNOWN;
	}

	if (status->error) {
		/* FIXME: clearing stuff doesn't seem to help, it keeps printing the
		   same error over and over, so just let's just ignore these errors
		   for now. */
		//      warning ("mpd status error: %s\n", status->error);
		//      mpd_executeCommand(g_mpd, "clearerror");
		mpd_clearError(g_mpd);
	}

	state = status->state;
	mpd_freeStatus(status);

	if (state != MPD_STATUS_STATE_PLAY) {
		mpd_finishCommand(g_mpd);
		return state;
	}

	if (g_mpd->error) {
		lmc_failure();
		return MPD_STATUS_STATE_UNKNOWN;
	}

	mpd_nextListOkCommand(g_mpd);

	while ((entity = mpd_getNextInfoEntity(g_mpd)) != NULL
	       && entity->type != MPD_INFO_ENTITY_TYPE_SONG) {
		mpd_freeInfoEntity(entity);
	}

	if (entity == NULL) {
		mpd_finishCommand(g_mpd);
		return MPD_STATUS_STATE_UNKNOWN;
	}

	if (g_mpd->error) {
		mpd_freeInfoEntity(entity);
		lmc_failure();
		return MPD_STATUS_STATE_UNKNOWN;
	}

	mpd_finishCommand(g_mpd);
	if (g_mpd->error) {
		mpd_freeInfoEntity(entity);
		lmc_failure();
		return MPD_STATUS_STATE_UNKNOWN;
	}

	*song_r = mpd_songDup(entity->info.song);
	mpd_freeInfoEntity(entity);
	return MPD_STATUS_STATE_PLAY;
}

/**
 * Update: determine MPD's current song and enqueue submissions.
 */
static gboolean
lmc_update(G_GNUC_UNUSED gpointer data)
{
	struct mpd_song *prev;
	int elapsed;

	prev = current_song;
	elapsed = lmc_current(&current_song);

	if (elapsed == MPD_STATUS_STATE_PAUSE) {
		if (!was_paused)
			song_paused();
		was_paused = true;
		return true;
	} else if (elapsed != MPD_STATUS_STATE_PLAY) {
		current_song = NULL;
		last_id = -1;
	} else if (current_song->artist == NULL ||
		   current_song->title == NULL) {
		if (current_song->id != last_id) {
			notice("new song detected with tags missing (%s)",
			       current_song->file);
			last_id = current_song->id;
		}

		mpd_freeSong(current_song);
		current_song = NULL;
	}

	if (was_paused) {
		if (current_song != NULL && current_song->id == last_id)
			song_continued();
		was_paused = false;
	}

	/* submit the previous song */
	if (prev != NULL &&
	    (current_song == NULL || prev->id != current_song->id))
		song_ended(prev);

	/* new song. */
	if (current_song != NULL && current_song->id != last_id) {
		song_started(current_song);
		last_id = current_song->id;
	}

	if (prev != NULL)
		mpd_freeSong(prev);

	return true;
}

static void
lmc_schedule_update(void)
{
	assert(update_source_id == 0);

	update_source_id = g_timeout_add(file_config.sleep * 1000,
					 lmc_update, NULL);
}
