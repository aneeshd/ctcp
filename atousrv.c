/*                          thd@ornl.gov  ffowler@cs.utk.edu
 *  atousrv [-options] 
	-s	enable SACK 
	-d ##	amount to delay ACK's in ms (default 0, often 200)
	-p ##	port number to receive on (default 7890)
	-b ##	set socket receive buffer size (default 8192)
	-D ##   enable debug level
 * udp server   (only good for one session, have to ctrl-c it )
 * version sending ack for every other packet & using select timer for
 *   emergency timeout (if ack has not been sent in last 200 ms)
 * TODO:  sack, TCP control port?
 *   could do delayed ACK with select() timeout (200ms), with timer signal
 * (too much overhead), or best, select() timeout with delta mod 200 ms
 */

#include <stdio.h>
#include	<stdlib.h>
#include <sys/types.h>
#include        <sys/socket.h>
#include        <netinet/in.h>
#include        <arpa/inet.h>
#include        <unistd.h>
#include        <netdb.h>
#include        <signal.h>
#include	<sys/time.h>
#include	<errno.h>

char *RCSid = "$Header: /home/thistle/dunigan/src/atou/atousrv.c,v 1.8 2002/06/20 14:28:46 dunigan Exp dunigan $";
char *version = "$Revision: 1.8 $";
#define PORT 7890
#define BUFFSIZE        65536
double dbuff[BUFFSIZE/8];
char *buff = (char *)dbuff;
int sockfd, rcvspace;
int inlth,sackcnt,pkts, dups, drops,hi,maxooo;
int debug = 0,expect=1, expected, acks, acktimeouts=0, sendack=0;
/* holes has ascending order of missing pkts, shift left when fixed */
#define MAXHO 1024
int  hocnt, holes[MAXHO];
/*implementing sack & delack */
int sack=0, clilen; 
struct sockaddr_in	cli_addr;
int ackdelay=0 /* usual is 200 ms */, ackheadr, sackinfo;
int  settime=0;
int start[3], endd[3];
void bldack();
/* stats */
double et,minrtt=999999., maxrtt=0, avrgrtt;
static double rto,delta,srtt=0,rttvar=3., h=.25, g=.125;
double due,rcvt,st,et,secs();
unsigned int millisecs(), rtt_base=0;

struct Pr_Msg {
	double tstamp;
	unsigned int msgno;  /* msgno or ackno */
	unsigned int blkcnt;
	struct Sblks {
		unsigned int sblk,eblk;
	} sblks[3];
} *msg, ack;

unsigned int tempno;

void ctrlc(void){
	int i;
	et = et-st;  /* elapsed time */
	if (et==0)et=.1;
	/* don't include first pkt in data/pkt rate */
	printf("%d pkts  %d acks  %d bytes %f KBs %f Mbs %f secs \n",
           pkts,acks,inlth*pkts,1.e-3*inlth*(pkts-1)/et,
	   8.e-6*inlth*(pkts-1)/et,et);
	printf("dups %d drop|oo %d sacks %d inlth %d bytes maxseg %d maxooo %d acktouts %d\n",
	 dups,drops,sackcnt,inlth,hi,maxooo,acktimeouts);
	for(i=0;i<hocnt;i++) printf("lost pkt %d\n",holes[i]);
	exit(0);
}

void usage(void) {
  fprintf(stderr, "Usage: atousrv [-options]\n   \
	-s	enable SACK (default reno) \n  \
	-d ##	amount to delay ACKs (ms) (default 0, often 200)\n   \
	-p ##	port number to receive on (default 7890)\n  \
	-b ##	set socket receive buffer size (default 8192)\n \
	-D ##   enable debug level\n");
  exit(0);
} 

int main(int argc, char** argv){
	int optlen,rlth,port = PORT, n;
	struct sockaddr_in	serv_addr;
	int c, retval=0, tmout, timout;
	double rtt;

	while((c = getopt(argc, argv, "sd:p:b:D:")) != -1) { 
	  switch (c) {
	    case 's':
		sack=1;
		break;
	    case 'd':
		ackdelay = atoi(optarg);  /* ms */
		if (ackdelay < 0) ackdelay = 0;
		break;
	    case 'p':
		port = atoi(optarg);
		break;
	    case 'b':
		rcvspace = atoi(optarg);
		break;
	    case 'D':
		debug = atoi(optarg);
		break;
	    default:
		usage();
	  }
	}

	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err_sys("server: can't open datagram socket");

	signal(SIGINT,ctrlc);
	/*
	 * Bind our local address so that the client can send to us.
	 */

	memset((char *) &serv_addr,0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port        = htons(port);

	optlen = sizeof(rlth);
	if (rcvspace) {
	   setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF, (char *) &rcvspace, optlen);
	}
	getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char *) &rlth, &optlen);
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
		err_sys("server: can't bind local address");
	printf("atousrv %s using port %d rcvspace %d sack %d ackdelay %d ms\n",
	  version,port,rlth,sack,ackdelay);

	memset(buff,0,BUFFSIZE);        /* pretouch */
	msg = (struct Pr_Msg *)buff;
	clilen = sizeof(cli_addr);
	n = recvfrom(sockfd,buff,sizeof(dbuff),0,(struct sockaddr *)&cli_addr,&clilen);
	ackheadr = sizeof(double) + 2*(sizeof(unsigned int));
	sackinfo = sizeof(unsigned int) * 2;
	while(n > 0) {
	  pkts++;
	  et=secs();  /* last read */
	  if (st == 0)st = et;  /* first pkt time */
	  vntohl(buff,sizeof(struct Pr_Msg)/4);  /* to host order, 12 bytes? */
	  if (msg->msgno > hi) hi = msg->msgno ;  /* high water mark */

 if (debug && msg->msgno != expect ) printf("exp %d got %d dups %d sacks %d hocnt %d\n",expect, msg->msgno,dups,sackcnt,hocnt); 

	  if (msg->msgno > expect)addho(msg->msgno);
	  if (msg->msgno < expect)fixho(msg->msgno);

	  expected = expect;
	  if (msg->msgno >= expect) expect = msg->msgno + 1;

	  if(ackdelay) { /* using delayed acks */
	    if(msg->msgno != expected) sendack=1;  /* ack if unexpected */
	    settime = ackdelay - (millisecs()%ackdelay);
            retval = acktimer(sockfd, settime); /* set the timer */
            if(sendack || (expected%2))  {
	      bldack();
	      sendack=0;
	    }
	  }	    
          else { /* not using delayed acks */
	    bldack();
  	  }

	  inlth=n;
	  n = recvfrom(sockfd,buff,sizeof(dbuff),0,(struct sockaddr *)&cli_addr,&clilen);
	}
	if (n < 0 ) err_sys("server recvfrom");
}

err_sys(s)
char *s;
{
	perror(s);
	ctrlc();     /* do stats on error */
}

void bldack()
{
	int i, j, k, first=-1, retransmit=0, newpkt=0;

	/* construct the reply packet */
	ack.tstamp = msg->tstamp;
	ack.blkcnt=0;   /* assume no sacks */
	if (hocnt) {  /* we have a list of lost pkts */
	  newpkt = msg->msgno;
  	  ack.msgno = holes[0];  /* oldest missing */
	  if(sack) {
/*if sack is enabled and there are missing packets--
  find the beginning and ending of up to 3 contiguous blocks of data
  received and put (most recently recvd first)into the ack table(sblks).
*/
          j = 0;
          k = hocnt-1;
/*start with the latest pkt received--unless it is a retransmit */
          if(msg->msgno>=expected)
            i = msg->msgno;
          else {
	    retransmit=1;
	    if(expected<expect)
              i = expected;
	    else
	      i = expected-1;
	  }

          while((k>=0)&&(j<3)) {
/*record the end of the block*/
            ack.blkcnt++;
            endd[j]=i;
            while(holes[k]<i) {
              i--;
            }
/*find and record the beginning of the block*/
            start[j]=i+1;
            j++;
/*find the end of the next most recent, isolated, contiguous 
  block of data received */
            while((holes[k]==i)&&(k>=0)) {
              k--;
              i--;
            }
          }
/*zero out any blks not used*/
          for(i=j; i<3; i++)
            start[i] = endd[i] = 0;
          sackcnt++;
/*make sure most recently reported block is first!*/
	if(retransmit && newpkt>holes[0]) { 
	  first = check_order(newpkt);
	  if(first<0)
	    k=0;
	  else 
	    k=1;
	}
	else
	  k=0;
        for(i=0; i<3; i++) {
          if(i!=first) { 
            ack.sblks[k].sblk=start[i];
            ack.sblks[k].eblk=endd[i];
            k++;
          }
        }
      }

/* No HOLES  */
  } else ack.msgno =  expect;
  k = ackheadr+ack.blkcnt*sackinfo;
  vhtonl(&ack,k/4);  /* to net order */
  if (sendto(sockfd,(char *)&ack,k,0,(struct sockaddr *)&cli_addr,clilen)!=k){
  	err_sys("sendto");
  }
  acks++;
}

int check_order(int newpkt) {
  int i;

  for(i=0; i<3; i++) {
    if(newpkt>=start[i] && newpkt<=endd[i]) {
      ack.sblks[0].sblk = start[i];
      ack.sblks[0].eblk = endd[i];
      return(i);
    }
  }
  return(-1);
}

addho( n)
{
	/* add one or more holes */
	int i,j;

	if (n-expect > maxooo) maxooo = n -expect;
	if (hocnt + n - expect > MAXHO) {
          printf("hole table overflow %d %d %d %d\n",hocnt,n,expect,MAXHO);
	  return;
	}
	for (j=expect; j< n; j++){
	  holes[hocnt++] = j;
	  drops++;   /* or could just be out of order */
	}
}

fixho(n)
{
   /* remove missing pkt, shift vector over replaced pkt */
	int i,j;

	for (i=0;i<hocnt;i++) {
		if (holes[i]==n) {
			for(j=i; j< hocnt-1; j++) holes[j] = holes[j+1];
			hocnt--;
			return;
		}
	}
	dups ++;  /* didn't find a hole */
}

double
secs()
{
#include <sys/time.h>
        struct timeval t;
        gettimeofday(&t, (struct timezone *)0);
	if(rtt_base==0) {
	  rtt_base = t.tv_sec;
/*	  fprintf(stderr, "rtt_base: %u\n", rtt_base);*/
	}
        return(t.tv_sec+ t.tv_usec*1.e-6);
}

unsigned int
millisecs()
{
#include <sys/time.h>
        struct timeval tv;
	unsigned int ts;

        gettimeofday(&tv, (struct timezone *)0);
/*fprintf(stderr, "time of day: %u:%u--", tv.tv_sec, tv.tv_usec);*/
	ts = ((tv.tv_sec-rtt_base) * 1000) + (tv.tv_usec / 1000);
/*fprintf(stderr, "%ld=%u + %u\n", ts, (tv.tv_sec-rtt_base)*1000, tv.tv_usec/1000);*/
        return(ts);
}

void vntohl(int *p, int cnt){
	/* convert vector of words to host byte order */
	int i;
	for(i=0;i<cnt;i++) p[i] = ntohl(p[i]);
}

void vhtonl(int *p, int cnt){
	/* convert vector of words to network byte order */
	int i;
	for(i=0;i<cnt;i++) p[i] = htonl(p[i]);
}

int acktimer(int fd, int t) {
  struct timeval tv;
  fd_set rset;
  int n;

  FD_ZERO(&rset);
  FD_SET(fd, &rset);

    tv.tv_sec = 0;
    tv.tv_usec = (t * 1000);

 while(1) {
    n = select(fd+1, &rset, NULL, NULL, &tv);

  if(n < 0) {
    if(errno == EINTR) {
      continue;
    }
    else
	err_sys("select ERROR");
  }
  else
    if(n==0) {
      if(debug > 2)
        fprintf(stderr, "acktimeout: %d\n", expected);
      acktimeouts++;
      sendack=1;
    }
    
  return n;
 }
}
