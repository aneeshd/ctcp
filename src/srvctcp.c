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


/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void
ctrlc(){
    total_time = getTime()-start_time;
    endSession();
    exit(1);
}

void
usage()
{
    fprintf(stderr, "Usage: srvctcp [-options] \n                   \
      -c    configuration file to be used ex: config/vegas\n        \
      -l    set the log name. Defaults to current datetime\n        \
      -p    port number to listen to. Defaults to 9999\n");
    exit(0);
}

int
main (int argc, char** argv){
    struct addrinfo hints, *servinfo;
    socket_t sockfd; /* network file descriptor */
    int rv;
    int c;

    srandom(getpid());

    while((c = getopt(argc,argv, "c:p:l:")) != -1)
    {
        switch (c)
        {
        case 'c':
            configfile = optarg;
            break;
        case 'p':
            port       = optarg;
            break;
        case 'l':
            log_name   = optarg;
            break;
        default:
            usage();
        }
    }

    readConfig();    // Read config file
    initialize();    // Initialize global variables and threads

    signal(SIGINT, ctrlc);

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

    /*--------------------------------------------------------------------------*/


    char *file_name = malloc(1024);
    while(1){
      fprintf(stdout, "\nWaiting for requests...\n");
      if((numbytes = recvfrom(sockfd, file_name, 1024, 0,
                              &cli_addr, &clilen)) == -1){
        //printf("%s\n", file_name);
        err_sys("recvfrom: Failed to receive the request\n");
      }
      
      // Save the client address as the primary client
      cli_addr_storage[0] = cli_addr; 

      printf("sending %s\n", file_name);
      
      if ((snd_file = fopen(file_name, "rb"))== NULL){
        err_sys("Error while trying to create/open a file");
      }
      
      if (debug > 3) openLog(log_name);

      restart();      
      doit(sockfd);
      terminate(sockfd); // terminate
      endSession();
    }
    return 0;
}

/*
 * This is contains the main functionality and flow of the client program
 */
int
doit(socket_t sockfd){
    int i,r;
    int path_id = 0;                              // Connection identifier

    i=sizeof(sndbuf);

    //--------------- Setting the socket options --------------------------------//
    setsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,i);
    getsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,(socklen_t*)&i);
    setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,i);
    getsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,(socklen_t*)&i);
    printf("config: sndbuf %d rcvbuf %d\n",sndbuf,rcvbuf);
    //---------------------------------------------------------------------------//

    /* send out initial segments, then go for it */
    start_time = getTime();

    // read and code the first two blocks
    for (i=1; i <= 2; i++){
        coding_job_t* job = malloc(sizeof(coding_job_t));
        job->blockno = i;
        job->dof_request = (int) ceil(BLOCK_SIZE*1.0);
        job->coding_wnd = 1; //INIT_CODING_WND;  TODO: remove comment if stable
        dof_remain[i%NUM_BLOCKS] += job->dof_request;  // Update the internal dof counter
        addJob(&workers, &coding_job, job, &free, LOW);
    }

    // This is where the segments are sent
    send_segs(sockfd, path_id);

    Ack_Pckt *ack = malloc(sizeof(Ack_Pckt));
    double idle_timer;

    while(!done){

      idle_timer = getTime();
      double rto_max = rto[0];
      for (i=1; i < max_path_id; i++){
        if (rto[i] > rto_max) rto_max = rto[i];
      }
      
      r = timedread(sockfd, rto_max + RTO_BIAS);
      idle_total += getTime() - idle_timer;

      if (r > 0) {  /* ack ready */
        
        // The recvfrom should be done to a separate buffer (not buff)
        r= recvfrom(sockfd, buff, ACK_SIZE, 0, &cli_addr, &clilen); 
        // Unmarshall the ack from the buff
        unmarshallAck(ack, buff);

        if (debug > 6){
          printf("Got an ACK: ackno %d blockno %d dof_req %d -- RTT est %f \n", ack->ackno, ack->blockno, ack->dof_req, getTime()-ack->tstamp);
        }

        /* ---- Decide if the ack is a request for new connections ------- */
        /* -------- Check which path the ack belong to --------------------*/
        if (ack->flag == SYN){
          if (max_path_id < MAX_CONNECT){
            // max_path_id goes from 1 to MAX_CONNECT
            // path_id goes from 0 to MAX_CONNECT-1 
            path_id = max_path_id;  
            max_path_id++;

            // add the client address info to the client lookup table
            cli_addr_storage[path_id] = cli_addr;

            printf("Request for a new path: Client port %d\n", ((struct sockaddr_in*)&cli_addr)->sin_port);

            // Initially send a few packets to keep it going
            last_ack_time[path_id] = getTime();
            send_segs(sockfd, path_id);
            continue;        // Go back to the beginning of the while loop
          }
        } else{
          // Use the cli_addr to find the right path_id for this Ack
          // Start searching through other possibilities in the cli_addr_storage

          
          while (sockaddr_cmp(&cli_addr, &cli_addr_storage[path_id]) != 0){
            path_id = (path_id + 1)%max_path_id;
          }
          //printf("path_id %d \t", path_id);

        }

        /*-----------------------------------------------------------------*/
            
            
        if (r <= 0) err_sys("read");
        double rcvt = getTime();
        ipkts++;
        
        last_ack_time[path_id] = rcvt;
        handle_ack(sockfd, ack, path_id);

        for (i = 1; i < max_path_id; i++){
          if(rcvt - last_ack_time[(path_id+i)%max_path_id] > rto[(path_id + i)%max_path_id] + RTO_BIAS){
            timeout(sockfd, (path_id + i)%max_path_id);
          }
        }
        path_id = (path_id + 1)%max_path_id; // return the path_id to what it used to be before looping. 

      } else if (r < 0) {
        err_sys("select");
      } else if (r==0) {
        timeout(sockfd, path_id);
        path_id = (path_id + 1)%max_path_id;
      }
    }  /* while more pkts */

    total_time = getTime()-start_time;
    free(ack);

    return 0;
}

void
timeout(socket_t sockfd, int path_id){
        /* see if a packet has timedout */        
  //if (idle[path_id] > maxidle) {
  //  /* give up */
  //  printf("*** idle abort *** on path %d\n", path_id);
  //  // removing the path from connections

  //   // TODO TODO Need to remove the path from cli_addr_storage-
  //  break;
  //}
  
  if (debug > 1){
    fprintf(stderr,
            "timerxmit %6.2f \t on path_id %d \t blockno %d blocklen %d pkt %d  snd_nxt %d  snd_cwnd %d  \n",
            getTime()-start_time,
            path_id,
            curr_block,
            blocks[curr_block%NUM_BLOCKS].len,
            snd_una[path_id],
            snd_nxt[path_id],
            (int)snd_cwnd[path_id]);
  }
  
  timeouts++;
  idle[path_id]++;
  slr[path_id] = 0;
  //slr_long[path_id] = SLR_LONG_INIT;
  rto[path_id] = 2*rto[path_id]; // Exponential back-off
  
  snd_ssthresh[path_id] = snd_cwnd[path_id]*multiplier; /* shrink */
  
  if (snd_ssthresh[path_id] < initsegs) {
    snd_ssthresh[path_id] = initsegs;
  }          
  slow_start[path_id] = 1;
  snd_cwnd[path_id] = initsegs;  /* drop window */
  snd_una[path_id] = snd_nxt[path_id];
  
  cli_addr = cli_addr_storage[path_id];   // Make sure we are sending on the right path
  send_segs(sockfd, path_id);  /* resend */                     
  
  // Update the path_id so that we timeout based on another path, and try every path in a round
  // Avoids getting stuck if the current path dies and packets/acks on other paths are lost
  
}

void
terminate(socket_t sockfd){
  // TODO path_id = 0??
  Data_Pckt *msg = dataPacket(snd_nxt[0], curr_block, 0);
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
send_segs(socket_t sockfd, int path_id){
  int win = 0;

  // TODO: FIX THIS
  win = snd_cwnd[path_id] - (snd_nxt[path_id] - snd_una[path_id]);
  if (win < 1) return;  /* no available window => done */

  int CurrOnFly = 0;
  int i,j;
  for (j = 0; j < max_path_id; j++){
    for(i = snd_una[j]; i < snd_nxt[j]; i++){
      CurrOnFly += (OnFly[j][i%MAX_CWND] == curr_block);
    }
  }

  int CurrWin = win;
  int NextWin = 0;


  //Redundancy for transition

  //double p = total_loss[path_id]/snd_una[path_id];
  double p = slr[path_id]/(2.0-slr[path_id]);   // Compensate for server's over estimation of the loss rate caused by lost acks

  // The total number of dofs the we think we should be sending (for the current block) from now on
  int dof_needed = MAX(0, (int) (ceil((dof_req_latest + ALPHA/2*(ALPHA*p + sqrt(pow(ALPHA*p,2.0) + 4*dof_req_latest*p) ) )/(1-p))) - CurrOnFly);

  if (dof_req_latest - CurrOnFly < win){
    CurrWin = MIN(win, dof_needed);
    NextWin = win - CurrWin;
  }

  // Check whether we have enough coded packets for current block
  if (dof_remain[curr_block%NUM_BLOCKS] < dof_needed){

    //printf("requesting more dofs: curr path_id %d curr block %d,  dof_remain %d, dof_needed %d dof_req_latest %d\n", path_id, curr_block, dof_remain[curr_block%NUM_BLOCKS], dof_needed, dof_req_latest);

    coding_job_t* job = malloc(sizeof(coding_job_t));
    job->blockno = curr_block;
    job->dof_request = MIN_DOF_REQUEST + dof_needed - dof_remain[curr_block%NUM_BLOCKS];
    dof_remain[curr_block%NUM_BLOCKS] += job->dof_request; // Update the internal dof counter
      
    // Update the coding_wnd based on the slr (Use look-up table)
    /*int coding_wnd;
      for (coding_wnd = 0; slr[path_id] >= slr_wnd_map[coding_wnd]; coding_wnd++);
      job->coding_wnd = coding_wnd;*/
    job->coding_wnd = INIT_CODING_WND;

    if (dof_req_latest <= 3) {
      job->coding_wnd = MAX_CODING_WND;
      printf("Requested jobs with coding window %d - blockno %d dof_needed %d  \n", job->coding_wnd, curr_block, dof_needed);
    }

    addJob(&workers, &coding_job, job, &free, HIGH);
  }

  while (CurrWin>=1) {

    send_one(sockfd, curr_block, path_id);
    snd_nxt[path_id]++;
    CurrWin--;
    dof_remain[curr_block%NUM_BLOCKS]--;   // Update the internal dof counter

  }


  if (curr_block != maxblockno){

    //printf("dof_Remain(next block) %d Nextwin %d\n", dof_remain[(curr_block+1)%NUM_BLOCKS], NextWin);
    // Check whether we have enough coded packets for next block
    if (dof_remain[(curr_block+1)%NUM_BLOCKS] < NextWin){
 
      coding_job_t* job = malloc(sizeof(coding_job_t));
      job->blockno = curr_block+1;
      job->dof_request = MIN_DOF_REQUEST + NextWin - dof_remain[(curr_block+1)%NUM_BLOCKS];
      dof_remain[(curr_block+1)%NUM_BLOCKS] += job->dof_request; // Update the internal dof counter

      // Update the coding_wnd based on the slr (Use look-up table)
      /*int coding_wnd;
        for (coding_wnd = 0; slr[path_id] >= slr_wnd_map[coding_wnd]; coding_wnd++);
        job->coding_wnd = coding_wnd;*/
      job->coding_wnd = INIT_CODING_WND;
     
      addJob(&workers, &coding_job, job, &free, LOW);
    }


    // send from curr_block + 1
    while (NextWin>=1) {
      send_one(sockfd, curr_block+1, path_id);
      snd_nxt[path_id]++;
      NextWin--;
      dof_remain[(curr_block+1)%NUM_BLOCKS]--;   // Update the internal dof counter
    }
  }


}


void
send_one(socket_t sockfd, uint32_t blockno, int path_id){
  // Send coded packet from block number blockno

  if (debug > 6){
    fprintf(stdout, "\n block %d DOF left %d q size %d\n",blockno, dof_remain[blockno%NUM_BLOCKS], coded_q[blockno%NUM_BLOCKS].size);
  }

  // Get a coded packet from the queue
  //q_pop is blocking. If the queue is empty, we wait until the coded packets are created
  // We should decide in send_segs whether we need more coded packets in the queue
  Data_Pckt *msg = (Data_Pckt*) q_pop(&coded_q[blockno%NUM_BLOCKS]);

  // Correct the header information of the outgoing message
  msg->seqno = snd_nxt[path_id];
  msg->tstamp = getTime();


  if (debug > 6){
    printf("Sending... on path_id %d. blockno %d blocklen %d  seqno %d  snd_una %d snd_nxt %d  start pkt %d snd_cwnd %d   port %d \n",
           path_id,
           blockno,
           blocks[curr_block%NUM_BLOCKS].len,
           msg->seqno,
           snd_una[path_id],
           snd_nxt[path_id],
           msg->start_packet,
           (int)snd_cwnd[path_id],
           ((struct sockaddr_in*)&cli_addr)->sin_port  );
  }


  // Marshall msg into buf
  int message_size = marshallData(*msg, buff);

  do{
    if((numbytes = sendto(sockfd, buff, message_size, 0,
                          &cli_addr, clilen)) == -1){
      printf("Sending... on path_id %d. blockno %d blocklen %d  seqno %d  snd_una %d snd_nxt %d  start pkt %d snd_cwnd %d   port %d \n",
             path_id,
             blockno,
             blocks[curr_block%NUM_BLOCKS].len,
             msg->seqno,
             snd_una[path_id],
             snd_nxt[path_id],
             msg->start_packet,
             (int)snd_cwnd[path_id],
             ((struct sockaddr_in*)&cli_addr)->sin_port  );
      perror("atousrv: sendto");
      exit(1);
    }

  } while(errno == ENOBUFS && ++enobufs); // use the while to increment enobufs if the condition is met

    
  if(numbytes != message_size){
    err_sys("write");
  }

  // Update the packets on the fly
  OnFly[path_id][snd_nxt[path_id]%MAX_CWND] = blockno;

  //printf("Freeing the message - blockno %d snd_nxt[path_id] %d ....", blockno, snd_nxt[path_id]);

  opkts++;
  free(msg->packet_coeff);
  free(msg->payload);
  free(msg);

  //printf("Done Freeing the message - blockno %d snd_nxt[path_id] %d \n\n\n", blockno, snd_nxt[path_id]);
}

void
endSession(void){
  char myname[128];
  char* host = "Host"; // TODO: extract this from the packet

  int mss = PAYLOAD_SIZE;
  gethostname(myname,sizeof(myname));
  printf("\n\n%s => %s for %f secs\n",
         myname,host, total_time);

  int path_id;
  for (path_id = 0; path_id < max_path_id; path_id++){
    printf("******* Priniting Statistics for path   %d   ******** \n ", path_id);
    printf("**THRU** %f Mbs -- pkts in %d  out %d  enobufs %d \n",
           8.e-6*(snd_una[path_id]*PAYLOAD_SIZE)/total_time, ipkts,opkts,enobufs);
    printf("**LOSS* total bytes out %d   Loss rate %6.3f%%    %f Mbs \n",
           opkts*mss,100.*total_loss[path_id]/snd_una[path_id],8.e-6*opkts*mss/total_time);
    if (ipkts) avrgrtt[path_id] /= ipkts;
    printf("**RTT** minrtt  %f maxrtt %f avrgrtt %f\n",
           minrtt[path_id],maxrtt[path_id],avrgrtt[path_id]);
    printf("**RTT** rto %f  srtt %f \n",rto[path_id],srtt[path_id]);
    printf("%f max_delta\n", max_delta[path_id]);
    printf("vdecr %d v0 %d  vdelta %f\n",vdecr[path_id], v0[path_id],vdelta[path_id]);
    printf("snd_nxt %d snd_cwnd %d  snd_una %d ssthresh %d goodacks%d\n\n",
           snd_nxt[path_id],(int)snd_cwnd[path_id], snd_una[path_id],snd_ssthresh[path_id], goodacks);
  }

  printf("Total idle time %f\n", idle_total);
  if(snd_file) fclose(snd_file);
  if(db)       fclose(db);
  snd_file = NULL;
  db       = NULL;
}

void
handle_ack(socket_t sockfd, Ack_Pckt *ack, int path_id){
  double rtt;
  uint32_t ackno = ack->ackno;
  double rcvt = last_ack_time[path_id];
  if (debug > 8 )printf("ack rcvd %d\n",ackno);

  //------------- RTT calculations --------------------------//
  /*fmf-rtt & rto calculations*/
  rtt = rcvt - ack->tstamp; // this calculates the rtt for this coded packet
  if (rtt < minrtt[path_id]) minrtt[path_id] = rtt;
  if (rtt > maxrtt[path_id]) maxrtt[path_id] = rtt;
  avrgrtt[path_id] += rtt;

  /* RTO calculations */
  srtt[path_id] = (1-g)*srtt[path_id] + g*rtt; 

  if (rtt > rto[path_id]/beta){
    rto[path_id] = (1-g)*rto[path_id] + g*beta*rtt;
  }else {
    rto[path_id] = (1-g/5)*rto[path_id] + g/5*beta*rtt;
  }

  if (debug > 6) {
    fprintf(db,"%f %d %f  %d %d ack\n",rcvt-start_time,ackno,rtt,(int)snd_cwnd[path_id],snd_ssthresh[path_id]);
  }
  if (ackno > snd_una[path_id]){
    vdelta[path_id] = 1-srtt[path_id]/rtt;
    if (vdelta[path_id] > max_delta[path_id]) max_delta[path_id] = vdelta[path_id];  /* vegas delta */
  }
  //------------- RTT calculations --------------------------//

  if (ack->blockno > curr_block){

    pthread_mutex_lock(&blocks[curr_block%NUM_BLOCKS].block_mutex);

    freeBlock(curr_block);
    q_free(&coded_q[curr_block%NUM_BLOCKS], &free_coded_pkt);

    curr_block++;                      // Update the current block identifier

    pthread_mutex_unlock(&blocks[(curr_block-1)%NUM_BLOCKS].block_mutex);


    if(maxblockno && ack->blockno > maxblockno){
      done = TRUE;
      printf("THIS IS THE LAST ACK\n");
      return; // goes back to the beginning of the while loop in main() and exits
    }

    if (!maxblockno){

      //readBlock(curr_block+1);
      coding_job_t* job = malloc(sizeof(coding_job_t));
      job->blockno = curr_block+1;
      //job->dof_request = ceil(BLOCK_SIZE*( 1.04 + 2*slr[path_id] ));
      job->dof_request = BLOCK_SIZE;
      dof_remain[(curr_block+1)%NUM_BLOCKS] += job->dof_request;  // Update the internal dof counter

      // Update the coding_wnd based on the slr (Use look-up table)
      /*int coding_wnd;
        for (coding_wnd = 0; slr[path_id] >= slr_wnd_map[coding_wnd]; coding_wnd++);*/
      job->coding_wnd = 1;//coding_wnd;  TODO: remove comment if stable

      addJob(&workers, &coding_job, job, &free, LOW);
    }

    dof_req_latest = ack->dof_req;     // reset the dof counter for the current block

    if (debug > 5 && curr_block%10==0){
      printf("Now sending block %d, cwnd %f, SLR %f%%, SRTT %f ms \n", curr_block, snd_cwnd[path_id], 100*slr[path_id], srtt[path_id]*1000);
    }
  }

  if (ackno > snd_nxt[path_id]
      || ack->blockno != curr_block) {
    /* bad ack */
    if (debug > 4) fprintf(stderr,
                           "path_id %d Bad ack: curr block %d badack no %d snd_nxt %d snd_una %d cli.port %d, cli_storage[path_id].port %d\n\n",
                           path_id, curr_block, ackno, snd_nxt[path_id], snd_una[path_id],  ((struct sockaddr_in*)&cli_addr)->sin_port,  ((struct sockaddr_in*)&cli_addr_storage[path_id])->sin_port);
    badacks++;
  } else {

    // Late or Good acks count towards goodput
    fprintf(db,"%f %d %f %d %f %f %f %f %f xmt\n", getTime()-start_time, ack->blockno, snd_cwnd[path_id], snd_ssthresh[path_id], slr[path_id], slr_long[path_id], srtt[path_id], rto[path_id], rtt);

    idle[path_id] = 0; // Late or good acks should stop the "idle" count for max-idle abort.
          
    if (ackno <= snd_una[path_id]){
      //late ack
      if (debug > 5) fprintf(stderr,
                             "Late ack: curr block %d ack-blockno %d badack no %d snd_nxt %d snd_una %d\n",
                             curr_block, ack->blockno, ackno, snd_nxt[path_id], snd_una[path_id]);
    } else {
      // good ack TODO
      goodacks++;

      int losses = ackno - (snd_una[path_id] +1);
      /*
        if (losses > 0){
        printf("Loss report curr block %d ackno - snd_una[path_id] %d\n", curr_block, ackno - snd_una[path_id]);
        }
      */
      total_loss[path_id] += losses;
      double loss_tmp =  pow(1-slr_mem, losses);
      slr[path_id] = loss_tmp*(1-slr_mem)*slr[path_id] + (1 - loss_tmp);
      loss_tmp =  pow(1-slr_longmem, losses);
      slr_long[path_id] = loss_tmp*(1-slr_longmem)*slr_long[path_id] + (1 - loss_tmp);
      // NECESSARY CONDITION: slr_longmem must be smaller than 1/2. 
      slr_longstd[path_id] = (1-slr_longmem)*slr_longstd[path_id] + slr_longmem*(fabs(slr[path_id] - slr_long[path_id]) - slr_longstd[path_id]);

      snd_una[path_id] = ackno;
    }
 
    // Updated the requested dofs for the current block 
    // The MIN is to avoid outdated infromation by out of order ACKs or ACKs on different paths
    //dof_req[path_id] = MIN(ack->dof_req, dof_req[path_id]); 
    dof_req_latest = MIN(dof_req_latest, ack->dof_req);


    advance_cwnd(path_id);

    send_segs(sockfd, path_id);  /* send some if we can */


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
  // Initialize the default values of config variables
  
  rcvrwin    = 20;          /* rcvr window in mss-segments */
  increment  = 1;           /* cc increment */
  multiplier = 0.5;         /* cc backoff  &  fraction of rcvwind for initial ssthresh*/
  initsegs   = 2;          /* slowstart initial */
  ssincr     = 1;           /* slow start increment */
  maxpkts    = 0;           /* test duration */
  maxidle    = 10;          /* max idle before abort */
  valpha     = 0.05;        /* vegas parameter */
  vbeta      = 0.2;         /* vegas parameter */
  sndbuf     = MSS*MAX_CWND;/* UDP send buff, bigger than mss */
  rcvbuf     = MSS*MAX_CWND;/* UDP recv buff for ACKs*/
  debug      = 5           ;/* Debug level */

  /* read config if there, keyword value */
  FILE *fp;
  char line[128], var[32];
  double val;
  time_t t;

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
    else if (strcmp(var,"initsegs")==0) initsegs = val;
    else if (strcmp(var,"ssincr")==0) ssincr = val;
    else if (strcmp(var,"maxpkts")==0) maxpkts = val;
    else if (strcmp(var,"maxidle")==0) maxidle = val;
    else if (strcmp(var,"valpha")==0) valpha = val;
    else if (strcmp(var,"vbeta")==0) vbeta = val;
    else if (strcmp(var,"sndbuf")==0) sndbuf = val;
    else if (strcmp(var,"rcvbuf")==0) rcvbuf = val;
    else if (strcmp(var,"debug")==0) debug = val;
    else printf("config unknown: %s\n",line);
  }

  t = time(NULL);
  printf("*** CTCP %s ***\n",version);
  printf("config: port %s debug %d, %s",port,debug, ctime(&t));
  printf("config: rcvrwin %d  increment %d  multiplier %f\n",
         rcvrwin,increment,multiplier);
  printf("config: alpha %f beta %f\n", valpha,vbeta);

}


void
advance_cwnd(int path_id){
  // TODO check bwe_pkt, different slopes for increasing and decreasing?
  // TODO make sure ssthresh < max cwnd
  // TODO increment and decrement values should be adjusted
  /* advance cwnd according to slow-start of congestion avoidance */
  if (snd_cwnd[path_id] <= snd_ssthresh[path_id] && slow_start[path_id]) {
    /* slow start, expo growth */
    snd_cwnd[path_id] += ssincr;
  } else{
    /* congestion avoidance phase */
    int incr;
    incr = increment;

    /*
      Range --(1)-- valpha --(2)-- vbeta --(3)--
      (1): increase window
      (2): stay
      (3): decrease window
    */
    /* vegas active and not in recovery */
    if (vdelta[path_id] > vbeta ){
      if (debug > 6){
        printf("vdelta %f going down from %f \n", vdelta[path_id], snd_cwnd[path_id]);
      }
      incr= -increment; /* too fast, -incr /RTT */
      vdecr[path_id]++;
    } else if (vdelta[path_id] > valpha) {
      if (debug > 6){
        printf("vdelta %f staying at %f\n", vdelta[path_id], snd_cwnd[path_id]);
      }
      incr =0; /* just right */
      v0[path_id]++;

    }
    snd_cwnd[path_id] = snd_cwnd[path_id] + incr/snd_cwnd[path_id]; /* ca */
    /*
      if (incr !=0){
      printf("window size %d\n", (int)snd_cwnd[path_id]);
      }*/
    slow_start[path_id] = 0; /* no vegas ss now */
  }
  if (slr[path_id] > slr_long[path_id] + slr_longstd[path_id]){
    snd_cwnd[path_id] -= slr[path_id]/2;
  }

  if (snd_cwnd[path_id] < initsegs) snd_cwnd[path_id] = initsegs;
  if (snd_cwnd[path_id] > MAX_CWND) snd_cwnd[path_id] = MAX_CWND; // XXX
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
    
  // Check if the blockno is already done
    if( blockno < curr_block ){
      if (debug > 5){
        printf("Coding job request for old block - curr_block %d blockno %d dof_request %d \n\n", curr_block,  blockno, dof_request);
      }
      goto release;
    }


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
      printf("Error: the initially requested dofs are less than the block length - blockno %d dof_request %d block_len %d\n\n\n\n\n",  blockno, dof_request, block_len);
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
    if(feof(snd_file)){
      maxblockno = blockno;
      printf("This is the last block %d\n", maxblockno);
    }    
  }

  file_position = blockno + 1;  // Advance the counter


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
openLog(char* log_name){

  char* file;
  time_t rawtime;
  struct tm* ptm;
  time(&rawtime);
  ptm = localtime(&rawtime);


  //---------- Remake Log and Fig Directories ----------------//

  if(!mkdir("logs", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
    perror("An error occurred while making the logs directory");
  }

  if(!mkdir("figs", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
    perror("An error occurred while making the figs directory");
  }

  char* dir_name = malloc(20);

  sprintf(dir_name, "figs/%d-%02d-%02d",
          ptm->tm_year + 1900,
          ptm->tm_mon + 1,
          ptm->tm_mday);

  if(!mkdir(dir_name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
    perror("An error occurred while making the fig date directory");
  }

  sprintf(dir_name, "logs/%d-%02d-%02d",
          ptm->tm_year + 1900,
          ptm->tm_mon + 1,
          ptm->tm_mday);

  if(!mkdir(dir_name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
    perror("An error occurred while making the log date directory");
  }

  //------------------------------------------------//

  int auto_log = !log_name;

  if(auto_log)
    {
      file = malloc(15);
      log_name = malloc(32);

      sprintf(file, "%02d:%02d.%02d.log",
              ptm->tm_hour,
              ptm->tm_min,
              ptm->tm_sec);

      sprintf(log_name, "%s/%s",
              dir_name,
              file );
    }

  db = fopen(log_name, "w+");

  if(!db){
    perror("An error ocurred while opening the log file");
  }
    

  if(auto_log)
    {
      free(file);
      free(dir_name);
      free(log_name);
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

  // the total size in bytes of the current packet
  int size = PAYLOAD_SIZE 
    + sizeof(double) 
    + sizeof(flag_t) 
    + sizeof(msg.seqno) 
    + sizeof(msg.blockno) 
    + (partial_blk_flg) 
    + sizeof(msg.start_packet) 
    + sizeof(msg.num_packets) 
    + msg.num_packets*sizeof(msg.packet_coeff); 

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


// Compare the IP address and Port of two sockaddr structs

int 
sockaddr_cmp(struct sockaddr* addr1, struct sockaddr* addr2){

  if (addr1->sa_family != addr2->sa_family)    return 1;   // No match
  
  if (addr1->sa_family == AF_INET){
    // IPv4 format
    // Cast to the IPv4 struct
    struct sockaddr_in *tmp1 = (struct sockaddr_in*)addr1;
    struct sockaddr_in *tmp2 = (struct sockaddr_in*)addr2;
  
    if (tmp1->sin_port != tmp2->sin_port) return 1;                // ports don't match
    if (tmp1->sin_addr.s_addr != tmp2->sin_addr.s_addr) return 1;  // Addresses don't match
    
    return 0; // We have a match
  } else if (addr1->sa_family == AF_INET6){
    // IPv6 format
    // Cast to the IPv6 struct
    struct sockaddr_in6 *tmp1 = (struct sockaddr_in6*)addr1;
    struct sockaddr_in6 *tmp2 = (struct sockaddr_in6*)addr2;
  
    if (tmp1->sin6_port != tmp2->sin6_port) return 1;                // ports don't match
    if (memcmp(&tmp1->sin6_addr, &tmp2->sin6_addr, sizeof(struct in6_addr)) != 0) return 1;  // Addresses don't match
    
    return 0; // We have a match
    
  } else {
    printf("Cannot recognize socket address family\n");
    return 1;
  }
 
}


// Initialize the global objects (called only once)
void
initialize(void){
  int i;

  // initialize the thread pool
  thrpool_init( &workers, THREADS );
  
  // Initialize the file mutex and position
  pthread_mutex_init(&file_mutex, NULL);
    
  // Initialize the block mutexes and queue of coded packets and counters
  for(i = 0; i < NUM_BLOCKS; i++){
    pthread_mutex_init( &blocks[i].block_mutex, NULL );
    q_init(&coded_q[i], 2*BLOCK_SIZE);
  }

}


// Initialize all of the global variables except the ones read from config file
void
restart(void){
  // Print to the db file to differentiate traces
  fprintf(stdout, "\n\n*************************************\n****** Starting New Connection ******\n*************************************\n");

  int i,j;

  for(i = 0; i < NUM_BLOCKS; i++){
    dof_remain[i] = 0;
  }

  memset(buff,0,BUFFSIZE);        /* pretouch */


  for (i=0; i < MAX_CONNECT; i++){
    dof_req[i] = BLOCK_SIZE;
    dof_req_latest = BLOCK_SIZE;
    for(j=0; j < MAX_CWND; j++) OnFly[i][j] = 0;

    last_ack_time[i] = 0;
    snd_nxt[i]  = 1;
    snd_una[i]  = 1;
    snd_cwnd[i] = initsegs;
     
    if (multiplier) {
      snd_ssthresh[i] = multiplier*MAX_CWND;
    } else {
      snd_ssthresh[i] = 2147483647;  /* normal TCP, infinite */
    }

    // Statistics // 
    idle[i]       = 0;
    vdelta[i]     = 0;    /* Vegas delta */
    max_delta[i]  = 0;
    slow_start[i] = 1;    /* in vegas slow start */
    vdecr[i]      = 0;
    v0[i]         = 0;    /* vegas decrements or no adjusts */


    minrtt[i]     = 999999.0;
    maxrtt[i]     = 0;
    avrgrtt[i]    = 0;
    srtt[i]       = 0;
    rto[i]        = INIT_RTO;

    slr[i]        = 0;
    slr_long[i]   = SLR_LONG_INIT;
    slr_longstd[i]= 0;
    total_loss[i] = 0;
  }

  //path_id         = 0;     // client identifier
  max_path_id     = 1;     // Total number of paths (clients) at any time



  //--------------------------------------------------------
  done = FALSE;
  curr_block = 1;
  maxblockno = 0;
  file_position = 1; // Initially called to read the first block

  timeouts   = 0;
  ipkts      = 0;
  opkts      = 0;
  badacks    = 0;
  goodacks   = 0;
  enobufs    = 0;
  start_time = 0;
  idle_total = 0;
}



