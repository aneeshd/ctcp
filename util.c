#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <assert.h>
#include <math.h>
#include "util.h"
#include "md5.h"

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
Packet(unsigned int msgno, int payload_size){
  Ctcp_Pckt* packet = malloc(sizeof(Ctcp_Pckt));
  packet->tstamp = getTime();
  packet->flag = NORMAL;
  packet->msgno = msgno;
  packet->payload = malloc(payload_size);
  packet->payload_size = payload_size;
  return packet;
}

/*
 * Takes a Ctcp_Pckt struct and puts its raw contents into the buffer.
 * This assumes that there is enough space in buf to store all of these.
 * The return value is the number of bytes used for the marshalling
 */
int
marshall(Ctcp_Pckt msg, char* buf){
  int index = 0;
  int part = 0;  
  int payload_size = msg.payload_size;
  int size = payload_size + HDR_SIZE;

  //Set to zeroes before starting
  memset(buf, 0, size);

  // Marshall the fields of the packet into the buffer
  htonp(&msg);
  memcpy(buf + index, &msg.tstamp, (part = sizeof(msg.tstamp)));
  index += part;
  memcpy(buf + index, &msg.flag, (part = sizeof(msg.flag)));
  index += part;
  memcpy(buf + index, &msg.msgno, (part = sizeof(msg.msgno)));
  index += part;
  memcpy(buf + index, &msg.payload_size, (part = sizeof(msg.payload_size)));
  index += part;
  memcpy(buf + index, msg.payload, (part = payload_size));
  index += part;
  
  //----------- MD5 Checksum calculation ---------//
  MD5_CTX mdContext;
  MD5Init(&mdContext);
  MD5Update(&mdContext, buf, size);
  MD5Final(&mdContext);

  // Put the checksum in the marshalled buffer
  int i;
  for(i = 0; i < CHECKSUM_SIZE; i++){
    memcpy(buf + index, &mdContext.digest[i], (part = sizeof(mdContext.digest[i])));
    index += part;
  }

  return index;
}

bool
unmarshall(Ctcp_Pckt* msg, char* buf){
  int index = 0;
  int part = 0;

  memcpy(&msg->tstamp, buf+index, (part = sizeof(msg->tstamp)));
  index += part;
  memcpy(&msg->flag, buf+index, (part = sizeof(msg->flag)));
  index += part;
  memcpy(&msg->msgno, buf+index, (part = sizeof(msg->msgno)));
  index += part;
  memcpy(&msg->payload_size, buf+index, (part = sizeof(msg->payload_size)));
  index += part;
  ntohp(msg);
  msg->payload = malloc(msg->payload_size);
  memcpy(msg->payload, buf+index, (part = msg->payload_size));
  index += part;
  
  int begin_checksum = index;
 
  // -------------------- Extract the MD5 Checksum --------------------//
  int i;
  for(i=0; i < CHECKSUM_SIZE; i++){
    memcpy(&msg->checksum[i], buf+index, (part = sizeof(msg->checksum[i])));
    index += part;
  }

  // Before computing the checksum, fill zeroes where the checksum was
  memset(buf+begin_checksum, 0, CHECKSUM_SIZE);

  //-------------------- MD5 Checksum Calculation  -------------------//
  MD5_CTX mdContext;
  MD5Init(&mdContext);
  MD5Update(&mdContext, buf, msg->payload_size + HDR_SIZE);
  MD5Final(&mdContext);

  bool match = TRUE;
  for(i = 0; i < CHECKSUM_SIZE; i++){
    if(msg->checksum[i] != mdContext.digest[i]){
      match = FALSE;
    }
  }
  return match;
}

void
htonp(Ctcp_Pckt* msg){
  msg->flag = htonl(msg->flag);
  msg->msgno = htonl(msg->msgno);
  msg->payload_size = htonl(msg->payload_size);
}

void
ntohp(Ctcp_Pckt* msg){
  msg->flag = ntohl(msg->flag);
  msg->msgno = ntohl(msg->msgno);
  msg->payload_size = ntohl(msg->payload_size);
}
