#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct _Block {
	struct _Block *prev;
	struct _Block *next;
	size_t size;
	char *host;
	char *path;
	char[MAX_OBJECT_SIZE] object;
} Block;

typedef struct _Cache {
	struct _Block *head;
	struct _Block *tail;
	size_t size;
	size_t count;
} Cache;

/* Global variable */
extern Cache cache;

int in_cache(char *host, char *path, char *response);
void evict(size_t new_block_size);
void allocate(char *host, char *path, char *buf, size_t *bufsize);
void to_head(Block *b);
int cache_check();

#endif /* __CACHE_H__ */