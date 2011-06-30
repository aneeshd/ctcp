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

char*
get_ip_str(const struct sockaddr *sa, char *s, size_t maxlen)
{
  switch(sa->sa_family) {
  case AF_INET:
    inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
              s, maxlen);
    break;

  case AF_INET6:
    inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
              s, maxlen);
    break;

  default:
    strncpy(s, "Unknown AF", maxlen);
    return NULL;
  }

  return s;
}

int
main (int argc, char** argv){
    struct addrinfo hints, *servinfo;
    struct addrinfo *result; //This is where the info about the server is stored
    struct sockaddr cliAddr;
    socklen_t cliAddrLen = sizeof cliAddr;
    int       sockfd;
    int       rv;
    char      ip[INET6_ADDRSTRLEN] = {0};
    char      buff[BUFSIZE];
    int       numbytes;

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

        char* tmp = malloc(numbytes);

        snprintf(tmp, numbytes+1, "%s", buff);

        printf("Got %s from %s\n", tmp,
               get_ip_str(&cliAddr, ip, INET6_ADDRSTRLEN));

        printf("Echoing...\n");

        int len = strlen(tmp) + 7;
        char* echoString = malloc(len);

        snprintf(echoString, len, "Echo: %s", tmp);

//        snprintf(echoString, len, "Echo: %s from %s", buff, get_ip_str(&cliAddr, ip, INET6_ADDRSTRLEN));
//        printf("The echoString is %s\n", echoString);

        if((numbytes = sendto(sockfd, echoString, len, 0,
                              &cliAddr, cliAddrLen)) == -1)
        {
            perror("DemoServer: sento failed\n");
        }

        printf("Echo sent\n");
        //free(echoString);
        //free(tmp);
    }

    return 0;
}
