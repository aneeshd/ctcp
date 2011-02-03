#ifndef ATOUSRV_H_
#define ATOUSRV_H_

#define MAXHO 1024
#define BUFFSIZE  65536

char *RCSid = "$Header: /home/thistle/dunigan/src/atou/atousrv.c,v 1.8 2002/06/20 14:28:46 dunigan Exp dunigan $";
char *version = "$Revision: 1.8 $";
double dbuff[BUFFSIZE/8];
int *buff = (int *)dbuff;
int sockfd, rcvspace;
int inlth,sackcnt,pkts, dups, drops,hi,maxooo;
int debug = 0,expect=1, expected, acks, acktimeouts=0, sendack=0;
/* holes has ascending order of missing pkts, shift left when fixed */
#define MAXHO 1024
int  hocnt, holes[MAXHO];
/*implementing sack & delack */
int sack=0;
socklen_t clilen;
int ackdelay=0 /* usual is 200 ms */, ackheadr, sackinfo;
int  settime=0;
int start[3], endd[3];

/* stats */
double et,minrtt=999999., maxrtt=0, avrgrtt;
double due,rcvt,st,et,secs();

unsigned int rtt_base=0; // Used to keep track of the rtt
unsigned int tempno;

unsigned int millisecs();


/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void  ctrlc(void);

/*
 */
void err_sys(char *s);

/*
 */
void bldack();

/*
 */
int check_order(int newpkt);

/*
 */
void addho(int n);

/*
 */
void fixho(int n);

/*
 * Returns the current time. Sets the rtt_base global parameter to the current time.
 */
double secs();
  
/*
 */
unsigned int millisecs();

/*
 */
int acktimer(socket_t fd, int t);

#endif // ATOUSRV_H_
