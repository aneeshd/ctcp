#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
/* Include sockio.h if needed */
#ifndef SIOCGIFCONF
#include <sys/sockio.h>
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <net/if.h>
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
#define MAX_SUBSTREAMS 5

/*
 * this is straight from beej's network tutorial. It is a nice wrapper
 * for inet_ntop and helpes to make the program IPv6 ready
 */
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
main(int argc, char** argv){
    int activeSockets = 0;
    int c;
    char* host = "127.0.0.1";
    int sockfd[MAX_SUBSTREAMS];
    int substreams = 1;

    char buff[BUFSIZE];

    while((c = getopt(argc, argv, "h:s:")) != -1){
        switch(c){
        case 'h':
            host = optarg;
            break;
        case 's':
            substreams = atoi(optarg);
            break;
        default:
            printf("Usage: ./demoClient -h host [-m message]\n");
            exit(1);
        }
    }

    struct sockaddr srvAddr;
    socklen_t srvAddrLen = sizeof srvAddr;
    char            ip[INET6_ADDRSTRLEN] = {0};
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

    char            buf[8192] = {0};
    struct ifconf   ifc = {0};
    struct ifreq   *ifr = NULL;
    int             nInterfaces = 0;
    int             i = 0;
    struct ifreq    *item;
    struct sockaddr *addr;
    char            hostname[NI_MAXHOST];

    // loop through all the results and bind to the first possible
loop:
    for(result = servInfo; result != NULL; result = result->ai_next) {
        int i;
        for(i = 0; i < MAX_SUBSTREAMS; i++)
        {
            printf("Initializing socket %d\n", i);
            if((sockfd[i] = socket(result->ai_family,
                                   result->ai_socktype,
                                   result->ai_protocol)) == -1)
            {
                perror("failed to initialize sokcket %d\n");
                goto loop;
            }
        }

        break;
    }

    // If we got here, it means that we couldn't initialize the socket.
    if(result  == NULL){
        perror("failed to create a socket");
        exit(1);
    }

    printf("socket initi success\n");

    /* Query available interfaces. */
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if(ioctl(sockfd[0], SIOCGIFCONF, &ifc) < 0)
    {
        perror("ioctl(SIOCGIFCONF)");
        exit(1);
    }

    /* Iterate through the list of interfaces. */
    ifr = ifc.ifc_req;
    nInterfaces = ifc.ifc_len / sizeof(struct ifreq);
    printf("We have a total of %d interfaces\n", nInterfaces);
    for(i = 0; i < nInterfaces; i++) {
        memset(hostname, 0, NI_MAXHOST);

        item = &ifr[i];
        addr = &(item->ifr_addr);

        /* Get the address
         * This may seem silly but it seems to be needed on some systems
         */
        if(ioctl(sockfd[0], SIOCGIFADDR, item) < 0) {
            perror("ioctl(OSIOCGIFADDR)");
            exit(1);
        }

        if(strcmp(item->ifr_name,"lo") == 0) continue;

        printf("Binding socket %d to %s %s\n",
               i, item->ifr_name,
               get_ip_str(addr, ip, INET6_ADDRSTRLEN));


        bind(sockfd[activeSockets], addr, srvAddrLen);

        activeSockets++;
    }


    int j = 0;
    while(1)
    {
        char* line = readline("Enter message:");

        printf("Got: %s\n", line);

        if((numbytes = sendto(sockfd[j % activeSockets], line, strlen(line), 0,
                              result->ai_addr, result->ai_addrlen)) == -1)
        {
            perror("DemoClient sendto failed");
        }

        if((numbytes = recvfrom(sockfd[j % activeSockets], buff, BUFSIZE, 0,
                                &srvAddr, &srvAddrLen)) == -1)
        {
            perror("DemoClient recvfrom failed");
        }

        j++;

        if(strlen(buff) == 0)
        {
            printf("Exiting Client...\n");
            for(j = 0; j < substreams; j++)
            {
                close(sockfd[j]);
            }
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

