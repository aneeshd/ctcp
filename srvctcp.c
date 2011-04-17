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

#include <assert.h>

#include "scoreboard.h" //TODO: remove this 
#include "srvctcp.h"


#define SND_CWND 64

#define MIN(x,y) (y)^(((x) ^ (y)) &  - ((x) < (y))) 
#define MAX(x,y) (y)^(((x) ^ (y)) & - ((x) > (y)))


int sndbuf = MSS*MAX_CWND;		/* udp send buff, bigger than mss */
int rcvbuf = MSS*MAX_CWND;		/* udp recv buff for ACKs*/
int mss=1472;			/* user payload, can be > MTU for UDP */
int numbytes;


double idle_total = 0; // The total time the server has spent waiting for the acks 
double coding_delay = 0; // Total time spent on encoding

int total_loss = 0;
double slr = 0; // Smoothed loss rate
double slr_mem = 1.0/BLOCK_SIZE; // The memory of smoothing function

// Should make sure that 50 bytes is enough to store the port string
char *port;

/*
 * Print usage message
 */
void
usage(void){
	printf("atousrv [configfile] \n");
	exit(1);
}

/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void
ctrlc(){
	et = getTime()-et;
	maxpkts=snd_max;
	endSession();
  fclose(snd_file);
  fclose(db);
	exit(1);
}

int
main (int argc, char** argv){
  struct sockaddr_storage;
  struct addrinfo hints, *servinfo;
  socket_t	sockfd;			/* network file descriptor */
  int rv;

  srandom(getpid());
  
	if (argc > 1) configfile = argv[1];

	signal(SIGINT,ctrlc);
	readConfig();

	if (thresh_init) {
    snd_ssthresh = thresh_init*MAX_CWND;
  } else {
    snd_ssthresh = 2147483647;  /* normal TCP, infinite */
  }

  // Setup the hints struct
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  // Get the server's info
  if((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0){
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    err_sys(""); 
    return 1;
  }

  // Loop through all the results and connect to the first possible
  for(result = servinfo; result != NULL; result = result->ai_next) {
    if((sockfd = socket(result->ai_family, 
                    result->ai_socktype,
                    result->ai_protocol)) == -1){
      perror("atousrv: error during socket initialization");
      continue;
    }

    if (bind(sockfd, result->ai_addr, result->ai_addrlen) == -1) {
      close(sockfd);
      err_sys("atousrv: can't bind local address");
      continue;
    }

    break;
  }

  if (result == NULL) { // If we are here, we failed to initialize the socket
    err_sys("atousrv: failed to initialize socket");
    return 2;
  }

  freeaddrinfo(servinfo);

  // initialize the thread pool
  thrpool_init( &workers, THREADS );

  // Initialize the block mutexes and queue of coded packets and counters
  int i;
  for(i = 0; i < NUM_BLOCKS; i++){
    pthread_mutex_init( &blocks[i].block_mutex, NULL );
    q_init(&coded_q[i], 2*BLOCK_SIZE);
    dof_remain[i] = 0;
  }


  char *file_name = malloc(1024);
  while(1){
    fprintf(stdout, "\nWaiting for requests...\n");
    if((numbytes = recvfrom(sockfd, file_name, 1024, 0,
                            &cli_addr, &clilen)) == -1){
      //printf("%s\n", file_name);
      err_sys("recvfrom: Failed to receive the request\n");
    }

    printf("sending %s\n", file_name);

    if ((snd_file = fopen(file_name, "rb"))== NULL){
      err_sys("Error while trying to create/open a file");
    }


    if (debug > 3) openLog();

    doit(sockfd);
    terminate(sockfd); // terminate 
    endSession();
    fclose(snd_file);
    fclose(db);
    restart();
  }
  return 0;
}

/*
 * This is contains the main functionality and flow of the client program
 */
int
doit(socket_t sockfd){
  int	i,r;
	double t;

	i=sizeof(sndbuf);

  // Not really sure if this is needed
  //--------------- Setting the socket options --------------------------------//
	setsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,i);
	getsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,(socklen_t*)&i);
  setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,i);
  getsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,(socklen_t*)&i);
  printf("config: sndbuf %d rcvbuf %d\n",sndbuf,rcvbuf);
  //----------------------------------------------------------------------------------//

	/* init control variables */
	memset(buff,0,BUFFSIZE);        /* pretouch */

	/* send out initial segments, then go for it */
	et = getTime(); 

  // Initialize the OnFly vectore
  for(i=0; i < MAX_CWND; i++) OnFly[i] = 0;

  done = FALSE;
  curr_block = 1; 
  // read and code the first two blocks
  for (i=1; i <= 2; i++){
    coding_job_t* job = malloc(sizeof(coding_job_t));
    job->blockno = i;
    job->dof_request = (int) ceil(BLOCK_SIZE*1.1);
    priority_t coding_urgency = LOW;
    addJob(&workers, &coding_job, job, &free_coding_job, coding_urgency);
    dof_remain[i%NUM_BLOCKS] += job->dof_request;  // Update the internal dof counter
  }
  

  snd_nxt = snd_una = 1;
  // snd_cwnd = SND_CWND;
  snd_cwnd = initsegs;

  //	if (bwe_on) bwe_pkt = snd_nxt;
  //	if (maxpkts == 0) maxpkts = 1000000000; // Default time is 10 seconds
  rto = tick;
	due = getTime() + timeout;  /* when una is due */

  // This is where the segments are sent
	send_segs(sockfd);
  
  Ack_Pckt *ack = malloc(sizeof(Ack_Pckt));
  double idle_timer;

	while(!done){

    idle_timer = getTime();
		r = timedread(sockfd, tick);

    idle_total += getTime() - idle_timer;

		if (r > 0) {  /* ack ready */
      
      // The recvfrom should be done to a separate buffer (not buff)
			r= recvfrom(sockfd, buff, ACK_SIZE, 0, &cli_addr, &clilen); // TODO maybe we can set flags so that recvfrom has a timeout of tick seconds

      // Unmarshall the ack from the buff
      unmarshallAck(ack, buff);

      if (debug > 6){
        printf("Got an ACK: ackno %d blockno %d -- RTT est %f \n", ack->ackno, ack->blockno, getTime()-ack->tstamp);
      }

			if (r <= 0) err_sys("read");
			rcvt = getTime();
			ipkts++; 

      handle_ack(sockfd, ack);

		} else if (r < 0) {  
			err_sys("select");
		}

   	t=getTime();
		if (maxtime && (t-et) > maxtime) maxpkts = snd_max; /*time up*/
		/* see if a packet has timedout */
		if (t > due) {   /* timeout */
			if (idle > maxidle) {
				/* give up */
				printf("*** idle abort ***\n");
				maxpkts=snd_max;
				break;
			}
			
      if (debug > 1){
        fprintf(stderr,
                "timerxmit %6.2f blockno %d blocklen %d pkt %d  snd_nxt %d  snd_cwnd %d  coding wnd %d\n",
                t-et,curr_block, 
                blocks[curr_block%NUM_BLOCKS].len,
                snd_una, 
                snd_nxt, 
                (int)snd_cwnd,
                coding_wnd);
      }


			timeouts++;
			rxmts++;
			bwe_pkt=0;
			idle++;
			dupacks=0; dup_acks=0;
      slr = 0;

			//snd_ssthresh = snd_cwnd*multiplier; /* shrink */

			if (snd_ssthresh < initsegs) {
			  snd_ssthresh = initsegs;
      }

      // TODO: check vinss again
      vinss = 1;
			snd_cwnd = initsegs;  /* drop window */
      snd_una = snd_nxt;
      send_segs(sockfd);  /* resend */

			due = t + timeout;  
		}
	}  /* while more pkts */

	et = getTime()-et;
  free(ack);
  return 0;
}

void 
terminate(socket_t sockfd){
  Data_Pckt *msg = dataPacket(snd_nxt, curr_block, 0);
  msg->tstamp = getTime();

  // FIN_CLI
  msg->flag = FIN_CLI;

  int size = marshallData(*msg, buff);

  do{

    if((numbytes = sendto(sockfd, buff, size, 0, &cli_addr, clilen)) == -1){
      perror("atousrv: sendto");
      exit(1);
    }  

  } while(errno == ENOBUFS && ++enobufs); // use the while to increment enobufs if the condition is met


  if(numbytes != mss){
    err_sys("write");
  }
  free(msg->payload);
  free(msg);
}

void
send_segs(socket_t sockfd){
  int win = 0;
  win = snd_cwnd - (snd_nxt - snd_una);
  if (win < 1) return;  /* no available window => done */

  int CurrOnFly = 0;
  int i;
  for(i = snd_una; i < snd_nxt; i++){
    CurrOnFly += (OnFly[i%MAX_CWND] == curr_block);
  }
  

  int CurrWin = win;
  int NextWin = 0;


  //Redundancy for transition

  //double p = total_loss/snd_una;
  double p = slr/(2.0-slr);   // Compensate for server's over estimation of the loss rate caused by lost acks

  // The total number of dofs the we think we should be sending (for the current block) from now on
  int dof_needed = (int) ceil((dof_req + ALPHA/2*(ALPHA*p + sqrt(pow(ALPHA*p,2.0) + 4*dof_req*p) ) )/(1-p)) - CurrOnFly;

  if (dof_req - CurrOnFly < win){
    CurrWin = MIN(win, dof_needed);
    NextWin = win - CurrWin;
  }

  // Check whether we have enough coded packets for current block
  if (dof_remain[curr_block%NUM_BLOCKS] < dof_needed){
    
    printf("requesting more dofs: curr block %d,  dof_remain %d, dof_needed %d dof_req %d\n", curr_block, dof_remain[curr_block%NUM_BLOCKS], dof_needed, dof_req);

    coding_job_t* job = malloc(sizeof(coding_job_t));
    job->blockno = curr_block;
    job->dof_request = dof_needed - dof_remain[curr_block%NUM_BLOCKS];
    priority_t coding_urgency = HIGH;
    addJob(&workers, &coding_job, job, &free_coding_job, coding_urgency);
    dof_remain[curr_block%NUM_BLOCKS] += job->dof_request; // Update the internal dof counter
  }


  while (CurrWin>=1) {
    send_one(sockfd, curr_block);
    snd_nxt++;
    CurrWin--;
    dof_remain[curr_block%NUM_BLOCKS]--;   // Update the internal dof counter
  }

  /* if (blocks[curr_block%NUM_BLOCKS].snd_nxt >= BLOCK_SIZE){
  printf("Calling for more - curr_blk %d, ack->dof_req %d, 
  Flag==EXT %d snd_nxt - snd_una %d NextBlockOnFly %d snd_CWND %f\n", 
  curr_block, ack->dof_req, ack->flag == EXT_MOD, 
  snd_nxt - snd_una, NextBlockOnFly, snd_cwnd);
  }  */

  if (curr_block != maxblockno){

    // Check whether we have enough coded packets for next block 
    if (dof_remain[(curr_block+1)%NUM_BLOCKS] < NextWin){
      coding_job_t* job = malloc(sizeof(coding_job_t));
      job->blockno = curr_block+1;
      job->dof_request = NextWin - dof_remain[(curr_block+1)%NUM_BLOCKS];
      priority_t coding_urgency = LOW;
      addJob(&workers, &coding_job, job, &free_coding_job, coding_urgency);
      dof_remain[(curr_block+1)%NUM_BLOCKS] += job->dof_request; // Update the internal dof counter
    }


    // send from curr_block + 1
    while (NextWin>=1) {
      send_one(sockfd, curr_block+1);
      snd_nxt++;
      NextWin--;
      dof_remain[(curr_block+1)%NUM_BLOCKS]--;   // Update the internal dof counter
    }
  }


}


void
send_one(socket_t sockfd, uint32_t blockno){
  // Send coded packet from block number blockno
  
  if (debug > 6){
    fprintf(stdout, "\n block %d DOF left %d q size %d\n",blockno, dof_remain[blockno%NUM_BLOCKS], coded_q[blockno%NUM_BLOCKS].size);
  }


  // Get a coded packet from the queue
  //q_pop is blocking. If the queue is empty, we wait until the coded packets are created
  // We should decide in send_segs whether we need more coded packets in the queue
  Data_Pckt *msg = (Data_Pckt*) q_pop(&coded_q[blockno%NUM_BLOCKS]);
  

  /* // TEST ONLY
  pthread_mutex_lock(&coded_q[blockno%NUM_BLOCKS].q_mutex_);

  //fprintf(stdout, "\n queue size %d HEAD %d, TAIL %d\n",coded_q[blockno%NUM_BLOCKS].size, coded_q[blockno%NUM_BLOCKS].head, coded_q[blockno%NUM_BLOCKS].tail);

  int k;
  for (k=1; k <= coded_q[blockno%NUM_BLOCKS].size; k++){
    Data_Pckt *tmp = (Data_Pckt*) coded_q[blockno%NUM_BLOCKS].q_[coded_q[blockno%NUM_BLOCKS].tail+k];
    printf("tmp msg block no %d start pkt %d\n", tmp->blockno, tmp->start_packet);
  }

  pthread_mutex_unlock(&coded_q[blockno%NUM_BLOCKS].q_mutex_);
  */


  // Correct the header information of the outgoing message
  msg->seqno = snd_nxt;
  msg->tstamp = getTime();
  
  if (debug > 6){
    printf("Sending.... blockno %d blocklen %d  seqno %d  snd_una %d snd_nxt %d  start pkt %d snd_cwnd %d  coding wnd %d\n",
           blockno, 
           blocks[curr_block%NUM_BLOCKS].len,
           msg->seqno, 
           snd_una,       
           snd_nxt, 
           msg->start_packet,
           (int)snd_cwnd,
           coding_wnd);
    fprintf(db,"%f %d xmt\n", msg->tstamp-et, blockno);
  }

  // Marshall msg into buf
  int message_size = marshallData(*msg, buff);

  do{

    if((numbytes = sendto(sockfd, buff, message_size, 0, 
                          &cli_addr, clilen)) == -1){
      perror("atousrv: sendto");
      exit(1);
    }

  } while(errno == ENOBUFS && ++enobufs); // use the while to increment enobufs if the condition is met

  if(numbytes != message_size){
    err_sys("write");
  }

  // Update the packets on the fly
  OnFly[snd_nxt%MAX_CWND] = blockno;
  
	//printf("Freeing the message - blockno %d snd_nxt %d ....", blockno, snd_nxt);

	opkts++;
  free(msg->packet_coeff);
  free(msg->payload);
  free(msg);

	//printf("Done Freeing the message - blockno %d snd_nxt %d \n\n\n", blockno, snd_nxt);
}


void
endSession(void){
	char myname[128];
  char* host = "Host"; // TODO: extract this from the packet

	mss-=12;
  gethostname(myname,sizeof(myname));
  printf("%s => %s %f Mbs win %d rxmts %d\n",
         myname,host,8.e-6*maxpkts*mss/et,rcvrwin,rxmts);
  printf("%f secs %d good bytes goodput %f KBs %f Mbs \n",
         et,maxpkts*mss,1.e-3*maxpkts*mss/et,8.e-6*maxpkts*mss/et);
  printf("pkts in %d  out %d  enobufs %d\n",
         ipkts,opkts,enobufs);
  printf("total bytes out %d   Loss rate %6.3f%%    %f Mbs \n",
         opkts*mss,100.*total_loss/snd_una,8.e-6*opkts*mss/et);
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
         snd_nxt,(int)snd_cwnd, snd_una,snd_ssthresh,snd_max);
  
  printf("goodacks %d cumacks %d ooacks %d\n", goodacks, cumacks, ooacks);
  
  printf("Total idle time %f, Total coding delay %f\n", idle_total, coding_delay);  
}

void
handle_ack(socket_t sockfd, Ack_Pckt *ack){
	double rtt;
  
	ackno = ack->ackno;
 
  //------------- RTT calculations --------------------------// 
	if (debug > 8 )printf("ack rcvd %d\n",ackno);

  /*fmf-rtt & rto calculations*/
	rtt = rcvt - ack->tstamp; // this calculates the rtt for this coded packet
	if (rtt < minrtt) minrtt = rtt;
  if (rtt > maxrtt) maxrtt = rtt;
  // TODO: remove (?) avrgrtt, vcnt???, and remove one of the min/max
	avrgrtt += rtt;
  vcnt++;

	/* RTO calculations */
  // TODO: ??? srtt? :P WTF
	srtt = (1-g)*srtt + g*rtt;
	delta = fabs(rtt - srtt);
	rttvar = (1-h)*rttvar + h*(delta - rttvar);
	rto = srtt + RTT_DECAY*rttvar;  /* may want to force it > 1 */
  
	if (debug > 6) {
    fprintf(db,"%f %d %f  %d %d ack\n",
            rcvt-et,ackno,rtt,(int)snd_cwnd,snd_ssthresh);
  }
	/* rtt bw estimation, vegas like */
  // TODO: remove some of these conditions? 
  if (bwe_on && bwe_pkt && ackno > bwe_pkt) bwe_calc(rtt);
  //----------------------------------------------------------------------------//

  if (ack->blockno > curr_block){

    if(maxblockno && ack->blockno > maxblockno){
      done = TRUE;
      printf("THIS IS THE LAST ACK\n");
      return; // goes back to the beginning of the while loop in main() and exits
    }
    
    freeBlock(curr_block);
    q_free(&coded_q[curr_block%NUM_BLOCKS], &free_coded_pkt);

    if (!maxblockno){
      //readBlock(curr_block+2);
      coding_job_t* job = malloc(sizeof(coding_job_t));
      job->blockno = curr_block+2;
      job->dof_request = (int) ceil(BLOCK_SIZE*(1.02+2*slr));
      priority_t coding_urgency = LOW;
      addJob(&workers, &coding_job, job, &free_coding_job, coding_urgency);
      dof_remain[(curr_block+2)%NUM_BLOCKS] += job->dof_request;  // Update the internal dof counter
    }
    
    curr_block++;

    if (debug > 5 && curr_block%10==0){
      printf("Now sending block %d, cwnd %f, SLR %f%%, SRTT %f ms\n", curr_block, snd_cwnd, 100*slr, srtt*1000);
    }
  }

  if (ackno > snd_nxt 
      || ackno < snd_una 
      || ack->blockno != curr_block) {
		/* bad ack */
		if (debug > 5) fprintf(stderr,
                           "Bad ack: curr block %d badack no %d snd_nxt %d snd_una %d\n",
                           curr_block, ackno, snd_nxt, snd_una);
		badacks++;
	} else  {
    goodacks++;

    int losses = ackno - (snd_una +1);

     if (losses > 0){
      printf("Loss report curr block %d ackno - snd_una %d\n", curr_block, ackno - snd_una);
      }  

    total_loss += losses;
    double loss_tmp =  pow(1-slr_mem, losses);
    slr = loss_tmp*(1-slr_mem)*slr + (1 - loss_tmp);

    snd_una = ackno;


    idle=0;
    due = rcvt + timeout;   /*restart timer */
    advance_cwnd();

    dof_req = ack->dof_req;  // Updated the requested dofs for the current block
    send_segs(sockfd);  /* send some if we can */

  } // end else goodack
}


/*
 * Checks for partial ack.  If partial ack arrives, force the retransmission
 * of the next unacknowledged segment, do not clear dupacks, and return
 * 1.  By setting snd_nxt to ackno, this forces retransmission timer to
 * be started again.  If the ack advances at least to snd_recover, return 0.
 */
/*
int
tcp_newreno(socket_t sockfd){
	if (ackno < snd_recover){
		int ocwnd = snd_cwnd;
		int onxt = snd_nxt;
    
		if ( debug > 1)fprintf(stderr,
                           "packrxmit pkt %d nxt %d max %d cwnd %d  thresh %d recover %d una %d\n",
                           ackno,snd_nxt, snd_max, (int)snd_cwnd,
                           snd_ssthresh,snd_recover,snd_una);
		rxmts++;
		packs++;
		due = rcvt + timeout;   restart timer 
		snd_cwnd = 1 + ackno - snd_una;
		snd_nxt = ackno;
		send_segs(sockfd); 
		snd_cwnd = ocwnd;
		if (onxt > snd_nxt) snd_nxt = onxt;
//partial deflation, una not updated yet
		snd_cwnd -= (ackno - snd_una -1);
		if (snd_cwnd < initsegs) snd_cwnd = initsegs;
return TRUE;  //yes was a partial ack 
	}
	return FALSE;
}*/


// Perhaps this is unnecesary.... No need to use select -> maybe use libevent (maybe does not have timeou?)
socket_t
timedread(socket_t sockfd, double t){
	struct timeval tv;
	fd_set rset;
  
	tv.tv_sec = t;
	tv.tv_usec = (t - tv.tv_sec)*1000000;
	FD_ZERO(&rset);
	FD_SET(sockfd, &rset);
	return ( select(sockfd+1,&rset,NULL,NULL, &tv) );
}

void
err_sys(char* s){
  perror(s);
  endSession();
  exit(1);
}

void
readConfig(void){
	/* read config if there, keyword value */
	FILE *fp;
	char line[128], var[32];
	double val;
  time_t t;
	port = malloc(10);  

	fp = fopen(configfile,"r");

	if (fp == NULL) {
		printf("atousrv unable to open %s\n",configfile);
		return;
	}

	while (fgets(line, sizeof (line), fp) != NULL) {
		sscanf(line,"%s %lf",var,&val);
		if (*var == '#') continue;  
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
		else if (strcmp(var,"port")==0) sprintf(port, "%d", (int)val); 
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
  printf("config: atousrv %s port %s debug %d %s", version,port,debug,
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

int
GetNextRetran() {
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

void
MarkRetran (int retran_seqno, int snd_max) { 
  /*mark the packet as retransmitted*/
  SBN[retran_seqno%SBSIZE].retran_ = 1;
  SBN[retran_seqno%SBSIZE].snd_nxt_ = snd_max;
}

int
RetransOK (int retran_seqno) { 
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

void
ClearScoreBoard() {
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

void
floyd_aimd(int cevent){
  
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

void
bwe_calc(double rtt){
	/* bw estimate each lossless RTT, vegas delta */
  /* once per rtt and not in recovery */
  if (vcnt) { /* only if we've been had some samples */
    vdelta = minrtt * ((snd_nxt - snd_una)/minrtt - (snd_nxt - bwe_pkt)/srtt);
    if (vdelta > max_delta) max_delta = vdelta;  /* vegas delta */
  } else vdelta=0;  /* no samples */
  /* TODO : might need this later?
  bwertt = (mss * (ackno-bwe_prev-1))/(rtt* (1 << 17)); // shift by 17 to the left to convert to Mbytes
  if (bwertt > bwertt_max) bwertt_max = bwertt;

  if (debug > 4 ) fprintf(stderr,"bwertt %f %f %f %d %f\n",rcvt-et,bwertt,rtt,ackno-bwe_prev-1,vdelta);
  */
  bwe_prev = bwe_pkt;
  bwe_pkt = snd_nxt;

  vcnt=0;
}


void
advance_cwnd(void){
  // TODO check bwe_pkt, different slopes for increasing and decreasing?
  // TODO make sure ssthresh < max cwnd
  // TODO increment and decrement values should be adjusted
	/* advance cwnd according to slow-start of congestion avoidance */
  if (snd_cwnd <= snd_ssthresh && vinss) {
    /* slow start, expo growth */
      snd_cwnd += ssincr;
  } else{
    /* congestion avoidance phase */
    int incr;    
    incr = increment;
    if (bwe_pkt) {
      /* vegas active and not in recovery */
      if (vdelta > vbeta ){
        incr= -increment; /* too fast, -incr /RTT */
        vdecr++;
      } else if (vdelta > valpha) {
        incr =0; /* just right */
        v0++;
      }
    }
    snd_cwnd = snd_cwnd + incr/snd_cwnd; /* ca */
    if (snd_cwnd < initsegs) snd_cwnd = initsegs;
    if (snd_cwnd > MAX_CWND ) snd_cwnd = MAX_CWND; // XXX
    vinss = 0; /* no vegas ss now */
  }
}

//---------------WORKER FUNCTION ----------------------------------
void*
coding_job(void *a){
  coding_job_t* job = (coding_job_t*) a;
  //printf("Job processed by thread %lu: blockno %d dof %d\n", pthread_self(), job->blockno, job->dof_request);

  uint32_t blockno = job->blockno;
  int dof_request = job->dof_request;  

  pthread_mutex_lock(&blocks[blockno%NUM_BLOCKS].block_mutex);

  // Check whether the requested blockno is already read, if not, read it from the file
  // generate the first set of degrees of freedom according toa  random permutation

  uint8_t block_len = blocks[blockno%NUM_BLOCKS].len;

  if (block_len  == 0){

    readBlock(blockno);
    block_len = blocks[blockno%NUM_BLOCKS].len;


    // Compute a random permutation of the rows
    
    uint8_t order[block_len];
    int i, j, swap_temp;
    for (i=0; i < block_len; i++){
     order[i] = i;
    }
    for (i=block_len - 1; i > 0; i--){
      j = random()%(i+1);
      swap_temp = order[i];
      order[i] = order[j];
      order[j] = swap_temp;
    }

    // Make sure this never happens!
    if (dof_request < block_len){
      printf("Error: the initially requested dofs are less than the block length\n");
      dof_request = block_len;
    }

    
    // Generate random combination by picking rows based on order


    int dof_ix, row;

    for (dof_ix = 0; dof_ix < block_len; dof_ix++){

      uint8_t num_packets = MIN(coding_wnd, block_len);
      Data_Pckt *msg = dataPacket(0, blockno, num_packets);

      row = order[dof_ix];


      // TODO Fix this, i.e., make sure every packet is involved in coding_wnd equations
      msg->start_packet = MIN(MAX(row%block_len - coding_wnd/2, 0), MAX(block_len - coding_wnd, 0));

      //memset(msg->payload, 0, PAYLOAD_SIZE);

      msg->packet_coeff[0] = 1;
      memcpy(msg->payload, blocks[blockno%NUM_BLOCKS].content[msg->start_packet], PAYLOAD_SIZE);

      for(i = 1; i < num_packets; i++){
        msg->packet_coeff[i] = (uint8_t)(1 + random()%255);
   
        for(j = 0; j < PAYLOAD_SIZE; j++){
          msg->payload[j] ^= FFmult(msg->packet_coeff[i], blocks[blockno%NUM_BLOCKS].content[msg->start_packet+i][j]);
        }
      }

      if(block_len < BLOCK_SIZE){
        msg->flag = PARTIAL_BLK;
        msg->blk_len = block_len;
      }

      /*
      printf("Pushing ... block %d, row %d \t start pkt %d\n", blockno, row, msg->start_packet);     
      fprintf(stdout, "before BEFORE push  queue size %d HEAD %d, TAIL %d\n",coded_q[blockno%NUM_BLOCKS].size, coded_q[blockno%NUM_BLOCKS].head, coded_q[blockno%NUM_BLOCKS].tail);
     
      if (coded_q[blockno%NUM_BLOCKS].size > 0){
        int k;
        for (k=1; k <= coded_q[blockno%NUM_BLOCKS].size; k++){
          Data_Pckt *tmp = (Data_Pckt*) coded_q[blockno%NUM_BLOCKS].q_[coded_q[blockno%NUM_BLOCKS].tail+k];
          printf("before BEFORE push buff msg block no %d start pkt %d\n", tmp->blockno, tmp->start_packet);
        }
      }
      */



      q_push_back(&coded_q[blockno%NUM_BLOCKS], msg);


    }  // Done with forming the initial set of coded packets

    dof_request = MAX(0, dof_request - block_len);  // This many more to go
  }

  if (dof_req > 0){
    // Extra degrees of freedom are generated by picking a row randomly

    int i, j;
    int dof_ix, row;

    for (dof_ix = 0; dof_ix < dof_request; dof_ix++){

      uint8_t num_packets = MIN(coding_wnd, block_len);
      Data_Pckt *msg = dataPacket(0, blockno, num_packets);

      row = random();
      // TODO Fix this, i.e., make sure every packet is involved in coding_wnd equations
      msg->start_packet = MIN(MAX(row%block_len - coding_wnd/2, 0), MAX(block_len - coding_wnd, 0));

      memset(msg->payload, 0, PAYLOAD_SIZE);

      msg->packet_coeff[0] = 1;
      memcpy(msg->payload, blocks[blockno%NUM_BLOCKS].content[msg->start_packet], PAYLOAD_SIZE);

      for(i = 1; i < num_packets; i++){
        msg->packet_coeff[i] = (uint8_t)(1 + random()%255);
   
        for(j = 0; j < PAYLOAD_SIZE; j++){
          msg->payload[j] ^= FFmult(msg->packet_coeff[i], blocks[blockno%NUM_BLOCKS].content[msg->start_packet+i][j]);
        }
      }

      if(block_len < BLOCK_SIZE){
        msg->flag = PARTIAL_BLK;
        msg->blk_len = block_len;
      }
      q_push_back(&coded_q[blockno%NUM_BLOCKS], msg);
    }  // Done with forming the remaining set of coded packets

  }
  //printf("Almost done with block %d\n", blockno);

  pthread_mutex_unlock( &blocks[blockno%NUM_BLOCKS].block_mutex );

  return NULL;
}

//----------------END WORKER ---------------------------------------

// Free Handler for the coding_job
void*
free_coding_job(const void* a)
{
  coding_job_t* job = (coding_job_t*) a; // XXX Do we need this? 
  free(job);
  return NULL;
}

// Free Handler for the coded packets in coded_q
void*
free_coded_pkt(const void* a)
{
  Data_Pckt* msg = (Data_Pckt*) a;  
  //printf("freeing msg blockno %d start pkt %d\n", msg->blockno, msg->start_packet);
  free(msg->packet_coeff);
  free(msg->payload);
  free(msg);
  return NULL;
}

//--------------------------------------------------------------------
void
readBlock(uint32_t blockno){

  // TODO: Make sure that the memory in the block is released before calling this function
  blocks[blockno%NUM_BLOCKS].len = 0;
  blocks[blockno%NUM_BLOCKS].content = malloc(BLOCK_SIZE*sizeof(char*));

  while(blocks[blockno%NUM_BLOCKS].len < BLOCK_SIZE && !feof(snd_file)){
    char* tmp = malloc(PAYLOAD_SIZE);
    memset(tmp, 0, PAYLOAD_SIZE); // This is done to pad with 0's 
    uint16_t bytes_read = (uint16_t) fread(tmp + 2, 1, PAYLOAD_SIZE-2, snd_file);
    bytes_read = htons(bytes_read);
    memcpy(tmp, &bytes_read, sizeof(uint16_t));
    
    // Insert this pointer into the blocks datastructure
    blocks[blockno%NUM_BLOCKS].content[blocks[blockno%NUM_BLOCKS].len] = tmp;
    blocks[blockno%NUM_BLOCKS].len++;
  }

  if(feof(snd_file)){
    maxblockno = blockno;
    printf("This is the last block %d\n", maxblockno);
  }
}


/*
 * Frees a block from memory
 */
void
freeBlock(uint32_t blockno){
  int i;
  for(i = 0; i < blocks[blockno%NUM_BLOCKS].len; i++){
    free(blocks[blockno%NUM_BLOCKS].content[i]);
  }
    free(blocks[blockno%NUM_BLOCKS].content);
    // reset the counters
    blocks[blockno%NUM_BLOCKS].len = 0;
    dof_remain[blockno&NUM_BLOCKS] = 0;
}
//-------------------------------------------------------------------------------------
void
openLog(void){
  time_t rawtime;
  struct tm* ptm;
  time(&rawtime);
  ptm = localtime(&rawtime);
  
  log_name = malloc(5*sizeof(int) + 9); // This is the size of the formatted string + 1
  
  sprintf(log_name, "%d-%02d-%d %02d:%02d.%02d.log", 
          ptm->tm_year + 1900, 
          ptm->tm_mon + 1, 
          ptm->tm_mday,
          ptm->tm_hour,
          ptm->tm_min,
          ptm->tm_sec);
  
  printf("Size %Zd\n", strlen(log_name));
  printf("The log name is %s\n", log_name);

	db = fopen(log_name, "w+");
  if(!db){
    perror("An error ocurred while trying to open the log file");
  }
}

/*
 * Takes a Data_Pckt struct and puts its raw contents into the buffer.
 * This assumes that there is enough space in buf to store all of these.
 * The return value is the number of bytes used for the marshalling
 */
int
marshallData(Data_Pckt msg, char* buf){
  int index = 0;
  int part = 0;  
  
  int partial_blk_flg = 0;
  if (msg.flag == PARTIAL_BLK) partial_blk_flg = sizeof(msg.blk_len);
  
  int size = PAYLOAD_SIZE + sizeof(double) + sizeof(flag_t) + sizeof(msg.seqno) + sizeof(msg.blockno) + (partial_blk_flg) + sizeof(msg.start_packet) + sizeof(msg.num_packets) + msg.num_packets*sizeof(msg.packet_coeff); // the total size in bytes of the current packet

  //Set to zeroes before starting
  memset(buf, 0, size);

  // Marshall the fields of the packet into the buffer

  htonpData(&msg);
  memcpy(buf + index, &msg.tstamp, (part = sizeof(msg.tstamp)));
  index += part;
  memcpy(buf + index, &msg.flag, (part = sizeof(msg.flag)));
  index += part;
  memcpy(buf + index, &msg.seqno, (part = sizeof(msg.seqno)));
  index += part;
  memcpy(buf + index, &msg.blockno, (part = sizeof(msg.blockno)));
  index += part;

  if (partial_blk_flg > 0){
    memcpy(buf + index, &msg.blk_len, (part = sizeof(msg.blk_len)));
    index += part;
  }
  memcpy(buf + index, &msg.start_packet, (part = sizeof(msg.start_packet)));
  index += part;

  memcpy(buf + index, &msg.num_packets, (part = sizeof(msg.num_packets)));
  index += part;

  int i;
  for(i = 0; i < msg.num_packets; i ++){
    memcpy(buf + index, &msg.packet_coeff[i], (part = sizeof(msg.packet_coeff[i])));
    index += part;
  }

  memcpy(buf + index, msg.payload, (part = PAYLOAD_SIZE));
  index += part;
  
  /*
  //----------- MD5 Checksum calculation ---------//
  MD5_CTX mdContext;
  MD5Init(&mdContext);
  MD5Update(&mdContext, buf, size);
  MD5Final(&mdContext);

  // Put the checksum in the marshalled buffer
  int i;
  for(i = 0; i < CHECKSUM_SIZE; i++){
    memcpy(buf + index, &mdContext.digest[i], (part = sizeof(mdContext.digest[i])));
    index += part;
    }*/

  return index;
}

bool
unmarshallAck(Ack_Pckt* msg, char* buf){
  int index = 0;
  int part = 0;

  memcpy(&msg->tstamp, buf+index, (part = sizeof(msg->tstamp)));
  index += part;
  memcpy(&msg->flag, buf+index, (part = sizeof(msg->flag)));
  index += part;
  memcpy(&msg->ackno, buf+index, (part = sizeof(msg->ackno)));
  index += part;
  memcpy(&msg->blockno, buf+index, (part = sizeof(msg->blockno)));
  index += part;
  memcpy(&msg->dof_req, buf+index, (part = sizeof(msg->dof_req)));
  index += part;
  ntohpAck(msg);

  bool match = TRUE;  
  /*
  int begin_checksum = index;
 
  // -------------------- Extract the MD5 Checksum --------------------//
  int i;
  for(i=0; i < CHECKSUM_SIZE; i++){
    memcpy(&msg->checksum[i], buf+index, (part = sizeof(msg->checksum[i])));
    index += part;
  }

  // Before computing the checksum, fill zeroes where the checksum was
  memset(buf+begin_checksum, 0, CHECKSUM_SIZE);

  //-------------------- MD5 Checksum Calculation  -------------------//
  MD5_CTX mdContext;
  MD5Init(&mdContext);
  MD5Update(&mdContext, buf, msg->payload_size + HDR_SIZE);
  MD5Final(&mdContext);


  for(i = 0; i < CHECKSUM_SIZE; i++){
    if(msg->checksum[i] != mdContext.digest[i]){
      match = FALSE;
    }
    }*/
  return match;
}

void
restart(void){
  // Print to the db file to differentiate traces
  fprintf(stdout, "\n\n*************************************\n****** Starting New Connection ******\n*************************************\n");
  done = FALSE;

  /* TCP pcb like stuff */
  dupacks = 0;			/* consecutive dup acks recd */
  snd_max = 0; 		/* biggest send */
  snd_recover = 0;	/* One RTT beyond last good data, newreno */
  maxpkts = 0;
  ackno = 0;
  
  //--------------- vegas working variables ------------//
  vinss=1;   /* in vegas slow start */
  vsscnt=0;  /* number of vegas slow start adjusts */
  vcnt = 0;  /* number of rtt samples */
  vdecr = 0; 
  v0  = 0; /* vegas decrements or no adjusts */
  vdelta = 0;
  vrtt = 0; 
  vrttsum = 0;
  vrttmax = 0; 
  vrttmin=999999;
  //------------------------------------------------------------//
  
 
  bwe_pkt = 0;
  bwe_prev = 0;
  bwe_on=1; 
  bwertt = 0;
  bwertt_max = 0;
  max_delta = 0;  /* vegas like tracker */
  
  /* stats */
  ipkts = 0;
  opkts = 0;
  dup3s = 0;
  dups = 0;
  packs = 0;
  badacks = 0;
  maxburst = 0;
  maxack = 0;
  rxmts = 0;
  timeouts = 0;
  enobufs = 0;
  ooacks = 0;
  et = 0;
  minrtt=999999.;
  maxrtt=0;
  avrgrtt = 0;
  rto = 0;
  delta = 0;
  srtt=0;
  rttvar=3.;
  h=.25;
  g=.125;
  due = 0;
  rcvt = 0;
  total_loss = 0;
  slr = 0;
}



