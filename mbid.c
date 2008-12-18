/*

mbid.c v1.0

LICENSE

Copyright (c) 2006, David Nicolson
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
  3. Neither the name of the author nor the names of its contributors
     may be used to endorse or promote products derived from this software
     without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT UNLESS REQUIRED BY
LAW OR AGREED TO IN WRITING WILL ANY COPYRIGHT HOLDER OR CONTRIBUTOR
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


History/Changes:

2006-05-28  Kuno Woudt <kuno@frob.nl

  Added .ogg support. (well working on it anyway).
    
*/

#include "mbid.h"

#include <glib.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifndef __NT__
#include <sys/errno.h>
#endif

#ifdef MBID_DEBUG
#define debug(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define debug(...)
#endif


static unsigned
toSynchSafe(const uint8_t *bytes)
{
    return ((unsigned)bytes[0] << 21) + ((unsigned)bytes[1] << 14) +
      ((unsigned)bytes[2] << 7) + (unsigned)bytes[3];
}

static unsigned
toInteger(const uint8_t *bytes)
{
    return GUINT32_FROM_BE(*(const uint32_t*)bytes);
}

static bool
mfile(size_t length, void *ret, FILE *fp)
{
  return fread(ret, 1, length, fp) == length;
}

/* (yes, the ogg parser is VERY dirty, sorry, I was in a hurry :P). 
 * I expect most .ogg files will have less than 8kbyte of metadata. */
#define OGG_MAX_CHUNK_SIZE 0x2000 

static size_t
read_ogg_chunk_length (FILE *fp)
{
  uint32_t data;
  size_t bytes = fread (&data, sizeof(data), 1, fp);
  if (bytes != 1)
    {
      debug ("Failed to read ogg chunk.\n");
      return -1;
    }

  return GUINT32_FROM_LE(data);
}

static int
read_ogg_chunk (char *data, FILE *fp)
{
  size_t length = read_ogg_chunk_length (fp);
  size_t bytes;

  if (length >= OGG_MAX_CHUNK_SIZE)
    { 
      /* chunk way to big for us to care about, just skip it. */
      fseek (fp, length, SEEK_CUR);
      data[0] = 0x00;
      return 0;
    }

  bytes = fread (data, 1, length, fp);
  if (bytes < length)
    {
      debug ("Error reading ogg file.\n");
      return -1;
    }

  data[length] = 0x00;

  return 0;
}

static int
getOGG_MBID(const char *path, char mbid[MBID_BUFFER_SIZE]) 
{
  FILE *fp;
  size_t bytes, marker_size, items;
  const char *marker = "\003vorbis";
  char data[OGG_MAX_CHUNK_SIZE]; 
  int offset;

  if (path == NULL)
    {
      debug("Received null path\n");
      errno = EINVAL;
      return -1;
    }

  fp = fopen(path,"rb");
  if (fp == NULL) 
    {
      debug("Failed to open music file: %s\n", path);
      return -1;
    }

  bytes = fread (data, 1, OGG_MAX_CHUNK_SIZE, fp);
  if (bytes != OGG_MAX_CHUNK_SIZE)
    goto ogg_failed;

  marker_size = strlen (marker);

  offset = -1;
  for (size_t i=0; i < OGG_MAX_CHUNK_SIZE - marker_size; i++)
    if (!strncmp (data+i, marker, marker_size))
      {
        offset = i + marker_size;
        break;
      }

  if (offset < 0)
    goto ogg_failed;

  if (fseek (fp, offset, SEEK_SET))
    goto ogg_failed;

  // actual (dirty) ogg parsing starts here. 
  if (read_ogg_chunk (data, fp))
    goto ogg_failed;

  items = read_ogg_chunk_length (fp);
  for (size_t i = 0; i < items; i++)
    {
      if (read_ogg_chunk (data, fp))
        goto ogg_failed;

      if (!strncmp (data, "MUSICBRAINZ_TRACKID", strlen ("MUSICBRAINZ_TRACKID")))
        { 
          g_strlcpy(mbid, data + strlen("MUSICBRAINZ_TRACKID="),
                    MBID_BUFFER_SIZE);
          fclose (fp);
          return 0;
        }
    }

ogg_failed:
  fclose (fp);
  debug ("Failed to parse .ogg file: %s\n", path);
  return -1;
}

/* I expect most .flac files will have less than 8kbyte of metadata. 
 * (yes, the flac parser is fairly dirty, sorry, I was in a hurry :P). */
#define FLAC_MAX_CHUNK_SIZE 0x2000

static int
read_flac_block(FILE *fp)
{
  int size;
  int bytes;
  unsigned char header[4];
  
  bytes = fread (header, 1, 4, fp);
  if (bytes != 4)
    return -1;

  size = header[3] | header[2]<<0x08 | header[1]<<0x10;

  if ((header[0] & 0x7F) == 0x04)
    return 0;

  fseek (fp, size, SEEK_CUR);
  
  return -1;
}

static int
getFLAC_MBID(const char *path, char mbid[MBID_BUFFER_SIZE]) 
{
  FILE *fp;
  size_t bytes, items;
  char data[FLAC_MAX_CHUNK_SIZE];

  if (path == NULL)
    {
      debug("Received null path\n");
      errno = EINVAL;
      return -1;
    }

  fp = fopen(path,"rb");
  if (fp == NULL) 
    {
      debug("Failed to open music file: %s\n", path);
      return -1;
    }

  bytes = fread (data, 1, 4, fp);
  if (bytes < 4)
    goto flac_failed;

  if (strncmp ("fLaC", data, 4))
    goto flac_failed;  // not a flac file.

  while (!feof (fp)) 
    {
      if (read_flac_block(fp))
        continue;

      // actual vorbis comment  parsing starts here. 
      if (read_ogg_chunk (data, fp))
        goto flac_failed;

      items = read_ogg_chunk_length (fp);
      for (size_t i = 0; i < items; i++)
        {
          if (read_ogg_chunk (data, fp))
            goto flac_failed;

          if (!strncmp (data, "MUSICBRAINZ_TRACKID", strlen ("MUSICBRAINZ_TRACKID")))
            { 
              g_strlcpy(mbid, data + strlen("MUSICBRAINZ_TRACKID="),
                        MBID_BUFFER_SIZE);
              fclose (fp);
              return 0;
            }
        }

      break;
    }

flac_failed:
  fclose (fp);
  debug ("Failed to parse .flac file: %s\n", path);
  return -1;
}

static int
getMP3_MBID(const char *path, char mbid[MBID_BUFFER_SIZE])
{

    FILE *fp;
    char head[3];
    char version[2];
    char flag[1];
    uint8_t size[4];
    uint8_t size_extended[4];
    unsigned tag_size;
    unsigned extended_size;
    uint8_t frame[4];
    uint8_t frame_header[4];
    unsigned frame_size;

    if (path == NULL) {
        debug("Received null path\n");
        errno = EINVAL;
        return -1;
    }

    fp = fopen(path,"rb");
    if (fp == NULL) {
        debug("Failed to open music file: %s\n",path);
        return -1;
    }

    while (true) {
        bool ret;
        int version_major;

        ret = mfile(3, head, fp) && mfile(2, version, fp) &&
          mfile(1, flag, fp);
        if (!ret || !strncmp(head,"ID3",3) == 0) {
            debug("No ID3v2 tag found: %s\n",path);
            break;
        }

        version_major = (int)version[0];
        if (version_major == 2) {
            debug("ID3v2.2.0 does not support MBIDs: %s\n",path);
            break;
        }
        if (version_major != 3 && version_major != 4) {
            debug("Unsupported ID3 version: v2.%d.%d\n",version_major,(int)version[1]);
            break;
        }

        if ((unsigned int)flag[0] & 0x00000040) {
            debug("Extended header found\n");
            if (version[0] == 4) {
                ret = mfile(4, size_extended, fp);
                if (!ret)
                    break;

                extended_size = toSynchSafe(size_extended);
            } else {
                ret = mfile(4, size_extended, fp);
                if (!ret)
                    break;

                extended_size = toInteger(size_extended);
            }
            debug("Extended header size: %d\n",extended_size);
            fseek(fp,extended_size,SEEK_CUR);
        }
    
        ret = mfile(4, size, fp);
        if (!ret)
            break;

        tag_size = toSynchSafe(size);
        debug("Tag size: %d\n",tag_size);

        while (true) {
            if (ftell(fp) > tag_size || ftell(fp) > 1048576) {
                break;
            }

            ret = mfile(4, frame, fp) && mfile(4, frame_header, fp);
            if (!ret || frame[0] == 0x00) {
                break;
            }
            if (version_major == 4) {
                frame_size = toSynchSafe(frame_header);
            } else {
                frame_size = toInteger(frame_header);
            }

            fseek(fp,2,SEEK_CUR);
            debug("Reading %d bytes from frame %s\n",frame_size,frame);

            if (memcmp(frame,"UFID",4) == 0) {
                char frame_data[frame_size + 1];
                ret = mfile(frame_size, frame_data, fp);
                if (ret && frame_size >= 59 &&
                    strncmp(frame_data, "http://musicbrainz.org", 22) == 0) {
                    char *tmbid = frame_data;
                    tmbid = frame_data + 23;
                    frame_data[frame_size] = 0;
                    g_strlcpy(mbid, tmbid, MBID_BUFFER_SIZE);
                    fclose(fp);
                    return 0;
                }
            } else {
                fseek(fp,frame_size,SEEK_CUR);
            }
        }
        break;
    }
    
    if (fp) {
        fclose(fp);
    }

    debug("Failed to read music file: %s\n",path);
    return -1;

}

int 
getMBID(const char *path, char mbid[MBID_BUFFER_SIZE])
{
  if (getFLAC_MBID (path, mbid)
      && getMP3_MBID (path, mbid) 
      && getOGG_MBID (path, mbid))
      return -1;
  
  return 0; 
}

