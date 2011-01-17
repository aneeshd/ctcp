/* Version implementing the following options based on the config file:
   NEWRENO using the algorithm from rfc2582
   SACK using the scoreboard algorithms and structures from ns (Network Simulator) 
   WINTRIM using the algorithm from "Forward Acknowledgement: Refining TCP Congestion Control" by Mathis and Mahdavi
   FACK using the algorithm from same paper as WINTRIM
   SACK can be used alone or with WINTRIM--FACK includes WINTRIM & builds
     on SACK
*/
/*  atoucli.c   udp version    thd@ornl.gov  ffowler@cs.utk.edu
 * atoucli host [configfile]
 *    also optional config file,  test with probesrv
 *   a reliable (TCP-like) UDP transport protocol for bulk transfers
 *    over high bandwidth, high delay networks
 * TODO:  sack
 *      once we have sack, can do "effective window" (rcvr consumes non-contig)
 *       may need to multi-thread, so can process ack's  ?
 *        and for RCVBUF for incoming ACK's (small 16K), haven't seen lost ACKs
 *    could add a TCP control port
 *       need to use RTO calculations for timeouts (1s ok for ornl-nersc)
 *  connect() will break return pkts from swift
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include <netinet/in.h>      
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

/*---added---*/
#include "scoreboard.h"
#include "util.h"

#define TRUE 1
#define FALSE 0
/*-----------*/

char *RCSid = "$Header: /home/wisp/dunigan/src/atou/atoucli.c,v 1.38 2003/01/16 19:07:57 dunigan Exp dunigan $";
char *version = "$Revision: 1.38 $";

FILE *db;     /* debug trace file */

  /* configurable variables */
int droplist[11];  /* debuggin */
int debug;
char *host;
#define PORT 7890
int port=PORT;			/* udp port */
int sndbuf = 32768;		/* udp send buff, bigger than mss */
int rcvbuf = 32768;		/* udp recv buff for ACKs*/
int mss=1472;			/* user payload, can be > MTU for UDP */
double tick = 1.0;		/* recvfrom timeout -- select */
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
double thresh_init = 1.0;      /* fraction of rvcvwind for initial ssthresh*/
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

/* vegas working variables */
int vinss=0;   /* in vegas slow start */
int vsscnt=0;  /* number of vegas slow start adjusts */
int vcnt;  /* number of rtt samples */
int vdecr, v0 ; /* vegas decrements or no adjusts */
double vdelta, vrtt,vrttsum,vrttmax, vrttmin=999999;

int initial_ss =1;   /* initial slow start */


unsigned int bwe_pkt, bwe_prev, bwe_on=1;
double bwertt, bwertt_max;
double max_delta;  /* vegas like tracker */

#define BUFFSIZE        65536
double dbuff[BUFFSIZE/8];
int *buff = (int *)dbuff;

struct Pr_Msg {
        double tstamp;
        unsigned int msgno;
	unsigned int blkcnt;
        struct Sblks {
                unsigned int sblk,eblk;
        } sblks[3];
} *msg, *ack;

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

/* stats */
int ipkts,opkts,dup3s,dups,packs,badacks,maxburst,maxack, rxmts, timeouts;
int enobufs, ooacks;
double et,minrtt=999999., maxrtt=0, avrgrtt;
static double rto,delta,srtt=0,rttvar=3., h=.25, g=.125;
double due,rcvt,secs();

struct	sockaddr_in san;	/* socket address */

char *configfile = "config";

//------------- Function prototypes-----------------//
void done(void);
void usage(void);
void rdconfig(void);
int doit(char* host);
void send_segs(socket_t fd);
void err_sys(char* s);
socket_t timedread(socket_t fd, double t);
void handle_ack(socket_t fd);
void handle_sack(socket_t fd);
void floyd_aimd(int cevent);
void send_one(socket_t fd, unsigned int n);

void bwe_calc(double rtt);
int tcp_newreno(socket_t fd);
void advance_cwnd(void);
void duplicate(socket_t fd, int sackno);
//--------------------------------------------------------//

void ctrlc(){
	et = secs()-et;
	maxpkts=snd_max;
	done();
	exit(1);
}

int main (int argc, char** argv){
	if (argc < 2) usage();
	host = argv[1];
	if (argc > 2) configfile = argv[2];
	signal(SIGINT,ctrlc);
	rdconfig();
	if (debug > 3)db = fopen("db.tmp","w");
	doit(host);
	done();
  return 0;
}

void done(void){
	char myname[128];

	mss-=12;
        gethostname(myname,sizeof(myname));
        printf("%s => %s %f Mbs win %d rxmts %d\n",
         myname,host,8.e-6*maxpkts*mss/et,rcvrwin,rxmts);
        printf("%f secs %d good bytes goodput %f KBs %f Mbs \n",
          et,maxpkts*mss,1.e-3*maxpkts*mss/et,8.e-6*maxpkts*mss/et);
        printf("pkts in %d  out %d  enobufs %d\n",
         ipkts,opkts,enobufs);
        printf("total bytes out %d loss %6.3f %% %f Mbs \n",
         opkts*mss,100.*rxmts/opkts,8.e-6*opkts*mss/et);
        printf("rxmts %d dup3s %d packs %d timeouts %d  dups %d badacks %d maxack %d maxburst %d\n",
          rxmts,dup3s,packs,timeouts,dups,badacks,maxack,maxburst);
        if (ipkts) avrgrtt /= ipkts;
        printf("minrtt  %f maxrtt %f avrgrtt %f\n",
               minrtt,maxrtt,avrgrtt/*,8.e6*rcvrwin/avrgrtt*/);
        printf("rto %f  srtt %f  rttvar %f\n",rto,srtt,rttvar);
        printf("win/rtt = %f Mbs  bwdelay = %d KB  %d segs\n",
           8.e-6*rcvrwin*mss/avrgrtt, (int)(1.e-3*avrgrtt*opkts*mss/et),
           (int)(avrgrtt*opkts/et));
	printf("bwertt %f bwertt_max %f max_delta %f\n",bwertt,bwertt_max,max_delta);
        if (vegas) printf("vsscnt %d vdecr %d v0 %d vrtt %f vdelta %f\n",
          vsscnt,vdecr, v0,vrtt,vdelta);
        printf("snd_nxt %d snd_cwnd %d  snd_una %d ssthresh %d snd_max %d\n",
         snd_nxt,(int)snd_cwnd,snd_una,snd_ssthresh,snd_max);

printf("goodacks %d cumacks %d ooacks %d\n", goodacks, cumacks, ooacks);
}

void usage(void){
	printf("atoucli host [configfile] \n");
	exit(1);
}

int doit(char* host){
	struct	hostent *him;		/* host table entry */
	struct sockaddr from;
	int lost,duplicates,outofseq;
	int fromlen = sizeof(from);
	unsigned int   inaddr;
  socket_t	fd;			/* network file descriptor */
	int	i,r;
	double t, secs();

	outofseq =duplicates=lost=0;
	if (thresh_init) snd_ssthresh = thresh_init*rcvrwin;
	 else snd_ssthresh = 2147483647;  /* normal TCP, infinite */
	snd_cwnd = initsegs;

	if ( (inaddr = inet_addr(host)) != -1) {
		san.sin_family = AF_INET;
		memcpy((char *)&san.sin_addr,(char *) &inaddr,sizeof(inaddr));
	}else {
		if ((him = gethostbyname(host)) == NULL) {
		fprintf(stderr, "atoucli: Unknown host %s\n", host);
		return(-1);
		}
        	san.sin_family = him->h_addrtype;      
        	memcpy(&san.sin_addr,him->h_addr, him->h_length);
	}
	san.sin_port = htons(port);

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		err_sys("socket");
	}
	i=sizeof(sndbuf);
	setsockopt(fd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,i);
	getsockopt(fd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,(socklen_t*)&i);
        setsockopt(fd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,i);
        getsockopt(fd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,(socklen_t*)&i);
        printf("config: sndbuf %d rcvbuf %d\n",sndbuf,rcvbuf);


#ifdef CONNECT
	connect(fd,&san,sizeof(san));  /* catch port unreachable */
#endif

	/* init control variables */
	memset(buff,0,BUFFSIZE);        /* pretouch */
	ack=msg = (struct Pr_Msg *)buff;
	/* send out initial segments, then go for it */
	et=secs();
	snd_fack=snd_una=snd_nxt=1;
	if (bwe_on) bwe_pkt = snd_nxt;
	if (maxpkts == 0 && maxtime) maxpkts = 1000000000;
	due = secs() + timeout;  /* when una is due */
	send_segs(fd);
	while(snd_una < maxpkts){
		r = timedread(fd,tick);
		if (r > 0) {  /* ack ready */
#ifdef CONNECT
			r=read(fd,buff,mss);
#else
			r= recvfrom(fd,buff,mss,0,&from,(socklen_t*)&fromlen);
#endif
			if (r <= 0) err_sys("read");
			rcvt = secs();
			ipkts++;
        		vntohl(buff,sizeof(struct Pr_Msg)/4);/* to host order */
		    if(sack)
			handle_sack(fd);
		    else
			handle_ack(fd);
		} else if (r < 0) {  
			err_sys("select");
		}
		t=secs();
		if (maxtime && (t-et) > maxtime) maxpkts = snd_max; /*time up*/
		/* see if a packet has timedout */
		if (t > due) {   /* timeout */
			if (idle > maxidle) {
				/* give up */
				printf("*** idle abort ***\n");
				maxpkts=snd_max;
				break;
			}
			if (debug > 1)fprintf(stderr,
	"timerxmit %6.2f pkt %d snd_nxt %d snd_max %d  snd_cwnd %d  thresh %d\n",
  	 t-et,snd_una,snd_nxt, snd_max, (int)snd_cwnd,snd_ssthresh);
			timeouts++;
			rxmts++;
			bwe_pkt=0;
			idle++;
			dupacks=0; dup_acks=0;
/* See: http://www.icir.org/floyd/papers/draft-floyd-tcp-highspeed-00c.txt */
			if (floyd) floyd_aimd(1);  /* adjust increment*/
			snd_ssthresh = snd_cwnd*multiplier; /* shrink */
			if (snd_ssthresh < initsegs) 
			  snd_ssthresh = initsegs;
			snd_cwnd = initsegs;  /* drop window */
			if(sack) {              /*SACK is implemented*/
			  if(ack->blkcnt > 0) { /*pkts have been lost*/
			    if(!FastRecovery) { /*NOT in FastRecovery yet*/
			      UpdateScoreBoard(ack->msgno-1);
			      FastRecovery = 1;
			      snd_recover = snd_max-1;
			    }
			    if(FastRecovery) {  /*ARE in FastRecovery*/
			      send_one(fd, snd_una);
			      retran_data++;
		  	      MarkRetran(snd_una, snd_nxt-1);
	  	  	      Pipe=1;
			      if(rampdown) wintrim = 0.0;
			    }
			  }
                          else { /*timeout but no losses indicated!?!*/
			    snd_nxt = snd_una;
                            send_segs(fd);
			  }
			}
			else {   /*newreno is implemented*/
			snd_nxt = snd_una;
			send_segs(fd);  /* resend */
	        	}	
			due = t + 2*timeout;  /* fancy exp. backoff? */
		}
	}  /* while more pkts */
	et = secs()-et;
  return 0;
}

void send_segs(socket_t fd){
	int win=0, trimwin=0, retran=0;

	if (snd_cwnd > rcvrwin) { 
	  snd_cwnd = rcvrwin; /* contain growth */
        }
/*Compute #pkts that can be sent in the present window*/
	if(rampdown && FastRecovery) {
	  trimwin = (int)((snd_cwnd < rcvrwin ? (double) snd_cwnd : (double) rcvrwin) + wintrim);
	  if(fack) {
	    awnd = snd_nxt - snd_fack + retran_data;
	    win = trimwin - awnd;
if(debug > 5)
fprintf(db, "  win: %d = %d - %d when %d = %d - %d + %d\n", 
	win, trimwin, awnd, awnd, snd_nxt, snd_fack, retran_data);
	  }
	  else {
	    win = trimwin - Pipe;
if(debug > 5)
fprintf(db, "  win: %d = %d - %d\n", win, trimwin, Pipe);
	  }
	}
	else if(FastRecovery) {
	  if(fack) {
	    awnd = snd_nxt - snd_fack + retran_data;
	    win = (int)snd_cwnd - awnd;
if(debug > 5)
fprintf(db, "  win: %d = %d - %d when %d = %d - %d + %d\n", 
	 win, (int)snd_cwnd, awnd, awnd, snd_nxt, snd_fack, retran_data);
	  }
          else 
	    if(sack) {
	      win = (int)snd_cwnd - Pipe;
if(debug > 5)
fprintf(db, "  win: %d = %d - %d\n", win, (int)snd_cwnd, Pipe);
	    }
        }
	else {
	  win = snd_cwnd - (snd_nxt - snd_una);
if(debug > 5 && !newreno)
fprintf(db, "  win: %d = %d - (%d - %d)\n", win, (int)snd_cwnd, snd_nxt, snd_una);
	}

	if (win <= 0 || snd_nxt >= maxpkts) return;  /* no avail window |done */
	if (win > maxburst) maxburst=win;
/*Can't send more than a specified burst_limit at a time*/
	if (burst_limit && win > burst_limit) 
	  win = burst_limit;
	if(FastRecovery) 
          retran = GetNextRetran();
	while (win-- && ((snd_nxt < maxpkts) || (retran>0))) {
	  if(FastRecovery) {
	       if(retran>0) {
		  send_one(fd, retran);
		  MarkRetran(retran, snd_nxt-1);
		  rxmts++; 
      packs++;
		  retran_data++;
		 
      if ( debug > 1)fprintf(stderr,
      "packrxmit pkt %d nxt %d max %d cwnd %d  thresh %d recover %d una %d\n",
       retran,snd_nxt, snd_max, (int)snd_cwnd, snd_ssthresh,snd_recover,snd_una);
	        }
		else {
		  send_one(fd, snd_nxt);
		  snd_nxt++;
		}
		Pipe++;
		retran = GetNextRetran();
	  }
  	  else {
		send_one(fd, snd_nxt);
		snd_nxt++;
	  }
	}
}

void send_one(socket_t fd, unsigned int n){
	/* send msg number n */
	int i,r;

	if (snd_nxt >= snd_max) snd_max = snd_nxt+1;
	msg->msgno = n;
	msg->tstamp = secs();
	if (debug > 3)fprintf(db,"%f %d xmt\n",
	  msg->tstamp-et,n);
/*fmf-check to see if this pkt should be dropped*/
	/* could add a drop_rate too with rand() */
	for (i=0; droplist[i]; i++) if (droplist[i] == n) {
		droplist[i]=-1;  /* do it once */
		return; 
	}
	vhtonl(buff,sizeof(struct Pr_Msg)/4);  /* to net order, 12 bytes? */
again:
#ifdef CONNECT
	r = write(fd, buff, mss);
#else
	r = sendto(fd,buff,mss,0,(struct sockaddr *)&san,sizeof(san));
#endif
	if (r != mss) {
		enobufs++;
		if (errno == ENOBUFS) goto again;
		err_sys("write");
	}
	if (debug > 8)printf("send %d snd_nxt %d snd_max %d\n", n,snd_nxt,snd_max);
	opkts++;
}


void handle_ack(socket_t fd){
	double rtt;
	int ackd;	/* ack advance */

	ackno = ack->msgno;
        
	if (debug > 8 )printf("ack rcvd %d\n",ackno);
/*fmf-rtt & rto calculations*/
	rtt = rcvt - ack->tstamp;
	if (rtt < minrtt) minrtt = rtt;
	 else if (rtt > maxrtt) maxrtt = rtt;
	avrgrtt += rtt;
        if (rtt < vrttmin) vrttmin=rtt;  /* min for rtt interval */
        if (rtt > vrttmax) vrttmax=rtt;  /* max for rtt interval */
        vrttsum += rtt;
        vcnt++;
	/* RTO calculations */
	srtt = (1-g)*srtt + g*rtt;
	delta = rtt - srtt;
	if (delta < 0 ) delta = -delta;
	rttvar = (1-h)*rttvar + h * (delta - rttvar);
	rto = srtt + 4*rttvar;  /* may want to force it > 1 */

	if (debug > 3) {
          fprintf(db,"%f %d %f  %d %d ack\n",
            rcvt-et,ackno,rtt,(int)snd_cwnd,snd_ssthresh);
          if (ack->blkcnt && debug > 6)
            fprintf(db, "   SACK(blkcnt:%d) %d-%d %d-%d %d-%d\n",
              ack->blkcnt, ack->sblks[0].sblk,ack->sblks[0].eblk,
              ack->sblks[1].sblk,ack->sblks[1].eblk,
              ack->sblks[2].sblk,ack->sblks[2].eblk);
        }
	/* rtt bw estimation, vegas like */
        if (bwe_on && bwe_pkt && ackno > bwe_pkt) bwe_calc(rtt);

	if (ackno > snd_max || ackno < snd_una ) {
		/* bad ack */
		if (debug > 5) fprintf(stderr,
		"badack %d snd_max %d snd_nxt %d snd_una %d\n",
		  ackno, snd_max, snd_nxt, snd_una);
		badacks++;
		if ( ackno < snd_una ) dupacks=0; /* out of order */
	} else  {
	    goodacks++;
	    if (ackno == snd_una ){
		/* dup acks */
		dups++;
		if (++dupacks == dup_thresh) { 
			/* rexmit threshold */
			if (debug > 1)fprintf(stderr,
	"3duprxmit %6.2f pkt %d nxt %d max %d cwnd %d thresh %d recover %d %d\n",
			rcvt-et,
			snd_una,snd_nxt,snd_max,(int)snd_cwnd,
			snd_ssthresh,snd_recover,(int)vdelta);
			dup3s++;
			rxmts++;
			bwe_pkt=0;
			if (newreno && ackno < snd_recover ){
			   /* false retransmit, dont shrink */
			   dupacks=0;
			   snd_cwnd++;  /* why not advance by dupacks? */
			   /* ? freebsd doesn't rexmit una ? */
			} else {
			   if (floyd) floyd_aimd(1);  /* adjust increment*/
			   snd_cwnd = snd_cwnd*multiplier;  /* shrink */
			   if (snd_cwnd < initsegs) snd_cwnd = initsegs;
			   snd_recover = snd_max;   /* newreno */
			   snd_ssthresh = snd_cwnd;
			   snd_cwnd += dupacks;  /* inflate */
			}
			due = rcvt + timeout;   /*restart timer */
			send_one(fd,snd_una);   /* retransmit */
			return;
		} else if (dupacks > dup_thresh) {
		/* if dupacks < 3 worry about linear incr. ? */
		        snd_cwnd++;  /* dup, but stuff still leaving net*/
		        send_segs(fd);   /* right edge recovery */
		} else {
                        /* dupacks < dup_thresh */
                        if(snd_nxt < maxpkts) {
                                send_one(fd, snd_nxt);  /* rfc 3042 */
                                snd_nxt++;
                        }
                }
          
	} else {
		/* advancing ack */
		if (newreno == 0){
		  if (dupacks > dup_thresh && snd_cwnd > snd_ssthresh)
		   snd_cwnd = snd_ssthresh; /* deflate */
		   dupacks=0;  /* clear */
		} else if (dupacks > dup_thresh && !tcp_newreno(fd) ){
			/* in newreno but not a partial ack,
			 *inflation left us with ssthresh outstanding
			 * rather than send a burst, use slow start
			 */
			int inflight = snd_max - ackno + 4; /* init win 4 */
			if (burst_limit && inflight < snd_ssthresh &&
			 (snd_cwnd-snd_ssthresh)>burst_limit)
			   snd_cwnd = inflight;
			else snd_cwnd = snd_ssthresh; /* deflate */
			dupacks=0;  /* clear */
		}
		if (dupacks < dup_thresh) dupacks=0;  /* clear */
		ackd = ackno-snd_una;
		if (ackd > maxack) maxack = ackd;
		snd_una = ackno;
		if (snd_nxt < ackno) snd_nxt = ackno;
                if (bwe_on && ackno > snd_recover && bwe_pkt == 0){
                  bwe_prev = bwe_pkt = snd_nxt; /* out of recovery */
                  vrttmin = 999999;  /* restart vegas collecting */
                  vrttsum=vcnt = vrttmax = 0;
                  initial_ss = 0;
                  if (debug > 3 ) fprintf(stderr, "CAexit %6.2f ack %d\n",
                    rcvt-et, ackno);
                }
		idle=0;
		due = rcvt + timeout;   /*restart timer */
		advance_cwnd();
                send_segs(fd);  /* send some if we can */
	}
     }
}

/*
 * Checks for partial ack.  If partial ack arrives, force the retransmission
 * of the next unacknowledged segment, do not clear dupacks, and return
 * 1.  By setting snd_nxt to ackno, this forces retransmission timer to
 * be started again.  If the ack advances at least to snd_recover, return 0.
 */

int tcp_newreno(socket_t fd){
	if (ackno < snd_recover){
		int ocwnd = snd_cwnd;
		int onxt = snd_nxt;

		if ( debug > 1)fprintf(stderr,
	"packrxmit pkt %d nxt %d max %d cwnd %d  thresh %d recover %d una %d\n",
		 ackno,snd_nxt, snd_max, (int)snd_cwnd,
		 snd_ssthresh,snd_recover,snd_una);
		rxmts++;
		packs++;
		due = rcvt + timeout;   /*restart timer */
		snd_cwnd = 1 + ackno - snd_una;
		snd_nxt = ackno;
		send_segs(fd); 
		snd_cwnd = ocwnd;
		if (onxt > snd_nxt) snd_nxt = onxt;
		/* partial deflation, una not updated yet */
		snd_cwnd -= (ackno - snd_una -1);
		if (snd_cwnd < initsegs) snd_cwnd = initsegs;
		return TRUE;  /* yes was a partial ack */
	}
	return FALSE;
}

double secs(void)
{
	struct timeval t;
	gettimeofday(&t, (struct timezone *)0);
	return(t.tv_sec+ t.tv_usec*1.e-6);
}

socket_t timedread(socket_t fd, double t){
	struct timeval tv;
	fd_set rset;

	tv.tv_sec = t;
	tv.tv_usec = (t - tv.tv_sec)*1000000;
	FD_ZERO(&rset);
	FD_SET(fd, &rset);
	return ( select(fd+1,&rset,NULL,NULL, &tv) );
}

void err_sys(char* s){
  perror(s);
  done();
  exit(1);
}

void rdconfig(void){
	/* read config if there, keyword value */
	FILE *fp;
	char line[128], var[32];
	double val;
        time_t t;

	fp = fopen(configfile,"r");
	if (fp == NULL) {
		printf("atoucli unable to open %s\n",configfile);
		return;
	}
	while (fgets(line, sizeof (line), fp) != NULL) {
		sscanf(line,"%s %lf",var,&val);
		if (*var == '#') continue;  /* comment */
		else if (strcmp(var,"rcvrwin")==0) rcvrwin = val;
		else if (strcmp(var,"increment")==0) increment = val;
		else if (strcmp(var,"multiplier")==0) multiplier = val;
		else if (strcmp(var,"tick")==0) tick = val;
		else if (strcmp(var,"newreno")==0) newreno = val;
		else if (strcmp(var,"sack")==0) sack = val;
                else if (strcmp(var,"delack")==0) delack = val;
		else if (strcmp(var,"timeout")==0) timeout = val;
		else if (strcmp(var,"initsegs")==0) initsegs = val;
		else if (strcmp(var,"ssincr")==0) ssincr = val;
		else if (strcmp(var,"thresh_init")==0) thresh_init = val;
		else if (strcmp(var,"max_ssthresh")==0) max_ssthresh = val;
		else if (strcmp(var,"maxpkts")==0) maxpkts = val;
		else if (strcmp(var,"maxidle")==0) maxidle = val;
		else if (strcmp(var,"maxtime")==0) maxtime = val;
		else if (strcmp(var,"mss")==0) mss = val;
		else if (strcmp(var,"port")==0) port = val;
                else if (strcmp(var,"vegas")==0) vegas = val;
                else if (strcmp(var,"vss")==0) vss = val;
                else if (strcmp(var,"valpha")==0) valpha = val;
                else if (strcmp(var,"vbeta")==0) vbeta = val;
                else if (strcmp(var,"vgamma")==0) vgamma = val;
		else if (strcmp(var,"dup_thresh")==0) dup_thresh = val;
		else if (strcmp(var,"sndbuf")==0) sndbuf = val;
		else if (strcmp(var,"rcvbuf")==0) rcvbuf = val;
		else if (strcmp(var,"burst_limit")==0) burst_limit = val;
		else if (strcmp(var,"droplist")==0){
		   /* set up droplist */
		   sscanf(line,"%s %d %d %d %d %d %d %d %d %d %d",
		    var,droplist,droplist+1,droplist+2,droplist+3,
		    droplist+4, droplist+5, droplist+6,droplist+7,
		    droplist+8, droplist+9);

		}
		else if (strcmp(var,"debug")==0) debug = val;
		else if (strcmp(var,"rampdown")==0) rampdown = val;
		else if (strcmp(var,"fack")==0) fack = val;
		else if (strcmp(var,"floyd")==0) floyd = val;
		else printf("config unknown: %s\n",line);
	}
        t=time(NULL);
        printf("config: atoucli %s port %d debug %d %s", version,port,debug,
          ctime(&t));
        printf("config: initsegs %d mss %d tick %f timeout %f\n",
         initsegs,mss,tick,timeout);
	printf("config: maxidle %d maxtime %d\n",maxidle, maxtime);
        printf("config: floyd %d rcvrwin %d  increment %d  multiplier %f kai %f\n",
          floyd,rcvrwin,increment,multiplier,kai);
        printf("config: thresh_init %f ssincr %d max_ssthresh %d\n",
          thresh_init, ssincr, max_ssthresh);
        printf("config: rcvrwin %d  increment %d  multiplier %f thresh_init %f\n",
          rcvrwin,increment,multiplier,thresh_init);
        printf("config: newreno %d sack %d rampdown %d fack %d delack %d maxpkts %d burst_limit %d dup_thresh %d\n",
          newreno,sack,rampdown,fack,delack,maxpkts,burst_limit,dup_thresh);
        if (vegas) {
                bwe_on = 1;
                printf("config: vegas %d vss %d valpha %f vbeta %f vgamma %f\n",
                 vegas,vss,valpha,vbeta,vgamma);
        }
	if (droplist[0]){
	   int i;
	   printf("config:droplist ");
	   for(i=0;droplist[i];i++)printf("%d ",droplist[i]);
	   printf("\n");
	}
/* fack is an alteration of sack and uses rampdown */
	if(fack) {
	  sack = 1;
	}
/* rampdown goes with sack and/or fack so one or both must be enabled */
	if(!(sack || fack))
	  rampdown = 0;
}

/*----added----*/
/*handle_sack() implements the use of selective acks by using the information
  provided by the receiver when packets are lost leaving holes in the data.
*/
  
void handle_sack(socket_t fd){
  int sackno, decr, ackd;
  double rtt;

  ackno = sackno = ack->msgno-1;
        
// rtt & rto calculations
	rtt = rcvt - ack->tstamp;
	if (rtt < minrtt) minrtt = rtt;
	 else if (rtt > maxrtt) maxrtt = rtt;
	avrgrtt += rtt;
        if (rtt < vrttmin) vrttmin=rtt;  /* min for rtt interval */
        if (rtt > vrttmax) vrttmax=rtt;  /* max for rtt interval */
        vrttsum += rtt;
        vcnt++;
	/* RTO calculations */
	srtt = (1-g)*srtt + g*rtt;
	delta = rtt - srtt;
	if (delta < 0 ) delta = -delta;
	rttvar = (1-h)*rttvar + h * (delta - rttvar);
	rto = srtt + 4*rttvar;  /* may want to force it > 1 */

 	if (debug > 3) {
	  fprintf(db,"%f %d %f  %d %d ack ",
	    rcvt-et,sackno,rtt,(int)snd_cwnd,snd_ssthresh);
          if (ack->blkcnt) 
            fprintf(db, "blkcnt:%d  %d-%d %d-%d %d-%d\n",
              ack->blkcnt, ack->sblks[0].sblk,ack->sblks[0].eblk,
              ack->sblks[1].sblk,ack->sblks[1].eblk,
              ack->sblks[2].sblk,ack->sblks[2].eblk);
          else
	    fprintf(db, "\n");
	}
       if (bwe_on && bwe_pkt && ackno > bwe_pkt) bwe_calc(rtt);

/*if ackno is outside the window, it is a bad ack*/
  if(sackno > snd_max || sackno < HighAck) {
    badacks++;
    if (debug > 5) fprintf(stderr,
      "badack %d snd_max %d snd_nxt %d snd_una %d\n",
      sackno, snd_max, snd_nxt, snd_una);
  }
  else {
    goodacks++;
    if(sackno<snd_una) {  /*DUPLICATE!!*/
      if(ack->blkcnt <= 0) {
	    /* to cover:
		1. acks duped by net 
		2. dup acks due to retransmittal of segs just re-ordered by 
		     the net--not lost 
		3. acks maliciously duplicated
	     */
        if (debug > 4)fprintf(stderr, "Got a dup ack %d--no sack info??\n",
		sackno);
	ooacks++;
      }
      else { 
	dups++; dup_acks++;
/*ACKs reporting new data at the receiver either decrease retran_data or
  advance snd_fack (per fack paper)
*/
        if(ack->sblks[0].eblk > (snd_fack-1))	
          snd_fack = ack->sblks[0].eblk+1;
	else
	  retran_data--;
      }
      if(!FastRecovery) { /* Does this trigger FastRecovery? */
/*Note for the future:
 * look at the first 'if' condition...sometimes it causes a premature
 * entry into Fast Recovery when there is a lot of re-ordering on the
 * network...could: make it configurable or break out of fast recovery
 * early if re-ordering appears likely or ????
*/

	if((fack && ((snd_fack - snd_una) > dup_thresh)) || dup_acks==dup_thresh){
/*UpdateScoreBoard*/
          if(ack->blkcnt > 0) {
            decr = UpdateScoreBoard(sackno);
	    if(debug > 5)
	      fprintf(db, "Updates: %d\n", SACKed);
          }
	  duplicate(fd, sackno);
	}  
	else {           /* No--just got dup 1 or dup 2 */
	  if(snd_nxt < maxpkts) {
	    send_one(fd, snd_nxt);
	    snd_nxt++;
  	    if(snd_cwnd < snd_ssthresh) {
    	      snd_cwnd += ssincr;
 	    }
	  }
	}
      }
      else { /* already in FastRecovery */
/*UpdateScoreBoard*/
        if(ack->blkcnt > 0) {
          decr = UpdateScoreBoard(sackno);
	  if(debug > 5)
	    fprintf(db, "Updates: %d\n", SACKed);
        }
	Pipe-=SACKed;
	if(SACKed && rampdown) {
	  if(debug > 5)
	    fprintf(db, "wintrim: %f = %f -  (%d * %f)\n", 
	      wintrim - SACKed * winmult, wintrim, SACKed, winmult);
	   wintrim = wintrim - SACKed * winmult;
	   if(wintrim < 0) 
	     wintrim = 0;
	}
        SACKed=0;
	send_segs(fd);
  	if(snd_cwnd < snd_ssthresh) {
    	  snd_cwnd += ssincr;
  	}
      }
    } else  {  	/* sackno >= snd_una */
      ackd = sackno - snd_una;
      dup_acks = 0;
      if(ackd > maxack) maxack = ackd;
      snd_una = sackno+1;
      if(snd_una > snd_fack)
        snd_fack = snd_una;
      HighAck = sackno;
      if(FastRecovery) {
      /*UpdateScoreBoard*/
        if(ack->blkcnt > 0) {
          decr = UpdateScoreBoard(sackno);
	  if(debug > 5)
	    fprintf(db, "Updates: %d\n", SACKed);
        }

        cumacks++;
        if(sackno >= snd_recover) { /* end FastRecovery */
	  if(debug > 5)
            fprintf(db, "Exit FastRecovery: SACKNO %d >= snd_recover %d\n", 
  	    sackno, snd_recover);
          FastRecovery=0;
       	  ClearScoreBoard();
	  retran_data = 0;
        }
        else { /* Partial Ack */
	  if(debug > 5)
 fprintf(db, "Partial Ack: SACKNO %d < snd_recover %d\n", sackno, snd_recover);
          Pipe-=2; /*per Simulation-based Comparisons paper*/
		   /*Allman says should be by HighAck - OldHighAck!*/
	  retran_data--;
        }
	SACKed=0;
      }
/*EXPERIMENT:  don't mess with snd_cwnd during FastRecovery*/
      if(!FastRecovery) {   /* advancing ack */
	advance_cwnd();
      }
      if (bwe_on && !FastRecovery && bwe_pkt == 0){
                  bwe_prev = bwe_pkt = snd_nxt; /* out of recovery */
                  vrttmin = 999999;  /* restart vegas collecting */
                  vrttsum=vcnt = vrttmax = 0;
                  initial_ss = 0;
                  if (debug > 3 ) fprintf(stderr, "CAexit %6.2f ack %d\n",
                    rcvt-et, ackno);
      }
      idle=0;
      due = rcvt+timeout; /*restart timer */
      if(debug > 8)
        fprintf(db, "handle_sack--Partial Ack reset timer: %f\n", due);
      send_segs(fd);
    }
  }
}

void duplicate(socket_t fd, int sackno) {
  int i, end; 

/*Go into FastRecovery until sackno >= snd_recover*/
  FastRecovery = 1;
  snd_recover = snd_max-1;

if (debug > 1)fprintf(stderr,
  "3duprxmit %6.2f pkt %d nxt %d max %d cwnd %d thresh %d recover %d %d\n",
	 rcvt-et,
         snd_una,snd_nxt,snd_max,(int)snd_cwnd,
	 snd_ssthresh,snd_recover,(int)vdelta);

if(debug > 5) {
  fprintf(db, "SCORE: snd_una %d snd_fack %d sblk %d eblk %d dup_acks %d\n", 
        snd_una, snd_fack, ack->sblks[0].sblk, ack->sblks[0].eblk, dup_acks);
  end=ack->sblks[0].eblk;
  for(i=snd_una; i<=end; i++)
    fprintf(db, "(%d: %d %d %d\n", SBNI.seq_no_, SBNI.ack_flag_, SBNI.sack_flag_, SBNI.retran_);
}
  if (floyd) floyd_aimd(1);   /* adjust increment */
  snd_cwnd *= multiplier;
  if(snd_cwnd < initsegs)
    snd_cwnd = initsegs;
  snd_ssthresh = snd_cwnd;
  rxmts++; dup3s++;
  bwe_pkt=0;
/*if rampdown--do wintrim */
  if(rampdown) {
    wintrim = (snd_nxt - snd_fack) * winmult;
if(debug > 5)
fprintf(db, "wintrim: %f = (%d - %d) * .5\n", wintrim, snd_nxt, snd_fack);
  }


/*Used with sack:
    if > 1/2 a window of data has been lost--avoid a timeout w/slow start
	ns does this per Foward Acknowledgment paper 
*/
  if(ack->sblks[0].sblk - snd_una > snd_cwnd && !fack) {

if(debug > 5)
  fprintf(db, "TRYING TO AVOID TIMEOUT AFTER A BIG LOSS WITH SLOW START!--%d - %d > %d\n", ack->sblks[0].sblk, snd_una, (int)snd_cwnd);
    if(floyd) floyd_aimd(1);   /* adjust increment */
    snd_ssthresh = snd_cwnd*multiplier;
    if (snd_ssthresh < initsegs)
      snd_ssthresh = initsegs;
    snd_cwnd = initsegs;
    Pipe = initsegs;
    if(rampdown) wintrim = 0.0;
  }
  else  
    Pipe = snd_nxt-1  - HighAck - SACKed;
  SACKed=0; 
  due = rcvt+timeout; /*restart timer */

if(debug > 8)
  fprintf(db, "3duprxmit for pkt %d--reset timer: %f\n", sackno, due);

  send_one(fd, snd_una);
  retran_data++; Pipe++;
  MarkRetran(snd_una, snd_nxt-1);
  send_segs(fd);
  if(snd_cwnd < snd_ssthresh) {
fprintf(db, "slow start and 3 dups: %f < %u\n", snd_cwnd, snd_ssthresh);
    snd_cwnd += ssincr;  
  }
}

int UpdateScoreBoard(int last_ack) {
  int i, sack_left, sack_right, sack_index;
  int retran_decr = 0;

/*if there is no scoreboard, create one */
  if(length_ == 0) {
    i = last_ack+1;
    SBNI.seq_no_ = i;
    SBNI.ack_flag_ = 0;
    SBNI.sack_flag_ = 0;
    SBNI.retran_ = 0;
    SBNI.snd_nxt_ = 0;
    first_ = i%SBSIZE;
    length_++;
    if(length_ >= SBSIZE) {
      printf("ERROR-1: scoreboard overflow(increase SBSIZE = %d)\n",SBSIZE);
      ctrlc();
    }
  }
/*for each sack block indicated, fill in info on every affected pkt*/
  for(sack_index=0; sack_index < ack->blkcnt; sack_index++) {
    sack_left = ack->sblks[sack_index].sblk;
    sack_right = ack->sblks[sack_index].eblk;  

   if(sack_right > SBN[(first_+length_+SBSIZE-1)%SBSIZE].seq_no_) { 
    for(i=SBN[(first_+length_+SBSIZE-1)%SBSIZE].seq_no_+1; i<=sack_right; i++) { 
      SBNI.seq_no_ = i; 
      SBNI.ack_flag_ = 0;
      SBNI.sack_flag_ = 0;
      SBNI.retran_ = 0;
      SBNI.snd_nxt_ = 0;
      length_++;
      if(length_ >= SBSIZE) {
        printf("ERROR-2: scoreboard overflow(increase SBSIZE = %d)\n",SBSIZE);
        ctrlc();
      }
    }
   }

/*for each partial ack received, advance the left edge of the block*/
   if(SBN[first_].seq_no_ <= last_ack) {
    for(i=SBN[(first_)%SBSIZE].seq_no_; i<=last_ack; i++) {
      if(SBNI.seq_no_ <= last_ack) {
        first_ = (first_+1)%SBSIZE;
	length_--;
	SBNI.ack_flag_ = 1;
/*added--keep count of packets acked*/
	ACKed++;
	if(SBNI.retran_) {
     	  SBNI.retran_=0;
	  SBNI.snd_nxt_=0;
	  retran_decr++;
	}
	if(length_==0)
	  break;
      }
    }
   }
/*mark all sacked segments per the latest block information*/
   for(i=SBN[(first_)%SBSIZE].seq_no_; i<=sack_right; i++) {
    if(SBNI.seq_no_ >= sack_left && SBNI.seq_no_ <=sack_right) {
      if( ! SBNI.sack_flag_) {
	SBNI.sack_flag_ = 1;
/*added--keep count of packets sacked*/
	SACKed++;
      }
      if(SBNI.retran_) {
	SBNI.retran_=0;
	retran_decr++;
      }
    }
  }
 }
 return(retran_decr);
}
    
int CheckSndNxt() {
  int i, sack_index, sack_left, sack_right;
  int force_timeout=0;

  for(sack_index=0; sack_index < ack->blkcnt; sack_index++) {
    sack_left = ack->sblks[sack_index].sblk;
    sack_right = ack->sblks[sack_index].eblk; 

    for(i=SBN[(first_)%SBSIZE].seq_no_; i<= sack_right; i++) {
      if(SBNI.retran_ && SBNI.snd_nxt_ <= sack_right) {
/*the packet was lost again!!*/
	SBNI.snd_nxt_=0;
	force_timeout = 1;
      }
    }
  }
  return(force_timeout);
}

int GetNextRetran() {
  int i;
/*get the next packet to be retransmitted--if any*/
  if(length_) {
    for(i=SBN[(first_)%SBSIZE].seq_no_;
        i<SBN[(first_)%SBSIZE].seq_no_+length_; i++) {
      if(!SBNI.ack_flag_ && !SBNI.sack_flag_ && !SBNI.retran_) {
        return(i);
      }
    }
  }
  return(-1);
}

void MarkRetran (int retran_seqno, int snd_max) { 
/*mark the packet as retransmitted*/
  SBN[retran_seqno%SBSIZE].retran_ = 1;
  SBN[retran_seqno%SBSIZE].snd_nxt_ = snd_max;
}

int RetransOK (int retran_seqno) { 
/*see if the packet was retransmitted*/
  if(SBN[retran_seqno%SBSIZE].retran_ > 0)
/*if it was, has it been lost again? */
    if(SBN[retran_seqno%SBSIZE].snd_nxt_ < snd_fack)
      return(1);
    else
      return(0);
  else
      return(1);
}

void ClearScoreBoard() {
  length_ = 0;
}

/* floyd aimd calculator  may '02  */
static struct Aimd_Vals {
	unsigned int cwnd;
	unsigned int increment;
	unsigned int decrement;
} aimd_vals[] = {
  {     0},{     1},{  128}, /*  0.50 */
  {    38},{     1},{  128}, /*  0.50 */
  {   118},{     2},{  112}, /*  0.44 */
  {   221},{     3},{  104}, /*  0.41 */
  {   347},{     4},{   98}, /*  0.38 */
  {   495},{     5},{   93}, /*  0.37 */
  {   663},{     6},{   89}, /*  0.35 */
  {   851},{     7},{   86}, /*  0.34 */
  {  1058},{     8},{   83}, /*  0.33 */
  {  1284},{     9},{   81}, /*  0.32 */
  {  1529},{    10},{   78}, /*  0.31 */
  {  1793},{    11},{   76}, /*  0.30 */
  {  2076},{    12},{   74}, /*  0.29 */
  {  2378},{    13},{   72}, /*  0.28 */
  {  2699},{    14},{   71}, /*  0.28 */
  {  3039},{    15},{   69}, /*  0.27 */
  {  3399},{    16},{   68}, /*  0.27 */
  {  3778},{    17},{   66}, /*  0.26 */
  {  4177},{    18},{   65}, /*  0.26 */
  {  4596},{    19},{   64}, /*  0.25 */
  {  5036},{    20},{   62}, /*  0.25 */
  {  5497},{    21},{   61}, /*  0.24 */
  {  5979},{    22},{   60}, /*  0.24 */
  {  6483},{    23},{   59}, /*  0.23 */
  {  7009},{    24},{   58}, /*  0.23 */
  {  7558},{    25},{   57}, /*  0.22 */
  {  8130},{    26},{   56}, /*  0.22 */
  {  8726},{    27},{   55}, /*  0.22 */
  {  9346},{    28},{   54}, /*  0.21 */
  {  9991},{    29},{   53}, /*  0.21 */
  { 10661},{    30},{   52}, /*  0.21 */
  { 11358},{    31},{   52}, /*  0.20 */
  { 12082},{    32},{   51}, /*  0.20 */
  { 12834},{    33},{   50}, /*  0.20 */
  { 13614},{    34},{   49}, /*  0.19 */
  { 14424},{    35},{   48}, /*  0.19 */
  { 15265},{    36},{   48}, /*  0.19 */
  { 16137},{    37},{   47}, /*  0.19 */
  { 17042},{    38},{   46}, /*  0.18 */
  { 17981},{    39},{   45}, /*  0.18 */
  { 18955},{    40},{   45}, /*  0.18 */
  { 19965},{    41},{   44}, /*  0.17 */
  { 21013},{    42},{   43}, /*  0.17 */
  { 22101},{    43},{   43}, /*  0.17 */
  { 23230},{    44},{   42}, /*  0.17 */
  { 24402},{    45},{   41}, /*  0.16 */
  { 25618},{    46},{   41}, /*  0.16 */
  { 26881},{    47},{   40}, /*  0.16 */
  { 28193},{    48},{   39}, /*  0.16 */
  { 29557},{    49},{   39}, /*  0.15 */
  { 30975},{    50},{   38}, /*  0.15 */
  { 32450},{    51},{   38}, /*  0.15 */
  { 33986},{    52},{   37}, /*  0.15 */
  { 35586},{    53},{   36}, /*  0.14 */
  { 37253},{    54},{   36}, /*  0.14 */
  { 38992},{    55},{   35}, /*  0.14 */
  { 40808},{    56},{   35}, /*  0.14 */
  { 42707},{    57},{   34}, /*  0.13 */
  { 44694},{    58},{   33}, /*  0.13 */
  { 46776},{    59},{   33}, /*  0.13 */
  { 48961},{    60},{   32}, /*  0.13 */
  { 51258},{    61},{   32}, /*  0.13 */
  { 53677},{    62},{   31}, /*  0.12 */
  { 56230},{    63},{   30}, /*  0.12 */
  { 58932},{    64},{   30}, /*  0.12 */
  { 61799},{    65},{   29}, /*  0.12 */
  { 64851},{    66},{   28}, /*  0.11 */
  { 68113},{    67},{   28}, /*  0.11 */
  { 71617},{    68},{   27}, /*  0.11 */
  { 75401},{    69},{   26}, /*  0.10 */
  { 79517},{    70},{   26}, /*  0.10 */
  { 84035},{    71},{   25}, /*  0.10 */
  { 89053},{    72},{   24}, /*  0.10 */
  { 94717},{    73},{   23}, /*  0.09 */
  {999999},{    73},{   23}  /*  0.09 */
};

void floyd_aimd(int cevent){

	static int current =1;  /* points at upper bound */

	if (snd_cwnd >  aimd_vals[current].cwnd ){
		/* find new upper bound */
		while (snd_cwnd >  aimd_vals[current].cwnd ) current++;
	} else  if (snd_cwnd < aimd_vals[current-1].cwnd ){
		/* find new lower bound */
		while (snd_cwnd < aimd_vals[current-1].cwnd ) current--;
	} else return;   /* no change */
	increment = aimd_vals[current].increment;
	multiplier = 1 - aimd_vals[current].decrement / 256.; 
	if (cevent) fprintf(stderr,"floyd cwnd %d current %d incr %d mult %f\n",
	   (int)snd_cwnd,current,(int)increment,multiplier);
}

void bwe_calc(double rtt){
	/* bw estimate each lossless RTT, vegas delta */
                /* once per rtt and not in recovery */
  if (vcnt) { /* only if we've been had some samples */
    if (vegas== 2) vrtt = vrttmin;
    else if (vegas== 3) vrtt = vrttsum/vcnt;
    else if (vegas== 4) vrtt = vrttmax;
    else vrtt = rtt;  /* last rtt */
    vdelta = minrtt * ((snd_nxt - snd_una)/minrtt - (snd_nxt - bwe_pkt)/vrtt);
    if (vdelta > max_delta) max_delta=vdelta;  /* vegas delta */
  } else vdelta=0;  /* no samples */
  bwertt = 8.e-6 * mss * (ackno-bwe_prev-1)/rtt;
  if (bwertt > bwertt_max) bwertt_max = bwertt;
  if (debug > 4 ) fprintf(stderr,"bwertt %f %f %f %d %f\n",rcvt-et,bwertt,rtt,ackno-bwe_prev-1,vdelta);
  bwe_prev = bwe_pkt;
  bwe_pkt = snd_nxt;
  vrttmin=999999;
  vrttsum=vcnt=vrttmax=0;
}

void advance_cwnd(void){
	/* advance cwnd according to slow-start of congestion avoidance */
  if (snd_cwnd <= snd_ssthresh) {
    /* slow start, expo growth */
    if (initial_ss && vegas &&  bwe_pkt && (vinss || vdelta > vgamma)){
      /*
       * here if initial ss and vegas is on and no CA
       * vegas would normally leave slow start
       *  but we revert to  floyd's slow start
                                 */
                                if (vinss ==0 ){
                                 vsscnt++;   /* count vegas ss adjusts*/
                                 if ( debug > 2) fprintf(stderr,
        "vss %6.2f pkt %d nxt %d max %d  cwnd %d  thresh %d recover %d %f %f\n",
                        rcvt-et, snd_una,snd_nxt, snd_max, (int)snd_cwnd,
                        snd_ssthresh,snd_recover,vdelta,vrtt);
                                }
                                vinss = 1;
                                /* use vss flag to choose
                                 * between ssthresh =2 cwnd = actual or floyd ss
                                 */
                                if (vss ==1 ) snd_cwnd += (0.5 * 100)/snd_cwnd;
                                else {
                                    /* standard vegas leave slow start */
                                    if (vss == 2)snd_cwnd = snd_cwnd-vdelta; /* actual ? */
				      else snd_cwnd = snd_cwnd- snd_cwnd/8;
                                    if (snd_cwnd < initsegs) snd_cwnd=initsegs;
                                    snd_ssthresh = 2;
                                    bwe_pkt=0;  /* prevent bwe this RTT */
                                    initial_ss = 0;  /* once only */
                                }
                         } else if (max_ssthresh <=0 || snd_cwnd <= max_ssthresh )
                          snd_cwnd += ssincr; /* standard */
                          /*otherwise reduce rate -- floyd */
                        else snd_cwnd += (0.5 * max_ssthresh) / snd_cwnd;
                 } else{
                        /* congestion avoidance phase */
                        int incr;

                        if (floyd) floyd_aimd(0);  /* adjust increment */
                        incr = increment;
                        if (vegas &&  bwe_pkt) {
                           /* vegas active and not in recovery */
                           if (vdelta > vbeta ){
                                incr= -increment; /* too fast, -incr /RTT */
                                vdecr++;
                           } else if (vdelta > valpha) {
                                incr =0; /* just right */
                                v0++;
                          }
                        }
			/* kelly precludes vegas */
			if (kai && kai > incr/snd_cwnd) 
			  snd_cwnd += kai;  /* kelly scalable TCP */
			 else snd_cwnd = snd_cwnd + incr/snd_cwnd; /* ca */
                        if (snd_cwnd < initsegs) snd_cwnd = initsegs;
                        vinss = 0; /* no vegas ss now */
                 }
}
