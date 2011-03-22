#ifndef UTIL_H_
#define UTIL_H_

#include <stdint.h>

typedef int bool;
#define TRUE 1
#define FALSE 0

// flags for Data_Pckt
typedef enum {NORMAL=0, EXT_MOD, FIN_CLI} flag_t;

#define MSS 1472
#define CHECKSUM_SIZE 16 // MD5 is a 16 byte checksum
#define PAYLOAD_SIZE 1400
#define BLOCK_SIZE 64 // Maximum # of packets in a block

typedef int socket_t;
typedef struct timeval timeval_t;

typedef  struct{
  uint8_t packet_id; // The id of the packet that is mixed wrt blockno
  uint8_t packet_coeff; // The coefficient of the packet that is mixed
} coding_info_t;

typedef struct{

  double tstamp;
  flag_t flag;
  uint16_t  seqno; // Sequence # of the coded packet sent
  uint32_t  blockno; // Base of the current block
  uint8_t  num_packets; // The number of packets that are mixed
  coding_info_t* coding_info;
  //  unsigned char checksum[CHECKSUM_SIZE];  // MD5 checksum
  char *payload;  // The payload size is negotiated at the beginning of the transaction
} Data_Pckt;

typedef struct{
  double tstamp;
  flag_t flag;
  uint16_t  ackno; // The sequence # that is being acked --> this is to make it Reno-like
  uint32_t  blockno; // Base of the current block
  //  unsigned char checksum[CHECKSUM_SIZE];  // MD5 checksum
} Ack_Pckt;

typedef struct{
  uint16_t payload_size;
  char* payload;
} Bare_Pckt; // This is the datastructure for holding packets before encoding

typedef struct{ // TODO: this datastructure can store the dof's and other state related to the blocks
  uint32_t len; // Number of bare packets inside the block
  char* content; // Array of bare content (packets)
} Block_t;


void vntohl(int *p, double cnt);
void vhtonl(int *p, double cnt);
double getTime(void);
Data_Pckt* dataPacket(uint16_t seqno, uint32_t blockno, uint8_t num_packets);
Ack_Pckt* ackPacket(uint16_t ackno, uint32_t blockno);
int marshallData(Data_Pckt msg, char* buf);
int marshallAck(Ack_Pckt msg, char* buf);
bool unmarshallData(Data_Pckt* msg, char* buf);
bool unmarshallAck(Ack_Pckt* msg, char* buf);
void htonpData(Data_Pckt *msg);
void htonpAck(Ack_Pckt *msg);
void ntohpData(Data_Pckt *msg);
void ntohpAck(Ack_Pckt *msg);
#endif // UTIL_H_
