#define _GNU_SOURCE

#include <assert.h>
#include <chromaprint.h>
#include <curl/curl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <FLAC/metadata.h>

typedef struct FileNode {
  FILE *file;
  char* filename;
  char* fingerprint;
  int duration;
} FileNode_t;

typedef struct arg {
  FileNode_t* arr;
  int threadNo;
  int totalNumFiles;
} arg_t;

typedef FLAC__StreamMetadata data;

#define NO_THREADS 4
const int sample_rate = 44100;
const int num_channels = 2;
const int FILE_BLOCK_SIZE = 128;
const char* ACOUSTID_API_URL = "http://api.acoustid.org/v2/lookup";
const char* APPLICATION_ID = "jif76R78Wd";

int fingerprintFile(FILE *file, char** fingerprint) {
  ChromaprintContext *ctx;
  ctx = chromaprint_new(CHROMAPRINT_ALGORITHM_DEFAULT);

  chromaprint_start(ctx,sample_rate,num_channels);

 int16_t *buffer = (int16_t*) malloc((FILE_BLOCK_SIZE * sizeof(int16_t)));
  int read;
  while ((read = fread(buffer,FILE_BLOCK_SIZE*sizeof(int16_t), 1, file))) {
    if(!chromaprint_feed(ctx,buffer,read/sizeof(int16_t))) {
      fprintf(stderr, "Error feeding Chromaprint from buffer\n");
      exit(2);
    }
  }

  if (!chromaprint_finish(ctx)) {
    fprintf(stderr, "Error finishing Chromaprint feed\n");
    exit(2);
  }
  free(buffer);
  if (!chromaprint_get_fingerprint(ctx, fingerprint)) {
    fprintf(stderr, "Error retrieving fingerprint from Chromaprint\n");
    exit(2);
  }
  int duration = chromaprint_get_item_duration_ms(ctx);
  chromaprint_free(ctx);

  return duration;
}

void fetchMetadata(char* fingerprint, int duration) {
  printf("Fetching Metadata...\n | Duration: %d\n | Fingerprint: %s\n",duration, fingerprint);
  CURL *curl = curl_easy_init();
  if(curl) {
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, ACOUSTID_API_URL);
    char *body;
    asprintf(&body,"client=%s&duration=%d&fingerprint=%s", APPLICATION_ID, duration, fingerprint);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);

    res = curl_easy_perform(curl);
    if(res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
          curl_easy_strerror(res));
    curl_easy_cleanup(curl);
    free(body);
  } else {
    fprintf(stderr, "Error Initializing cURL");
  }
}

FileNode_t* consumeDirectory(char* directory, int *filecount) {
  DIR *d = opendir(directory);
  struct dirent *dir;
  *filecount = 0;
  while ((dir = readdir(d)) != NULL) {
    if (strstr(dir->d_name, ".flac") != NULL) {
      (*filecount)++;
    }
  }
  closedir(d);

  int i = 0;
  d = opendir(directory);
  FileNode_t *flacFiles = malloc(sizeof(FileNode_t) * (*filecount));
  while ((dir = readdir(d)) != NULL) {
    if (strstr(dir->d_name, ".flac") != NULL) {
      FileNode_t *node = &flacFiles[i];
      char* pathname;
      asprintf(&pathname, "%s/%s", directory, dir->d_name);
      FILE *file = fopen(pathname, "r");
      node->file = file;
      node->filename = dir->d_name;
      i++;
    }
  }
  assert(*filecount == i);

  return flacFiles;
}

void* threadFunction (void* voidArgs) {
  arg_t* args = (arg_t*) voidArgs;
  for (int i = args->threadNo; i < args->totalNumFiles; i+= NO_THREADS) {
    FileNode_t *cur = &(args->arr)[i];
    cur->duration = fingerprintFile(cur->file,&cur->fingerprint);
  }
  return NULL;
}

void practice(FLAC__Metadata_SimpleIterator *flac_iter, bool use, char* title, char* album, char* artist) {

  int tlen = strlen(title);
  int allen = strlen(album);
  int arlen = strlen(artist);
  
  // iterate while there are blocks to read
  while (FLAC__metadata_simple_iterator_next(flac_iter)) {
    FLAC__StreamMetadata* meta = FLAC__metadata_simple_iterator_get_block(flac_iter);
    FLAC__StreamMetadata* temp = FLAC__metadata_object_new (FLAC__METADATA_TYPE_VORBIS_COMMENT);

    
    printf("meta contents: %u\n", meta->type);
    if(meta->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
      FLAC__StreamMetadata_VorbisComment_Entry* t = (FLAC__StreamMetadata_VorbisComment_Entry*)malloc(sizeof(FLAC__StreamMetadata_VorbisComment_Entry) * 3);

      //temp->data.vorbis_comment.comments = (FLAC__StreamMetadata_VorbisComment_Entry*)malloc(sizeof(FLAC__StreamMetadata_VorbisComment_Entry) * 3);

      t[0].entry = (FLAC__byte *)malloc(sizeof(FLAC__byte) * tlen);
      t[1].entry = (FLAC__byte *)malloc(sizeof(FLAC__byte) * allen);
      t[2].entry = (FLAC__byte *)malloc(sizeof(FLAC__byte) * arlen);
      
      //FLAC__StreamMetadata_VorbisComment vorbis = meta->data.vorbis_comment;
      
      printf("num_comments: %d\n", meta->data.vorbis_comment.num_comments);
      
      for(int i = 0; i < meta->data.vorbis_comment.num_comments; i++) {
	printf("%s\n", meta->data.vorbis_comment.comments[i].entry);

	if(use) {

	  FLAC__byte* title_bytes = malloc(sizeof(FLAC__byte) * tlen + 1);
	  memcpy(title_bytes, title, tlen);
	  title_bytes[tlen + 1] = '\0';

	  FLAC__byte* album_bytes = malloc(sizeof(FLAC__byte) * allen + 1);
	  memcpy(album_bytes, album, allen);
	  album_bytes[allen + 1] = '\0';

	  FLAC__byte* artist_bytes = malloc(sizeof(FLAC__byte) * arlen + 1);
	  memcpy(artist_bytes, artist, arlen);
	  artist_bytes[arlen + 1] = '\0';

	  //create vorbis entry and populate it
	  t[0].length = tlen;
	  t[0].entry = title_bytes;

	  t[1].length = allen;
	  t[1].entry = album_bytes;

	  t[2].length = arlen;
	  t[2].entry = artist_bytes;

	  temp->data.vorbis_comment.comments = t;
	  temp->data.vorbis_comment.num_comments = 3;

	  FLAC__metadata_object_vorbiscomment_replace_comment(temp, *t, false, false);
	  assert(FLAC__metadata_simple_iterator_set_block(flac_iter, temp, false));
	}
      }
    }
  }
  printf("Hello\n");
}


// RUN BEFORE EVERY CALL
//!git checkout input/cool.flac
int main (int argc, char *argv[]) {

  char* title = "TITLE=Keep My Cool";
  char* artist = "ARTIST=the Wellness";
  char* album = "ALBUM=doubles";

  FLAC__Metadata_SimpleIterator *flac_iter = FLAC__metadata_simple_iterator_new();

  // Attach iterator to a FLAC file (this function returns a flac bool)
  if (!FLAC__metadata_simple_iterator_init(flac_iter, "input/cool.flac", false, false)) {
    // error message
    printf("Error with initializing iterator\n");
  }

  // make sure the file is writable
  if (!FLAC__metadata_simple_iterator_is_writable(flac_iter)) {
    // error? flac isn't writable
    // print something, then exit
    printf("iter1 failed\n");
  }


  printf("First time running:\n");
  practice(flac_iter, true, title, album, artist);
  FLAC__metadata_simple_iterator_delete(flac_iter);




  FLAC__Metadata_SimpleIterator *flac_iter2 = FLAC__metadata_simple_iterator_new();

  // Attach iterator to a FLAC file (this function returns a flac bool)
  if (!FLAC__metadata_simple_iterator_init(flac_iter2, "input/cool.flac", false, false)) {
    // error message
    printf("Error with initializing iterator\n");
  }

  // make sure the file is writable
  if (!FLAC__metadata_simple_iterator_is_writable(flac_iter2)) {
    // error? flac isn't writable
    // print something, then exit
    printf("iter2 failed\n");
  }


  printf("\nSecond time running:\n");
  practice(flac_iter2, false, title, album, artist);

  /*
  int totalNumFiles;
  FileNode_t *flacFiles = consumeDirectory(argv[1], &totalNumFiles);
  char *fingerprint;
  int duration;

  pthread_t threads[NO_THREADS];
  for (int i = 0; i < NO_THREADS; i++) {
    arg_t *arg = (arg_t*) malloc(sizeof(arg_t));
    arg->arr = flacFiles;
    arg->totalNumFiles = totalNumFiles;
    arg->threadNo = i;
    if (pthread_create(&(threads[i]), NULL, threadFunction, arg)) {
      perror("Error creating thread");
    }
  }
  for(int i = 0; i < NO_THREADS; i++) {
    if (pthread_join(threads[i],0)) {
      perror("Error joining with thread");
      exit(2);
    }
  }
  for(int i = 0; i < totalNumFiles; i++) {
    FileNode_t *node = &flacFiles[i];
    printf("File: %s\n | Duration: %d\n | Fingerprint: %s\n\n",node->filename, node->duration, node->fingerprint);
  }
  */

  // check if valid file
  // create a flac iterator
  
  return 0;
}
