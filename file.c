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

#include "file.h"
#include "as.h"
#include "misc.h"
#include "config.h"

#include <glib.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <regex.h>

struct pair
{
  char *key;
  char *val;
  struct pair *next;
};

enum file_type { conf_type, cache_type, log_type, };

struct config file_config = {
  .loc = file_unknown,
};

FILE *file_loghandle = NULL;
int file_saved_count = 0;

static char *file_getname (enum file_type type);

static const char *blurb = 
  "mpdscribble (" AS_CLIENT_ID " " AS_CLIENT_VERSION ").\n"
  "another audioscrobbler plugin for music player daemon.\n"
  "Copyright 2005,2006 Kuno Woudt <kuno@frob.nl>.\n"
  "\n";


static void
version (void)
{
  printf (blurb);

  printf ("mpdscribble comes with NO WARRANTY, to the extent permitted by law.\n"
          "You may redistribute copies of mpdscribble under the terms of the\n"
          "GNU General Public License.\n"
          "For more information about these matters, see the file named COPYING.\n"
          "\n");

  exit (1);
}

static void
help (void)
{
  printf (blurb);

  printf ("Usage: mpdscribble [OPTIONS]\n"
          "\n"
          "  --help                      \tthis message\n"
          "  --version                   \tthat message\n"
          "  --log            <filename> \tlog file\n"
          "  --cache          <filename> \tcache file\n"
          "  --conf           <filename> \tconfiguration file\n"
          "  --host           <host>     \tmpd host\n"
          "  --port           <port>     \tmpd port\n"
          "  --proxy          <proxy>    \tHTTP proxy URI\n"
          "  --sleep          <interval> \tupdate interval (default 1 second)\n"
          "  --cache-interval <interval> \twrite cache file every i seconds\n"
          "                              \t(default 600 seconds)\n"
          "  --verbose <0-2>             \tverbosity (default 2)\n"
          "\n"
          "Report bugs to <kuno@frob.nl>.\n");

  exit (1);
}

static int
file_atoi (const char *s)
{
  if (!s)
    return 0;

  return atoi (s);
}

static void
free_pairs (struct pair *p)
{
  struct pair *n;

  if (!p)
    return;

  do
    {
      n = p->next;
      free (p->key);
      free (p->val);
      free (p);
      p = n;
    }
  while (p);
}

static void
add_pair (struct pair **stack, const char *ptr,
          int s0, int e0, int s1, int e1)
{
  char *key = (char *) calloc (e0 - s0 + 1, 1);
  char *val = (char *) calloc (e1 - s1 + 1, 1);
  struct pair *p = (struct pair *) malloc (sizeof (struct pair));
  struct pair *last;

  if (!p)
    exit (ENOMEM);

  memcpy (key, ptr + s0, e0 - s0);
  memcpy (val, ptr + s1, e1 - s1);

  p->key = key;
  p->val = val;
  p->next = NULL;

  if (!*stack)
    {
      *stack = p;
      return;
    }

  last = *stack;
  while (last->next)
    last = last->next;
  last->next = p;
}

static struct pair *
get_pair(const char *str)
{
  struct pair *p = NULL;
  const char *ptr;
  regex_t compiled;
  regmatch_t m[4];
  int error = 0;

  if ((error = regcomp (&compiled,
                        "^(#.*|[ \t]*|([A-Za-z_][A-Za-z0-9_]*) = (.*))$",
                        REG_NEWLINE | REG_EXTENDED)))
    fatal ("error %i when compiling regexp, this is a bug.\n", error);

  m[0].rm_eo = 0;
  ptr = str - 1;
  do
    {
      ptr += m[0].rm_eo + 1;
      if(*ptr == '\0') {
         break;
      }
      error = regexec (&compiled, ptr, 4, m, 0);
      if (!error && m[3].rm_eo != -1)
        add_pair (&p, ptr,
                  m[2].rm_so, m[2].rm_eo,
                  m[3].rm_so, m[3].rm_eo);
    }
  while (!error);

  regfree (&compiled);

  return p;
}

static char *
read_file(const char *filename)
{
  long size;
  char *ret;
  size_t count;

  FILE *handle = fopen (filename, "rb");
  if (!handle)
    {
      warning_errno ("error opening %s", filename);
      return NULL;
    }

  fseek (handle, 0, SEEK_END);
  size = ftell (handle);
  fseek (handle, 0, SEEK_SET);

  ret = (char *) calloc (size+1, 1);
  count = fread (ret, 1, size, handle);
  fclose (handle);

  if (count == size)
    return ret;

  warning_errno ("error reading %s", filename);
  free (ret);
  return NULL;
}

static int
file_exists(const char *filename)
{
  FILE *handle = fopen (filename, "rb");
  if (!handle)
    return 0;

  fclose (handle);
  return 1;
}

static char *
file_expand_tilde(const char *path)
{
  const char *home;

  if (path[0] != '~')
    return g_strdup(path);
    
  home = getenv ("HOME");
  if (!home)
    home = "./";

  return g_strconcat(home, path + 1, NULL);
}


static char *
file_getname (enum file_type type)
{
  char *file = NULL;

  switch (type)
    {
    case conf_type:
      file = file_expand_tilde (FILE_HOME_CONF);
      if (file_exists (file))
        {
          file_config.loc = file_home;
        }
      else
        {
          free (file);
          file = file_expand_tilde (FILE_CONF);
          if (!file_exists (file))
            {
              free (file);
              return NULL;
            }
          file_config.loc = file_etc;
        }
      break;

    case cache_type:
      if (file_config.loc == file_home)
        file = file_expand_tilde (FILE_HOME_CACHE);
      else if (file_config.loc == file_etc)
        file = file_expand_tilde (FILE_CACHE);
      break;
    case log_type:
      if (file_config.loc == file_home)
        file = file_expand_tilde (FILE_HOME_LOG);
      else if (file_config.loc == file_etc)
        file = file_expand_tilde (FILE_LOG);
      break;
    }

  if (!file)
    {
      switch (type)
        {
        case conf_type:
          fatal ("internal error. this is a bug.");
        case cache_type:
          fatal ("please specify where to put the cache file.");
        case log_type:
          fatal ("please specify where to put the log file.");
        }
    }

  return file;
}

FILE *
file_open_logfile (void)
{
  char *log = file_config.log;

  file_loghandle = fopen (log, "ab");
  if (!file_loghandle)
    fatal_errno ("cannot open %s", log);

  return file_loghandle;
}

static void
replace (char **dst, char *src)
{
  if (*dst)
    free (*dst);
  *dst = src;
}

int
file_read_config (int argc, char **argv)
{
  char *mpd_host = getenv ("MPD_HOST");
  char *mpd_port = getenv ("MPD_PORT");
  char *http_proxy = getenv ("http_proxy");
  char *data = NULL;
  int i;

  file_config.verbose = -1;

  /* look for config path in command-line options. */
  for (i = 0; i < argc; i++)
    {
      if (!strcmp ("--conf", argv[i]))
        replace (&file_config.conf, g_strdup(argv[++i]));
    }

  if (!file_config.conf
      || !file_exists (file_config.conf))
    {
      file_config.conf = file_getname (conf_type);
    }

  /* parse config file options. */
  if (file_config.conf && (data = read_file (file_config.conf)))
    {
      struct pair *p = get_pair (data);

      while (p)
        {
          if (!strcmp ("username", p->key))
            file_config.username = g_strdup(p->val);
          else if (!strcmp ("password", p->key))
            file_config.password = g_strdup(p->val);
          else if (!strcmp ("log", p->key))
            file_config.log = g_strdup(p->val);
          else if (!strcmp ("cache", p->key))
            file_config.cache = g_strdup(p->val);
          else if (!strcmp ("musicdir", p->key))
            file_config.musicdir = g_strdup(p->val);
          else if (!strcmp ("host", p->key))
            file_config.host = g_strdup(p->val);
          else if (!strcmp ("port", p->key))
            file_config.port = file_atoi (p->val);
          else if (!strcmp ("proxy", p->key))
            file_config.proxy = g_strdup(p->val);
          else if (!strcmp ("sleep", p->key))
            file_config.sleep = file_atoi (p->val);
          else if (!strcmp ("cache_interval", p->key))
            file_config.cache_interval = file_atoi (p->val);
          else if (!strcmp ("verbose", p->key))
            file_config.verbose = file_atoi (p->val);
          p = p->next;
        }

      free_pairs (p);
    }

  /* parse command-line options. */
  for (i = 0; i < argc; i++)
    {
      if (!strcmp ("--help", argv[i]))
        help ();
      else if (!strcmp ("--version", argv[i]))
        version ();
      else if (!strcmp ("--host", argv[i]))
        replace (&file_config.host, g_strdup(argv[++i]));
      else if (!strcmp ("--log", argv[i]))
        replace (&file_config.log, g_strdup(argv[++i]));
      else if (!strcmp ("--cache", argv[i]))
        replace (&file_config.cache, g_strdup(argv[++i]));
      else if (!strcmp ("--port", argv[i]))
        file_config.port = file_atoi (argv[++i]);
      else if (!strcmp ("--sleep", argv[i]))
        file_config.sleep = file_atoi (argv[++i]);
      else if (!strcmp ("--cache-interval", argv[i]))
        file_config.cache_interval = file_atoi (argv[++i]);
      else if (!strcmp ("--verbose", argv[i]))
        file_config.verbose = file_atoi (argv[++i]);
      else if (!strcmp ("--proxy", argv[i]))
        file_config.proxy = g_strdup(argv[++i]);
    }

  if (!file_config.conf)
    fatal ("cannot find configuration file");

  if (!file_config.username)
    fatal ("no audioscrobbler username specified in %s", file_config.conf);
  if (!file_config.password)
    fatal ("no audioscrobbler password specified in %s", file_config.conf);
  if (!file_config.host)
    file_config.host = g_strdup(mpd_host);
  if (!file_config.host)
    file_config.host = g_strdup(FILE_DEFAULT_HOST);
  if (!file_config.log)
    file_config.log = file_getname (log_type);
  if (!file_config.cache)
    file_config.cache = file_getname (cache_type);
  if (!file_config.port && mpd_port)
    file_config.port = file_atoi (mpd_port);
  if (!file_config.port)
    file_config.port = FILE_DEFAULT_PORT;
  if (!file_config.proxy)
    file_config.proxy = http_proxy;
  if (!file_config.sleep)
    file_config.sleep = 1;
  if (!file_config.cache_interval)
    file_config.cache_interval = 600;
  if (file_config.verbose == -1)
    file_config.verbose = 2;

  free (data);
  return 1;
}

void
file_cleanup (void)
{
  if (file_config.username)
    free (file_config.username);
  if (file_config.password)
    free (file_config.password);
  if (file_config.host)
    free (file_config.host);
  if (file_config.log)
    free (file_config.log);
  if (file_config.conf)
    free (file_config.conf);
  if (file_config.cache)
    free (file_config.cache);

  if (file_loghandle)
    fclose (file_loghandle);
}

int
file_write_cache (struct song *sng)
{
  struct song *tmp = sng;
  int count = 0;
  FILE *handle;

  if (!tmp && file_saved_count == 0)
    return -1;

  handle = fopen (file_config.cache, "wb");
  if (!handle)
    {
      warning_errno ("error opening %s", file_config.cache);
      return 0;
    }

  while (tmp)
    {
      fprintf (handle, "# song %i in queue\na = %s\nt = %s\nb = %s\nm = %s\n"
               "i = %s\nl = %i\n\n", ++count, tmp->artist, tmp->track,
               tmp->album, tmp->mbid, tmp->time, tmp->length);

      tmp = tmp->next;
    }

  fclose (handle);

  file_saved_count = count;
  return count;
}

static void
clear_song (struct song *s)
{
  s->artist = NULL;
  s->track = NULL;
  s->album = NULL;
  s->mbid = NULL;
  s->time = NULL;
  s->length = 0;
}

int
file_read_cache (void)
{
  char *data;
  int count = 0;

  if ((data = read_file (file_config.cache)))
    {
      struct pair *root = get_pair (data);
      struct pair *p = root;
      struct song sng;

      clear_song (&sng);

      while (p)
        {
          if (!strcmp ("a", p->key)) sng.artist = g_strdup(p->val);
          if (!strcmp ("t", p->key)) sng.track = g_strdup(p->val);
          if (!strcmp ("b", p->key)) sng.album = g_strdup(p->val);
          if (!strcmp ("m", p->key)) sng.mbid = g_strdup(p->val);
          if (!strcmp ("i", p->key)) sng.time = g_strdup(p->val);
          if (!strcmp ("l", p->key))
            {
              sng.length = file_atoi (p->val);

              as_songchange ("", sng.artist, sng.track, sng.album,
                             sng.mbid, sng.length, sng.time);

              count++;

              if (sng.artist) { free (sng.artist); sng.artist = NULL; }
              if (sng.track)  { free (sng.track);  sng.track  = NULL; }
              if (sng.album)  { free (sng.album);  sng.album  = NULL; }
              if (sng.mbid)   { free (sng.mbid);   sng.mbid   = NULL; }
              if (sng.time)   { free (sng.time);   sng.time   = NULL; }

              clear_song (&sng);
            }

          p = p->next;
        }

      free_pairs (p);
    }

  file_saved_count = count;

  free (data);
  return count;
}
