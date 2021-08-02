#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

// buffer area for web  
struct buffer_block{
	// storing web content
	char buffer[MAX_OBJECT_SIZE];
	sem_t w;
	sem_t mutex;
	int readcnt;
	// storing web url
	char name[MAXLINE];
	sem_t n_w;
	sem_t n_mutex;
	int n_readcnt;
	// storing the time last visit
	int time;
	sem_t t_w;
	sem_t t_mutex;
	int t_readcnt;
};

// represent the rough visit time
// not accurate
int count=0;

// storing connect file descriptor and visit time
struct argv{
	int connfd;
	int time;
};

struct buffer_block cache[10];  // 10 buffer block

void doit(int fd, int time);
void read_requesthdrs(rio_t *rp, char *requesthdrs, char *requesthost);
void parse_uri(char *ori_uri, char *hostname, char *portnum, char *filename);
int con_server(char *hostname, char *portnum, char *filename, char *requesthdrs, char *requesthost);
void serve(int fd, int serverfd, char *uri, int time);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg); 
void *thread(void *vargp);

// reader and writer model
void cache_init();
void cache_read(int index, int time, char *buf) ;
void cache_n_read(int index, char *buf);
int cache_t_read(int index);
void cache_write(int index, int time, char *name, char *buf);

/*
 main function to listen the connection request
*/ 
int main(int argc, char **argv)
{	
    int listenfd;
    struct argv *arg;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    pthread_t tid;
    
    // if no port number, print error
    if(argc != 2) {
    	fprintf(stderr, "usage: %s <port>\n", argv[0]);
    	exit(1);
	}
	
	listenfd = Open_listenfd(argv[1]);		// set the listen file descriptor
	cache_init();							// initialize the cache and semaphore
	while(1) {
		clientlen = sizeof(clientaddr);			// get the struct size of clientaddr
		arg = Malloc(sizeof(struct argv));		// get a new memory area to avoid race
		arg->connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);		// get a connection request
		arg->time = count++;												// store the connection time
		Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);	// get the client information
		Pthread_create(&tid, NULL, thread, arg);							// create new thread to deal with the connection
		printf("Accepted connection from (%s, %s)\n", hostname, port);
	}
}

/*
 thread routine
*/
void *thread(void *vargp) {
	// get the data from buffer area
	int connfd = ((struct argv *)vargp)->connfd;		
	int time = ((struct argv *)vargp)->time;
	Pthread_detach(pthread_self());			// detach from main thread
	Free(vargp);							// free memory area
	doit(connfd, time);						// connection with client
	Close(connfd);							// close the connection
	return NULL;
}

/*
 return buffer content 
*/
void doit(int fd, int time) {
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char hostname[MAXLINE], portnum[MAXLINE], filename[MAXLINE], requesthdrs[MAXLINE], requesthost[MAXLINE];
	rio_t rio;
	char *ret;
	
	Rio_readinitb(&rio, fd);				// initialize the Rio function
	Rio_readlineb(&rio, buf, MAXLINE);
	
	// seperate the request line
	printf("Request headers:\n");
	printf("%s", buf);
	sscanf(buf, "%s %s %s", method, uri, version);
	
	// if not get method, return error to client and close the connection 
	if(strcasecmp(method, "GET")) {
		clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
		return;
	}
	// parse request headers and extract the hostname if exists
	read_requesthdrs(&rio, requesthdrs, requesthost);
	
	// if has cache, return content and close the connection
	int i;
	for(i=0; i<10; i++) {
		char tmp[MAXLINE];
		cache_n_read(i, tmp);
		if(!strcmp(tmp, uri)) break;
	}
	if(i<10) {
		ret = (char *)Malloc(MAX_OBJECT_SIZE*sizeof(char));
		ret[0] = '\0'; 
		cache_read(i, time, ret);					// read content from cache
		Rio_writen(fd, ret, strlen(ret));			// reponse to client
		Free(ret);
		return;
	}
	// otherwise connect to web server
	parse_uri(uri, hostname, portnum, filename);				// parse the uri
	int serverfd = con_server(hostname, portnum, filename, requesthdrs, requesthost);		// connect to remote server
	serve(fd, serverfd, uri, time);   // receive and process data from server
	
}	

/*
 if connection fail, send error information to client
*/
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
	char buf[MAXBUF], body[MAXBUF];
	
	// store the reponse body
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
	
	// send the reponse header
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	// send the reponse body
	Rio_writen(fd, body, strlen(body));
}

/*
 read and parse the request header, then form a new request header
*/
void read_requesthdrs(rio_t *rp, char *requesthdrs, char *requesthost) {
	char buf[MAXLINE];
	
	Rio_readlineb(rp, buf, MAXLINE);
	printf("%s", buf);
	// if the first line is hostname, store it in requesthost
	if(strstr(buf, "Host:")) strcpy(requesthost, buf);
	// else copy the line to requesthdrs
	else strcpy(requesthdrs, buf);
	// copy each line and check if is hostname
	while(strcmp(buf, "\r\n")) {
		Rio_readlineb(rp, buf, MAXLINE);
		printf("%s", buf);
		if(strstr(buf, "Host:")) strcpy(requesthost, buf);
		else strcat(requesthdrs, buf);
	}
	return;
}

/*
 parse the uri into three parts: hostname, portnum and filename
*/ 
void parse_uri(char *ori_uri, char *hostname, char *portnum, char *filename) {
	char *uri;
	char *ptr;
	// the format of uri is http://[hostname]: [port] [/file path and name]
	// copy the original uri to new memory area
	uri = (char *)Malloc(MAXLINE);
	strcpy(uri, ori_uri);				
	// remove the 'http://' head
	if(strstr(uri, "http://")) {
		uri+=7;
	}
	
	// step 1: deal with filename
	// from the first '/', string represents file path and name
	ptr = index(uri, '/');
	// if '/' exists, copy it
	if(ptr) {
		strcpy(filename, ptr);
		*ptr = '\0';			// in the uri string, remove the part after '/'
	}
	// else add a '/' to filename
	else strcpy(filename, "/");
	
	// step 2: deal with port number
	ptr = index(uri, ':');
	// if ':' exists, copy it to portnum
	if(ptr) {
		strcpy(portnum, ptr+1);
		*ptr = '\0';
	}
	// else set portnum as empty
	else strcpy(portnum, "");
	
	// step 3: copy the hostname
	strcpy(hostname, uri);
}

/*
 set the connection to real server
*/
int con_server(char *hostname, char *portnum, char *filename, char *requesthdrs, char *requesthost) {
	char buf[MAXLINE];
	int serverfd;
	char *ptr;
	
	// step 1: set the connection 
	// check port number and send connection to the real server
	if(!strcmp(portnum, "")) {
		serverfd = Open_clientfd(hostname, NULL);
	}
	else serverfd = Open_clientfd(hostname, portnum);
	
	// step 2: send request line
	sprintf(buf, "GET %s HTTP/1.0\r\n", filename);
	
	// step 3: add some new contents before original request headers and sent it
	// if hostname exists, send original hostname
	if(strstr(requesthost, "Host")) strcat(buf, requesthost);	
	// else send new hostname
	else sprintf(buf, "%sHost: %s\r\n", buf, hostname);
	sprintf(buf, "%s%s", buf, user_agent_hdr);
	sprintf(buf, "%sConnection: close\r\n", buf);
	sprintf(buf, "%sProxy-Connection: close\r\n", buf);
	strcat(buf, requesthdrs);			// add the original request headers
	Rio_writen(serverfd, buf, strlen(buf));	
	return serverfd;
}

/*
 receive, store and send data from the real server
*/
void serve(int fd, int serverfd, char *uri, int time) {
	rio_t rio;
	size_t n;
	char buf[MAXLINE];
	char ret[MAX_OBJECT_SIZE];
	ret[0] = '\0';
	
	Rio_readinitb(&rio, serverfd);	
	int len = 0;
	while((n = Rio_readlineb(&rio, buf, MAXLINE))!=0) {
		len+=n;
		if(len<MAX_OBJECT_SIZE) strcat(ret, buf);		// copy data
		Rio_writen(fd, buf, n);			// send data to client
	} 
	// if size is too large, don't cache
	if(len>=MAX_OBJECT_SIZE) {
		return;
	}
	
	// if the cache is full, replace the least used one
	int i, min=0, min_time=cache_t_read(0);
	for(i=1; i<10; i++) {	
		int t = cache_t_read(i);
		if(t<min_time) {	
			min_time = t;
			min = i;
		}
	}
	// write the data into cache
	cache_write(min, time, uri, ret);
	return;
}

// below are cache and semaphore part
/*
 initialize the cache and semaphore
*/
void cache_init() {
	int i;
	for(i=0; i<10; i++) {
		cache[i].buffer[0] = '\0';
		cache[i].name[0] = '\0';
		cache[i].time = 0;
		Sem_init(&cache[i].mutex, 0, 1);
		Sem_init(&cache[i].w, 0, 1);
		cache[i].readcnt = 0;
		Sem_init(&cache[i].n_mutex, 0, 1);
		Sem_init(&cache[i].n_w, 0, 1);
		cache[i].n_readcnt = 0;
		Sem_init(&cache[i].t_mutex, 0, 1);
		Sem_init(&cache[i].t_w, 0, 1);
		cache[i].t_readcnt = 0;		
	}
	return;
}

/*
 read web content from the cache
*/
void cache_read(int index, int time, char *buf) {
	// step 1: the number of reader + 1
	P(&cache[index].mutex);    	// lock the cache block, other people cannot access the cache
	cache[index].readcnt++; 
	if(cache[index].readcnt==1) P(&cache[index].w);    // if reader exists, cannot write
	V(&cache[index].mutex);		// unlock the cache block, other people can read it now
	
	// step 2: copy the content
	strcpy(buf, cache[index].buffer);
	
	// step 3: update the visit time
	P(&cache[index].t_w);		// lock the time, other people cannot write it
	cache[index].time = time; 
	V(&cache[index].t_w);		// unlock the time
	
	// step 4: the number of reader - 1
	P(&cache[index].mutex);		
	cache[index].readcnt--;
	if(cache[index].readcnt==0) V(&cache[index].w);   // if no reader, now can write
	V(&cache[index].mutex);
	
	return;
}

void cache_n_read(int index, char *buf) {	
	// step 1: the number of reader + 1
	P(&cache[index].n_mutex);
	cache[index].n_readcnt++;
	if(cache[index].n_readcnt==1) P(&cache[index].n_w);
	V(&cache[index].n_mutex);
	// step 2: copy the uri
	strcpy(buf, cache[index].name);
	// step 3: the number of reader - 1
	P(&cache[index].n_mutex);
	cache[index].n_readcnt--;
	if(cache[index].n_readcnt==0) V(&cache[index].n_w);
	V(&cache[index].n_mutex);
	
	return;
}
 
int cache_t_read(int index) {
	int t;
	// step 1: the number of reader + 1
	P(&cache[index].t_mutex);
	cache[index].t_readcnt++;
	if(cache[index].t_readcnt==1) {	
		P(&cache[index].t_w); 
	}
	V(&cache[index].t_mutex);
	// step 2: copy the time
	t = cache[index].time;	
	// step 3: the number of reader - 1
	P(&cache[index].t_mutex);
	cache[index].t_readcnt--;
	if(cache[index].t_readcnt==0) {
		V(&cache[index].t_w);
	}
	V(&cache[index].t_mutex);
	
	return t;	
}

/*
 write data to cache
*/
void cache_write(int index, int time, char *name, char *buf) {
	// block all write request
	P(&cache[index].w);
	P(&cache[index].t_w);
	P(&cache[index].n_w);
	// copy data
	strcpy(cache[index].buffer, buf);
	strcpy(cache[index].name, name);
	cache[index].time = time;
	// unblock all write request
	V(&cache[index].n_w);
	V(&cache[index].t_w);
	V(&cache[index].w);
	return;
}
