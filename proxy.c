/*
 * Ha-Eun Hwangbo
 * 2013-10892
 */

#include <stdio.h>
#include "csapp.h"
#include "pthread.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define VERBOSE 1
#define CACHE_ENABLE 0

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

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *eof_hdr = "\r\n";

/* global variables */
pthread_rwlock_t rwlock;
Cache cache = {NULL, NULL, 0, 0};

/* function prototypes */
void *thread(void *vargp);
void proxy(int fd);
int parse_uri(char *uri, char *host, int *portp, char *path);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
int startswith(const char *target, const char *prefix);

/* cache functions */
int in_cache(char *host, char *path, char *response);
void evict(int new_block_size);
void allocate(char *host, char *path, char *buf, int bufsize);
void to_head(Block *b);
int cache_check();

int main(int argc, char **argv) 
{
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    struct sockaddr_storage clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    pthread_t tid;
    int lock_err;

	Signal(SIGPIPE, SIG_IGN); /* ignore SIGPIPE signal */
	
	/* initialize reader-writer lock */
	if ((lock_err = pthread_rwlock_init(&rwlock, NULL)) != 0)
		printf("Read-Write Lock initialization failed errno: %d\n", lock_err);

    if (VERBOSE)
    {
    	printf("Server starting...\n");
    	cache_check();
    }

    /* Check command line args */
    if (argc != 2) 
    {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }

    /* open listening socket descriptor */
    listenfd = Open_listenfd(argv[1]); //argv[1]: port

    while (1) 
    {
    	/* connection socekt descriptor */
		connfdp = Malloc(sizeof(int));
		*connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		
		Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		if (VERBOSE)
			printf("Accepted connection from (%s, %s)\n", hostname, port);
		/* details done in thread routine */
		Pthread_create(&tid, NULL, thread, connfdp); 
	}

	return 0;
}

/* 
 * thread - thread routine 
 */
void *thread(void *vargp)
{
	int connfd = *((int *)vargp);
	Pthread_detach(pthread_self());
	Free(vargp);
	proxy(connfd);
	Close(connfd);
	return NULL;
}

/*
 * proxy - serve as proxy
 *         read request from client and send it to server
 *         read the server's response and forward it to client
 *         1. open socket to server
 *         2. read request from client and concatenate request headers
 *         3-1. if request object is in cache, just resend it END
 *         3-2. send request to server
 *         4. if response is cacheable, cache it in memory
 *         5. send response to client
 */
/* $begin doit */
void proxy(int connfd) 
{
	/* connfd : proxy acting as server to client
	 * clientfd : proxy acting as client to server running on host
	 */
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char request[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE], request_hdr[MAXLINE];
    char host[MAXLINE], path[MAXLINE], portstr[MAXLINE];
    char cache_buf[MAX_OBJECT_SIZE];
    int port;
    /* rio from server, rio from client */
    rio_t rio_server, rio_client; 
    int clientfd;
    int request_len, n, sum;

    if (VERBOSE) printf("Proxy thread starting...\n");

    /* read and parse request line */
	Rio_readinitb(&rio_client, connfd); // need to forward it to server
	Rio_readlineb(&rio_client, buf, MAXLINE);

	if (strcmp(buf, "") == 0)
	{
		if (VERBOSE) printf("Empty line\n");
		clienterror(connfd, buf, "400", "Bad Request", "received blank line");
		return;
	}

	sscanf(buf, "%s %s %s", method, uri, version);

    /* only handle GET reqeust */
    if (strcasecmp(method, "GET")) 
    {
    	if (VERBOSE) printf("Not GET\n");
        clienterror(connfd, method, "501", "Not Implemented", "only GET is implemented");
        return;
    }

    /* parse uri */
    if (!parse_uri(uri, host, &port, path)) 
    {
    	if (VERBOSE) printf("Could not parse\n");
		clienterror(connfd, uri, "400", "Bad Request", "could not parse request");
		return;
    }

	if (VERBOSE)
		printf("host: %s\nport: %d\npath: %s\n", host, port, path);

	/* 3-1. if request object is in cache, just resend it END */
	if (CACHE_ENABLE) 
	{	
		pthread_rwlock_rdlock(&rwlock);
		
		if (in_cache(host, path, buf));
			Rio_writen(connfd, buf, strlen(buf)); 
		
		pthread_rwlock_unlock(&rwlock);
		return;
	}

	/* 1. open socket to server
	 * establishes connection with a server running on host listening on port*/
	sprintf(portstr, "%d", port);
	clientfd = Open_clientfd(host, portstr);  
	Rio_readinitb(&rio_server, clientfd);

	/* 2. read request from client and concatenate request headers */
	sprintf(request_hdr, requestlint_hdr_format, path);
	Rio_readlineb(&rio_client, buf, MAXLINE);
	while (strcmp(buf, "\r\n")) 
	{
		if (startswith(buf, "Host"))
		{
			strcpy(host_hdr, buf);
			if (VERBOSE)
				printf("Host specified: %s", host_hdr);
		}
		else if (!startswith(buf, "User-Agent") 
			&& !startswith(buf, "Connection") 
			&& !startswith(buf, "Proxy-Connection"))
		{
			strcat(other_hdr, buf);
		}
		Rio_readlineb(&rio_client, buf, MAXLINE); // read next line
	}

	if (strlen(host_hdr) == 0)
		sprintf(host_hdr, host_hdr_format, host);

	sprintf(request, "%s%s%s%s%s%s%s", request_hdr, host_hdr, conn_hdr, proxy_hdr, user_agent_hdr, other_hdr, eof_hdr);

	if (VERBOSE)
	{
		printf("***This is the requst read from client***\n");
		printf("%s", request);
	}

	/* 3-2. send request to server */
	request_len = strlen(request);
	Rio_writen(clientfd, request, request_len);

	/* 4. if response is cacheable, cache it in memory */
	/* 5. send response to client */
	sum = 0;
	while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)) > 0)
	{
		sum += n;
		if (sum <= MAX_OBJECT_SIZE)
			strcat(cache_buf, buf);
		Rio_writen(connfd, buf, n);
	}

	/* if cacheable, allocate in cache */
	if (CACHE_ENABLE && sum <= MAX_OBJECT_SIZE) 
	{
		pthread_rwlock_wrlock(&rwlock);
		allocate(host, path, cache_buf, sum);
		pthread_rwlock_unlock(&rwlock);
	}

	if (VERBOSE)
		printf("Proxy forwarded %d bytes server resposne to client\n", sum);

	cache_check();
	Close(clientfd);
}
/* $end doit */

/*
 * parse_uri - parse URI into host, port, path
 *             return 1 on normal, 0 on error
 *             uri is in the format
 *             http://{host}:{port}/{path}
 *             http://{host}/{path}
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *host, int *portp, char *path)
{
	*portp = 80; // default port
    char *ptr, *ptr2;
    ptr = uri;
    
    if (startswith(ptr, "http://"))
    	ptr += 7;
    /* here: ptr points to start of host name */

    ptr2 = strstr(ptr, ":");
    if (ptr2) /* port mumber is specified */
    {
    	*ptr2 = '\0'; // replace '/' with '\0'
    	sscanf(ptr, "%s", host);
    	sscanf(ptr2 + 1, "%d%s", portp, path);
    }

    else /* use default port 80 */
    {
    	ptr2 = strstr(ptr, "/");
    	if (ptr2) /* path exists */
    	{
    		*ptr2 = '\0';
    		sscanf(ptr, "%s", host);
    		*ptr2 = '/';
    		sscanf(ptr2, "%s", path);
    	}
    	else
    	{
    		sscanf(ptr, "%s", host);
    	}
    }
    return 1;
}

/*
 * startswith - return nonzero if target starts with prefix
 */
int startswith(const char *target, const char *prefix)
{
	int prefixlen = strlen(prefix);
	return (!strncmp(target, prefix, prefixlen));
}

/* $end parse_uri */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */

/**************************/
/**** Cache Functions******/
/**************************/

int in_cache(char *host, char *path, char *response)
{
	Block *curr = cache.head;
	for (; curr != NULL; curr = curr->next) 
	{
		/* use host, path as key */
		if (!strcmp(curr->host, host) && !strcmp(curr->path, path)) 
		{
			strncpy(response, curr->object, curr->size);
			to_head(curr); /* recently accessed block is in the head */
			return 1; // found
		}
	}
	return 0; // not found
}

void evict(int new_block_size)
{
	int victim_size, temp_cache_size;
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

void allocate(char *host, char *path, char *buf, int bufsize)
{
	/* make room for new block */
	int temp_cache_size = cache.size + bufsize;
	if (temp_cache_size > MAX_CACHE_SIZE) 
		evict(bufsize);

	/* create new block */
	Block *b = Malloc(sizeof(Block));
	b->prev = NULL;
	b->next = NULL;
	b->size = bufsize;
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
