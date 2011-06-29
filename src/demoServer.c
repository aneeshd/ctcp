#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#include <readline/readline.h>

#define PORT "8888"
#define BUFSIZE 500

int
main (int argc, char** argv){
    struct addrinfo hints, *servinfo;
    struct addrinfo *result; //This is where the info about the server is stored
    struct sockaddr cliAddr;
    socklen_t cliAddrLen = sizeof cliAddr;
    int sockfd;
    int rv;

    char buff[BUFSIZE];
    int numbytes;

    printf("Starting Demo Server\n");

    // Setup the hints struct
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags    = AI_PASSIVE;

    // Get the server's info
    if((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // Loop through all the results and connect to the first possible
    for(result = servinfo; result != NULL; result = result->ai_next) {

        if((sockfd = socket(result->ai_family,
                            result->ai_socktype,
                            result->ai_protocol)) == -1){
            perror("demoServer: error during socket initialization");
            continue;
        }
        if (bind(sockfd, result->ai_addr, result->ai_addrlen) == -1) {
            close(sockfd);
            perror("demoServer: can't bind local address");
            continue;
        }
        break;
    }

    if (result == NULL) { // If we are here, we failed to initialize the socket
        perror("atousrv: failed to initialize socket");
        return 2;
    }

    freeaddrinfo(servinfo);


    printf("***Demo Server Ready***\nWaiting for requests...\n");

    while(1)
    {
        if((numbytes = recvfrom(sockfd, buff, BUFSIZE, 0,
                                &cliAddr, &cliAddrLen)) == -1)
        {
            perror("DemoServer: recvfrom failed\n");
        }

        printf("Got %s\n", buff);
        printf("Echoing...\n");

        int len = numbytes + 13;
        char* echoString = malloc(len);

        snprintf(echoString, len, "***Echo***: %s", buff);
        printf("The echoString is %s\n", echoString);

        if((numbytes = sendto(sockfd, echoString, len, 0,
                              &cliAddr, cliAddrLen)) == -1)
        {
            perror("DemoServer: sento failed\n");
        }

        free(echoString);
    }

    return 0;
}
