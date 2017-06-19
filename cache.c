#include "cache.h"

int in_cache(char *host, char *path, char *response)
{
	Block *curr = cache.head;
	for (; curr != NULL; curr = curr->next) 
	{
		/* use host, path as key */
		if (!strcmp(curr->host, host) && !strcmp(curr->uri, uri)) 
		{
			strncpy(response, curr->object, curr->size);
			to_head(curr); /* recently accessed block is in the head */
			return 1; // found
		}
	}
	return 0; // not found
}

void evict(size_t new_block_size)
{
	size_t victim_size, temp_cache_size;
	Block *curr = cache.tail;
	victim_size = curr->size;

	/* remove victim block */
	cache.tail = curr->prev;
	curr->prev->next = NULL;
	Free(curr);

	cache.size -= victim_size;
	cache.count--;
	temp_cache_size = cache.size + new_block_size;

	if (temp_cache_size > MAX_CACHE_SIZE){
		if (cache.head == NULL) 
			return;
		evict(new_block_size);
	}

	return;
}

void allocate(char *host, char *path, char *buf, size_t *bufsize)
{
	/* make room for new block */
	size_t temp_cache_size = cache.size + bufsize;
	if (temp_cache_size > MAX_CACHE_SIZE) 
		evict(bufsize);

	/* create new block */
	Block *b = Malloc(sizeof(Block));
	b->prev = NULL;
	b->next = NULL;
	b->size = bufsize;
	b->host = Malloc(sizeof(char) * strlen(host));
	b->path = Malloc(sizeof(char) * strlen(uri));
	b->object = Malloc(sizeof(char) * bufsize);
	strcpy(b->host, host);
	strcpy(b->path, path);
	strncpy(b->object, buf, bufsize);

	if (cache.head == NULL) 
	{
		cache.head = b;
		cache.tail = b;
	}
	else 
	{
		cache.head->prev = b;
		b->next = cache.head;
		cache.head = b;
	}
	
	cache.size += bufsize; /* increase cache size */
	cache.count ++;
	return;

}
void to_head(Block *b)
{
	if (b == cache.head)
		return;

	b->prev->next = b->next;
	if (b == cache.tail)
		cache.tail = b->prev;
	else
		b->next->prev = b->prev;

	cache.head->prev = b;
	b->next = cache.head;
	b->prev = NULL;
	cache.head = b;
}

int cache_check()
{
	int cnt = 0;
	Block *curr;

	if (cache.count == 0)
	{
		return 1;
	}

	if ((cache.count == 1) && (cache.head != cache.tail))
	{
		printf("Error: count==1, head does not equal tail\n");
		return 0;
	}

	if (cache.head->prev != NULL) 
	{
		printf("Error: head->prev should be NULL\n");
		return 0;	
	}

	if (cache.tail->next != NULL) 
	{
		printf("Error: tail->next should be NULL\n");
		return 0;	
	}

	for(curr = cache.head; curr->next != NULL; curr = curr->next)
	{
		cnt++;
		if (curr != (curr->next)->prev)
		{
			printf("Error: adjacent blocks not matching\n");
			return 0;
		}
	}

	if (cnt != cache.count) 
	{
		printf("Error: cache count wrong %d %d\n", cnt, cache.count);
		return 0;			
	}

	return 1;
}