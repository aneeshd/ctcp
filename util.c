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

Data_Pckt*
dataPacket(uint16_t seqno, uint32_t blockno, uint8_t num_packets){
  Data_Pckt* packet = malloc(sizeof(Data_Pckt));
  packet->tstamp = getTime();
  packet->flag = NORMAL;
  packet->seqno = seqno;
  packet->blockno = blockno;
  packet->num_packets = num_packets;
  packet->coding_info = malloc(num_packets*sizeof(coding_info_t));
  packet->payload = malloc(PAYLOAD_SIZE);
  return packet;
}

Ack_Pckt*
ackPacket(uint16_t ackno, uint32_t blockno){
  Ack_Pckt* ack = malloc(sizeof(Ack_Pckt));
  ack->tstamp = getTime();
  ack->flag = NORMAL;
  ack->ackno = ackno;
  ack->blockno = blockno;
  return ack;
}

/*
 * Takes a Data_Pckt struct and puts its raw contents into the buffer.
 * This assumes that there is enough space in buf to store all of these.
 * The return value is the number of bytes used for the marshalling
 */
int
marshallData(Data_Pckt msg, char* buf){
  int index = 0;
  int part = 0;  
  int size = PAYLOAD_SIZE + sizeof(double) + 2 + 4 + 1 + msg.num_packets*2; // the total size in bytes of the current packet

  //Set to zeroes before starting
  memset(buf, 0, size);

  // Marshall the fields of the packet into the buffer

  htonpData(&msg);
  memcpy(buf + index, &msg.tstamp, (part = sizeof(msg.tstamp)));
  index += part;
  memcpy(buf + index, &msg.flag, (part = sizeof(msg.flag)));
  index += part;
  memcpy(buf + index, &msg.seqno, (part = sizeof(msg.seqno)));
  index += part;
  memcpy(buf + index, &msg.blockno, (part = sizeof(msg.blockno)));
  index += part;
  memcpy(buf + index, &msg.num_packets, (part = sizeof(msg.num_packets)));
  index += part;

  int i;
  for(i = 0; i < msg.num_packets; i ++){
    memcpy(buf + index, &msg.coding_info[i].packet_id, (part = sizeof(msg.coding_info[i].packet_id)));
    index += part;

    memcpy(buf + index, &msg.coding_info[i].packet_coeff, (part = sizeof(msg.coding_info[i].packet_coeff)));
    index += part;
  }

  memcpy(buf + index, msg.payload, part = PAYLOAD_SIZE);
  index += part;
  
  /*
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
    }*/

  return index;
}

int
marshallAck(Ack_Pckt msg, char* buf){
  int index = 0;
  int part = 0;  
  int ack_size = sizeof(double) + sizeof(int) + 2 + 4;

  //Set to zeroes before starting
  memset(buf, 0, ack_size);

  // Marshall the fields of the packet into the buffer
  htonpAck(&msg);
  memcpy(buf + index, &msg.tstamp, (part = sizeof(msg.tstamp)));
  index += part;
  memcpy(buf + index, &msg.flag, (part = sizeof(msg.flag)));
  index += part;
  memcpy(buf + index, &msg.ackno, (part = sizeof(msg.ackno)));
  index += part;
  memcpy(buf + index, &msg.blockno, (part = sizeof(msg.blockno)));
  index += part;
  /*
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
    }*/

  return index;
}

bool
unmarshallData(Data_Pckt* msg, char* buf){
  int index = 0;
  int part = 0;

  memcpy(&msg->tstamp, buf+index, (part = sizeof(msg->tstamp)));
  index += part;
  memcpy(&msg->flag, buf+index, (part = sizeof(msg->flag)));
  index += part;
  memcpy(&msg->seqno, buf+index, (part = sizeof(msg->seqno)));
  index += part;
  memcpy(&msg->blockno, buf+index, (part = sizeof(msg->blockno)));
  index += part;
  memcpy(&msg->num_packets, buf+index, (part = sizeof(msg->num_packets)));
  index += part;

  ntohpData(msg);

  msg->coding_info = malloc(msg->num_packets*sizeof(coding_info_t));

  int i;
  for(i = 0; i < msg->num_packets; i++){
    memcpy(&msg->coding_info[i].packet_id, buf+index, (part = sizeof(msg->coding_info[i].packet_id)));
    index += part;
    memcpy(&msg->coding_info[i].packet_coeff, buf+index, (part = sizeof(msg->coding_info[i].packet_coeff)));
    index += part;
  }

  msg->payload = malloc(PAYLOAD_SIZE);
  memcpy(msg->payload, buf+index, (part = PAYLOAD_SIZE));
  index += part;

  bool match = TRUE;  
  /*
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


  for(i = 0; i < CHECKSUM_SIZE; i++){
    if(msg->checksum[i] != mdContext.digest[i]){
      match = FALSE;
    }
    }*/
  return match;
}

bool
unmarshallAck(Ack_Pckt* msg, char* buf){
  int index = 0;
  int part = 0;

  memcpy(&msg->tstamp, buf+index, (part = sizeof(msg->tstamp)));
  index += part;
  memcpy(&msg->flag, buf+index, (part = sizeof(msg->flag)));
  index += part;
  memcpy(&msg->ackno, buf+index, (part = sizeof(msg->ackno)));
  index += part;
  memcpy(&msg->blockno, buf+index, (part = sizeof(msg->blockno)));
  index += part;
  ntohpAck(msg);

  bool match = TRUE;  
  /*
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


  for(i = 0; i < CHECKSUM_SIZE; i++){
    if(msg->checksum[i] != mdContext.digest[i]){
      match = FALSE;
    }
    }*/
  return match;
}

void
htonpData(Data_Pckt* msg){
  msg->flag = htonl(msg->flag);  
  msg->seqno = htons(msg->seqno);
  msg->blockno = htonl(msg->blockno);
}

void
htonpAck(Ack_Pckt* msg){
  msg->flag = htonl(msg->flag);
  msg->ackno = htons(msg->ackno);
  msg->blockno = htonl(msg->blockno);
}

void
ntohpData(Data_Pckt* msg){
  msg->flag = ntohl(msg->flag);
  msg->seqno = ntohs(msg->seqno);
  msg->blockno = ntohl(msg->blockno);
}

void
ntohpAck(Ack_Pckt* msg){
  msg->flag = ntohl(msg->flag);
  msg->ackno = ntohs(msg->ackno);
  msg->blockno = ntohl(msg->blockno);
}
