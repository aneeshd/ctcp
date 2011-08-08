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
ctrlc(srvctcp_sock* sk){
  sk->total_time = getTime() - sk->start_time -2;
  endSession(sk);
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

/*
int
main (int argc, char** argv){

    char *file_name = "Avatar.mov";
    FILE *snd_file; // The file to be sent
    char *version = "$version 0.0$";
    char *configfile = "config/vegas";
    char *port = "9999";  // This is the port that the server is listening to
    int i, c;

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
            //case 'l':
            //log_name   = optarg;
            //break;
        default:
            usage();
        }
    }

    printf("sending %s\n", file_name);
    if ((snd_file = fopen(file_name, "rb"))== NULL){
      perror("Error while trying to create/open a file");
      return 1;
    }

    ////////// open ctcp server //////////////

     srvctcp_sock* sk = open_srvctcp(port);

     if (sk == NULL){
       printf("Could not create CTCP socket\n");
       return 1;
     } else{
       // read from the file and send over ctcp socket

       size_t buf_size = 2000000;
       size_t f_bytes_read, bytes_sent;
       char *file_buff = malloc(buf_size*sizeof(char));
       size_t total_bytes_sent =0;
       size_t total_bytes_read = 0;
       
       while(!feof(snd_file)){
         f_bytes_read = fread(file_buff, 1, buf_size, snd_file);
         total_bytes_read += f_bytes_read;
         //printf("%d bytes read from the file \n", total_bytes_read);
         
         bytes_sent = 0;
         while(bytes_sent < f_bytes_read){
           bytes_sent += send_ctcp(sk, file_buff + bytes_sent, f_bytes_read - bytes_sent);
         }
         total_bytes_sent += bytes_sent;
       }
       
     printf("Total bytes sent %d\n", total_bytes_sent);

     }
     
     sleep(2);

     fclose(snd_file);

     ctrlc(sk);

     return 0;
}
*/


srvctcp_sock*
open_srvctcp(char *port){ 
    int numbytes;
  
    struct addrinfo *result; //This is where the info about the server is stored
    struct addrinfo hints, *servinfo;
    int rv;

    srvctcp_sock* sk =  create_srvctcp_sock();
  
    //signal(SIGINT, ctrlc);

    // Setup the hints struct
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags    = AI_PASSIVE;

    // Get the server's info
    if((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        perror("");
        return NULL;
    }
    // Loop through all the results and connect to the first possible
    for(result = servinfo; result != NULL; result = result->ai_next) {
        if((sk->sockfd = socket(result->ai_family,
                            result->ai_socktype,
                            result->ai_protocol)) == -1){
            perror("Error during socket initialization");
            continue;
        }
        if (bind(sk->sockfd, result->ai_addr, result->ai_addrlen) == -1) {
            close(sk->sockfd);
            perror("Can't bind local address");
            continue;
        }
        break;
    }

    if (result == NULL) { // If we are here, we failed to initialize the socket
        perror("Failed to initialize socket");
        return NULL;
    }

    freeaddrinfo(result);

    int sndbuf     = MSS*MAX_CWND;/* UDP send buff, bigger than mss */
    int rcvbuf     = MSS*MAX_CWND;/* UDP recv buff for ACKs*/

    int i = sizeof(sndbuf);
    //--------------- Setting the UDP socket options -----------------------------//
    setsockopt(sk->sockfd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,i);
    getsockopt(sk->sockfd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,(socklen_t*)&i);
    setsockopt(sk->sockfd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,i);
    getsockopt(sk->sockfd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,(socklen_t*)&i);
    //printf("config: sndbuf %d rcvbuf %d\n",sndbuf,rcvbuf);
    //---------------------------------------------------------------------------//

    //printf("Trying to bind to address %s port %d\n", inet_ntoa(((struct sockaddr_in*) &(result->ai_addr))->sin_addr), ((struct sockaddr_in*)&(result->ai_addr))->sin_port);

    /*------------------------WAIT FOR SYN PACKETS TO COME--------------------------------------------------*/
    printf("Listening for SYN on port %s\n", port);
    return sk;
}

/*
  returns 0 success
  returns -1 error
 */

int
listen_srvctcp(srvctcp_sock* sk){
  struct sockaddr cli_addr;
  socklen_t clilen = sizeof(cli_addr);
  int numbytes, rv;
  char *buff = malloc(BUFFSIZE);
  char* log_name = NULL; // Name of the log
  
  Ack_Pckt *ack = malloc(sizeof(Ack_Pckt));
  memset(buff,0,BUFFSIZE);        /* pretouch */
  
  if((numbytes = recvfrom(sk->sockfd, buff, ACK_SIZE, 0, &cli_addr, &clilen)) == -1){
    perror("recvfrom: Failed to receive the request\n");
    return -1;
  }
    
  unmarshallAck(ack, buff);

  if (ack->flag == SYN){
    printf("Request for a new session: Client address %s Client port %d\n", 
           inet_ntoa(((struct sockaddr_in*) &cli_addr)->sin_addr), 
           ((struct sockaddr_in*)&cli_addr)->sin_port);

    if (sk->debug > 3) openLog(sk, log_name);

    Substream_Path *stream = malloc(sizeof(Substream_Path));
    init_stream(sk, stream);
    // Save the client address as the primary client
    // cli_addr set the main loop...
    stream->cli_addr = cli_addr;
    sk->active_paths[0] = stream;
    sk->num_active++;


    Data_Pckt* msg = dataPacket(0, 0, 0);
    msg->flag = SYN_ACK;
    // Marshall msg into buf
    int message_size = marshallData(*msg, buff);

    if((numbytes = sendto(sk->sockfd, buff, message_size, 0,
                          &cli_addr, clilen)) == -1){
      perror("Could not send the SYN_ACK");
      return -1;
    }

    if(numbytes != message_size){
      perror("write");
      return -1;
    }

    free(buff);
          
    pthread_t daemon_thread;

    rv = pthread_create( &daemon_thread, NULL, server_worker, (void *) sk);
          
    return 0;
  } else{
    printf("Expecting SYN packet, received something else\n");
  }

  free(buff);
  return -1;
}

/*
 * This is contains the main functionality and flow of the client program
 */
size_t
send_ctcp(srvctcp_sock *sk, const void *usr_buf, size_t usr_buf_len){

  //printf("Calling send ctcp curr block %d maxblockno %d maxblockno.len %d\n", sk->curr_block, sk->maxblockno,  sk->blocks[sk->maxblockno%NUM_BLOCKS].len);
  if (usr_buf_len == 0){
    return 0;
  }

  size_t bytes_read;
  uint32_t bytes_left = usr_buf_len;
  int block_len_tmp;

  int i = sk->maxblockno;
  while (bytes_left > 0){
    pthread_mutex_lock(&(sk->blocks[i%NUM_BLOCKS].block_mutex));
    i = MAX(i, sk->maxblockno);

    if (i == sk->curr_block + NUM_BLOCKS){
      printf("waiting on block free %d\n", sk->curr_block);
      pthread_cond_wait( &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_free_condv), &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex));
    }

    pthread_rwlock_wrlock(&(sk->blocks[i%NUM_BLOCKS].block_rwlock));

    block_len_tmp = sk->blocks[i%NUM_BLOCKS].len;   // keep the block len before reading
    if(i < sk->curr_block){
      printf("**ERROR** reading block %d, currblock %d\n", i, sk->curr_block);
    }
    bytes_read = readBlock(&(sk->blocks[i%NUM_BLOCKS]), usr_buf+usr_buf_len-bytes_left, bytes_left);
    bytes_left -= bytes_read;
    //printf("bytes_read %d bytes_left %d maxblockno %d blockno %d\n", bytes_read, bytes_left,  sk->maxblockno, i);


    if (bytes_read > 0) {
      coding_job_t* job = malloc(sizeof(coding_job_t));
      job->socket = sk;
      job->blockno = i;
      job->start = block_len_tmp;
      job->dof_request = sk->blocks[i%NUM_BLOCKS].len - block_len_tmp;
      job->coding_wnd = 0;
      sk->dof_remain[i%NUM_BLOCKS] += job->dof_request;  // Update the internal dof counter
      //printf("send_ctcp adding job: blockno %d, dofrequested %d \n", i, job->dof_request);
      addJob(&(sk->workers), &coding_job, job, &free, LOW);

      sk->maxblockno = i;
      if (i == sk->curr_block){
        sk->dof_req_latest += job->dof_request;
      }
    }

    pthread_rwlock_unlock(&(sk->blocks[i%NUM_BLOCKS].block_rwlock));
    pthread_cond_signal( &(sk->blocks[i%NUM_BLOCKS].block_ready_condv));
    pthread_mutex_unlock(&(sk->blocks[i%NUM_BLOCKS].block_mutex));

    //printf("Total bytes_read %d bytes_left %d maxblockno %d currblock %d\n", usr_buf_len - bytes_left, bytes_left,  sk->maxblockno, sk->curr_block);
    i++;
  }
  /*
  i = sk->curr_block;
  while (i <= sk->maxblockno){
    pthread_mutex_lock(&(sk->blocks[i%NUM_BLOCKS].block_mutex));
    pthread_cond_wait( &(sk->blocks[i%NUM_BLOCKS].block_free_condv), &(sk->blocks[i%NUM_BLOCKS].block_mutex));
    pthread_mutex_unlock(&(sk->blocks[i%NUM_BLOCKS].block_mutex));
    i++;
  }
  */

  return usr_buf_len - bytes_left;

}


void 
*server_worker(void *arg){

  srvctcp_sock* sk = (srvctcp_sock*) arg;

  char *buff = malloc(BUFFSIZE);
  int numbytes;
  
  struct sockaddr cli_addr;
  socklen_t clilen = sizeof cli_addr;

  int i, r;
  double idle_timer;

  int path_index=0;              // Connection identifier


  // printf("Time %f Read from %d to %d\n \n ", getTime(), sk->curr_block, sk->maxblockno);
    
  /* ----- DONE READING THE BUFFER INTO BLOCKS ------- */


  int done = 0;
  memset(buff,0,BUFFSIZE);        /* pretouch */
  sk->start_time = getTime();

  pthread_rwlock_rdlock(&(sk->blocks[sk->curr_block%NUM_BLOCKS].block_rwlock));
  sk->dof_req_latest = sk->blocks[sk->curr_block%NUM_BLOCKS].len ;     // reset the dof counter for the current block
  pthread_rwlock_unlock(&(sk->blocks[sk->curr_block%NUM_BLOCKS].block_rwlock));


  /* send out initial segments, then go for it */  // First send_seg
  send_segs(sk, path_index);

  Ack_Pckt *ack = malloc(sizeof(Ack_Pckt));

  while(done == 0){

    double rto_max = 0;
    for (i=0; i < sk->num_active; i++){
      if (sk->active_paths[i]->rto > rto_max) rto_max = sk->active_paths[i]->rto;
    }

    idle_timer = getTime();


    r = timedread(sk->sockfd, rto_max + RTO_BIAS);

    if (r > 0){  /* ack ready */

      // The recvfrom should be done to a separate buffer (not buff)
      if ( (r = recvfrom(sk->sockfd, buff, ACK_SIZE, 0, &cli_addr, &clilen)) <= 0){
        perror("Error in receveing ACKs\n");
        continue;  
      }

      sk->idle_total += getTime() - idle_timer;

      unmarshallAck(ack, buff);

      if (sk->debug > 6){
        printf("Got an ACK: ackno %d blockno %d dof_rec %d -- RTT est %f \n",
               ack->ackno,
               ack->blockno,
               ack->dof_rec,
               getTime()-ack->tstamp);
      }

      /* ---- Decide if the ack is a request for new connections ------- */
      /* -------- Check which path the ack belong to --------------------*/
      if (ack->flag == SYN){
        if (sk->num_active < MAX_CONNECT){

          sk->active_paths[sk->num_active] = malloc(sizeof(Substream_Path));
          init_stream(sk, sk->active_paths[sk->num_active]);
          // add the client address info to the client lookup table
          sk->active_paths[sk->num_active]->cli_addr = cli_addr;
          sk->active_paths[sk->num_active]->last_ack_time = getTime();

          sk->num_active++;

          printf("Request for a new path: Client address %s Client port %d\n", inet_ntoa(((struct sockaddr_in*) &cli_addr)->sin_addr), ((struct sockaddr_in*)&cli_addr)->sin_port);

          // Initially send a few packets to keep it going
          send_segs(sk,sk->num_active -1);
          continue;        // Go back to the beginning of the while loop
        }
      }else{
        // Use the cli_addr to find the right path_id for this Ack
        // Start searching through other possibilities
        while (sockaddr_cmp(&cli_addr, &(sk->active_paths[path_index]->cli_addr)) != 0){
          path_index = (path_index + 1)%(sk->num_active);
        }
      }
      /*-----------------------------------------------------------------*/

      double rcvt = getTime();

      if (ack->flag == NORMAL) {
        sk->ipkts++;
        sk->active_paths[path_index]->last_ack_time = rcvt;

        if((done = handle_ack(sk, ack, path_index)) == 0){
          send_segs(sk, path_index);
        }
      }

      // Check all the other paths, and see if any of them timed-out.
      for (i = 1; i < sk->num_active; i++){
        path_index = (path_index+i)%(sk->num_active);
        if(rcvt - sk->active_paths[path_index]->last_ack_time > sk->active_paths[path_index]->rto + RTO_BIAS){
          if (timeout(sk, path_index)==TRUE){
            // Path timed out, but still alive
            send_segs(sk, path_index);
          }else{
            // Path is dead and is being removed
            removePath(sk, path_index);
            sk->num_active--;
            path_index--;
          }
        }
      }

    } else if (r < 0) {
      err_sys(sk,"select");
    } else if (r==0) {
      for (i = 0; i < sk->num_active; i++){
        if (timeout(sk, i)==TRUE){
          // Path timed out, but still alive
          send_segs(sk, i);
        }else{
          // Path is dead and is being removed
          removePath(sk, i);
          sk->num_active--;
          i--;
        }
      }
    }
    if(sk->num_active==0){
      // no path alive, terminate
      endSession(sk);
      return 0;
    }
  }  /* while more pkts */


  free(ack);

  //      send_FIN_CLI(sk);


  /*
  for(i=1; i<num_active; i++){
    free(active_paths[i]);
  }
  free(active_paths);
  */  

  return NULL;
}

void
removePath(srvctcp_sock* sk, int dead_index){

  free(sk->active_paths[dead_index]);
  int i;
  for(i = dead_index; i < sk->num_active; i++){
    sk->active_paths[i] = sk->active_paths[i+1];
  }
  sk->active_paths[sk->num_active-1] = NULL;
}

/*
  Returns FALSE if the path is dead
  Returns TRUE if the path is still potentially alive
 */

int 
timeout(srvctcp_sock* sk, int pin){
  Substream_Path *subpath = sk->active_paths[pin];
        /* see if a packet has timedout */
  if (subpath->idle > sk->maxidle) {
    /* give up */
    printf("*** idle abort *** on path \n");
    // removing the path from connections
    return FALSE;
  }

  if (sk->debug > 1){
    fprintf(stderr,
            "timerxmit %6.2f \t on %s:%d \t blockno %d blocklen %d pkt %d  snd_nxt %d  snd_cwnd %d  \n",
            getTime()-sk->start_time,
            inet_ntoa(((struct sockaddr_in*) &subpath->cli_addr)->sin_addr),
            ((struct sockaddr_in*) &subpath->cli_addr)->sin_port,
            sk->curr_block,
            sk->blocks[sk->curr_block%NUM_BLOCKS].len,
            subpath->snd_una,
            subpath->snd_nxt,
            (int)subpath->snd_cwnd);
  }

  // THIS IS A GLOBAL COUNTER FOR STATISTICS ONLY.
  sk->timeouts++;

  subpath->idle++;
  subpath->slr = subpath->slr_long;
  //slr_long[path_id] = SLR_LONG_INIT;
  subpath->rto = 2*subpath->rto; // Exponential back-off

  subpath->snd_ssthresh = subpath->snd_cwnd*sk->multiplier; /* shrink */

  if (subpath->snd_ssthresh < sk->initsegs) {
    subpath->snd_ssthresh = sk->initsegs;
  }
  subpath->slow_start = 1;
  subpath->snd_cwnd = sk->initsegs;  /* drop window */
  subpath->snd_una = subpath->snd_nxt;

  return TRUE;
  // Update the path_id so that we timeout based on another path, and try every path in a round
  // Avoids getting stuck if the current path dies and packets/acks on other paths are lost

}

int
send_FIN_CLI(srvctcp_sock* sk){

  char *buff = malloc(BUFFSIZE);
  int numbytes;
  // FIN_CLI sequence number is meaningless
  Data_Pckt *msg = dataPacket(0, sk->curr_block, 0);
  msg->tstamp = getTime();
  // FIN_CLI
  msg->flag = FIN;

  int size = marshallData(*msg, buff);

  do{
    // THE CLI_ADDR HERE IS SET IN THE MAIN LOOP
    socklen_t clilen = sizeof(sk->active_paths[0]->cli_addr);
    if((numbytes = sendto(sk->sockfd, buff, size, 0, &(sk->active_paths[0]->cli_addr), clilen)) == -1){
      perror("send_Fin_Cli error: sendto");
      return -1;
    }
  } while(errno == ENOBUFS && ++(sk->enobufs)); // use the while to increment enobufs if the condition is met

  if(numbytes != size){
    perror("send_Fin_Cli error: sent fewer bytes");
    return -1;
  }
  
  //free(msg->payload);
  free(msg);
  free(buff);
  return 0;
}

void
send_segs(srvctcp_sock* sk, int pin){

  if (sk->dof_req_latest == 0){
    //printf("waiting on block ready %d\n", sk->curr_block);
    
    pthread_mutex_lock(   &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex) );
    pthread_cond_signal(  &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_free_condv));
    pthread_cond_wait(    &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_ready_condv), &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex));
    pthread_mutex_unlock( &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex) );
    
    //printf("Returned from block ready %d\n", sk->curr_block);

  }


  Substream_Path* subpath = sk->active_paths[pin];

  int win = 0;
  win = subpath->snd_cwnd - (subpath->snd_nxt - subpath->snd_una);

  if (win < 1) return;  /* no available window => done */

  int CurrWin;
  int dof_needed;
  int dof_request_tmp;

  double mean_rate = 0;
  double mean_latency = 0;
  double mean_OnFly = 0;

  double delay_diff_tmp;
  double p, t0;
  
  double current_time = getTime();

  int CurrOnFly[MAX_CONNECT];
  double d[MAX_CONNECT];    // delay vector

  int j,k;
  for (j = 0; j < sk->num_active; j++){
    CurrOnFly[j] = 0;
    for(k = sk->active_paths[j]->snd_una; k < sk->active_paths[j]->snd_nxt; k++){
      CurrOnFly[j] += (sk->active_paths[j]->OnFly[k%MAX_CWND] == sk->curr_block) & (sk->active_paths[j]->tx_time[k%MAX_CWND] >= current_time - (1.5*sk->active_paths[j]->srtt + RTO_BIAS));
    }

    d[j] = sk->active_paths[j]->srtt/2.0;
  }

  //////////////////////////  START CHECKING EACH BLOCK   //////////////////////////

  int blockno = sk->curr_block;
  while ( (win > 0) && (blockno < sk->curr_block + NUM_BLOCKS) && (!sk->maxblockno || blockno <= sk->maxblockno) ){

    if (blockno == sk->curr_block){
      dof_request_tmp = sk->dof_req_latest;
    }else{

      pthread_rwlock_rdlock(&(sk->blocks[blockno%NUM_BLOCKS].block_rwlock));
      dof_request_tmp = sk->blocks[blockno%NUM_BLOCKS].len;
      pthread_rwlock_unlock(&(sk->blocks[blockno%NUM_BLOCKS].block_rwlock));

      
      for (j = 0; j < sk->num_active; j++){
        CurrOnFly[j] = sk->active_paths[j]->packets_sent[blockno%NUM_BLOCKS];
      }
    }


    mean_rate = 0;
    mean_latency = 0;
    mean_OnFly = 0;

    for (j = 0; j < sk->num_active; j++){

      // Compensate for server's over estimation of the loss rate caused by lost acks
      p = sk->active_paths[j]->slr/(2.0 - sk->active_paths[j]->slr);

      delay_diff_tmp = subpath->srtt/2.0 - d[j];
      if ((delay_diff_tmp > 0) && (d[j] > 0)){
        mean_rate    += (1-p)*sk->active_paths[j]->snd_cwnd/(2*d[j]);
        mean_latency += (1-p)*sk->active_paths[j]->snd_cwnd/(2.0);
      }
      mean_OnFly += (1-p)*(CurrOnFly[j]);
    }

    p = subpath->slr/(2.0 - subpath->slr);

    // The total number of dofs the we think we should be sending (for the current block) from now on
    dof_needed = 0;
    while ( (dof_needed < win) &&  ( (dof_needed)*(1-p) + (mean_rate*subpath->srtt/2.0 - mean_latency + mean_OnFly)  < dof_request_tmp) ) {
      dof_needed++;
    }

    if (dof_needed == 0){
      
      if (mean_rate > 0){
        t0 = (dof_request_tmp - mean_OnFly + mean_latency)/(mean_rate);
        
        if (t0 > subpath->srtt/2.0){
          printf("current path delay %f  t0 %f \n", subpath->srtt/2.0, t0);
        }
        
      }

      for (j = 0; j < sk->num_active; j++){
        if (d[j] < subpath->srtt/2.0){
          d[j] = t0;
        }
      }


      if (sk->debug > 6 && sk->num_active == 2){
        printf("path_id %d blockno %d mean OnFly %f dof_request tmp %d win %d CurrOnFly[0] %d CurrOnFly[1] %d srtt[0] %f srtt[1] %f\n", pin, blockno, mean_OnFly, dof_request_tmp, win, CurrOnFly[0], CurrOnFly[1], sk->active_paths[0]->srtt*1000, sk->active_paths[1]->srtt*1000);
      }

    }

    //printf("Time %f win %d curr_block %d  block no %d dof_needed %d meanOnFly %f dof_req %d \n", getTime(), win, sk->curr_block, blockno, dof_needed, mean_OnFly, dof_request_tmp);

    CurrWin = dof_needed;
    win = win - CurrWin;

      //printf("Currenet Block %d win %d  dof_needed %d dof_req_latest %d CurrOnFly[%d] %d mean Onfly %f t0 %f\n", blockno, win, dof_needed, dof_request_tmp, pin, CurrOnFly[pin], mean_OnFly, t0);  

    // Check whether we have enough coded packets for current block
    if (sk->dof_remain[blockno%NUM_BLOCKS] < dof_needed){
      /*
        printf("requesting more dofs: curr path_id %d curr block %d,  dof_remain %d, dof_needed %d dof_req_latest %d\n",
        path_id, blockno, dof_remain[blockno%NUM_BLOCKS], dof_needed, dof_req_latest);
      */
      coding_job_t* job = malloc(sizeof(coding_job_t));
      job->socket = sk;
      job->blockno = blockno;
      job->start = 0;
      job->dof_request = MIN_DOF_REQUEST + dof_needed - sk->dof_remain[blockno%NUM_BLOCKS];
      sk->dof_remain[blockno%NUM_BLOCKS] += job->dof_request; // Update the internal dof counter
      job->coding_wnd = MAX_CODING_WND;

      addJob(&(sk->workers), &coding_job, job, &free, HIGH);
    }

    while (CurrWin>=1) {
      send_one(sk, blockno, pin);
      subpath->snd_nxt++;
      CurrWin--;
      sk->dof_remain[blockno%NUM_BLOCKS]--;   // Update the internal dof counter
      subpath->packets_sent[blockno%NUM_BLOCKS]++;
   }

  blockno++;

  } //////////////////////// END CHECKING BLOCKS ////////////////////

}


void
send_one(srvctcp_sock* sk, uint32_t blockno, int pin){

  char *buff = malloc(BUFFSIZE);
  int numbytes;

  // Send coded packet from block number blockno
  Substream_Path *subpath = sk->active_paths[pin];

  if (sk->debug > 6){
    fprintf(stdout, "\n block %d DOF left %d q size %d\n",
            blockno,
            sk->dof_remain[blockno%NUM_BLOCKS],
            sk->coded_q[blockno%NUM_BLOCKS].size);
  }

  // Get a coded packet from the queue
  // q_pop is blocking. If the queue is empty, we wait until the coded packets are created
  // We should decide in send_segs whether we need more coded packets in the queue
  Data_Pckt *msg = (Data_Pckt*) q_pop(&(sk->coded_q[blockno%NUM_BLOCKS]));

  // Correct the header information of the outgoing message
  msg->seqno = subpath->snd_nxt;
  msg->tstamp = getTime();

  fprintf(sk->db,"%f %d xmt%d\n",
          getTime()-sk->start_time,
          blockno-sk->curr_block,
          pin);

  if (sk->debug > 6){
    printf("Sending... on blockno %d blocklen %d  seqno %d  snd_una %d snd_nxt %d  start pkt %d snd_cwnd %d   port %d \n",
           blockno,
           sk->blocks[sk->curr_block%NUM_BLOCKS].len,
           msg->seqno,
           subpath->snd_una,
           subpath->snd_nxt,
           msg->start_packet,
           (int)subpath->snd_cwnd,
           ((struct sockaddr_in*)&subpath->cli_addr)->sin_port);
  }

  // Marshall msg into buf
  int message_size = marshallData(*msg, buff);
  socklen_t clilen = sizeof(subpath->cli_addr);

  do{
    if((numbytes = sendto(sk->sockfd, buff, message_size, 0,
                          &subpath->cli_addr, clilen)) == -1){
      printf("Sending... on blockno %d blocklen %d  seqno %d  snd_una %d snd_nxt %d  start pkt %d snd_cwnd %d   port %d \n",
             blockno,
             sk->blocks[sk->curr_block%NUM_BLOCKS].len,
             msg->seqno,
             subpath->snd_una,
             subpath->snd_nxt,
             msg->start_packet,
             (int)subpath->snd_cwnd,
             ((struct sockaddr_in*)&subpath->cli_addr)->sin_port  );
      err_sys(sk,"Error: sendto");
    }
  } while(errno == ENOBUFS && ++(sk->enobufs)); // use the while to increment enobufs if the condition is met


  if(numbytes != message_size){
    err_sys(sk,"write");
  }

  // Update the packets on the fly
  subpath->OnFly[subpath->snd_nxt%MAX_CWND] = blockno;
  subpath->tx_time[subpath->snd_nxt%MAX_CWND] = msg->tstamp;
  //printf("Freeing the message - blockno %d snd_nxt[path_id] %d ....", blockno, snd_nxt[path_id]);
  sk->opkts++;
  free(msg->packet_coeff);
  if (msg->num_packets > 1){
    free(msg->payload);
  }
  free(msg);
  free(buff);
  //printf("---------- Done Freeing the message\n-------------");
}

void
endSession(srvctcp_sock* sk){
  char myname[128];
  char* host = "Host"; // TODO: extract this from the packet

  gethostname(myname,sizeof(myname));
  printf("\n\n%s => %s for %f secs\n",
         myname,host, sk->total_time);

  int i;
  for (i=0; i < sk->num_active; i++){
    printf("******* Priniting Statistics for path %d -- %s : %d ********\n",i,
           inet_ntoa(((struct sockaddr_in*) &(sk->active_paths[i]->cli_addr))->sin_addr),
           ((struct sockaddr_in*)&(sk->active_paths[i]->cli_addr))->sin_port);
    printf("**THRU** %f Mbs\n",
           8.e-6*(sk->active_paths[i]->snd_una*PAYLOAD_SIZE)/sk->total_time);
    printf("**LOSS* %6.3f%% \n",
           100.*sk->active_paths[i]->total_loss/sk->active_paths[i]->snd_una);
    if (sk->ipkts) sk->active_paths[i]->avrgrtt /= sk->ipkts;
    printf("**RTT** minrtt  %f maxrtt %f avrgrtt %f\n",
           sk->active_paths[i]->minrtt, sk->active_paths[i]->maxrtt,sk->active_paths[i]->avrgrtt);
    printf("**RTT** rto %f  srtt %f \n", sk->active_paths[i]->rto, sk->active_paths[i]->srtt);
    printf("**VEGAS** max_delta %f vdecr %d v0 %d vdelta %f\n",
           sk->active_paths[i]->max_delta, sk->active_paths[i]->vdecr, sk->active_paths[i]->v0, sk->active_paths[i]->vdelta);
    printf("**CWND** snd_nxt %d snd_cwnd %5.3f  snd_una %d ssthresh %d goodacks %d\n\n",
           sk->active_paths[i]->snd_nxt, sk->active_paths[i]->snd_cwnd, sk->active_paths[i]->snd_una,
           sk->active_paths[i]->snd_ssthresh, sk->goodacks);
  }

  printf("Total time %f Total idle time %f, Total timeouts %d\n", sk->total_time, sk->idle_total, sk->timeouts);
  printf("Total packets in: %d, out: %d, enobufs %d\n", sk->ipkts, sk->opkts, sk->enobufs);

  if(sk->db)       fclose(sk->db);

  sk->db       = NULL;
}

/*
  Returns 1 if subpath sp ready to send more
  Returns 0 if subpath sp is not ready to send (bad ack or done)
 */
int
handle_ack(srvctcp_sock* sk, Ack_Pckt *ack, int pin){
  Substream_Path *subpath = sk->active_paths[pin];
  int j;

  uint32_t ackno = ack->ackno;

  if (sk->debug > 8 )printf("ack rcvd %d\n", ackno);

  //------------- RTT calculations --------------------------//
  double rtt;
  rtt = subpath->last_ack_time - ack->tstamp; // this calculates the rtt for this coded packet
  if (rtt < subpath->minrtt) subpath->minrtt = rtt;
  if (rtt > subpath->maxrtt) subpath->maxrtt = rtt;
  subpath->avrgrtt += rtt;
  subpath->srtt = (1-g)*subpath->srtt + g*rtt;
  if (rtt > subpath->rto/beta){
    subpath->rto = (1-g)*subpath->rto + g*beta*rtt;
  }else {
    subpath->rto = (1-g/5)*subpath->rto + g/5*beta*rtt;
  }
  if (ackno > subpath->snd_una){
    subpath->vdelta = 1-subpath->srtt/rtt;
    // max_delta: only for statistics
    if (subpath->vdelta > subpath->max_delta) subpath->max_delta = subpath->vdelta;  /* vegas delta */
  }
  if (sk->debug > 3) {
    fprintf(sk->db,"%f %d %f  %d %d ack%d\n",
            subpath->last_ack_time, // - sk->start_time,
            ackno,
            rtt,
            (int)subpath->snd_cwnd,
            subpath->snd_ssthresh,
            pin);
  }
  //------------- RTT calculations --------------------------//


  while (ack->blockno > sk->curr_block){
    // Moving on to a new block

    //printf("waiting on block mutex to free block %d\n", sk->curr_block);

    pthread_mutex_lock(&(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex));
    pthread_rwlock_wrlock(&(sk->blocks[sk->curr_block%NUM_BLOCKS].block_rwlock));


    freeBlock(&(sk->blocks[sk->curr_block%NUM_BLOCKS]));
    
    for (j = 0; j < sk->num_active; j++){
      sk->active_paths[j]->packets_sent[sk->curr_block%NUM_BLOCKS] = 0;
      sk->active_paths[j]->OnFly[sk->curr_block%NUM_BLOCKS] = 0;
      sk->active_paths[j]->tx_time[sk->curr_block%NUM_BLOCKS] = 0;
    }

    sk->dof_remain[sk->curr_block%NUM_BLOCKS] = 0;

    q_free(&(sk->coded_q[sk->curr_block%NUM_BLOCKS]), &free_coded_pkt);

    sk->curr_block++;            // Update the current block identifier
    sk->maxblockno = MAX(sk->curr_block, sk->maxblockno);

    sk->dof_req_latest =  sk->blocks[sk->curr_block%NUM_BLOCKS].len - ack->dof_rec;     // reset the dof counter for the current block

    pthread_rwlock_unlock(&(sk->blocks[(sk->curr_block-1)%NUM_BLOCKS].block_rwlock));
    pthread_cond_signal( &(sk->blocks[(sk->curr_block-1)%NUM_BLOCKS].block_free_condv));
    pthread_mutex_unlock(&(sk->blocks[(sk->curr_block-1)%NUM_BLOCKS].block_mutex));



     
    for (j =0; j < sk->num_active; j++){
      sk->active_paths[j]->packets_sent[(sk->curr_block-1)%NUM_BLOCKS]=0;
    }

    if (sk->debug > 5 && sk->curr_block%10==0){
      printf("Now sending block %d, cwnd %f, SLR %f%%, SRTT %f ms \n",
             sk->curr_block, subpath->snd_cwnd, 100*subpath->slr, subpath->srtt*1000);
    }
  }

  if (ackno > subpath->snd_nxt || ack->blockno > sk->curr_block) {
    /* bad ack */
    if (sk->debug > 4) fprintf(stderr,

                           "Bad ack: curr block %d badack no %d snd_nxt %d snd_una %d cli.port %d\n\n",
                           sk->curr_block,
                           ackno,
                           subpath->snd_nxt,
                           subpath->snd_una,
                           ((struct sockaddr_in*)&subpath->cli_addr)->sin_port);

    sk->badacks++;
    if(subpath->snd_una < ackno) subpath->snd_una = ackno;

  } else {
    // Late or Good acks count towards goodput


    fprintf(sk->db,"%f %d %f %d %f %f %f %f %f xmt\n",
            getTime()-sk->start_time,
            ack->blockno,
            subpath->snd_cwnd,
            subpath->snd_ssthresh,
            subpath->slr,
            subpath->slr_long,
            subpath->srtt,
            subpath->rto,
            rtt);

    subpath->idle = 0; // Late or good acks should stop the "idle" count for max-idle abort.

    if (ackno <= subpath->snd_una){
      //late ack
      if (sk->debug > 6) fprintf(stderr,
                             "Late ack path %d: curr block %d ack-blockno %d badack no %d snd_nxt %d snd_una %d\n",
                             pin, sk->curr_block, ack->blockno, ackno, subpath->snd_nxt, subpath->snd_una);
    } else {
      sk->goodacks++;
      int losses = ackno - (subpath->snd_una +1);
      /*
        if (losses > 0){
        printf("Loss report curr block %d ackno - snd_una[path_id] %d\n", curr_block, ackno - snd_una[path_id]);
        }
      */
      subpath->total_loss += losses;

      double loss_tmp =  pow(1-slr_mem, losses);
      subpath->slr = loss_tmp*(1-slr_mem)*subpath->slr + (1 - loss_tmp);
      loss_tmp =  pow(1-slr_longmem, losses);
      subpath->slr_long = loss_tmp*(1-slr_longmem)*subpath->slr_long + (1 - loss_tmp);

      // NECESSARY CONDITION: slr_longmem must be smaller than 1/2.
      subpath->slr_longstd = (1-slr_longmem)*subpath->slr_longstd
        + slr_longmem*(fabs(subpath->slr - subpath->slr_long) - subpath->slr_longstd);
      subpath->snd_una = ackno;
    }
    // Updated the requested dofs for the current block
    // The MIN is to avoid outdated infromation by out of order ACKs or ACKs on different paths

	if (ack->blockno == sk->curr_block){
    pthread_rwlock_rdlock( &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_rwlock) );
    sk->dof_req_latest = MIN(sk->dof_req_latest,  sk->blocks[sk->curr_block%NUM_BLOCKS].len - ack->dof_rec);
    pthread_rwlock_unlock( &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_rwlock) );
    
	}
  advance_cwnd(sk, pin);

    return 0;
  } // end else goodack

  return -1;
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
err_sys(srvctcp_sock* sk, char* s){
  perror(s);
  endSession(sk);
  exit(1);
}

void
  readConfig(char* configfile, srvctcp_sock* sk){
  // Initialize the default values of config variables

  sk->rcvrwin    = 20;          /* rcvr window in mss-segments */
  sk->increment  = 1;           /* cc increment */
  sk->multiplier = 0.5;         /* cc backoff  &  fraction of rcvwind for initial ssthresh*/
  sk->initsegs   = 2;          /* slowstart initial */
  sk->ssincr     = 1;           /* slow start increment */
  sk->maxidle    = 10;          /* max idle before abort */
  sk->valpha     = 0.05;        /* vegas parameter */
  sk->vbeta      = 0.2;         /* vegas parameter */
  sk->debug      = 5           ;/* Debug level */

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
    else if (strcmp(var,"rcvrwin")==0) sk->rcvrwin = val;
    else if (strcmp(var,"increment")==0) sk->increment = val;
    else if (strcmp(var,"multiplier")==0) sk->multiplier = val;
    else if (strcmp(var,"initsegs")==0) sk->initsegs = val;
    else if (strcmp(var,"ssincr")==0) sk->ssincr = val;
    else if (strcmp(var,"maxidle")==0) sk->maxidle = val;
    else if (strcmp(var,"valpha")==0) sk->valpha = val;
    else if (strcmp(var,"vbeta")==0) sk->vbeta = val;
    else if (strcmp(var,"debug")==0) sk->debug = val;
    else printf("config unknown: %s\n",line);
  }

  t = time(NULL);
  printf("*** CTCP  ***\n");
  printf("config: debug %d, %s",sk->debug, ctime(&t));
  printf("config: rcvrwin %d  increment %d  multiplier %f\n",
         sk->rcvrwin,sk->increment,sk->multiplier);
  printf("config: alpha %f beta %f\n", sk->valpha,sk->vbeta);

}


void
advance_cwnd(srvctcp_sock* sk, int pin){
  /* advance cwnd according to slow-start of congestion avoidance */
  Substream_Path *subpath = sk->active_paths[pin];
  if (subpath->snd_cwnd <= subpath->snd_ssthresh && subpath->slow_start) {
    /* slow start, expo growth */
    subpath->snd_cwnd = subpath->snd_cwnd + sk->ssincr;
    return;
  } else{
    /* congestion avoidance phase */
    int incr;
    incr = sk->increment;
    /*
      Range --(1)-- valpha --(2)-- vbeta --(3)--
      (1): increase window
      (2): stay
      (3): decrease window
    */
    if (subpath->vdelta > sk->vbeta ){
      if (sk->debug > 6){
        printf("vdelta %f going down from %f \n", subpath->vdelta, subpath->snd_cwnd);
      }
      incr= -sk->increment; /* too fast, -incr /RTT */
      subpath->vdecr++;
    } else if (subpath->vdelta > sk->valpha) {
      if (sk->debug > 6){
        printf("vdelta %f staying at %f\n", subpath->vdelta, subpath->snd_cwnd);
      }
      incr =0; /* just right */
      subpath->v0++;
    }
    subpath->snd_cwnd += incr/subpath->snd_cwnd; /* ca */
    subpath->slow_start = 0;
  }
  if (subpath->slr > subpath->slr_long + subpath->slr_longstd){
    subpath->snd_cwnd -= subpath->slr/2;
  }
  if (subpath->snd_cwnd < sk->initsegs) subpath->snd_cwnd = sk->initsegs;
  if (subpath->snd_cwnd > MAX_CWND) subpath->snd_cwnd = MAX_CWND;
  return;
}

//---------------WORKER FUNCTION ----------------------------------
void*
coding_job(void *a){
  coding_job_t* job = (coding_job_t*) a;
  //printf("Job processed by thread %lu: blockno %d dof %d\n", pthread_self(), job->blockno, job->dof_request);

  uint32_t blockno = job->blockno;
  int start = job->start;
  int dof_request = job->dof_request;
  int coding_wnd = job->coding_wnd;
  srvctcp_sock* sk = job->socket;

  // Check if the blockno is already done
 
  if( blockno < sk->curr_block ){
      if (sk->debug > 5){
        printf("Coding job request for old block - curr_block %d blockno %d dof_request %d \n\n", sk->curr_block,  blockno, dof_request);
      }
      return NULL;
  }
  

  pthread_mutex_lock(&(sk->blocks[blockno%NUM_BLOCKS].block_mutex));

  // Check whether the requested blockno is already read, if not, read it from the file
  // generate the first set of degrees of freedom according toa  random permutation

  uint8_t block_len = sk->blocks[blockno%NUM_BLOCKS].len;
  
  if (block_len  == 0){
    printf("Error: Block %d not read yet\n", blockno);

    pthread_mutex_unlock(&(sk->blocks[blockno%NUM_BLOCKS].block_mutex));
    return NULL;
  } 
  
  ////////////////////////////// UNCODED PACKETIZATION REQUEST ///////////////////////

  if (coding_wnd == 0){

    coding_wnd = 1;
  
    /*
    if (dof_request < block_len){
      printf("Error: the initially requested dofs are less than the block length - blockno %d dof_request %d block_len %d\n\n\n\n\n",  blockno, dof_request, block_len);
    }
    */

    // Generate random combination by picking rows based on order
    int row;
    int end = MIN((start + dof_request), block_len);
    for (row = start; row < end; row++){

      //      Data_Pckt *msg = dataPacket(0, blockno, num_packets);

      // creat a new data packet 
      Data_Pckt* msg    = (Data_Pckt*) malloc(sizeof(Data_Pckt));
      msg->flag         = NORMAL;
      msg->blockno      = blockno;
      msg->num_packets  = 1;
      msg->packet_coeff = (uint8_t*) malloc(sizeof(uint8_t));

      msg->start_packet = row;
      msg->packet_coeff[0] = 1;

      msg->payload = sk->blocks[blockno%NUM_BLOCKS].content[msg->start_packet];

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
      q_push_back(&(sk->coded_q[blockno%NUM_BLOCKS]), msg);
    }  // Done with forming the initial set of coded packets
    dof_request = MAX(0, dof_request - (block_len - start));  // This many more to go
  }

  ///////////////////// ACTUAL RANDOM LINEAR CODING ///////////////////

  if (dof_request > 0){
    // Extra degrees of freedom are generated by picking a row randomly

    //fprintf(stdout, "coding job for %d coded packets \n", dof_request);

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
      memcpy(msg->payload, sk->blocks[blockno%NUM_BLOCKS].content[msg->start_packet], PAYLOAD_SIZE);

      for(i = 1; i < num_packets; i++){
        msg->packet_coeff[i] = (uint8_t)(1 + random()%255);

        for(j = 0; j < PAYLOAD_SIZE; j++){
          msg->payload[j] ^= FFmult(msg->packet_coeff[i], sk->blocks[blockno%NUM_BLOCKS].content[msg->start_packet+i][j]);
        }
      }

      q_push_back(&(sk->coded_q[blockno%NUM_BLOCKS]), msg);

    }  // Done with forming the remaining set of coded packets

  }

  //pthread_mutex_lock(&coded_q[blockno%NUM_BLOCKS].q_mutex_);
  //printf("Almost done with block %d - q size %d\n", blockno, coded_q[blockno%NUM_BLOCKS].size);
  //pthread_mutex_unlock(&coded_q[blockno%NUM_BLOCKS].q_mutex_);


  pthread_mutex_unlock(&(sk->blocks[blockno%NUM_BLOCKS].block_mutex));

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
  if (msg->num_packets > 1){
    free(msg->payload);
  }
  free(msg);
}

//--------------------------------------------------------------------
uint32_t
readBlock(Block_t* blk, const void *buf, size_t buf_len){

  // starting from buf, read up to buf_len bytes into block #blockno
  // If the block is already full, do nothing
  uint16_t bytes_read; 
  uint32_t bytes_left = buf_len; 
  while(blk->len < BLOCK_SIZE && bytes_left){
    char* tmp = malloc(PAYLOAD_SIZE);
    memset(tmp, 0, PAYLOAD_SIZE); // This is done to pad with 0's
    bytes_read = (uint16_t) MIN(PAYLOAD_SIZE-2, bytes_left);
    memcpy(tmp+2, buf+buf_len-bytes_left, bytes_read);
    bytes_read = htons(bytes_read);
    memcpy(tmp, &bytes_read, sizeof(uint16_t));

    bytes_read = ntohs(bytes_read);
    //printf("bytes_read from block %d = %d \t bytes_left %d\n", blockno, bytes_read, bytes_left);
    
    // Insert this pointer into the blocks datastructure
    blk->content[blk->len] = tmp;
    blk->len++;
    bytes_left -= bytes_read;
  }

  return buf_len - bytes_left;
}


/*
 * Frees a block from memory
 */
void
freeBlock(Block_t* blk){
  int i;
  for(i = 0; i < blk->len; i++){
    free(blk->content[i]);
  }

  // reset the counters
  blk->len = 0;
}

void
  openLog(srvctcp_sock* sk, char* log_name){

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

  sk->db = fopen(log_name, "w+");

  if(!sk->db){
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

  // the total size in bytes of the current packet
  int size = PAYLOAD_SIZE
    + sizeof(double)
    + sizeof(flag_t)
    + sizeof(msg.seqno)
    + sizeof(msg.blockno)
    + sizeof(msg.start_packet)
    + sizeof(msg.num_packets)
    + msg.num_packets*sizeof(msg.packet_coeff);

  //Set to zeroes before startingr
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
  memcpy(&msg->dof_rec, buf+index, (part = sizeof(msg->dof_rec)));
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


srvctcp_sock* 
create_srvctcp_sock(void){
  int i;

  srvctcp_sock* sk = malloc(sizeof(srvctcp_sock));

  sk->num_active = 0;
  sk->active_paths = malloc(MAX_CONNECT*sizeof(Substream_Path*));

  sk->dof_req_latest = BLOCK_SIZE;
  sk->curr_block = 1;
  sk->maxblockno = 1;

  for(i = 0; i < NUM_BLOCKS; i++){
    sk->dof_remain[i] = 0;
    sk->blocks[i].content = malloc(BLOCK_SIZE*sizeof(char*));
  }

  // initialize the thread pool
  thrpool_init( &(sk->workers), THREADS );
  // Initialize the block mutexes and queue of coded packets and counters
  for(i = 0; i < NUM_BLOCKS; i++){
    pthread_mutex_init( &(sk->blocks[i].block_mutex), NULL );
    pthread_rwlock_init( &(sk->blocks[i].block_rwlock), NULL );
    pthread_cond_init( &(sk->blocks[i].block_free_condv), NULL );
    pthread_cond_init( &(sk->blocks[i].block_ready_condv), NULL );
    q_init(&(sk->coded_q[i]), 2*BLOCK_SIZE);
  }


  //----------------- configurable variables -----------------//
  sk->rcvrwin    = 20;          /* rcvr window in mss-segments */
  sk->increment  = 1;           /* cc increment */
  sk->multiplier = 0.5;         /* cc backoff  &  fraction of rcvwind for initial ssthresh*/
  sk->initsegs   = 2;          /* slowstart initial */
  sk->ssincr     = 1;           /* slow start increment */
  sk->maxidle    = 10;          /* max idle before abort */
  sk->valpha     = 0.05;        /* vegas parameter */
  sk->vbeta      = 0.2;         /* vegas parameter */
  sk->debug      = 6;           /* Debug level */

  //------------------Statistics----------------------------------//
  sk->timeouts   = 0;
  sk->ipkts      = 0;
  sk->opkts      = 0;
  sk->badacks    = 0;
  sk->goodacks   = 0;
  sk->enobufs    = 0;
  sk->start_time = 0;
  sk->total_time = 0;
  sk->idle_total = 0;

  return sk;
}



void
init_stream(srvctcp_sock* sk, Substream_Path *subpath){
  int j;

  for(j=0; j < NUM_BLOCKS; j++) subpath->packets_sent[j] = 0;

  for(j=0; j < MAX_CWND; j++){
    subpath->OnFly[j] = 0;
    subpath->tx_time[j] = 0;
  }
  
  subpath->last_ack_time = 0;
  subpath->snd_nxt = 1;
  subpath->snd_una = 1;
  subpath->snd_cwnd = sk->initsegs;

  if (sk->multiplier) {
    subpath->snd_ssthresh = sk->multiplier*MAX_CWND;
  } else {
    subpath->snd_ssthresh = 2147483647;  /* normal TCP, infinite */
  }
    // Statistics //
  subpath->idle       = 0;
  subpath->vdelta     = 0;    /* Vegas delta */
  subpath->max_delta  = 0;
  subpath->slow_start = 1;    /* in vegas slow start */
  subpath->vdecr      = 0;
  subpath->v0         = 0;    /* vegas decrements or no adjusts */


  subpath->minrtt     = 999999.0;
  subpath->maxrtt     = 0;
  subpath->avrgrtt    = 0;
  subpath->srtt       = 0;
  subpath->rto        = INIT_RTO;

  subpath->slr        = 0;
  subpath->slr_long   = SLR_LONG_INIT;
  subpath->slr_longstd= 0;
  subpath->total_loss = 0;

  //subpath->cli_addr   = NULL;
}


