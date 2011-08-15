#ifndef ATOUSRV_H_
#define ATOUSRV_H_
#include <unistd.h>
#include "util.h"
#include "fifo.h"
#include <sys/poll.h>

typedef struct{
  char* interface;
  char* address;
  char* gateway;
  char* netmask;
} dhcp_lease;

typedef enum {ACTIVE=0, CLOSED} status_t;
typedef enum {NONE=0, CLOSE_ERR, SRVHUP} ctcp_err_t;

//---------------- DEFAULT CONNECTION PARAMETERS ------------------//
#define BUFFSIZE  3000
#define PORT "9999"
#define HOST "127.0.0.1"
#define FILE_NAME "Honda"

#define NUM_BLOCKS 4
#define MAX_SUBSTREAMS 5

#define POLL_ACK_TO 200   // in milliseconds
#define POLL_MAX_TRIES 10
#define TIMEOUT 5000
#define POLL_TO_FLG -7

FILE *rcv_file;

typedef struct{
  //---------------- CTCP PARAMETERS ------------------//
  Coded_Block_t blocks[NUM_BLOCKS];
  uint32_t curr_block;

  // MULTIPLE SUBSTREAMS
  int substreams;

  struct sockaddr srv_addr;
  struct sockaddr* ifc_addr[MAX_SUBSTREAMS];
  int sockfd[MAX_SUBSTREAMS];

  fifo_t usr_cache;

  pthread_t daemon_thread;
  
  status_t status;
  ctcp_err_t error;
  
  //---------------- STATISTICS & ACCOUTING ------------------//
  uint32_t pkts;
  uint32_t acks;
  int debug;
  int ndofs;
  int old_blk_pkts;
  int nxt_blk_pkts;
  int total_loss;
  double idle_total; // The total time the client has spent waiting for a packet
  double decoding_delay;
  double elimination_delay;
  int last_seqno;
  double start_time;
  double end_time;
} clictcp_sock; 

/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void ctrlc(clictcp_sock *csk);
void err_sys(char *s, clictcp_sock *csk);

void bldack(clictcp_sock* csk, Data_Pckt *msg, bool match, int substream);

void normalize(uint8_t* coefficients, char*  payload, uint8_t size);
int  shift_row(uint8_t* buf, int len);
bool isEmpty(uint8_t* coefficients, uint8_t size);

void initCodedBlock(Coded_Block_t *blk);
void unwrap(Coded_Block_t *blk);
void writeAndFreeBlock(Coded_Block_t *blk, fifo_t *buffer);
void partial_write(clictcp_sock* csk);

bool unmarshallData(Data_Pckt* msg, char* buf, clictcp_sock *csk);
int  marshallAck(Ack_Pckt msg, char* buf);

int  readLease(char *leasefile, dhcp_lease *leases);
int  add_routing_tables(char *lease_file);
void remove_routing_tables(char *lease_file);
void make_new_table(dhcp_lease* lease, int table_number, int mark_number);
void delete_table(int table_number, int mark_number);

void *handle_connection(void* arg);
clictcp_sock* create_clictcp_sock(void);

int  poll_flag(clictcp_sock *csk, flag_t flag, int timeout);
int  send_flag(clictcp_sock *csk, int path_id, flag_t flag);
void close_clictcp(clictcp_sock* csk);

void close_clictcp(clictcp_sock* csk);
clictcp_sock* connect_ctcp(char *host, char *port, char *lease_file);
uint32_t  read_ctcp(clictcp_sock* csk, void *usr_buf, size_t count);

int send_over(clictcp_sock* csk, int substream, const void* buf, size_t buf_len);

#endif // ATOUSRV_H_


//---------------- Variables no longer used -----------------------//
// int dups, drops, hi,maxooo, acktimeouts=0;
//unsigned int tempno;
//#define MAXHO 1024
/* holes has ascending order of missing pkts, shift left when fixed */
//#define MAXHO 1024
///*implementing sack & delack */
//int sack=0;
//int  hocnt, holes[MAXHO];
//int ackdelay=0 /* usual is 200 ms */, ackheadr, sackinfo;
//double et,minrtt=999999., maxrtt=0, avrgrtt;
//int start[3], endd[3];
//int  settime=0;
//int expect=1, expected, sendack=0, sackcnt, inlth;
//double rcvt

//---------------- Functions no longer used -----------------------//
//unsigned int millisecs();
