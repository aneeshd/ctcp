#ifndef ATOUCLI_H_
#define ATOUCLI_H_

#include "thr_pool.h"
#include "qbuffer.h"

#define NUM_BLOCKS 2

#define TRUE 1
#define FALSE 0

#define THREADS 5
#define PORT "7890" // This is the port that the server is listenin to
#define BUFFSIZE    65535
#define RTT_DECAY 3 // This is used as the factor to approximate the rto

#define ALPHA 2.32  // The number of std to deviate from mean to get 1% target error probability

FILE *db;     /* debug trace file */
char* log_name; // Name of the log
FILE *snd_file; // The file to be sent
char *version = "$Revision: 1.38 $";

struct addrinfo *result; //This is where the info about the server is stored
struct sockaddr cli_addr;
socklen_t clilen = sizeof cli_addr;

double dbuff[BUFFSIZE/8];
char *buff = (char *)dbuff;

uint32_t curr_block; // Current block number
bool done;
Block_t blocks[NUM_BLOCKS];

// ------------ Multithreading related variables ---------------//
qbuffer_t coded_q[NUM_BLOCKS];
thr_pool_t workers;

// Internal dof counter: the number of dofs left in the server (does not necessarily coincide with the number of packets in coded_q)
// Need to update this whenever we add a coding job, or pop the coded_q
int dof_remain[NUM_BLOCKS];  

pthread_mutex_t file_mutex;  // Allows locking the file while reading a block

//---------- CTCP specific variables --------------//
int coding_wnd = CODING_WND;
uint32_t maxblockno = 0; // 0 denoting infty, whenever we reach the maximum block of the file, we set it


int NextBlockOnFly = 0;
uint32_t OnFly[MAX_CWND]; 
int dof_req = BLOCK_SIZE;

uint32_t snd_nxt;
uint32_t snd_una;


/* TCP pcb like stuff */
int dupacks;			/* consecutive dup acks recd */
unsigned int snd_max; 		/* biggest send */
//unsigned int snd_una; 		/* last unacked */
unsigned int snd_fack;		/* Forward (right) most ACK */
unsigned int snd_recover;	/* One RTT beyond last good data, newreno */
double snd_cwnd;		/* congestion-controlled window */
unsigned int snd_ssthresh;	/* slow start threshold */

uint32_t ackno;
unsigned int file_position; // Indicates the block number at which we are in the file at any given moment

/* configurable variables */
int droplist[11];  /* debuggin */
int debug;
double tick = 0.2;		/* recvfrom timeout -- select */
double timeout=0.5;		/* pkt timeout */
int idle=0;                     /* successive timeouts */
int maxidle=10;                 /* max idle before abort */
int maxpkts=0;			/* test duration */
int maxtime=10;                 /* test duration */
int burst_limit = 0;		/* most to send at once --- weak */
int rcvrwin = 20;		/* rcvr window in mss-segments */
int dup_thresh =3;		/* dup ACKs causing retransmit */
int increment = 1;   		/* cc increment */
double multiplier = 0.5;	/* cc backoff */
double kai = 0.;		/* Kelly scalable TCP cwnd += kai */
int ssincr =1;			/* slow start increment */
double thresh_init = 1.0;      /* fraction of rcvwind for initial ssthresh*/
int max_ssthresh =0;            /* floyd modified slow start, ? consider frac */
int initsegs = 2;		/* slowstart initial */
int newreno = 0;		/* newreno flag */
int sack = 0;			/* sack flag */
int delack = 0;			/* delack flag */
int rampdown = 0;		/* enable wintrim */
int fack = 0;			/* fack flag */
int floyd = 0;			/* Sally Floyd's aimd changes */
int vegas=0;                    /* vegas flag 0:off 1:last RTT  2:min rtt  */
                                /*            3: avrg rtt  4: max rtt      */
int vss = 0;                    /* vegas slow start  0:exit ss  1: go to floyd*/
double valpha=1.0, vbeta=3.0, vgamma=1.0;  /* vegas parameters */

//--------------- vegas working variables ------------//
int vinss=1;   /* in vegas slow start */
int vsscnt=0;  /* number of vegas slow start adjusts */
int vcnt;  /* number of rtt samples */
int vdecr, v0 ; /* vegas decrements or no adjusts */
double vdelta, vrtt,vrttsum,vrttmax, vrttmin=999999;
//------------------------------------------------------------//

unsigned int bwe_pkt=1, bwe_prev, bwe_on=1; // Bandwith estimate??
double bwertt, bwertt_max;
double max_delta;  /* vegas like tracker */


/* stats */
int ipkts,opkts,dup3s,dups,packs,badacks,maxburst,maxack, rxmts, timeouts;
int enobufs, ooacks;
double et,minrtt=999999., maxrtt=0, avrgrtt;
static double rto,delta,srtt=0,rttvar=3., h=.25, g=1.0/BLOCK_SIZE;
double due,rcvt;

char *configfile = "config";

//---------------- Function Definitions -----------------------//


/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void ctrlc();
void endSession(void);
void terminate(socket_t fd);
void usage(void);
void readConfig(void);
int doit( socket_t fd);
void send_segs(socket_t fd);
void err_sys(char* s);
socket_t timedread(socket_t fd, double t);
void handle_ack(socket_t fd, Ack_Pckt* ack);
void readBlock(uint32_t blockno);
void freeBlock(uint32_t blockno);
void send_one(socket_t fd, unsigned int n);
void bwe_calc(double rtt);
int tcp_newreno(socket_t fd);
void advance_cwnd(void);
int marshallData(Data_Pckt msg, char* buf);
bool unmarshallAck(Ack_Pckt* msg, char* buf);
void duplicate(socket_t fd, int sackno);
void restart(void);
void openLog(void);

void* coding_job(void *a);
void free_coding_job(void* a);
void free_coded_pkt(void* a);



#endif // ATOUCLI_H_
