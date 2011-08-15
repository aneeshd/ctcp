#ifndef UTIL_H_
#define UTIL_H_

#include <stdint.h>

typedef int bool;
#define TRUE 1
#define FALSE 0
#define MIN(x,y) (y)^(((x) ^ (y)) &  - ((x) < (y)))
#define MAX(x,y) (y)^(((x) ^ (y)) & - ((x) > (y)))

// flags for Data_Pckt
typedef enum {NORMAL=0, SYN, SYN_ACK, FIN, FIN_ACK} flag_t;

//---------------- CTCP parameters ------------------//
#define MSS 1450 // XXX: make sure that this is fine...
#define UDP_HEADER_SIZE 0 // We have to take into account the UDP header 

#define CHECKSUM_SIZE 16 // MD5 is a 16 byte checksum
// TODO: change such that we can change MSS, BLOCK_SIZE, CODING_WIN via config file

#define BLOCK_SIZE 128 // Maximum # of packets in a block (Default block length)
#define MAX_CWND 45
#define MAX_CODING_WND 128

#define ACK_SIZE sizeof(double) \
    + sizeof(int)               \
    + sizeof(uint32_t)          \
    + sizeof(uint32_t)          \
    + sizeof(uint8_t)           

#define PAYLOAD_SIZE (MSS \
  - UDP_HEADER_SIZE \
  - (sizeof(double) \
   + sizeof(flag_t) \
   + sizeof(uint32_t) \
   + sizeof(uint32_t) \
   + sizeof(uint8_t) \
   + sizeof(uint8_t) \
   + MAX_CODING_WND*sizeof(uint8_t) ) ) 

typedef int socket_t;
typedef struct timeval timeval_t;

typedef struct{
  double tstamp;
  flag_t flag;
  uint32_t  seqno; // Sequence # of the coded packet sent
  uint32_t  blockno; // Base of the current block
  uint8_t start_packet; // The # of the first packet that is mixed
  uint8_t  num_packets; // The number of packets that are mixed
  uint8_t* packet_coeff; // The coefficients of the mixed packets
  //  unsigned char checksum[CHECKSUM_SIZE];  // MD5 checksum
  char *payload;  // The payload size is negotiated at the beginning of the transaction
} Data_Pckt;

typedef struct{
  uint16_t payload_size;
  char* payload;
} Bare_Pckt; // This is the datastructure for holding packets before encoding

// -------------- MultiThreading related variables ------------//

typedef struct{ // TODO: this datastructure can store the dof's and other state related to the blocks
  pthread_mutex_t block_mutex;
  pthread_rwlock_t block_rwlock;
  pthread_cond_t  block_free_condv;
  pthread_cond_t  block_ready_condv;
  uint32_t len; // Number of bare packets inside the block
  char** content; // Array of pointers that point to the marshalled data of the bare packets
} Block_t;

typedef struct{
  double tstamp;
  flag_t flag;
  uint32_t  ackno; // The sequence # that is being acked --> this is to make it Reno-like
  uint32_t  blockno; // Base of the current block
  uint8_t dof_rec;  // Number of dofs left from the block
  //  unsigned char checksum[CHECKSUM_SIZE];  // MD5 checksum
} Ack_Pckt;

typedef struct{
  uint8_t dofs; // Number of degrees of freedom thus far
  uint8_t len;
  char** rows; // Matrix of the coefficients of the coded packets
  char** content; // Contents of the coded packets
  int* row_len;  // maximum number of non-zeros in each row
  int max_coding_wnd; //largest number of packets mixed together
  int dofs_pushed;
  int max_packet_index;
} Coded_Block_t;

double getTime(void);
Data_Pckt* dataPacket(uint32_t seqno, uint32_t blockno, uint8_t num_packets);
Ack_Pckt* ackPacket(uint32_t ackno, uint32_t blockno, uint8_t dofs_left);
void htonpData(Data_Pckt *msg);
void htonpAck(Ack_Pckt *msg);
void ntohpData(Data_Pckt *msg);
void ntohpAck(Ack_Pckt *msg);
void prettyPrint(char** coeffs, int window);
uint8_t FFmult(uint8_t x, uint8_t y);

#endif // UTIL_H_
