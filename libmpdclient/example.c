#include "libmpdclient.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char ** argv) {
	mpd_Connection * conn;

	conn = mpd_newConnection("localhost",6600,10);

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
