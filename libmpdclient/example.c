/* libmpdclient
   (c)2003-2006 by Warren Dukes (warren.dukes@gmail.com)
   This project's homepage is: http://www.musicpd.org

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   - Neither the name of the Music Player Daemon nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "libmpdclient.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char ** argv) {
	mpd_Connection * conn;
	char *hostname = getenv("MPD_HOST");
	char *port = getenv("MPD_PORT");

	if(hostname == NULL)
		hostname = "localhost";
	if(port == NULL)
		port = "6600";

	conn = mpd_newConnection(hostname,atoi(port),10);

	if(conn->error) {
		fprintf(stderr,"%s\n",conn->errorStr);
		mpd_closeConnection(conn);
		return -1;
	}

	{
		int i;
		for(i=0;i<3;i++) {
			printf("version[%i]: %i\n",i,conn->version[i]);
		}
	}

	if(argc==1) {
		mpd_Status * status;
		mpd_InfoEntity * entity;

		mpd_sendCommandListOkBegin(conn);
		mpd_sendStatusCommand(conn);
		mpd_sendCurrentSongCommand(conn);
		mpd_sendCommandListEnd(conn);

		if((status = mpd_getStatus(conn))==NULL) {
			fprintf(stderr,"%s\n",conn->errorStr);
			mpd_closeConnection(conn);
			return -1;
		}

		printf("volume: %i\n",status->volume);
		printf("repeat: %i\n",status->repeat);
		printf("playlist: %lli\n",status->playlist);
		printf("playlistLength: %i\n",status->playlistLength);
		if(status->error) printf("error: %s\n",status->error);

		if(status->state == MPD_STATUS_STATE_PLAY || 
				status->state == MPD_STATUS_STATE_PAUSE) {
			printf("song: %i\n",status->song);
			printf("elaspedTime: %i\n",status->elapsedTime);
			printf("totalTime: %i\n",status->totalTime);
			printf("bitRate: %i\n",status->bitRate);
			printf("sampleRate: %i\n",status->sampleRate);
			printf("bits: %i\n",status->bits);
			printf("channels: %i\n",status->channels);
		}

		if(conn->error) {
			fprintf(stderr,"%s\n",conn->errorStr);
			mpd_closeConnection(conn);
			return -1;
		}

		mpd_nextListOkCommand(conn);

		while((entity = mpd_getNextInfoEntity(conn))) {
			mpd_Song * song = entity->info.song;

			if(entity->type!=MPD_INFO_ENTITY_TYPE_SONG) {
				mpd_freeInfoEntity(entity);
				continue;
			}

			printf("file: %s\n",song->file);
			if(song->artist) {
				printf("artist: %s\n",song->artist);
			}
			if(song->album) {
				printf("album: %s\n",song->album);
			}
			if(song->title) {
				printf("title: %s\n",song->title);
			}
			if(song->track) {
				printf("track: %s\n",song->track);
			}
			if(song->name) {
				printf("name: %s\n",song->name);
			}
			if(song->date) {
				printf("date: %s\n",song->date);
			}                                      			
			if(song->time!=MPD_SONG_NO_TIME) {
				printf("time: %i\n",song->time);
			}
			if(song->pos!=MPD_SONG_NO_NUM) {
				printf("pos: %i\n",song->pos);
			}

			mpd_freeInfoEntity(entity);
		}

		if(conn->error) {
			fprintf(stderr,"%s\n",conn->errorStr);
			mpd_closeConnection(conn);
			return -1;
		}

		mpd_finishCommand(conn);
		if(conn->error) {
			fprintf(stderr,"%s\n",conn->errorStr);
			mpd_closeConnection(conn);
			return -1;
		}
	
		mpd_freeStatus(status);
	}
	else if(argc==3 && strcmp(argv[1],"lsinfo")==0) {
		mpd_InfoEntity * entity;

		mpd_sendLsInfoCommand(conn,argv[2]);
		if(conn->error) {
			fprintf(stderr,"%s\n",conn->errorStr);
			mpd_closeConnection(conn);
			return -1;
		}

		while((entity = mpd_getNextInfoEntity(conn))) {
			if(entity->type==MPD_INFO_ENTITY_TYPE_SONG) {
				mpd_Song * song = entity->info.song;

				printf("file: %s\n",song->file);
				if(song->artist) {
					printf("artist: %s\n",song->artist);
				}
				if(song->album) {
					printf("album: %s\n",song->album);
				}
				if(song->title) {
					printf("title: %s\n",song->title);
				}
				if(song->track) {
					printf("track: %s\n",song->track);
				}
			}
			else if(entity->type==MPD_INFO_ENTITY_TYPE_DIRECTORY) {
				mpd_Directory * dir = entity->info.directory;
				printf("directory: %s\n",dir->path);
			}
			else if(entity->type==
					MPD_INFO_ENTITY_TYPE_PLAYLISTFILE) {
				mpd_PlaylistFile * pl = 
					entity->info.playlistFile;
				printf("playlist: %s\n",pl->path);
			}

			mpd_freeInfoEntity(entity);
		}

		if(conn->error) {
			fprintf(stderr,"%s\n",conn->errorStr);
			mpd_closeConnection(conn);
			return -1;
		}

		mpd_finishCommand(conn);
		if(conn->error) {
			fprintf(stderr,"%s\n",conn->errorStr);
			mpd_closeConnection(conn);
			return -1;
		}
	}
	else if(argc==2 && strcmp(argv[1],"artists")==0) {
		char * artist;
	
		mpd_sendListCommand(conn,MPD_TABLE_ARTIST,NULL);
		if(conn->error) {
			fprintf(stderr,"%s\n",conn->errorStr);
			mpd_closeConnection(conn);
			return -1;
		}

		while((artist = mpd_getNextArtist(conn))) {
			printf("%s\n",artist);
			free(artist);
		}

		if(conn->error) {
			fprintf(stderr,"%s\n",conn->errorStr);
			mpd_closeConnection(conn);
			return -1;
		}

		mpd_finishCommand(conn);
		if(conn->error) {
			fprintf(stderr,"%s\n",conn->errorStr);
			mpd_closeConnection(conn);
			return -1;
		}
	}

	mpd_closeConnection(conn);

	return 0;
}
