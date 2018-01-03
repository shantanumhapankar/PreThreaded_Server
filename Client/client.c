#include "csapp.c"
#include <stdio.h>
#include <stdlib.h>

int sendimage(int connectionfd, char* imgname);
int receiveimage(rio_t rio, int connectionfd, char* imgname);
int displaygray(char *imgname);
int getpath(void);

int main(int argc, char **argv) 
{
    int clientfd, port;
    int readsize;
    char *host, *image;
    rio_t rio;

    // if (argc != 4) {
    // 	fprintf(stderr, "usage: %s <host> <port> <image>\n", argv[0]);
    // 	exit(0);
    // }
    // host = argv[1];
    // port = atoi(argv[2]);
    // image = argv[3];
    getpath();


    // clientfd = Open_clientfd(host, port);
    // sendimage(clientfd, image);
    // receiveimage(rio, clientfd, image);
    // displaygray(image);
    // Close(clientfd);
    // exit(0);
}

int sendimage(int connectionfd, char* imgname){

    FILE *picture;
    char buff[256];
    int send_size, readret,imglen;
    picture = fopen(imgname,"rb");
    if(!picture)
    {
        printf("Error opening the image %s\n", imgname);
        exit(0);   
    }
    imglen = strlen(imgname);
    fseek(picture, 0, SEEK_END); //Go to the end of the file.
    send_size = ftell(picture);   //Get image size in bytes.
    Rio_writen(connectionfd, &send_size, sizeof(send_size)); //Send image size
    Rio_writen(connectionfd, &imglen, sizeof(int)); //Send image string length
    Rio_writen(connectionfd, imgname, imglen); //Send image name.

    printf("Sending picture %s of size %d bytes to the server \n", imgname, send_size);

    fseek(picture, 0, SEEK_SET); //Go to the start of the file.
    
    while(readret = fread(buff,1,256,picture))
    {   //Reading file in chunks of 256 because it gives a corrupt img file otherwise and writing it to the server.
        Rio_writen(connectionfd, buff, readret);
    }
    fclose(picture);

}

int receiveimage(rio_t rio, int connectionfd, char* imgname){
    FILE *picture;
    char gray[100] = "", recvBuff[256] = "";
    int bytesReceived = 0, imgsize = 0, recData = 256;

    Rio_readinitb(&rio, connectionfd);

    strcat(gray, "gs_");
    strcat(gray, imgname);
    
    Rio_readnb(&rio,&imgsize,sizeof(int));        
    printf("Gray Scaled image size - %d\n",imgsize );

    picture = fopen(gray,"wb");
    fseek(picture, 0, SEEK_SET);
    while(((bytesReceived = Rio_readnb(&rio,recvBuff,recData))>0) && imgsize>0)
    {
        fwrite(recvBuff, 1,bytesReceived,picture);
        imgsize -= bytesReceived;
        if (imgsize < 256)
            recData = imgsize;
    }
    fclose(picture);
}

int displaygray(char *imgname){
    if (Fork() == 0) {
        printf("Image Name %s\n", imgname);
        execl("/home/shantanu/MSEE/SEM_1/EOS/Codes/test/Client/cv_display", "./cvtest", imgname, (char *)0);
    }
    Wait(NULL);
    printf("Child reaped\n");
    return 1;
}

int getpath(void){
    FILE *config;
    char line[100] = "";
    char *token, n[20];
    char path[100] = "path";
    int i;
    config = fopen("config.txt","r");
    fseek(config, 0, SEEK_SET);
    if(!config)
    {
        printf("Error opening the config file\n");
        exit(0);   
    }
    while(fgets(line, 100, config) != NULL)
    {
        // printf ("LINE - %s\n", line);
        if(strstr(line, path)){
            // printf("Yes\n");
            token = strtok(line, "\"");
            while(token != NULL){
                strcpy(n,token);
                token = strtok(NULL,"\"");
            }
            printf("%s\n",token);

        }
    }
    fclose(config);
}