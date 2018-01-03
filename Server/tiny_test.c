#include "csapp.c"

#define NUM_OF_WORKER_THREADS 3 //One less than the number of cores in the system.
#define BUFFER_SIZE 6           //Twice of the number of threads.

typedef struct {
    int buf[BUFFER_SIZE];
    long head, tail;
    int full, empty;
    pthread_mutex_t *mut;
    pthread_cond_t *notFull, *notEmpty;
} queue;

void *workerthread(void *fifodata);
void *managerthread(void *fifodata);

char* receiveimage(rio_t rio, int connectionfd);
int sendimage(int connectionfd, char *imgname);
int colortogray(char *imgname);

queue *queueInit (void);
void queueDelete (queue *q);
void queueAdd (queue *q, int in);
void queueDel (queue *q, int *out);
void millisleep(int milliseconds);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

queue *fifo;
int listenfd;

int main(int argc, char **argv) 
{
    printf("Main Loop\n");
    //structure to store scheduling priority.
    struct sched_param param;
    pthread_t manager, worker[NUM_OF_WORKER_THREADS];
    int min_priority, policy, ret;
    //create high and low priority attributes. 
    pthread_attr_t hp_attr;
    pthread_attr_t lp_attr;

    int port;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    
    // Assign main thread a low priority with FIFO scheduling policy.
    param.sched_priority = sched_get_priority_min(SCHED_FIFO);
    min_priority = param.sched_priority;
    // min_priority = sched_get_priority_min(SCHED_FIFO);
    //Set the scheduling policty and the params of the thread.
    pthread_setschedparam(pthread_self(), SCHED_RR, &param);
    //Get policy and parameters of the thread.
    pthread_getschedparam (pthread_self(), &policy, &param);

    pthread_attr_init(&lp_attr);
    pthread_attr_init(&hp_attr);


    //Set the inheritance scheduling attribute of the thread attributes to take their scheduling attributes from the values specified by the attributes object.
    pthread_attr_setinheritsched(&lp_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setinheritsched(&hp_attr, PTHREAD_EXPLICIT_SCHED);
    
    //Set the tread attributes scheduling policy to FIFO.
    pthread_attr_setschedpolicy(&lp_attr, SCHED_FIFO);
    pthread_attr_setschedpolicy(&hp_attr, SCHED_FIFO);

    //priorities for low and high attributes.
    param.sched_priority = min_priority + 1;
    pthread_attr_setschedparam(&lp_attr, &param);
    param.sched_priority = min_priority + 2;
    pthread_attr_setschedparam(&hp_attr, &param);

    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);

    fifo = queueInit();
    if (fifo ==  NULL) {
        fprintf (stderr, "main: Queue Init failed.\n");
        exit (1);
    }

    pthread_create(&manager, &hp_attr, managerthread, fifo);

    for(int i = 0; i<NUM_OF_WORKER_THREADS; i++){
        pthread_create(&worker[i], &lp_attr,workerthread,fifo);
    }

    while(1);
}

void *managerthread(void *fifodata)
{
    printf("managerthread: spawned.\n");
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    struct hostent *hp;
    char *haddrp;
    struct sched_param param;
    int i, d, *connfd, count = 1;
    queue *fifo;
    rio_t rio;

    int priority, policy, ret;
    ret = pthread_getschedparam (pthread_self(), &policy, &param);
    priority = param.sched_priority;    
    printf("managerthread: priority-%d \n", priority);
    fifo = (queue *)fifodata;

    while (1) {
        printf("Waiting for a new connection\n");

        connfd = (int *)malloc(sizeof(int));
        clientlen = sizeof(clientaddr);
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        printf("Connection fd - %d and count = %d\n", *connfd, count);
        count ++;
        hp = Gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, 
                   sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        haddrp = inet_ntoa(clientaddr.sin_addr);
        printf("Server connected to %s (%s)\n", hp->h_name, haddrp);
        //Check if the buffer is full.
        pthread_mutex_lock (fifo->mut);
        while (fifo->full) {
            printf ("Buffer Full.\n");
            //if the buffer is full, wait for the condition signal.
            pthread_cond_wait (fifo->notFull, fifo->mut);
        }
        //if buffer is not full, add the connection descriptor in queue.
        queueAdd(fifo, *connfd);
        pthread_mutex_unlock (fifo->mut);
        pthread_cond_signal (fifo->notEmpty);
    }
}

void *workerthread(void *fifodata)
{
    printf("workerthread: Worker thread spawned.\n");
    queue *fifo;
    int i, d;
    rio_t rio;
    char *imagename = "";

    struct sched_param param;
    int priority,policy,ret;

    ret=pthread_getschedparam(pthread_self(),&policy,&param);
    priority=param.sched_priority;
    printf("workerthread: priority-%d\n",priority);

    fifo = (queue *)fifodata;
    while(1){
        pthread_mutex_lock(fifo->mut);
        while (fifo->empty) {
            // printf ("worker: queue EMPTY.\n");
            pthread_cond_wait (fifo->notEmpty, fifo->mut);
        }
        queueDel (fifo, &d);
        pthread_mutex_unlock (fifo->mut);
        pthread_cond_signal (fifo->notFull);

        // printf("worker received : %d \n", d);

        millisleep(300);
        doit(d);
        // imagename = receiveimage(rio, d);
        // colortogray(imagename);
        // sendimage(d, imagename);
        // free(imagename);
        Close(d);
    }
    return NULL;
}

char* receiveimage(rio_t rio, int connectionfd){
    FILE *picture;
    char *imgname = calloc(100, sizeof(char)), recvBuff[256] = "";
    int bytesReceived = 0, imgsize = 0, imgnamesize = 0, recData = 256;
    Rio_readinitb(&rio, connectionfd);
    //Get picture name, size and string length.
    Rio_readnb(&rio,&imgsize,sizeof(int));        
    Rio_readnb(&rio, &imgnamesize,sizeof(int));
    Rio_readnb(&rio,imgname,imgnamesize);
    printf("Receiving image %s of size %d\n", imgname,  imgsize);
    picture = fopen(imgname, "wb");
    fseek(picture, 0, SEEK_SET); //Go to the start of the picture.

    while(((bytesReceived = Rio_readnb(&rio,recvBuff,recData))>0) && imgsize>0)
    {
        fwrite(recvBuff, 1,bytesReceived,picture);
        imgsize -= bytesReceived;
        if (imgsize<256)
            recData = imgsize;
    }
    fclose(picture);
    return imgname;
}

int sendimage(int connectionfd, char* imgname){
    FILE *picture;
    char buff[256];
    int readret, send_size;
    picture = fopen(imgname, "rb");
    if(!picture)
    {
        printf("Error opening the image %s\n", imgname);
        exit(0);   
    }
    fseek(picture, 0, SEEK_END);
    send_size = ftell(picture);
    Rio_writen(connectionfd, &send_size, sizeof(int));
    fseek(picture, 0, SEEK_SET); //Go to the start of the file.
    while(readret = fread(buff,1,256,picture))
    {   //Reading file in chunks of 256 because it gives a corrupt img file otherwise and writing it to the server.
        Rio_writen(connectionfd, buff, readret);
    }
    return 1;
}

int colortogray(char *imgname){
    if (Fork() == 0) {
        execl("/home/shantanu/MSEE/SEM_1/EOS/Codes/test/Server/cvtest", "./cvtest", imgname, (char *)0);
    }
    Wait(NULL);
    return 1;
}


queue *queueInit (void)
{
    queue *q;

    q = (queue *)malloc (sizeof (queue));
    if (q == NULL) return (NULL);

    q->empty = 1;
    q->full = 0;
    q->head = 0;
    q->tail = 0;
    q->mut = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
    pthread_mutex_init (q->mut, NULL);
    q->notFull = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
    pthread_cond_init (q->notFull, NULL);
    q->notEmpty = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
    pthread_cond_init (q->notEmpty, NULL);
    
    return (q);
}

void queueDelete (queue *q)
{
    pthread_mutex_destroy (q->mut);
    free (q->mut);  
    pthread_cond_destroy (q->notFull);
    free (q->notFull);
    pthread_cond_destroy (q->notEmpty);
    free (q->notEmpty);
    free (q);
}

void queueAdd (queue *q, int in)
{
    q->buf[q->tail] = in;
    q->tail++;
    if (q->tail == BUFFER_SIZE)
        q->tail = 0;
    if (q->tail == q->head)
        q->full = 1;
    q->empty = 0;

    return;
}

void queueDel (queue *q, int *out)
{
    *out = q->buf[q->head];

    q->head++;
    if (q->head == BUFFER_SIZE)
        q->head = 0;
    if (q->head == q->tail)
        q->empty = 1;
    q->full = 0;

    return;
}

void millisleep(int milliseconds)
{
      usleep(milliseconds * 1000);
}

void doit(int fd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;
  
    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);                   //line:netp:doit:readrequest
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
       clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
        return;
    }                                                    //line:netp:doit:endrequesterr
    read_requesthdrs(&rio);                              //line:netp:doit:readrequesthdrs

    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs);       //line:netp:doit:staticcheck
    if (stat(filename, &sbuf) < 0) {                     //line:netp:doit:beginnotfound
    clienterror(fd, filename, "404", "Not found",
           "Tiny couldn't find this file");
    return;
    }                                                    //line:netp:doit:endnotfound

    if (is_static) { /* Serve static content */          
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { //line:netp:doit:readable
       clienterror(fd, filename, "403", "Forbidden",
            "Tiny couldn't read the file");
       return;
    }
    serve_static(fd, filename, sbuf.st_size);        //line:netp:doit:servestatic
    }
    else { /* Serve dynamic content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { //line:netp:doit:executable
       clienterror(fd, filename, "403", "Forbidden",
            "Tiny couldn't run the CGI program");
       return;
    }
    serve_dynamic(fd, filename, cgiargs);            //line:netp:doit:servedynamic
    }
}


/*
 * read_requesthdrs - read and parse HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    }
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;

    if (!strstr(uri, "cgi-bin")) {  /* Static content */ //line:netp:parseuri:isstatic
    strcpy(cgiargs, "");                             //line:netp:parseuri:clearcgi
    strcpy(filename, ".");                           //line:netp:parseuri:beginconvert1
    strcat(filename, uri);                           //line:netp:parseuri:endconvert1
    if (uri[strlen(uri)-1] == '/')                   //line:netp:parseuri:slashcheck
       strcat(filename, "home.html");               //line:netp:parseuri:appenddefault
    return 1;
    }
    else {  /* Dynamic content */                        //line:netp:parseuri:isdynamic
    ptr = index(uri, '?');                           //line:netp:parseuri:beginextract
    if (ptr) {
       strcpy(cgiargs, ptr+1);
       *ptr = '\0';
    }
    else 
       strcpy(cgiargs, "");                         //line:netp:parseuri:endextract
    strcpy(filename, ".");                           //line:netp:parseuri:beginconvert2
    strcat(filename, uri);                           //line:netp:parseuri:endconvert2
    return 0;
    }
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
 
    /* Send response headers to client */
    get_filetype(filename, filetype);       //line:netp:servestatic:getfiletype
    sprintf(buf, "HTTP/1.0 200 OK\r\n");    //line:netp:servestatic:beginserve
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));       //line:netp:servestatic:endserve

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);    //line:netp:servestatic:open
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);//line:netp:servestatic:mmap
    Close(srcfd);                           //line:netp:servestatic:close
    Rio_writen(fd, srcp, filesize);         //line:netp:servestatic:write
    Munmap(srcp, filesize);                 //line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
    else
    strcpy(filetype, "text/plain");
}  
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) { /* child */ //line:netp:servedynamic:fork
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1); //line:netp:servedynamic:setenv
    Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */ //line:netp:servedynamic:dup2
    Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
    }
    Wait(NULL); /* Parent waits for and reaps child */ //line:netp:servedynamic:wait
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
        char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}