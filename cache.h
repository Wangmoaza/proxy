#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct _Block {
	struct _Block *prev;
	struct _Block *next;
	int size;
	char host[MAXLINE];
	char path[MAXLINE];
	char object[MAX_OBJECT_SIZE];
} Block;

typedef struct _Cache {
	struct _Block *head;
	struct _Block *tail;
	int size;
	int count;
} Cache;

/* Global variable */
extern Cache cache;

int in_cache(char *host, char *path, char *response);
void evict(int new_block_size);
void allocate(char *host, char *path, char *buf, int bufsize);
void to_head(Block *b);
int cache_check();

#endif /* __CACHE_H__ */