#ifndef ATOUCLI_H_
#define ATOUCLI_H_

#define TRUE 1
#define FALSE 0

#define PORT "7890" // This is the port that the server is listening to
#define BUFFSIZE    65535
#define RTT_DECAY 4 // This is used as the factor to approximate the rto

FILE *db;     /* debug trace file */
FILE *snd_file; // The file to be sent
char *version = "$Revision: 1.38 $";

struct addrinfo *result; //This is where the info about the server is stored
struct sockaddr cli_addr;
socklen_t clilen = sizeof cli_addr;

double dbuff[BUFFSIZE/8];
char *buff = (char *)dbuff;


/* TCP pcb like stuff */
int dupacks;			/* consecutive dup acks recd */
unsigned int snd_nxt; 		/* send next */
unsigned int snd_max; 		/* biggest send */
unsigned int snd_una; 		/* last unacked */
unsigned int snd_fack;		/* Forward (right) most ACK */
unsigned int snd_recover;	/* One RTT beyond last good data, newreno */
double snd_cwnd;		/* congestion-controlled window */
unsigned int snd_ssthresh;	/* slow start threshold */

unsigned int ackno;
unsigned int file_position; // Indicates the msg number at which we are in the file at any given moment

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
int newreno = 1;		/* newreno flag */
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
int vinss=0;   /* in vegas slow start */
int vsscnt=0;  /* number of vegas slow start adjusts */
int vcnt;  /* number of rtt samples */
int vdecr, v0 ; /* vegas decrements or no adjusts */
double vdelta, vrtt,vrttsum,vrttmax, vrttmin=999999;
//------------------------------------------------------------//

int initial_ss =1;   /* initial slow start */

unsigned int bwe_pkt, bwe_prev, bwe_on=1; // Bandwith estimate??
double bwertt, bwertt_max;
double max_delta;  /* vegas like tracker */


/* stats */
int ipkts,opkts,dup3s,dups,packs,badacks,maxburst,maxack, rxmts, timeouts;
int enobufs, ooacks;
double et,minrtt=999999., maxrtt=0, avrgrtt;
static double rto,delta,srtt=0,rttvar=3., h=.25, g=.125;
double due,rcvt;

char *configfile = "config";

//---------------- Function Definitions -----------------------//


/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void ctrlc();

/*
 * 
 */
void done(void);

void terminate(socket_t fd);

/*
 * print the program's usage and exit
 */
void usage(void);

/*
 * 
 */
void readConfig(void);

/*
 *
 */
int doit( socket_t fd);

/*
 *
 */
void send_segs(socket_t fd);

/*
 *
 */
void err_sys(char* s);

/*
 *
 */
socket_t timedread(socket_t fd, double t);

/*
 *
 */
void handle_ack(socket_t fd, Ctcp_Pckt* ack);

/*
 *
 */
void floyd_aimd(int cevent);

/*
 *
 */
void send_one(socket_t fd, unsigned int n);

/*
 *
 */
void bwe_calc(double rtt);

/*
 *
 */
int tcp_newreno(socket_t fd);

/*
 *
 */
void advance_cwnd(void);

/*
 *
 */
void duplicate(socket_t fd, int sackno);

void restart(void);

#endif // ATOUCLI_H_
