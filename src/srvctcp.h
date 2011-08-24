#ifndef ATOUCLI_H_
#define ATOUCLI_H_

#include <stdint.h>
#include "util.h"
#include "thr_pool.h"
#include "qbuffer.h"

// ------------ Connection parameters ---------------//
#define BUFFSIZE    3000
#define MAX_CONNECT 5
typedef enum {ACTIVE=0, CLOSED, SK_CLOSING} status_t;
typedef enum {NONE=0, CLOSE_ERR, CLIHUP} ctcp_err_t;
typedef enum {SYN_RECV=0, SYN_ACK_SENT, ESTABLISHED, FIN_SENT, FIN_ACK_RECV, FIN_RECV, FIN_ACK_SENT, CLOSING} srvpath_t;

// ------------ CTCP parameters ---------------//
#define NUM_BLOCKS 4
#define THREADS 5
#define ALPHA (0.0)           // The number of std to deviate from mean to get 1% target error probability
#define MIN_DOF_REQUEST 0
#define SLR_LONG_INIT 0.05
#define RTO_BIAS 0.010      // (seconds) Bias the time-out timer
#define INIT_RTO 0.5
#define INIT_CODING_WND 5
#define CONTROL_MAX_RETRIES 10

//---------------Constants -----------------------//
#define slr_mem          1.0/BLOCK_SIZE      // The memory of smoothing function
#define slr_longmem      1.0/(BLOCK_SIZE*10) // Long term memory smoothing constant
#define g                1.0/(BLOCK_SIZE/5)      // Memory for updating slr, rto
#define beta             2.5                 // rto range compared to rtt

//------------ MultiPath variables ---------------//
typedef struct{
  uint32_t OnFly[MAX_CWND];
  double tx_time[MAX_CWND];
  int packets_sent[NUM_BLOCKS]; 
  double last_ack_time;
  uint32_t snd_nxt;
  uint32_t snd_una;
  double snd_cwnd;                     
  unsigned int snd_ssthresh;                // slow start threshold
  int idle;                                 // successive timeouts 
  double vdelta;
  int slow_start;                           // in slow start
  double srtt;
  double rto;
  double slr;                               // Smoothed loss rate
  double slr_long;                          // slr with longer memory
  double slr_longstd;                       // Standard Deviation of slr_long
  struct sockaddr cli_addr;
  int total_loss;
  double minrtt;
  double maxrtt;
  double avrgrtt;
  int vdecr;                                // vegas decrements or no adjusts 
  int v0;
  double max_delta;                         // vegas like tracker

  srvpath_t pathstate;                     // connection states

  
} Substream_Path;

typedef struct{
  Substream_Path** active_paths;            // 0-1 representing whether path alive
  int num_active;                           // Connection identifier
  socket_t sockfd;                          /* network file descriptor */

  int dof_req_latest;                       /* Latest information about dofs of the current block */
  uint32_t curr_block; // Current block number
  Block_t blocks[NUM_BLOCKS];
  uint32_t maxblockno; // use highest blockno possible, set when we reach the end of the file

  // ------------ Multithreading related variables ---------------//
  pthread_t daemon_thread;
  
  qbuffer_t coded_q[NUM_BLOCKS];
  thr_pool_t workers;
  /*
    Internal dof counter:
    -the number of dofs left in the server
    -does not necessarily coincide with the number of packets in coded_q)
    -need to update this whenever a coding job is added/coded packets are popped
  */
  int dof_remain[NUM_BLOCKS];

  //----------------- configurable variables -----------------//
  int debug;
  int maxidle;                     /* max idle before abort */
  int rcvrwin;                     /* rcvr window in mss-segments */    // TODO actual use rcvrwin!
  int increment;                   /* cc increment */
  double multiplier;               /* cc backoff  &  fraction of rcvwind for initial ssthresh*/
  int ssincr;                      /* slow start increment */
  int initsegs;                    /* slowstart initial */
  double valpha, vbeta;            /* vegas parameters (in terms of number of packets) */

  //------------------Statistics----------------------------------//
  int ipkts,opkts,badacks,timeouts,enobufs, goodacks;
  double start_time, total_time;
  double idle_total; // The total time the server has spent waiting for the acks

  status_t status;
  ctcp_err_t error;
  int status_log_fd;

  FILE *db;     /* debug trace file */

} srvctcp_sock;

typedef struct{
  uint32_t blockno;
  int start;
  int dof_request;
  int coding_wnd;
  srvctcp_sock* socket;
} coding_job_t;

//---------------- Function Definitions -----------------------//
/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void ctrlc(srvctcp_sock* sk);
void endSession(srvctcp_sock* sk);
void removePath(srvctcp_sock* sk, int dead_index);
void init_stream(srvctcp_sock* sk, Substream_Path *sp);
int send_flag(srvctcp_sock* sk, int pin, flag_t flag);
int timeout(srvctcp_sock* sk, int pin);
void send_segs(srvctcp_sock* sk, int pin);
socket_t timedread(socket_t fd, double t);
int handle_ack(srvctcp_sock* sk, Ack_Pckt* ack, int pin);
uint32_t readBlock(Block_t* blk, const void *buf, size_t buf_len);
void freeBlock(Block_t* blk);
void send_one(srvctcp_sock* sk, unsigned int n, int pin);
void advance_cwnd(srvctcp_sock* sk, int pin);
void readConfig(char* configfile, srvctcp_sock* sk);
void err_sys(srvctcp_sock* sk, char* s);
int marshallData(Data_Pckt msg, char* buf);
bool unmarshallAck(Ack_Pckt* msg, char* buf);
void openLog(srvctcp_sock* sk, char* log_name);
void* coding_job(void *a);
void free_coded_pkt(void* a);
int sockaddr_cmp(struct sockaddr* addr1, struct sockaddr* addr2);

srvctcp_sock* create_srvctcp_sock(void);
srvctcp_sock* open_srvctcp(char *port);
int listen_srvctcp(srvctcp_sock* sk);
void close_srvctcp(srvctcp_sock* sk);
size_t send_ctcp(srvctcp_sock* sk, const void *usr_buf, size_t usr_buf_len);
void *server_worker(void *arg);


void open_status_log(srvctcp_sock* sk, char* port);
void  log_srv_status(srvctcp_sock* sk);



#endif // ATOUCLI_H_

