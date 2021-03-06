#ifndef ATOUCLI_H_
#define ATOUCLI_H_

#include <stdint.h>
#include "util.h"
#include "thr_pool.h"
#include "qbuffer.h"
#include "child_remote.h"

// ------------ Connection parameters ---------------//
#define BUFFSIZE    3000
#define MAX_CONNECT 5
typedef enum {ACTIVE=0, CLOSED, SK_CLOSING} status_t;
typedef enum {NONE=0, CLOSE_ERR, CLIHUP} ctcp_err_t;
typedef enum {SYN_RECV=0, SYN_ACK_SENT, ESTABLISHED, FIN_SENT, FIN_ACK_RECV, FIN_RECV, FIN_ACK_SENT, CLOSING} srvpath_t;

// ------------ CTCP parameters ---------------//
#define THREADS 5
#define ALPHA (0.0)           // The number of std to deviate from mean to get 1% target error probability
#define MIN_DOF_REQUEST 0
#define SLR_LONG_INIT 0.05
#define RTO_BIAS 0.010      // (seconds) Bias the time-out timer
#define INIT_RTO 1 
#define RTO_MIN 0.2
#define INIT_CODING_WND 5
#define CONTROL_MAX_RETRIES 10

//---------------Constants -----------------------//
#define slr_mem          1.0/128      // The memory of smoothing function
#define slr_longmem      1.0/(BLOCK_SIZE*10) // Long term memory smoothing constant
#define g                1.0/(BLOCK_SIZE/5)      // Memory for updating slr, rto
#define beta             2.5                 // rto range compared to rtt

//------------ MultiPath variables ---------------//
typedef struct{
  uint32_t OnFly[MAX_CWND];
  double tx_time[MAX_CWND];
  int packets_sent[NUM_BLOCKS]; 
  double last_ack_time;
  double last_ack_time_user;
  uint32_t snd_nxt;
  uint32_t snd_una;
  uint32_t snd_cwnd;                     
  uint32_t snd_ssthresh;                // slow start threshold
  int idle;                                 // successive timeouts 
  double srtt;
  double srtt_user;                         // used to keep track of delay added by user space operation
  double rttvar;
  double mdev;
  double mdev_max;
  double rto;
  double slr;                               // Smoothed loss rate
  double slr_long;                          // slr with longer memory
  double slr_longstd;                       // Standard Deviation of slr_long
  struct sockaddr_in cli_addr;
  int total_loss;
  double minrtt;
  double maxrtt;
  double basertt;
  double rtt;
  double avrgrtt;
  int vdecr;                                // vegas decrements or no adjusts 
  int v0;
  double max_delta;                         // vegas like tracker
  uint32_t cntrtt;			    // vegas count of ACKs received
  uint32_t dec_snd_nxt;
  int toggle;
  uint32_t beg_snd_nxt;        		    // marker for next cwnd update 
  uint32_t beg_snd_una; 
  double time_snd_nxt;			    // keep track of time elaped between cwnd updates, for tput logging
  double rate;
  double goodput;
  int losscnt;
  
  srvpath_t pathstate;                     // connection states
  
} Substream_Path;

typedef struct{
  
  // ------------ Variables shared r/w by threads
  int dof_req_latest;           /* Latest information about dofs of the current block */    
  uint32_t maxblockno;          // use highest blockno possible, set when we reach the end of the file
  uint32_t curr_block;          // Current block number  
  pthread_mutex_t curr_block_mutex; // Lock for accessing curr_block,dof_req_latest,maxblockno
  // IMPORTANT: To avoid deadlocks, if need to take a lock on sk->curr_block_mutex and also on sk->blocks[].block_mutex, 
  // then always do this in the order sk->curr_block_mutex then sk->blocks[].block_mutex.
  Block_t blocks[NUM_BLOCKS];   // Each Block_t struct contains its own mutex lock that protects both blocks[] and dof_remain[]
  int dof_remain[NUM_BLOCKS];   // Internal dof counter -the number of dofs left in the server
  status_t status;              // Connection status - ACTIVE, CLOSED etc.  
  pthread_mutex_t status_mutex; // Lock for accessing status
  ctcp_err_t error;             
  pthread_mutex_t error_mutex;   //  Lock for accessing error
  qbuffer_t coded_q[NUM_BLOCKS]; //  Only accessed via own thread-safe methods q_push(), q_pop() etc
  thr_pool_t workers;            //  Only accessed via own thread-safe methods addJob(),  thrpool_init(), thrpool_kill)
  // Multi-threading stuff
  pthread_cond_t block_ready_condv; // Signal for when new data has been received from client.  Only accessed via p_thread methods
  pthread_t daemon_thread;          // Setup in listen_srvctcp(), used in close_srvctcp(). Only accessed via p_thread methods
  
  // ------------- Shared, but used read only within multi-threaded context. 
  // All are setup in open_srvctcp() or create_srvctcp_sock() and then used read-only.
  socket_t sockfd;                          /* Network file descriptor.   */
  char clientip[INET6_ADDRSTRLEN];          // Client IP address for this connection.  
  uint16_t clientport;                      // Client port. 
  // Configuration variables.  
  int debug;                       // Level of debug output.  
  char cong_control[32];           // Specified cong control algorithm to use.  
  double SLR_scaling;              /* multiplies the estimated loss rate when caclulating coding rate.  */
  int ctcp_probe;                  // enable ctcp-probe logging ? 
  char logdir[256];                // passed from config file
  int maxidle;                     /* max idle before abort */
  int rcvrwin;                     /* rcvr window in mss-segments */    // TODO actual use rcvrwin!
  int increment;                   /* cc increment */
  double multiplier;               /* cc backoff  &  fraction of rcvwind for initial ssthresh*/
  int ssincr;                      /* slow start increment */
  int initsegs;                    /* slowstart initial */
  int valpha, vbeta;               /* vegas parameters (in terms of number of packets) */

  // ------------- Variables used by a single thread within multi-threaded context
  Substream_Path** active_paths;            // 0-1 representing whether path alive
  int num_active;                           // Connection identifier      
  // Statistics and logging 
  int ipkts,opkts,badacks,timeouts,enobufs, goodacks;
  double start_time, total_time;
  double idle_total; // The total time the server has spent waiting for the acks    
  FILE *db;     /* debug trace file */
  FILE *pkt_log; // packet trace file (captures information from all packets sent from the target)
  int status_log_fd;
  
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
uint32_t readBlock(Block_t* blk, const void *buf, size_t buf_len, int debug);
void freeBlock(Block_t* blk);
void send_one(srvctcp_sock* sk, unsigned int n, int pin);
void advance_cwnd(srvctcp_sock* sk, int pin);
void readConfig(char* configfile, srvctcp_sock* sk);
void err_sys(srvctcp_sock* sk, char* s);
int marshallData(Msgbuf* msgbuf);
bool unmarshallAck(Msgbuf* msgbuf);
void openLog(srvctcp_sock* sk, char* log_name);
void* coding_job(void *a);
void free_coded_pkt(void* a);
int sockaddr_cmp(struct sockaddr_storage* addr1, struct sockaddr_storage* addr2);
int sockaddr_cmp4(struct sockaddr_in* addr1, struct sockaddr_in* addr2);

srvctcp_sock* create_srvctcp_sock(void);
srvctcp_sock* open_srvctcp(char *port, struct child_remote_cfg* cfg);
int listen_srvctcp(srvctcp_sock* sk);
void close_srvctcp(srvctcp_sock* sk);
size_t send_ctcp(srvctcp_sock* sk, const void *usr_buf, size_t usr_buf_len);
void *server_worker(void *arg);


void open_status_log(srvctcp_sock* sk, char* port);
void  log_srv_status(srvctcp_sock* sk);

void log_pkt(srvctcp_sock* sk, int pkt, int pkt_size);

#endif // ATOUCLI_H_

