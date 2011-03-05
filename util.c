#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <assert.h>
#include <math.h>
#include "util.h"



void
vntohl(int *p, double cnt){
        /* convert vector of words to host byte order */
        int i;
        for(i=0;i<cnt;i++) p[i] = ntohl(p[i]);
}

void
vhtonl(int *p, double cnt){
        /* convert vector of words to network byte order */
        int i;
        for(i=0;i<cnt;i++) p[i] = htonl(p[i]);
}

/*
 * Returns the current UNIX time in seconds.
 */
double
getTime(void){
	timeval_t time;
	gettimeofday(&time, NULL);
	return(time.tv_sec+ time.tv_usec*1.e-6);
}

Ctcp_Pckt*
Packet(unsigned int msgno, char* payload){
  Ctcp_Pckt* packet = malloc(sizeof(Ctcp_Pckt));
  packet->tstamp = getTime();
  packet->msgno = msgno;
  packet->payload = payload;
  return packet;
}

/*
 * Takes a Ctcp_Pckt struct and puts its raw contents into the buffer.
 * This assumes that there is enough space in buf to store all of these.
 */
void
marshall(Ctcp_Pckt msg, char* buf, int payload_size, int mss){
  int index = 0;
  int part = 0;

  memcpy(buf + index, &msg.tstamp, (part = sizeof(msg.tstamp)));
  index += part;
  memcpy(buf + index, &msg.msgno, (part = sizeof(msg.msgno)));
  index += part;
  memcpy(buf + index, msg.payload, (part = payload_size));
  index += part;
  assert(index <= mss);
}

void
unmarshall(Ctcp_Pckt* msg, char* buf, int payload_size, int mss){
  int index = 0;
  int part = 0;

  memcpy(&msg->tstamp, buf+index, (part = sizeof(msg->tstamp)));
  index += part;
  memcpy(&msg->msgno, buf+index, (part = sizeof(msg->msgno)));
  index += part;
  memcpy(msg->payload, buf+index, (part = payload_size));
  assert(index <= mss);
}
