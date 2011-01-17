#include <sys/types.h>
#include <netinet/in.h>

#include "util.h"

void vntohl(int *p, int cnt){
        /* convert vector of words to host byte order */
        int i;
         for(i=0;i<cnt;i++) p[i] = ntohl(p[i]);
}

void vhtonl(int *p, int cnt){
        /* convert vector of words to network byte order */
        int i;
        for(i=0;i<cnt;i++) p[i] = htonl(p[i]);
}

