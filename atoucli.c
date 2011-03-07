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
#include <signal.h>
#include	<errno.h>

#include "util.h"
#include "atoucli.h"

#define PORT "7890"
#define HOST "127.0.0.1"

Ctcp_Pckt *msg;
struct sockaddr cli_addr;
struct addrinfo *result;
int sockfd, rcvspace;

void
usage(void) {
  fprintf(stderr, "Usage: atoucli [-options]\n\
	-s	enable SACK (default reno)\n\
	-d ##	amount to delay ACKs (ms) (default 0, often 200)\n\
	-p ##	port number to receive on (default 7890)\n \
	-b ##	set socket receive buffer size (default 8192)\n      \
	-D ##  enable debug level\n");
  exit(0);
} 

/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void
ctrlc(void){
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


int
main(int argc, char** argv){
	int optlen,rlth;
  char *port = PORT;
  char *host = HOST;
  int numbytes;
  struct addrinfo hints, *servinfo;
  int rv;
	int c;
  
	while((c = getopt(argc, argv, "h:sd:p:b:D:")) != -1) { 
	  switch (c) {
    case 'h':
      host = optarg;
    case 's':
      sack=1;
      break;
    case 'd':
      ackdelay = atoi(optarg);  /* ms */
      if (ackdelay < 0) ackdelay = 0;
      break;
    case 'p':
      port = optarg;
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
  
  // Open the file where the contents of the file transfer will be stored
  rcv_file = fopen("atou_cli_rcv",  "wb");
  
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; // This works for buth IPv4 and IPv6
  hints.ai_socktype = SOCK_DGRAM;
  
  if((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  // loop through all the results and bind to the first possible
  for(result = servinfo; result != NULL; result = result->ai_next) {
    if((sockfd = socket(result->ai_family, 
                        result->ai_socktype,
                        result->ai_protocol)) == -1){
      perror("atoucli: failed to initialize socket");
      continue;
    }
    break;
  }

  // If we got here, it means that we couldn't initialize the socket.
  if(result  == NULL){
    err_sys("atoucli: failed to bind to socket");
  }

  freeaddrinfo(servinfo);

	signal(SIGINT, (__sighandler_t) ctrlc);

  // Send request to the server.
  fprintf(stdout, "Sending request\n");
  int req = 1861;
  if((numbytes = sendto(sockfd, &req, sizeof(int), 0,
                        result->ai_addr, result->ai_addrlen)) == -1){
    err_sys("sendto: Request failed");
  }
  fprintf(stdout, "Request sent\n");

    
	if (rcvspace) {
    setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF, (char *) &rcvspace, optlen);
	}
	getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char *) &rlth, (socklen_t*)&optlen);

	printf("atoucli %s using port %s rcvspace %d sack %d ackdelay %d ms\n",
         version,port,rlth,sack,ackdelay);
  
	memset(buff,0,BUFFSIZE);        /* pretouch */

  do{
    clilen = sizeof cli_addr; // TODO: this is not necessary -> remove
    // TODO: should be reading only a packet or multiple packets at a time, need to know the packet size in advance...
    if((numbytes = recvfrom(sockfd, buff, MSS, 0, 
                            &cli_addr, &clilen)) == -1){
      err_sys("recvfrom");
    }
    
    if(numbytes <= 0) break;

	  pkts++;
	  et = secs();  /* last read */
	  if (st == 0) st = et;  /* first pkt time */

    // Unmarshall the packet 
    unmarshall(msg, buff);

	  if (msg->msgno > hi) hi = msg->msgno ;  /* high water mark */

    if (debug && msg->msgno != expect ) printf("exp %d got %d dups %d sacks %d hocnt %d\n",expect, msg->msgno,dups,sackcnt,hocnt); 
    
    bldack();

	  inlth = numbytes;

  }while(numbytes > 0);

  fclose(rcv_file);

  return 0;
}


void
err_sys(char *s){
	perror(s);
	ctrlc();     /* do stats on error */
}

void
bldack(void){
  Ctcp_Pckt ack;
  ack.tstamp = getTime();
  ack.payload_size = 0;
  
  // check whether the packet we got has the packet number we expected
  if(msg->msgno == expect){
    // If so then write the payload to the file
    fwrite(msg->payload, 1, msg->payload_size, rcv_file);

    // And increase expect
    expect++;
  }

  ack.msgno = expect;
  
  // Marshall the ack into buff
  int size = marshall(ack, buff);
  
  if(sendto(sockfd, buff, size, 0, result->ai_addr, result->ai_addrlen) == -1){
    err_sys("bldack: sendto");
  }
  acks++;
}

double
secs(){
  timeval_t  time;
  gettimeofday(&time, NULL);
	if(rtt_base==0) {
	  rtt_base = time.tv_sec;
#if DEBUG == 1
    fprintf(stderr, "DEBUG=> rtt_base: %u\n", rtt_base);
#endif
	}
  return(time.tv_sec+ time.tv_usec*1.e-6);
}

unsigned int
millisecs(){
  struct timeval tv;
	unsigned int ts;
  
  gettimeofday(&tv, (struct timezone *)0);
  /*fprintf(stderr, "time of day: %u:%u--", tv.tv_sec, tv.tv_usec);*/
	ts = ((tv.tv_sec-rtt_base) * 1000) + (tv.tv_usec / 1000);
  /*fprintf(stderr, "%ld=%u + %u\n", ts, (tv.tv_sec-rtt_base)*1000, tv.tv_usec/1000);*/
  return(ts);
}

int
acktimer(socket_t fd, int t) {
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
