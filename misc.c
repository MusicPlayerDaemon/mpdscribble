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

#include "misc.h"

#include <sys/time.h>
#include <unistd.h>

#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

static FILE *g_log;
static int g_verbose;


static const char *
log_date (void)
{
  static char buf[20];
  time_t t;
  struct tm *tmp;

  t = time(NULL);
  tmp = gmtime(&t);
  if (tmp==NULL) {
    buf[0] = 0;
    return buf;
  }

  if (!strftime(buf, sizeof(buf), "%Y/%m/%d %H:%M:%S", tmp)) {
    buf[0] = 0;
    return buf;
  }
  return buf;
}

long
now (void)
{
  struct timeval tv;

  if (!gettimeofday (&tv, NULL))
    return tv.tv_sec;

  warning ("error getting current time, this is probably a bug.\n");
  return 0;
}

void
set_logfile (FILE *log, int verbose)
{
  g_log = log;
  g_verbose = verbose;
}

void
fatal (const char *template, ...)
{
  va_list ap;

  fprintf (g_log, "[%s] fatal: ", log_date());

  va_start (ap, template);
  (void) vfprintf (g_log, template, ap);
  va_end (ap);
  fprintf (g_log, "\n");

  exit (EXIT_FAILURE);
}

void
fatal_errno (const char *template, ...)
{
  va_list ap;

  fprintf (g_log, "[%s] fatal: ", log_date());

  va_start (ap, template);
  (void) vfprintf (g_log, template, ap);
  va_end (ap);
  fprintf (g_log, ": %s\n", strerror (errno));

  exit (EXIT_FAILURE);
}


void
warning (const char *template, ...)
{
  va_list ap;

  if (g_verbose < 1)
    return;

  fprintf (g_log, "[%s] warning: ", log_date());

  va_start (ap, template);
  (void) vfprintf (g_log, template, ap);
  va_end (ap);
  fprintf (g_log, "\n");
}

void
warning_errno (const char *template, ...)
{
  va_list ap;

  if (g_verbose < 1)
    return;

  fprintf (g_log, "[%s] warning: ", log_date());

  va_start (ap, template);
  (void) vfprintf (g_log, template, ap);
  va_end (ap);
  fprintf (g_log, ": %s\n", strerror (errno));
}


void
notice (const char *template, ...)
{
  va_list ap;

  if (g_verbose < 2)
    return;

  fprintf (g_log, "[%s] notice: ", log_date());

  va_start (ap, template);
  (void) vfprintf (g_log, template, ap);
  va_end (ap);
  fprintf (g_log, "\n");
}

/* concat originally from gnu libc manual,
   this version free()s everything submitted to it. */
char *
concatDX (char *str, ...)
{
  va_list ap;
  size_t allocated = 100;
  char *result = (char *) malloc (allocated);
  char *wp;
  char *s;

  if (result)
    {
      char *newp;

      va_start (ap, str);

      wp = result;
      for (s = str; s != NULL; s = va_arg (ap, char *))
        {
          size_t len = strlen (s);

          /* Resize the allocated memory if necessary. */
          if (wp + len + 1 > result + allocated)
            {
              allocated = (allocated + len) * 2;
              newp = (char *) realloc (result, allocated);
              if (newp == NULL)
                {
                  free (result);
                  return NULL;
                }
              wp = newp + (wp - result);
              result = newp;
            }

          memcpy (wp, s, len);
          wp += len;

          free (s);
        }

      /* Terminate the result string. */
      *wp++ = '\0';

      /* Resize memory to the optimal size. */
      newp = realloc (result, wp - result);
      if (newp != NULL)
        result = newp;

      va_end (ap);
    }

  return result;
}
