#ifndef ATOUCLI_H_
#define ATOUCLI_H_
#include "thr_pool.h"
#include "qbuffer.h"

// ------------ Connection parameters ---------------//
#define BUFFSIZE    65535
#define MAX_CONNECT 5

// ------------ CTCP parameters ---------------//
#define NUM_BLOCKS 2
#define THREADS 5
#define ALPHA 2.32           // The number of std to deviate from mean to get 1% target error probability
#define MIN_DOF_REQUEST 0
#define SLR_LONG_INIT 0.05
#define RTO_BIAS 0.10      // (seconds) Bias the time-out timer
#define INIT_RTO 1
#define INIT_CODING_WND 5

/**************************************************/
//---------------Constants -----------------------//
const double slr_wnd_map[10] = {0, 0.0002, 0.002, 0.015, 0.025, 0.042, 0.052, 0.062, 0.075, 1};
const double slr_mem         = 1.0/BLOCK_SIZE;      // The memory of smoothing function
const double slr_longmem     = 1.0/(BLOCK_SIZE*10); // Long term memory smoothing constant
const double g               = 1.0/(BLOCK_SIZE/5);      // Memory for updating slr, rto
const double beta            = 2.5;                 // rto range compared to rtt


// ------------ MultiPath variables ---------------//
uint32_t OnFly[MAX_CONNECT][MAX_CWND];
int dof_req[MAX_CONNECT];
int dof_req_latest;                       /* Latest information about dofs of the current block */
uint32_t snd_nxt[MAX_CONNECT];
uint32_t snd_una[MAX_CONNECT];
double snd_cwnd[MAX_CONNECT];             /* congestion-controlled window */
unsigned int snd_ssthresh[MAX_CONNECT];   /* slow start threshold */
int idle[MAX_CONNECT];                    /* successive timeouts */

double vdelta[MAX_CONNECT];
double max_delta[MAX_CONNECT];            /* vegas like tracker */
int slow_start[MAX_CONNECT];              /* in vegas slow start */
int vdecr[MAX_CONNECT], v0[MAX_CONNECT];  /* vegas decrements or no adjusts */

double minrtt[MAX_CONNECT];
double maxrtt[MAX_CONNECT];
double avrgrtt[MAX_CONNECT];
double srtt[MAX_CONNECT];
double rto[MAX_CONNECT];

double slr[MAX_CONNECT];                  // Smoothed loss rate
double slr_long[MAX_CONNECT];             // slr with longer memory
double slr_longstd[MAX_CONNECT];          // Standard Deviation of slr_long
int total_loss[MAX_CONNECT];              

int path_id;                              // Connection identifier
int max_path_id;                          // total_number of paths so far 

struct sockaddr cli_addr_storage[MAX_CONNECT];
//----------------------------------------------------------------//
FILE *db;     /* debug trace file */
char* log_name = NULL; // Name of the log
FILE *snd_file; // The file to be sent
char *version = "$version 0.0$";
char *configfile = "config/vegas";
char *port = "9999";  // This is the port that the server is listening to

struct addrinfo *result; //This is where the info about the server is stored
struct sockaddr cli_addr;
socklen_t clilen = sizeof cli_addr;

double dbuff[BUFFSIZE/8];
char *buff = (char *)dbuff;

unsigned int file_position; // Indicates the block number at which we are in the file at any moment
uint32_t curr_block; // Current block number
bool done;
Block_t blocks[NUM_BLOCKS];
uint32_t maxblockno; // use highest blockno possible, set when we reach the end of the file
int numbytes;


// ------------ Multithreading related variables ---------------//
qbuffer_t coded_q[NUM_BLOCKS];
thr_pool_t workers;
pthread_mutex_t file_mutex;  // Allows locking the file while reading a block
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
// double thresh_init = 1.0;     /* fraction of rcvwind for initial ssthresh*/
int initsegs;                    /* slowstart initial */
int maxpkts;                     /* test duration */
double valpha, vbeta;            /* vegas parameters (in terms of number of packets) */
int sndbuf;                      /* udp send buff, bigger than mss */
int rcvbuf;                      /* udp recv buff for ACKs*/

//------------------Statistics----------------------------------//
int ipkts,opkts,badacks,timeouts,enobufs, goodacks;
double start_time, total_time;
double rcvt;
double idle_total; // The total time the server has spent waiting for the acks


//---------------- Function Definitions -----------------------//
/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void ctrlc();
void endSession(void);
void terminate(socket_t fd);
void readConfig(void);
int doit( socket_t fd);
void send_segs(socket_t fd);
void err_sys(char* s);
socket_t timedread(socket_t fd, double t);
void handle_ack(socket_t fd, Ack_Pckt* ack);
void readBlock(uint32_t blockno);
void freeBlock(uint32_t blockno);
void send_one(socket_t fd, unsigned int n);
void update_coding_wnd(void);
void advance_cwnd(void);
int marshallData(Data_Pckt msg, char* buf);
bool unmarshallAck(Ack_Pckt* msg, char* buf);
void duplicate(socket_t fd, int sackno);
void restart(void);
void openLog(char* log_name);
void* coding_job(void *a);
void free_coding_job(void* a);
void free_coded_pkt(void* a);
void initialize(void);
int sockaddr_cmp(struct sockaddr* addr1, struct sockaddr* addr2);

#endif // ATOUCLI_H_

