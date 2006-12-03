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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifndef __NT__
#include <sys/errno.h>
#endif

#include "mbid.h"

#ifdef MBID_DEBUG
#define debug(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define debug(fmt, ...)
#endif


int toSynchSafe(char bytes[]) {
    return ((int)bytes[0] << 21) + ((int)bytes[1] << 14) + ((int)bytes[2] << 7) + (int)bytes[3];
}

int toInteger(char bytes[]) {
    int size = 0;
    int i;
    for (i=0; i<sizeof(bytes); i++) {
        size = size * 256 + ((int)bytes[i] & 0x000000FF);
    }
    return size;
}

int mfile(int length, char ret[], FILE *fp, int *s) {
    int bytes = fread(ret,1,length,fp);
    
    if (bytes != length) {
        *s = 0;
    }
}

/* (yes, the ogg parser is VERY dirty, sorry, I was in a hurry :P). 
 * I expect most .ogg files will have less than 8kbyte of metadata. */
#define OGG_MAX_CHUNK_SIZE 0x2000 

int read_ogg_chunk_length (FILE *fp)
{
  unsigned char data[4];

  int bytes = fread (data, 1, 4, fp);
  if (bytes != 4)
    {
      debug ("Failed to read ogg chunk.\n");
      return -1;
    }

  int length = data[0]
    | data[1]<<0x08
    | data[2]<<0x10
    | data[3]<<0x18;

  return length;
}


int read_ogg_chunk (char *data, FILE *fp)
{
  int length = read_ogg_chunk_length (fp);

  if (length >= OGG_MAX_CHUNK_SIZE)
    { 
      /* chunk way to big for us to care about, just skip it. */
      fseek (fp, length, SEEK_CUR);
      data[0] = 0x00;
      return 0;
    }

  int bytes = fread (data, 1, length, fp);
  if (bytes < length)
    {
      debug ("Error reading ogg file.\n");
      return -1;
    }

  data[length] = 0x00;

  return 0;
}

int 
getOGG_MBID(const char *path, char mbid[MBID_BUFFER_SIZE]) 
{
  FILE *fp;
  int i;
  char *marker = "\003vorbis";
  char data[OGG_MAX_CHUNK_SIZE]; 

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

  int bytes = fread (data, 1, OGG_MAX_CHUNK_SIZE, fp);
  int marker_size = strlen (marker);

  int offset = -1;
  for (i=0; i < OGG_MAX_CHUNK_SIZE - marker_size; i++)
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

  int items = read_ogg_chunk_length (fp);
  for (i = 0; i < items; i++)
    {
      if (read_ogg_chunk (data, fp))
        goto ogg_failed;

      if (!strncmp (data, "MUSICBRAINZ_TRACKID", strlen ("MUSICBRAINZ_TRACKID")))
        { 
          strncpy (mbid, data + strlen ("MUSICBRAINZ_TRACKID="), MBID_BUFFER_SIZE);
          mbid[MBID_BUFFER_SIZE-1] = 0x00;
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

int
read_flac_block (char *data, FILE *fp)
{
  int size;
  int bytes;
  unsigned char header[4];
  
  fread (header, 1, 4, fp);
  size = header[3] | header[2]<<0x08 | header[1]<<0x10;

  if ((header[0] & 0x7F) == 0x04)
    return 0;

  fseek (fp, size, SEEK_CUR);
  
  return -1;
}

int 
getFLAC_MBID(const char *path, char mbid[MBID_BUFFER_SIZE]) 
{
  FILE *fp;
  int i = 0;
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

  int bytes = fread (data, 1, 4, fp);
  if (bytes < 4)
    goto flac_failed;

  if (strncmp ("fLaC", data, 4))
    goto flac_failed;  // not a flac file.

  while (!feof (fp)) 
    {
      if (read_flac_block (data, fp))
        continue;

      // actual vorbis comment  parsing starts here. 
      if (read_ogg_chunk (data, fp))
        goto flac_failed;

      int items = read_ogg_chunk_length (fp);
      for (i = 0; i < items; i++)
        {
          if (read_ogg_chunk (data, fp))
            goto flac_failed;

          if (!strncmp (data, "MUSICBRAINZ_TRACKID", strlen ("MUSICBRAINZ_TRACKID")))
            { 
              strncpy (mbid, data + strlen ("MUSICBRAINZ_TRACKID="), MBID_BUFFER_SIZE);
              mbid[MBID_BUFFER_SIZE-1] = 0x00;
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

int getMP3_MBID(const char *path, char mbid[MBID_BUFFER_SIZE]) {

    FILE *fp;
    static int s = 1;
    char head[3];
    char version[2];
    char flag[1];
    char size[4];
    char size_extended[4];
    int tag_size = 0;
    int extended_size = 0;
    char frame[4];
    char frame_header[4];
    int frame_size = 0;

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

    while (s) {
        mfile(3,head,fp,&s);
        if (!strncmp(head,"ID3",3) == 0) {
            debug("No ID3v2 tag found: %s\n",path);
            break;
        }

        mfile(2,version,fp,&s);
        int version_major = (int)version[0];
        int version_minor = (int)version[1];
        if (version_major == 2) {
            debug("ID3v2.2.0 does not support MBIDs: %s\n",path);
            break;
        }
        if (version_major != 3 && version_major != 4) {
            debug("Unsupported ID3 version: v2.%d.%d\n",version_major,version_minor);
            break;
        }

        mfile(1,flag,fp,&s);
        if ((unsigned int)flag[0] & 0x00000040) {
            debug("Extended header found\n");
            if (version[0] == 4) {
                mfile(4,size_extended,fp,&s);
                extended_size = toSynchSafe(size_extended);
            } else {
                mfile(4,size_extended,fp,&s);
                extended_size = toInteger(size_extended);
            }
            debug("Extended header size: %d\n",extended_size);
            fseek(fp,extended_size,SEEK_CUR);
        }
    
        mfile(4,size,fp,&s);
        tag_size = toSynchSafe(size);
        debug("Tag size: %d\n",tag_size);

        while (s) {
            if (ftell(fp) > tag_size || ftell(fp) > 1048576) {
                break;
            }

            mfile(4,frame,fp,&s);
            if (frame[0] == 0x00) {
                break;
            }
            if (version_major == 4) {
                mfile(4,frame_header,fp,&s);
                frame_size = toSynchSafe(frame_header);
            } else {
                mfile(4,frame_header,fp,&s);
                frame_size = toInteger(frame_header);
            }

            fseek(fp,2,SEEK_CUR);
            debug("Reading %d bytes from frame %s\n",frame_size,frame);

            if (strncmp(frame,"UFID",4) == 0) {
                char frame_data[frame_size];
                mfile(frame_size,frame_data,fp,&s);
                if (frame_size >= 59 && strncmp(frame_data,"http://musicbrainz.org",22) == 0) {
                    char *tmbid = frame_data;
                    tmbid = frame_data + 23;
                    strncpy(mbid,tmbid,MBID_BUFFER_SIZE-1);
                    mbid[MBID_BUFFER_SIZE-1] = 0x00;
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
    if (!s) {
        debug("Failed to read music file: %s\n",path);
    }                
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

