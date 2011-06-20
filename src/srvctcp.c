#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
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

#include "srvctcp.h"

int sndbuf = MSS*MAX_CWND;  /* udp send buff, bigger than mss */
int rcvbuf = MSS*MAX_CWND;  /* udp recv buff for ACKs*/
int numbytes;


double idle_total = 0; // The total time the server has spent waiting for the acks
double coding_delay = 0; // Total time spent on encoding
int total_loss = 0;
double slr = 0; // Smoothed loss rate
double slr_mem = 1.0/BLOCK_SIZE; // The memory of smoothing function

/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void
ctrlc(){
    et = getTime()-et;
    maxpkts=snd_max;
    endSession();
    exit(1);
}

int
main (int argc, char** argv){
    struct sockaddr_storage;
    struct addrinfo hints, *servinfo;
    socket_t sockfd; /* network file descriptor */
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
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags    = AI_PASSIVE;

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

    // Initialize the file mutex and position
    pthread_mutex_init(&file_mutex, NULL);
    file_position = 1; // Initially called to read the first block

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
        restart();
    }
    return 0;
}

/*
 * This is contains the main functionality and flow of the client program
 */
int
doit(socket_t sockfd){
    int i,r;
    double t;

    i=sizeof(sndbuf);

    //--------------- Setting the socket options --------------------------------//
    setsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,i);
    getsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,(socklen_t*)&i);
    setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,i);
    getsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,(socklen_t*)&i);
    printf("config: sndbuf %d rcvbuf %d\n",sndbuf,rcvbuf);
    //---------------------------------------------------------------------------//

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
        job->dof_request = (int) ceil(BLOCK_SIZE*1.0);
        job->coding_wnd = INIT_CODING_WND;
        dof_remain[i%NUM_BLOCKS] += job->dof_request;  // Update the internal dof counter
        addJob(&workers, &coding_job, job, &free, LOW);
    }


    snd_nxt = snd_una = 1;
    // snd_cwnd = SND_CWND;
    snd_cwnd = initsegs;

    // if (bwe_on) bwe_pkt = snd_nxt;
    // if (maxpkts == 0) maxpkts = 1000000000; // Default time is 10 seconds
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
                        "timerxmit %6.2f blockno %d blocklen %d pkt %d  snd_nxt %d  snd_cwnd %d  \n",
                        t-et,curr_block,
                        blocks[curr_block%NUM_BLOCKS].len,
                        snd_una,
                        snd_nxt,
                        (int)snd_cwnd);
            }

            timeouts++;
            idle++;
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


    if(numbytes != size){
        err_sys("write");
    }
    free(msg->payload);
    free(msg);
}

void
send_segs(socket_t sockfd){
    int win = 0;

    // TODO: FIX THIS
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
    int dof_needed = MAX(0, (int) (ceil((dof_req + ALPHA/2*(ALPHA*p + sqrt(pow(ALPHA*p,2.0) + 4*dof_req*p) ) )/(1-p))) - CurrOnFly);

    if (dof_req - CurrOnFly < win){
        CurrWin = MIN(win, dof_needed);
        NextWin = win - CurrWin;
    }

    // Check whether we have enough coded packets for current block
    if (dof_remain[curr_block%NUM_BLOCKS] < dof_needed){

      //printf("requesting more dofs: curr block %d,  dof_remain %d, dof_needed %d dof_req %d\n", curr_block, dof_remain[curr_block%NUM_BLOCKS], dof_needed, dof_req);

        coding_job_t* job = malloc(sizeof(coding_job_t));
        job->blockno = curr_block;
        job->dof_request = MIN_DOF_REQUEST + dof_needed - dof_remain[curr_block%NUM_BLOCKS];
        dof_remain[curr_block%NUM_BLOCKS] += job->dof_request; // Update the internal dof counter
      
        // Update the coding_wnd based on the slr (Use look-up table)
        int coding_wnd;
        for (coding_wnd = 0; slr >= slr_wnd_map[coding_wnd]; coding_wnd++);
        job->coding_wnd = coding_wnd;

        if (dof_req <= 3) {
          job->coding_wnd = MAX_CODING_WND;
          printf("Requested jobs with coding window %d - blockno %d dof_needed %d  \n", job->coding_wnd, curr_block, dof_needed);
        }

        addJob(&workers, &coding_job, job, &free, HIGH);
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
          job->dof_request = MIN_DOF_REQUEST + NextWin - dof_remain[(curr_block+1)%NUM_BLOCKS];
          dof_remain[(curr_block+1)%NUM_BLOCKS] += job->dof_request; // Update the internal dof counter

          // Update the coding_wnd based on the slr (Use look-up table)
          int coding_wnd;
          for (coding_wnd = 0; slr >= slr_wnd_map[coding_wnd]; coding_wnd++);
          job->coding_wnd = coding_wnd;
     
          addJob(&workers, &coding_job, job, &free, LOW);
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
        printf("Sending.... blockno %d blocklen %d  seqno %d  snd_una %d snd_nxt %d  start pkt %d snd_cwnd %d \n",
               blockno,
               blocks[curr_block%NUM_BLOCKS].len,
               msg->seqno,
               snd_una,
               snd_nxt,
               msg->start_packet,
               (int)snd_cwnd);
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

    int mss = PAYLOAD_SIZE;
    gethostname(myname,sizeof(myname));
    printf("\n\n%s => %s for %f secs\n",
           myname,host, et);
    //printf("%f secs  %f good bytes good put %f KBs %f Mbs\n",
    //       et,maxpkts*mss,1.e-3*maxpkts*mss/et,8.e-6*maxpkts*mss/et);
    printf("**THRU** %f Mbs -- pkts in %d  out %d  enobufs %d \n",
           8.e-6*(snd_una*PAYLOAD_SIZE)/et, ipkts,opkts,enobufs);
    printf("**LOSS* total bytes out %d   Loss rate %6.3f%%    %f Mbs \n",
           opkts*mss,100.*total_loss/snd_una,8.e-6*opkts*mss/et);
    //printf("timeouts %d badacks %d\n",timeouts,badacks);
    if (ipkts) avrgrtt /= ipkts;
    printf("**RTT** minrtt  %f maxrtt %f avrgrtt %f\n",
           minrtt,maxrtt,avrgrtt/*,8.e6*rcvrwin/avrgrtt*/);
    printf("**RTT** rto %f  srtt %f  rttvar %f\n",rto,srtt,rttvar);
    //printf("win/rtt = %f Mbs  bwdelay = %d KB  %d segs\n",
    //       8.e-6*rcvrwin*mss/avrgrtt, (int)(1.e-3*avrgrtt*opkts*mss/et),
    //       (int)(avrgrtt*opkts/et));
    printf("%f max_delta\n", max_delta);
    printf("vdecr %d v0 %d  vdelta %f\n",vdecr, v0,vdelta);
    printf("snd_nxt %d snd_cwnd %d  snd_una %d ssthresh %d goodacks%d\n",
           snd_nxt,(int)snd_cwnd, snd_una,snd_ssthresh, goodacks);
    //printf("goodacks %d\n", goodacks);
    printf("Total idle time %f, Total coding delay %f\n", idle_total, coding_delay);
    if(snd_file) fclose(snd_file);
    if(db)       fclose(db);
    snd_file = NULL;
    db       = NULL;
}

void
handle_ack(socket_t sockfd, Ack_Pckt *ack){
    double rtt;

    ackno = ack->ackno;
    if (debug > 8 )printf("ack rcvd %d\n",ackno);

    //------------- RTT calculations --------------------------//
    /*fmf-rtt & rto calculations*/
    rtt = rcvt - ack->tstamp; // this calculates the rtt for this coded packet
    if (rtt < minrtt) minrtt = rtt;
    if (rtt > maxrtt) maxrtt = rtt;
    avrgrtt += rtt;

    /* RTO calculations */
    srtt = (1-g)*srtt + g*rtt;
    rttvar = (1-h)*rttvar + h*(fabs(rtt - srtt) - rttvar);
    
    rto = beta*srtt;
    // TODO: we may no longer need RTT_DECAY... 
    // rto = srtt + RTT_DECAY*rttvar;  /* may want to force it > 1 */

    if (debug > 6) {
        fprintf(db,"%f %d %f  %d %d ack\n",rcvt-et,ackno,rtt,(int)snd_cwnd,snd_ssthresh);
    }
    if (ackno > snd_una){
        vdelta = 1- (srtt/rtt);
        if (vdelta > max_delta) max_delta = vdelta;  /* vegas delta */
    }
    //------------- RTT calculations --------------------------//

    if (ack->blockno > curr_block){
        if(maxblockno && ack->blockno > maxblockno){
            done = TRUE;
            printf("THIS IS THE LAST ACK\n");
            return; // goes back to the beginning of the while loop in main() and exits
        }

        pthread_mutex_lock(&blocks[curr_block%NUM_BLOCKS].block_mutex);

        freeBlock(curr_block);
        q_free(&coded_q[curr_block%NUM_BLOCKS], &free_coded_pkt);

        pthread_mutex_unlock(&blocks[curr_block%NUM_BLOCKS].block_mutex);

        if (!maxblockno){

          //readBlock(curr_block+2);
          coding_job_t* job = malloc(sizeof(coding_job_t));
          job->blockno = curr_block+2;
          //job->dof_request = ceil(BLOCK_SIZE*( 1.04 + 2*slr ));
          job->dof_request = BLOCK_SIZE;
          dof_remain[(curr_block+2)%NUM_BLOCKS] += job->dof_request;  // Update the internal dof counter

          // Update the coding_wnd based on the slr (Use look-up table)
          int coding_wnd;
          for (coding_wnd = 0; slr >= slr_wnd_map[coding_wnd]; coding_wnd++);
          job->coding_wnd = coding_wnd;

          addJob(&workers, &coding_job, job, &free, LOW);
        }

        curr_block++;

        if (debug > 5 && curr_block%10==0){
          printf("Now sending block %d, cwnd %f, SLR %f%%, SRTT %f ms \n", curr_block, snd_cwnd, 100*slr, srtt*1000);
        }
    }

    if (ackno > snd_nxt
        || ack->blockno != curr_block) {
        /* bad ack */
        if (debug > 5) fprintf(stderr,
                               "Bad ack: curr block %d badack no %d snd_nxt %d snd_una %d\n",
                               curr_block, ackno, snd_nxt, snd_una);
        badacks++;
    } else {
      fprintf(db,"%f %d %f %f %f %f xmt\n", getTime()-et, ack->blockno, snd_cwnd, slr, srtt, rto);
          
        if (ackno <= snd_una){
            //late ack
            if (debug > 5) fprintf(stderr,
                                   "Late ack: curr block %d badack no %d snd_nxt %d snd_una %d\n",
                                   curr_block, ackno, snd_nxt, snd_una);
        } else {
            // good ack TODO
          goodacks++;

          int losses = ackno - (snd_una +1);
            /*
              if (losses > 0){
              printf("Loss report curr block %d ackno - snd_una %d\n", curr_block, ackno - snd_una);
              }
            */
          total_loss += losses;
          double loss_tmp =  pow(1-slr_mem, losses);
          slr = loss_tmp*(1-slr_mem)*slr + (1 - loss_tmp);

          snd_una = ackno;
        }

        dof_req = ack->dof_req;  // Updated the requested dofs for the current block

        idle=0;
        due = rcvt + timeout;   /*restart timer */
        advance_cwnd();

        send_segs(sockfd);  /* send some if we can */

    } // end else goodack
}

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
        printf("ctcp unable to open %s\n",configfile);
        return;
    }

    while (fgets(line, sizeof (line), fp) != NULL) {
        sscanf(line,"%s %lf",var,&val);
        if (*var == '#') continue;
        else if (strcmp(var,"rcvrwin")==0) rcvrwin = val;
        else if (strcmp(var,"increment")==0) increment = val;
        else if (strcmp(var,"multiplier")==0) multiplier = val;
        else if (strcmp(var,"tick")==0) tick = val;
        else if (strcmp(var,"timeout")==0) timeout = val;
        else if (strcmp(var,"initsegs")==0) initsegs = val;
        else if (strcmp(var,"ssincr")==0) ssincr = val;
        else if (strcmp(var,"thresh_init")==0) thresh_init = val;
        else if (strcmp(var,"maxpkts")==0) maxpkts = val;
        else if (strcmp(var,"maxidle")==0) maxidle = val;
        else if (strcmp(var,"maxtime")==0) maxtime = val;
        else if (strcmp(var,"port")==0) sprintf(port, "%d", (int)val);
        else if (strcmp(var,"valpha")==0) valpha = val;
        else if (strcmp(var,"vbeta")==0) vbeta = val;
        else if (strcmp(var,"sndbuf")==0) sndbuf = val;
        else if (strcmp(var,"rcvbuf")==0) rcvbuf = val;
        else if (strcmp(var,"debug")==0) debug = val;
        else printf("config unknown: %s\n",line);
    }

    t=time(NULL);
    printf("*** CTCP %s ***\n",version);
    printf("config: port %s debug %d, %s",port,debug, ctime(&t));
    printf("config: initsegs %d tick %f timeout %f\n", initsegs,tick,timeout);
    printf("config: maxidle %d maxtime %d\n",maxidle, maxtime);
    printf("config: thresh_init %f ssincr %d\n", thresh_init, ssincr);
    printf("config: rcvrwin %d  increment %d  multiplier %f thresh_init %f\n",
           rcvrwin,increment,multiplier,thresh_init);
    printf("config: alpha %f beta %f\n", valpha,vbeta);

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

        /* vegas active and not in recovery */
        if (vdelta > vbeta ){
            if (debug > 6){
                printf("vdelta %f going down from %f \n", vdelta, snd_cwnd);
            }
            incr= -increment; /* too fast, -incr /RTT */
            vdecr++;
        } else if (vdelta > valpha) {
            if (debug > 6){
                printf("vdelta %f staying at %f\n", vdelta, snd_cwnd);
            }
            incr =0; /* just right */
            v0++;

        }
        snd_cwnd = snd_cwnd + incr/snd_cwnd; /* ca */
        /*
          if (incr !=0){
          printf("window size %d\n", (int)snd_cwnd);
          }*/
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
    int coding_wnd = job->coding_wnd;

    pthread_mutex_lock(&blocks[blockno%NUM_BLOCKS].block_mutex);
    
    // Check whether the requested blockno is already read, if not, read it from the file
    // generate the first set of degrees of freedom according toa  random permutation

    uint8_t block_len = blocks[blockno%NUM_BLOCKS].len;

    if (block_len  == 0){


        if( !maxblockno || maxblockno >= blockno )
        {
            // lock the file
            pthread_mutex_lock(&file_mutex);

            readBlock(blockno);

            // unlock the file
            pthread_mutex_unlock(&file_mutex);
        }else{
            goto release;
        }

        if( (block_len = blocks[blockno%NUM_BLOCKS].len) == 0){
            goto release;
        }


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
            msg->start_packet = MIN(MAX(row%block_len - (coding_wnd-1)/2, 0), MAX(block_len - coding_wnd, 0));
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

    if (dof_request > 0){
        // Extra degrees of freedom are generated by picking a row randomly

        int i, j;
        int dof_ix, row;

        int coding_wnd_slope = floor((MAX_CODING_WND - coding_wnd)/dof_request);

        for (dof_ix = 0; dof_ix < dof_request; dof_ix++){

          coding_wnd += coding_wnd_slope;

          uint8_t num_packets = MIN(coding_wnd, block_len);
          int partition_size = ceil(block_len/num_packets);
          Data_Pckt *msg = dataPacket(0, blockno, num_packets);

            row = (random()%partition_size)*num_packets;
            // TODO Fix this, i.e., make sure every packet is involved in coding_wnd equations
            msg->start_packet = MIN(row, block_len - num_packets);

            //printf("selected row: %d, start packet %d \n", row, msg->start_packet);

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

release:

    pthread_mutex_unlock( &blocks[blockno%NUM_BLOCKS].block_mutex );

    return NULL;
}

//----------------END WORKER ---------------------------------------


// Free Handler for the coded packets in coded_q
void
free_coded_pkt(void* a)
{
    Data_Pckt* msg = (Data_Pckt*) a;
    //printf("freeing msg blockno %d start pkt %d\n", msg->blockno, msg->start_packet);
    free(msg->packet_coeff);
    free(msg->payload);
    free(msg);
}

//--------------------------------------------------------------------
void
readBlock(uint32_t blockno){

    // TODO: Make sure that the memory in the block is released before calling this function
    blocks[blockno%NUM_BLOCKS].len = 0;
    blocks[blockno%NUM_BLOCKS].content = malloc(BLOCK_SIZE*sizeof(char*));

    if (file_position != blockno){
        fseek(snd_file, (blockno-1)*BLOCK_SIZE*(PAYLOAD_SIZE-2), SEEK_SET);
    }

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

    file_position = blockno + 1;  // Advance the counter

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
    dof_remain[blockno%NUM_BLOCKS] = 0;
}

void
openLog(void){
    time_t rawtime;
    struct tm* ptm;
    time(&rawtime);
    ptm = localtime(&rawtime);

    log_name = malloc(5*sizeof(int) + 9); // This is the size of the formatted string + 1

    sprintf(log_name, "logs/%d-%02d-%d %02d:%02d.%02d.log",
            ptm->tm_year + 1900,
            ptm->tm_mon + 1,
            ptm->tm_mday,
            ptm->tm_hour,
            ptm->tm_min,
            ptm->tm_sec);

    printf("Size %Zd\n", strlen(log_name));
    printf("The log name is %s\n", log_name);

    mkdir("logs", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    db = fopen(log_name, "w+");
    if(!db){
        fprintf(stdout, "An error ocurred while trying to open the log file\n");
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
    file_position = 1; // Initially called to read the first block
    snd_max = 0; /* biggest send */
    maxpkts = 0;
    ackno = 0;

    vinss=1;   /* in vegas slow start */
    vdecr = 0;
    v0  = 0; /* vegas decrements or no adjusts */
    vdelta = 0;
    max_delta = 0;  /* vegas like tracker */

    ipkts = 0;
    opkts = 0;
    badacks = 0;
    timeouts = 0;
    enobufs = 0;
    et = 0;
    minrtt=999999.;
    maxrtt=0;
    avrgrtt = 0;
    rto = 0;
    srtt=0;
    rttvar=3.;
    h=.25;
    g=.125;
    due = 0;
    rcvt = 0;
    total_loss = 0;
    slr = 0;
}



