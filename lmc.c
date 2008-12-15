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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lmc.h"
#include "misc.h"

static mpd_Connection *g_mpd = NULL;
mpd_InfoEntity *g_entity = NULL;

static char *g_host;
static int g_port;
static int g_xfade_hack;

static void
lmc_failure ()
{
  char *ch;
  for (ch = g_mpd->errorStr; *ch; ++ch) {
    if (*ch=='\n' || *ch=='\t' || *ch=='\r' || *ch=='\v') {
      *ch = ' ';
    }
  }

  warning ("mpd error (%i): %s", g_mpd->error, g_mpd->errorStr);
  mpd_closeConnection (g_mpd);
  g_mpd = 0;
}

static int
lmc_reconnect (void)
{
  char *at = strchr (g_host, '@');
  char *host = g_host;
  char *password = NULL;

  if (at)
    {
      host = at+1;
      password = strndup2 (g_host, at-g_host);
    }

  g_mpd = mpd_newConnection (host, g_port, 10);
  if (g_mpd->error)
    {
      lmc_failure ();
      return 0;
    }

  if (password)
    {
      notice ("sending password ... ");

      mpd_sendPasswordCommand(g_mpd, password);
      mpd_finishCommand(g_mpd);
      free (password);
    }

  if (g_mpd->error)
    {
      lmc_failure ();
      return 0;
    }

  notice ("connected to mpd %i.%i.%i at %s:%i.",
          g_mpd->version[0], g_mpd->version[1], g_mpd->version[2],
          host, g_port);

  return 1;
}

void
lmc_connect (char *host, int port)
{
  g_host = host;
  g_port = port;
  g_xfade_hack = 0;
  lmc_reconnect ();
}

void
lmc_disconnect (void)
{
  if (g_entity)
    {
      mpd_freeInfoEntity (g_entity);
      g_entity = 0;
    }

  if (g_mpd)
    {
      mpd_closeConnection (g_mpd);
      g_mpd = 0;
    }
}

int
lmc_xfade_hack ()
{
    // call lmc_current to update this value.
    return g_xfade_hack;
}

int
lmc_current (struct mpd_song *songptr)
{
  struct mpd_song *song;
  mpd_Status *status;
  int elapsed;

  if (!g_mpd)
    {
      warning ("waiting 15 seconds before reconnecting.");
      sleep (15);
      warning ("attempting to reconnect to mpd... ");
      lmc_reconnect ();
      return LMC_NOTPLAYING;
    }

  mpd_sendCommandListOkBegin(g_mpd);
  mpd_sendStatusCommand(g_mpd);
  mpd_sendCurrentSongCommand(g_mpd);
  mpd_sendCommandListEnd(g_mpd);

  status = mpd_getStatus (g_mpd);
  if (!status)
    {
      lmc_failure ();
      warning ("waiting 15 seconds before reconnecting.");
      sleep (15);
      warning ("attempting to reconnect to mpd... ");
      lmc_reconnect ();
      return LMC_NOTPLAYING;
    }

  if (status->error)
    {
      /* FIXME: clearing stuff doesn't seem to help, it keeps printing the
         same error over and over, so just let's just ignore these errors
         for now. */
      //      warning ("mpd status error: %s\n", status->error);
      //      mpd_executeCommand(g_mpd, "clearerror");
      mpd_clearError (g_mpd);
    }

  if (status->state == MPD_STATUS_STATE_PAUSE)
    {
      elapsed = LMC_PAUSED;
    }
  else if (status->state != MPD_STATUS_STATE_PLAY)
    {
      elapsed = LMC_NOTPLAYING;
    }
  else
    {
      elapsed = status->elapsedTime;

      if (g_mpd->error)
        {
          lmc_failure ();
          return 0;
        }

      mpd_nextListOkCommand(g_mpd);

      if (g_entity)
        {
          mpd_freeInfoEntity (g_entity);
          g_entity = NULL;
        }

      while ((g_entity = mpd_getNextInfoEntity (g_mpd))
             && (g_entity->type != MPD_INFO_ENTITY_TYPE_SONG))
        {
          mpd_freeInfoEntity (g_entity);
          g_entity = NULL;
        }

      if (!g_entity)
        {
          elapsed = -1;
        }
      else
        {
          song = g_entity->info.song;
          memcpy (songptr, song, sizeof(*songptr));

          if (g_mpd->error)
            {
              lmc_failure ();
              return 0;
            }
        }
    }

  mpd_finishCommand(g_mpd);
  if (g_mpd->error)
    {
      lmc_failure ();
      return 0;
    }

  g_xfade_hack = status->crossfade;

  mpd_freeStatus(status);

  return elapsed;
}
