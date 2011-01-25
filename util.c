#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/time.h>
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

/*
 * Returns the current UNIX time in seconds.
 */
double getTime(void){
	timeval_t time;
	gettimeofday(&time, NULL);
	return(time.tv_sec+ time.tv_usec*1.e-6);
}
