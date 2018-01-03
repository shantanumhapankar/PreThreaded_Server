#include "csapp.c"
// #include <unistd.h>

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
            printf ("worker: queue EMPTY.\n");
            pthread_cond_wait (fifo->notEmpty, fifo->mut);
        }
        queueDel (fifo, &d);
        pthread_mutex_unlock (fifo->mut);
        pthread_cond_signal (fifo->notFull);

        printf("worker received : %d \n", d);

        // millisleep(30000);

        imagename = receiveimage(rio, d);
        colortogray(imagename);
        sendimage(d, imagename);
        free(imagename);
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
        printf("Testing\n");
        execl("/home/shantanu/MSEE/SEM_1/EOS/Codes/test/Server/cv_gray", "./cvtest", imgname, (char *)0);
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
