#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <readline/readline.h>

#define MIN(x,y) (y)^(((x) ^ (y)) &  - ((x) < (y)))
#define MAX(x,y) (y)^(((x) ^ (y)) &  - ((x) > (y)))

#define SERVERPORT "8888"
#define BUFSIZE 500

int
main(int argc, char** argv){
    int c;
    char* host = "127.0.0.1";
    int sockfd;

    char buff[BUFSIZE];

    while((c = getopt(argc, argv, "h:")) != -1){
        switch(c){
        case 'h':
            host = optarg;
            break;
        default:
            printf("Usage: ./demoClient -h host [-m message]\n");
            exit(1);
        }
    }


    struct sockaddr srvAddr;
    socklen_t srvAddrLen = sizeof srvAddr;

    struct addrinfo hints, *servInfo;
    struct addrinfo *result;
    int numbytes;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if((rv = getaddrinfo(host, SERVERPORT, &hints, &servInfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first possible
    for(result = servInfo; result != NULL; result = result->ai_next) {
        if((sockfd = socket(result->ai_family,
                            result->ai_socktype,
                            result->ai_protocol)) == -1){
            perror("failed to initialize socket");
            continue;
        }
        break;
    }

    // If we got here, it means that we couldn't initialize the socket.
    if(result  == NULL){
        perror("failed to create a socket");
    }

    while(1)
    {
        char* line = readline("Enter message:");

        printf("Got: %s\n", line);

        if((numbytes = sendto(sockfd, line, strlen(line), 0,
                              result->ai_addr, result->ai_addrlen)) == -1)
        {
            perror("DemoClient: sedto failed\n");
        }

        if((numbytes = recvfrom(sockfd, buff, BUFSIZE, 0,
                                &srvAddr, &srvAddrLen)) == -1)
        {
            perror("DemoClient: recvfrom failed\n");
        }

        if(strlen(buff) == 0)
        {
            printf("Exiting Client...\n");
            close(sockfd);
            break;
        }

        int i = 0;
        for(i=0; i < numbytes; i++)
        {
            printf("%c", buff[i]);
        }
        printf("\n");
    }

    return 0;
}

