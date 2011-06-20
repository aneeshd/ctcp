#ifndef ATOUCLI_H_
#define ATOUCLI_H_
#include "thr_pool.h"
#include "qbuffer.h"

// ------------ Connection parameters ---------------//
#define BUFFSIZE    65535


// ------------ CTCP parameters ---------------//
#define NUM_BLOCKS 2
#define THREADS 5
//#define RTT_DECAY 3 // This is used as the factor to approximate the rto
#define ALPHA 2.32  // The number of std to deviate from mean to get 1% target error probability
#define MIN_DOF_REQUEST 5
#define SND_CWND 64
#define SLR_LONG_INIT 0.05
#define INIT_RTO 1

#define MAX_CODING_WND 128
#define INIT_CODING_WND 5


/*************************************************************
 ************************************************************/

// ------------ CTCP variables ---------------//
FILE *db;     /* debug trace file */
char* log_name = NULL; // Name of the log
FILE *snd_file; // The file to be sent
char *version = "$version 0.0$";
char *configfile = "config";

char *port = "9999";  // This is the port that the server is listening to

struct addrinfo *result; //This is where the info about the server is stored
struct sockaddr cli_addr;
socklen_t clilen = sizeof cli_addr;

double dbuff[BUFFSIZE/8];
char *buff = (char *)dbuff;
uint32_t curr_block; // Current block number
bool done;
Block_t blocks[NUM_BLOCKS];
uint32_t maxblockno = 0; // use highest blockno possible, set when we reach the end of the file
int NextBlockOnFly = 0;
uint32_t OnFly[MAX_CWND];
int dof_req = BLOCK_SIZE;

double slr_wnd_map[10] = {0, 0.0002, 0.002, 0.015, 0.025, 0.042, 0.052, 0.062, 0.075, 1};

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

// ------------ TCP variables ---------------//

uint32_t snd_nxt;
uint32_t snd_una;
unsigned int snd_max; /* biggest send */
double snd_cwnd;     /* congestion-controlled window */
unsigned int snd_ssthresh; /* slow start threshold */
uint32_t ackno;
unsigned int file_position; // Indicates the block number at which we are in the file at any given moment

/* configurable variables */

int debug;
int idle=0;         /* successive timeouts */
int maxidle=10;     /* max idle before abort */
int maxpkts=0;      /* test duration */
int maxtime=10;     /* test duration */


// TODO actual use rcvrwin!
int rcvrwin = 20;         /* rcvr window in mss-segments */
int increment = 1;        /* cc increment */
double multiplier = 0.5;  /* cc backoff  &  fraction of rcvwind for initial ssthresh*/
int ssincr =1;            /* slow start increment */
// double thresh_init = 1.0; /* fraction of rcvwind for initial ssthresh*/
int initsegs = 2;         /* slowstart initial */

//--------------- vegas working variables ------------//
double valpha=0.05, vbeta=0.2;  /* vegas parameters (in terms of number of packets) */
double vdelta;
double max_delta=0;             /* vegas like tracker */
int slow_start=1;                    /* in vegas slow start */
int vdecr, v0 ;                 /* vegas decrements or no adjusts */

/* stats */
int ipkts,opkts,badacks,timeouts,enobufs, goodacks;
double start_time, total_time, maxrtt=0, avrgrtt;
static double rto,srtt=0, minrtt = 999999;//,rttvar=3.
static double h=.25, g=1.0/BLOCK_SIZE, beta=5;
double due,rcvt;

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

#endif // ATOUCLI_H_

