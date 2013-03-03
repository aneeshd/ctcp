#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/poll.h>
#include <fcntl.h>
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
  sk->total_time = getTime() - sk->start_time;
  endSession(sk);
  return;
}

srvctcp_sock*
open_srvctcp(char *port, struct child_remote_cfg *cfg){ 
  int numbytes, rv;  
  struct addrinfo *result; //This is where the info about the server is stored
  struct addrinfo hints, *servinfo;
  srvctcp_sock* sk =  create_srvctcp_sock();
 
  // extract config info passed to us from conf file.
  strcpy(sk->cong_control, cfg->cong_control);
  strcpy(sk->logdir, cfg->logdir);
  sk->ctcp_probe = cfg->ctcp_probe;
  sk->debug = cfg->debug;
 
  // signal(SIGINT, ctrlc);

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

  // ENABLE SIG_IO

   if (fcntl(sk->sockfd, F_SETOWN, getpid()) < 0){
    perror("fcntl");
   }

  open_status_log(sk, port);

  int sndbuf     = MSS*MAX_CWND;/* UDP send buff, bigger than mss */
  int rcvbuf     = MSS*MAX_CWND;/* UDP recv buff for ACKs*/

  int i = sizeof(sndbuf);
  int on = 1;
  //--------------- Setting the UDP socket options -----------------------------//
  setsockopt(sk->sockfd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,i);
  getsockopt(sk->sockfd,SOL_SOCKET,SO_SNDBUF,(char *) &sndbuf,(socklen_t*)&i);
  setsockopt(sk->sockfd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,i);
  getsockopt(sk->sockfd,SOL_SOCKET,SO_RCVBUF,(char *) &rcvbuf,(socklen_t*)&i);
  setsockopt(sk->sockfd, SOL_SOCKET, SO_TIMESTAMP, (const char *)&on, sizeof(on));
  //printf("config: sndbuf %d rcvbuf %d\n",sndbuf,rcvbuf);
  //---------------------------------------------------------------------------//

  //printf("Trying to bind to address %s port %d\n", inet_ntoa(((struct sockaddr_in*) &(result->ai_addr))->sin_addr), ((struct sockaddr_in*)&(result->ai_addr))->sin_port);

  /*------------------------WAIT FOR SYN PACKETS TO COME--------------------------------------------------*/
  //  printf("Listening for SYN on port %s\n", port);
  return sk;
}

/*
  listen_srvctcp
  returns 0 success
  returns -1 error
*/

char* get_addr(struct sockaddr_storage* sa, char* s, int len) {
    /*
    Extract IPv4 or IPv6 address as string
    */
    if (sa->ss_family == AF_INET)
       inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),s,len);
    else 
       inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),s,len);
    return s;
}

uint16_t get_port(struct sockaddr_storage* sa) {
    /*
    Extract IPv4 or IPv6 port number
    */
    if (sa->ss_family == AF_INET) 
       return ntohs( (((struct sockaddr_in*)sa)->sin_port) );
    else 
       return ntohs( (((struct sockaddr_in6*)sa)->sin6_port) );
}

int
listen_srvctcp(srvctcp_sock* sk){
  struct sockaddr_storage cli_addr;
  socklen_t clilen = sizeof(cli_addr);
  int numbytes, rv;
  char* log_name = NULL; // Name of the log
  Skb* skb=alloc_skb();
  char* buff = (char*) &(skb->msgbuf.buff);
  Ack_Pckt* ack;
  log_srv_status(sk);

  if((numbytes = recvfrom(sk->sockfd, buff, ACK_SIZE, 0, (struct sockaddr*)&cli_addr, &clilen)) == -1){
    perror("recvfrom: Failed to receive the request\n");
    return -1;
  }
    
  unmarshallAck(&(skb->msgbuf));
  ack = &(skb->msgbuf.ack);

  if (ack->flag == SYN){
    /*
    printf("Request for a new session: Client address %s Client port %d\n", 
           inet_ntoa(((struct sockaddr_in*) &cli_addr)->sin_addr), 
           ((struct sockaddr_in*)&cli_addr)->sin_port);
    */
    sk->clientport = get_port(&cli_addr);
    get_addr(&cli_addr,sk->clientip,INET6_ADDRSTRLEN);
    printf("Request for a new session: Client address %s Client port %d\n", sk->clientip, sk->clientport);

    if (sk->debug > 6) openLog(sk, log_name);

    Substream_Path *stream = malloc(sizeof(Substream_Path));
    init_stream(sk, stream);
    stream->cli_addr = cli_addr;
    stream->pathstate = SYN_RECV;
    log_srv_status(sk);

    sk->active_paths[0] = stream;
    sk->num_active++;

    // We could try to send SYN_ACK until successful with a while loop, but that would be blocking.
    if(send_flag(sk, 0, SYN_ACK)== 0){
      //      printf("Send SYN_ACK %u\n", getpid());
      sk->active_paths[0]->tx_time[0] = getTime();  // save the tx time to estimate rtt later
      stream->pathstate = SYN_ACK_SENT;
      log_srv_status(sk);
    }

    free_skb(skb);

    rv = pthread_create( &(sk->daemon_thread), NULL, server_worker, (void *) sk);
          
    return 0;

  } else{
    // TODO perhaps we should not exit immediately, if the first packet is not SYN?
    printf("Expecting SYN packet, received something else\n");
  }

  free_skb(skb);
  return -1;
}

/*
 * This is contains the main functionality and flow of the client program
 * returns no of bytes normally, 
 * -1 on error, and sets socket->error.
 */
size_t
send_ctcp(srvctcp_sock *sk, const void *usr_buf, size_t usr_buf_len){

  /*
    printf("Calling send ctcp curr block %d maxblockno %d maxblockno.len %d\n", 
    sk->curr_block, sk->maxblockno,  sk->blocks[sk->maxblockno%NUM_BLOCKS].len);
  */
  if (usr_buf_len == 0){
    return 0;
  }

  size_t bytes_read;
  uint32_t bytes_left = usr_buf_len;
  int block_len_tmp;

  int i = sk->maxblockno;
  while (bytes_left > 0){

    if (sk->status != ACTIVE){
      sk->error = CLIHUP;
      return -1;      
    }

    // Need to take lock as sk->blocks[i%NUM_BLOCKS] might be updated in handle_ack(), close_srvctcp() and
    // coding_job() (and elsewhere ?) which execute in different threads.
    pthread_mutex_lock(&(sk->blocks[i%NUM_BLOCKS].block_mutex));
    while(i < sk->maxblockno){
      pthread_mutex_unlock(&(sk->blocks[i%NUM_BLOCKS].block_mutex));
      i = sk->maxblockno;
      pthread_mutex_lock(&(sk->blocks[i%NUM_BLOCKS].block_mutex));
    }

    block_len_tmp = sk->blocks[i%NUM_BLOCKS].len;   // keep the block len before reading
    if(i < sk->curr_block){
      printf("**ERROR** reading block %d, currblock %d\n", i, sk->curr_block);
    }
    bytes_read = readBlock(&(sk->blocks[i%NUM_BLOCKS]), usr_buf+usr_buf_len-bytes_left, bytes_left);
    bytes_left -= bytes_read;
    //printf("bytes_read %d bytes_left %d maxblockno %d blockno %d\n", bytes_read, bytes_left,  sk->maxblockno, i);


    if (bytes_read > 0) { // there was space in block i to add new data
      coding_job_t* job = malloc(sizeof(coding_job_t));
      job->socket = sk;
      job->blockno = i;
      job->start = block_len_tmp;
      job->dof_request = sk->blocks[i%NUM_BLOCKS].len - block_len_tmp;
      job->coding_wnd = 0;
      sk->dof_remain[i%NUM_BLOCKS] += job->dof_request;  // Update the internal dof counter
      //printf("send_ctcp adding job: blockno %d, dofrequested %d \n", i, job->dof_request);
      addJob(&(sk->workers), &coding_job, job, &free, LOW);

      if (i == sk->curr_block){
        // Need to have a lock here as sk->dof_req_latest is also updated by handle_ack().  We can reuse
        // block_mutex for this as only update here and in handle_ack() after taking lock on block_mutex
        // for  sk->curr_block
        sk->dof_req_latest += job->dof_request;
      }
    } else if (sk->maxblockno < sk->curr_block+NUM_BLOCKS) {
      // No need to take a lock as sk->maxblockno is only modified here
      sk->maxblockno++;
    }  else {
      // We have sk->maxblockno == sk->curr_block+NUM_BLOCKS and so must wait for
      // at least one block to be freed (in handle_ack() by acks from receiver saying that is has arrived safely)
      // before we can receive more data from client.
      if (sk->debug>3) printf("Waiting on block free %d/%d/%d ...", sk->curr_block, sk->maxblockno, i);

      // Risk of deadlock with next line since block_free_condv signal is in handle_ack()
      // but thread running handle_ack() might be blocked waiting for block_ready_condv
      // signal which is sent below in send_segs().

      // pthread_cond_wait( &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_free_condv),
      //                   &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex));

      // We could just bail out here without waiting, but
      // then loop that calls us here in send_ctcp() will immediately return
      // us here, so use a timedwait instead.  DL
      struct timespec timeout;
      double endtime = getTime()+0.1; // wait for at most 100ms
      timeout.tv_sec = endtime; timeout.tv_nsec = (endtime-timeout.tv_sec)*1e9;
      int res = pthread_cond_timedwait( &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_free_condv),
                         &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex), &timeout);
      if (res == ETIMEDOUT) {
         // Timed out, so lets bail.  Need to clear lock ...
         pthread_mutex_unlock(&(sk->blocks[i%NUM_BLOCKS].block_mutex));
         // .. and we signal send_segs() not to wait (it might be waiting for new packets from
         // us here in send_ctcp()).   This is not perfect - if it was waiting, send_segs() will now proceed
         // and transmit packets from later blocks, if there are any.  If there are none to send, this runs the
         // risk of disrupting ack clocking.  Also, we are hoping that once send_segs() completes, there will be
         // some more acks received so that handle_acks() is called and eventually we free up at least one block
         // and so break the deadlock here in send_ctcp().  If acks completely stall, then we are stuck will be
         // stuck in a loop here.
         pthread_cond_signal( &(sk->blocks[i%NUM_BLOCKS].block_ready_condv));
         if (sk->debug>3) printf("timed out\n");
         break;
      } else
         if (sk->debug>3) printf("done\n");
    }

    // Clear lock
    pthread_mutex_unlock(&(sk->blocks[i%NUM_BLOCKS].block_mutex));
    // Signal send_segs() to continue, if blocked waiting for new data
    pthread_cond_signal( &(sk->blocks[i%NUM_BLOCKS].block_ready_condv));

    //printf("Total bytes_read %d bytes_left %d maxblockno %d currblock %d\n", usr_buf_len - bytes_left, bytes_left,  sk->maxblockno, sk->curr_block);
  }
  return usr_buf_len - bytes_left;
}


void 
*server_worker(void *arg){
  srvctcp_sock* sk = (srvctcp_sock*) arg;
  Skb* skb=alloc_skb();
  char *buff = (char*) &(skb->msgbuf.buff);
  Ack_Pckt *ack;
  int numbytes, i, r;
  struct sockaddr_storage cli_addr;
  socklen_t clilen = sizeof cli_addr;
  double idle_timer;
  int path_index=0;              // Connection identifier
  double rcvt;
  double rcvt_user;

  struct iovec iov;
  struct msghdr msg_sock;
  char ctrl_buff[CMSG_SPACE(sizeof(struct timeval))];
  struct cmsghdr *cmsg;
  iov.iov_base = buff;
  iov.iov_len = MSS;
  msg_sock.msg_iov = &iov;
  msg_sock.msg_iovlen = 1;
  msg_sock.msg_name = &cli_addr;
  msg_sock.msg_namelen = clilen;
  msg_sock.msg_control = (caddr_t)ctrl_buff;
  msg_sock.msg_controllen = sizeof(ctrl_buff);
  struct timeval time_now;

  sk->start_time = getTime();
  // sk->dof_req_latest only updates in the context of curr_block,
  // so we can reuse mutex to take lock
  pthread_mutex_lock(&(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex));
  sk->dof_req_latest = sk->blocks[sk->curr_block%NUM_BLOCKS].len ;
  pthread_mutex_unlock(&(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex));

  while(sk->status != CLOSED){
    double rto_min = 0.1; // this value is in seconds. DL
    for (i=0; i < sk->num_active; i++){
      if (sk->active_paths[i]->rto < rto_min) rto_min = sk->active_paths[i]->rto;
    }

    idle_timer = getTime();
    r = timedread(sk->sockfd, rto_min);
    rcvt = getTime();
    rcvt_user = 0;
    sk->idle_total += getTime() - idle_timer;

    if (r > 0){  /* ack ready */
      // The recvfrom should be done to a separate buffer (not buff)
      if((r = recvmsg(sk->sockfd, &msg_sock, 0))== -1){
        perror("Error in receiving ACKs\n");
        continue;  
      }
 
      // use packet timestamp from kernel, if possible ...
      cmsg = CMSG_FIRSTHDR(&msg_sock);
      if (cmsg &&
         cmsg->cmsg_level == SOL_SOCKET &&
         cmsg->cmsg_type == SCM_TIMESTAMP &&
         cmsg->cmsg_len == CMSG_LEN(sizeof(time_now))) {
                           /* Copy to avoid alignment problems: */
         memcpy(&time_now,CMSG_DATA(cmsg),sizeof(time_now));
         rcvt = time_now.tv_sec + time_now.tv_usec*1e-6;
         // record delay between a packet being received at the NIC and it becoming available here
         rcvt_user = getTime()-rcvt;
      }

      unmarshallAck(&(skb->msgbuf));
      ack = &(skb->msgbuf.ack);

      if (sk->debug > 6){
        printf("Got an ACK: ackno %d blockno %d dof_rec %d -- RTT est %f \n",
               ack->ackno,
               ack->blockno,
               ack->dof_rec,
               getTime()-ack->tstamp);
      }


      for(path_index=0; path_index < sk->num_active; path_index++){
        if (sockaddr_cmp(&cli_addr, &(sk->active_paths[path_index]->cli_addr))==0){
          break;
        }
      }

      if(path_index >= sk->num_active){
        // Packet from unknown path
        // check if SYN, then add a new path; otherwise discard
        if(ack->flag == SYN){
          if (sk->num_active < MAX_CONNECT && path_index >= sk->num_active){
            sk->active_paths[sk->num_active] = malloc(sizeof(Substream_Path));
            init_stream(sk, sk->active_paths[sk->num_active]);
            sk->active_paths[sk->num_active]->cli_addr = cli_addr;
            sk->active_paths[sk->num_active]->pathstate = SYN_RECV;
            log_srv_status(sk);
            sk->active_paths[sk->num_active]->last_ack_time = getTime();
            sk->active_paths[path_index]->last_ack_time_user = 0;

            send_flag(sk, sk->num_active, SYN_ACK);
            sk->active_paths[sk->num_active]->tx_time[0] = getTime();  // save the tx time to estimate rtt later

            sk->active_paths[sk->num_active]->pathstate = SYN_ACK_SENT;
            log_srv_status(sk);
            
            
            sk->num_active++;

            char s[INET6_ADDRSTRLEN];
            printf("Request for a new path: Client address %s Client port %d\n",get_addr(&cli_addr,s,INET6_ADDRSTRLEN), get_port(&cli_addr)); 
         
            continue;        // Go back to the beginning of the while loop
          }
        }else{
          printf("Non-SYN from an unknown path\n");
        }
      }else{
        // Packet from known path
        
        sk->active_paths[path_index]->last_ack_time = rcvt;
        sk->active_paths[path_index]->last_ack_time_user = rcvt_user;

        if (ack->flag == NORMAL) {
          
          if( sk->active_paths[path_index]->pathstate != SYN_ACK_SENT && 
              sk->active_paths[path_index]->pathstate != ESTABLISHED ){
            // discard inappropriate ACK packet
            //printf("State %d: Received NORMAL\n", sk->active_paths[path_index]->pathstate);
          }else if( sk->active_paths[path_index]->pathstate == SYN_ACK_SENT){
            // path is now established, and we send data packets
            // printf("Established path %d\n", path_index);

            // Initialize srtt based on the first round 
            sk->active_paths[path_index]->srtt = getTime() - sk->active_paths[path_index]->tx_time[0];
            sk->active_paths[path_index]->srtt_user = 0;
            sk->active_paths[path_index]->mdev = sk->active_paths[path_index]->srtt;
            sk->active_paths[path_index]->mdev_max = RTO_MIN/4;
            sk->active_paths[path_index]->rttvar = RTO_MIN/4;
            sk->active_paths[path_index]->tx_time[0] = 0;

            sk->active_paths[path_index]->pathstate = ESTABLISHED;
            log_srv_status(sk);
            send_segs(sk, path_index);
          }else{            
            // If we get here, the path is established and we just received an ACK        
            sk->ipkts++;
            
            if(handle_ack(sk, ack, path_index) == 0){
              for (i =0; i < sk->num_active; i++){
                send_segs(sk, i);
              }
            }            
          }
        }else if (ack->flag == SYN){
          if (sk->active_paths[path_index]->pathstate == SYN_ACK_SENT){
            // printf("Sending SYN_ACK path %d\n", path_index);
            send_flag(sk, path_index, SYN_ACK);
            sk->active_paths[path_index]->tx_time[0] = getTime();  // save the tx time to estimate rtt later
          }else{
            //printf("State %d: Received SYN\n", sk->active_paths[path_index]->pathstate);
          }
        }else if (ack->flag == FIN) {        

          if (sk->active_paths[path_index]->pathstate != CLOSING &&
              sk->active_paths[path_index]->pathstate != FIN_ACK_RECV){

            // printf("Sending FIN_ACK path %d\n", path_index);
            sk->active_paths[path_index]->pathstate = FIN_RECV;
	    sk->status = SK_CLOSING;
            log_srv_status(sk);

	    int fin_read_rv;
	    int tries = 0;
	    double rto_max = 0;
	    for (i=0; i < sk->num_active; i++){
	      if (sk->active_paths[i]->rto > rto_max) rto_max = sk->active_paths[i]->rto;
	    }
	    

	    do{
	      // TODO for now, when we receive FIN, we send FIN_ACK through all interfaces
	      // May want to change this if we want to add/remove paths independently
	      if (ack->flag == FIN){
		for(i= 0; i<sk->num_active; i++){
		  send_flag(sk, i, FIN_ACK);
		  sk->active_paths[i]->pathstate = FIN_ACK_SENT;
		  log_srv_status(sk);
		}
	      }

	      fin_read_rv = timedread(sk->sockfd, rto_max);
	      if (fin_read_rv > 0){  /* ready */
		// The recvfrom should be done to a separate buffer (not buff)
		if ( (fin_read_rv = recvfrom(sk->sockfd, buff, ACK_SIZE, 0, (struct sockaddr*)&cli_addr, &clilen)) <= 0){
		  perror("Error in receiving ACKs\n");
		  //sk->status = CLOSED;
		  sk->error = CLOSE_ERR;
		  tries++;
		}else{
		  unmarshallAck(&(skb->msgbuf));
                  ack = &(skb->msgbuf.ack);
		
		  if (ack->flag == FIN_ACK){

		    for(i= 0; i<sk->num_active; i++){
		      sk->active_paths[i]->pathstate = CLOSING;
		    }
		    sk->status = CLOSED;
		    log_srv_status(sk);
		    sk->error = NONE;
		  } else if (ack->flag == FIN){
		    tries++;
		  }

		} // end if recvfrom

	      } else if (fin_read_rv == 0){
		// time out on timedread
		tries++;
		rto_max = 2*rto_max;
		sk->error = CLOSE_ERR;
	      } else { // r < 0
		perror("timedread");	      
	      }

	    }while( sk->status != CLOSED && tries < CONTROL_MAX_RETRIES);

	    for(i= 0; i<sk->num_active; i++){
	      sk->active_paths[i]->pathstate = CLOSING;
	    }
	    sk->status = CLOSED;
	    log_srv_status(sk);
	    //	    printf("SRVCTCP SHOULD CLOSE BY NOW %u\n", getpid());
	  }  // end if not CLOSING & not FIN_ACK_RECV

        }  // end of ack->flag cases
      }  // end packet from known path
      
    }else if(r<0){
      perror("timedread");
    }else{
      if (sk->debug >3) printf("Timedread expired: returned 0; t=%f\n",rcvt);
    }

    // Done with processing the received packet. 
      
    if (sk->status != CLOSED){
      // Check all the other paths, and see if any of them timed-out.
      for (i = 0; i < sk->num_active; i++){
	path_index = (path_index+i)%(sk->num_active);
	if(rcvt - sk->active_paths[path_index]->last_ack_time > sk->active_paths[path_index]->rto){
	  if (timeout(sk, path_index)==TRUE){
          
	    // printf("Timeout %d:", path_index);
          
	    // Path timed out, but still alive
	    if(sk->active_paths[path_index]->pathstate == ESTABLISHED){
	      // printf("Sending data\n");
	      send_segs(sk, path_index);
	    }else if(sk->active_paths[path_index]->pathstate == SYN_RECV || 
		     sk->active_paths[path_index]->pathstate == SYN_ACK_SENT){
	      // printf("Sending SYN_ACK\n");
	      send_flag(sk, path_index, SYN_ACK);
        sk->active_paths[path_index]->tx_time[0] = getTime();  // save the tx time to estimate rtt later
	    }else{
	      // Should be in CLOSING state. Ignore packets and continue closing.
	      // printf("Still closing\n");
	    }
	  }else{
	    // set the path status to CLOSED and write in the file

	    // Path is dead and is being removed
	    removePath(sk, path_index);
	    path_index--;
          
	  }
	}
      }
    }
    
    if(sk->num_active==0){
      sk->status = CLOSED;
      sk->error = NONE;
      log_srv_status(sk);
    }
  }  /* while more pkts */

  // TODO free all the blocks up to maxblockno
  pthread_cond_signal( &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_free_condv));
  free_skb(skb);

  // Just for printing statistics
  // ctrlc(sk);
  return NULL;
}

/*
  closes srvctcp_sock
*/
void
close_srvctcp(srvctcp_sock* sk){
  int i, r, tries, success;
  struct sockaddr_storage cli_addr;
  socklen_t clilen = sizeof(cli_addr);
  Skb* skb=alloc_skb();
  char* buff = (char*) &(skb->msgbuf.buff);
  Ack_Pckt *ack = &(skb->msgbuf.ack);
  // Check whether send_segs have finished

  if (sk->num_active > 0){

    // counters for FIN_RETX
    tries = 0;
    success = 0;

    // Is the following thread-safe ? sk->curr_block looks like it is only updated in same thread as close_srvctcp()
    // but sk->dof_req_latest might be updated in other threads.  DL
    i = sk->curr_block;
    if( sk->dof_req_latest != 0){
      while (i <= sk->maxblockno && sk->status == ACTIVE){
        //  printf("Waiting on block %d to get free\n", i);
        pthread_mutex_lock(&(sk->blocks[i%NUM_BLOCKS].block_mutex));
        if (sk->dof_req_latest != 0 && i >= sk->curr_block){
          pthread_cond_wait( &(sk->blocks[i%NUM_BLOCKS].block_free_condv), &(sk->blocks[i%NUM_BLOCKS].block_mutex));
        }
        pthread_mutex_unlock(&(sk->blocks[i%NUM_BLOCKS].block_mutex));
        i++;
      }
      // Now all blocks are freed, and we have received all the acks
    }


    //   printf("RETURNED FROM WAITING ON BLOCKS currBlock %d, maxblock %d pid %u\n", sk->curr_block, sk->maxblockno, getpid());  
    sk->status = CLOSED;
    log_srv_status(sk);
    //Signal the worker thread to exit
    pthread_cond_signal(&(sk->blocks[(sk->maxblockno)%NUM_BLOCKS].block_ready_condv));
    pthread_join(sk->daemon_thread, NULL);


    // Send FIN or FIN_ACK
    if(sk->active_paths[0]->pathstate != CLOSING){
      // printf("Sending the FIN packet\n");

      double rto_max = 0;
      for (i=0; i < sk->num_active; i++){
        if (sk->active_paths[i]->rto > rto_max) rto_max = sk->active_paths[i]->rto;
      }

      do{
        // Send FIN through all interfaces
        for(i=0; i<sk->num_active; i++){
	  //          printf("SENDING FIN %u\n", getpid());
          send_flag(sk, i, FIN);                 
          sk->active_paths[i]->pathstate = FIN_SENT;
        }
        log_srv_status(sk);
      
        // Send FIN to the client, and wait for FIN_ACK
        r = timedread(sk->sockfd, rto_max);
        if (r > 0){  /* ready */
          // The recvfrom should be done to a separate buffer (not buff)
          if ( (r = recvfrom(sk->sockfd, buff, ACK_SIZE, 0, (struct sockaddr*)&cli_addr, &clilen)) <= 0){
            perror("Error in receveing ACKs\n");
            //sk->status = CLOSED;
            sk->error = CLOSE_ERR;
            tries++;
          }else{
            unmarshallAck(&(skb->msgbuf));
            ack = &(skb->msgbuf.ack);
          
            if (ack->flag == FIN){

              if ( sk->active_paths[0]->pathstate == FIN_SENT){

                for(i=0; i<sk->num_active; i++){
		  //      printf("SENDING FIN ACK %u\n", getpid());
                  if (send_flag(sk, i, FIN_ACK)==0){
                    sk->active_paths[i]->pathstate = FIN_ACK_SENT;
                  }
                  log_srv_status(sk);
                }

              }else if ( sk->active_paths[0]->pathstate == FIN_ACK_SENT){
	      
                success = 1;
                for(i=0; i<sk->num_active; i++){
                  sk->active_paths[i]->pathstate = CLOSING;
                }	
                log_srv_status(sk);
	    
              }

              sk->error = NONE;       
	    
            }else if (ack->flag == FIN_ACK){

              if ( sk->active_paths[0]->pathstate == FIN_SENT){
                for(i=0; i<sk->num_active; i++){
		  //                  printf("SENDING FIN ACK %u\n", getpid());
                  send_flag(sk, i, FIN_ACK);
                }
              }
	    
              success = 1;
              for(i=0; i<sk->num_active; i++){
                sk->active_paths[i]->pathstate = CLOSING;
              }	
              log_srv_status(sk);
              sk->error = NONE;       

            }else{
              // printf("got Non-FIN_ACK\n");
              sk->error = CLOSE_ERR;
            }
          }
        }else { /* r <=0 */
          //err_sys(sk, "close");
          // printf("r<=0 in close_srvctcp\n");
          sk->error = CLOSE_ERR;
          tries++;
          rto_max = 2*rto_max;
        }

      }while(tries < CONTROL_MAX_RETRIES && !success);

      // For now, just send FIN_ACK_ACK only if successfully received FIN_ACK
      /*
        if(success){
        printf("Successfully received FIN_ACK after %d tries\n", tries);
        }else{
        printf("Did not receive FIN_ACK after %d tries. Closing anyway\n", tries);
        }
      */
    }

  } else if (sk->num_active == 0){
    printf("CLOSING WITH ZERO PATHS %u\n", getpid());
  }



  if (close(sk->status_log_fd) == -1){
    perror("Could not close the status file");
  }

  char file_name[32];
  sprintf(file_name, "%s/%u", sk->logdir, getpid());

  if (remove(file_name) == -1){
    perror(file_name);
  }

  thrpool_kill( &(sk->workers));

  for(i=1; i<sk->num_active; i++){
    free(sk->active_paths[i]);
  }
  free(sk->active_paths);

  free(sk);
  free_skb(skb);

  //  printf("Cleared the ctcp socket %u\n", getpid());

  return;
}





void
removePath(srvctcp_sock* sk, int dead_index){
  free(sk->active_paths[dead_index]);
  int i;
  for(i = dead_index; i < sk->num_active-1; i++){
    sk->active_paths[i] = sk->active_paths[i+1];
  }
  sk->num_active--;
  sk->active_paths[sk->num_active] = NULL;
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
    char s[INET6_ADDRSTRLEN];
    fprintf(stderr,
            "timerxmit %6.2f \t on %s:%d \t blockno %d blocklen %d pkt %d  snd_nxt %d  snd_cwnd %d srtt %f \n",
            getTime()-sk->start_time,
            get_addr(&subpath->cli_addr,s,INET6_ADDRSTRLEN),
  	    get_port(&subpath->cli_addr),
            sk->curr_block,
            sk->blocks[sk->curr_block%NUM_BLOCKS].len,
            subpath->snd_una,
            subpath->snd_nxt,
            (int)subpath->snd_cwnd,
	    subpath->srtt);
  }

  // THIS IS A GLOBAL COUNTER FOR STATISTICS ONLY.
  sk->timeouts++;

  subpath->idle++;
  subpath->slr = subpath->slr_long;
  //slr_long[path_id] = SLR_LONG_INIT;
  subpath->rto = 2*subpath->rto; // Exponential back-off

  if (subpath->snd_nxt != subpath->snd_una){
    // Nothing is on the fly , we expect no ACKS so don't timeout

    subpath->snd_ssthresh = subpath->snd_cwnd*sk->multiplier; /* shrink */
    
    if (subpath->snd_ssthresh < sk->initsegs) {
      subpath->snd_ssthresh = sk->initsegs;
    }
    subpath->snd_cwnd = sk->initsegs;  /* drop window */
    subpath->snd_una = subpath->snd_nxt;
    
  }
  return TRUE;
  // Update the path_id so that we timeout based on another path, and try every path in a round
  // Avoids getting stuck if the current path dies and packets/acks on other paths are lost

}


/*
  sends control message with the flag
*/
int 
send_flag(srvctcp_sock* sk, int path_id, flag_t flag ){
  int numbytes;
  // FIN_CLI sequence number is meaningless
  Msgbuf msgbuf;
  Data_Pckt* msg = &(msgbuf.msg);
  msg->flag         = flag;
  msg->blockno      = sk->curr_block;
  msg->num_packets  = 0;
  msg->start_packet = 0;
  msg->packet_coeff = 0;
  msg->tstamp = getTime();

  int size = marshallData(&msgbuf);

  do{
    socklen_t clilen = sizeof(sk->active_paths[path_id]->cli_addr);
    if((numbytes = sendto(sk->sockfd, msgbuf.buff, size, 0, (struct sockaddr*)&(sk->active_paths[path_id]->cli_addr), clilen)) == -1){
      perror("send_Fin_Cli error: sendto");
      return -1;
    }
  } while(errno == ENOBUFS && ++(sk->enobufs)); // use the while to increment enobufs if the condition is met

  if(numbytes != size){
    perror("send_flag error: sent fewer bytes");
    return -1;
  }
  
  return 0;
}


void
send_segs(srvctcp_sock* sk, int pin){

  if (sk->status == CLOSED){
    return;
  }

  // Note that sk->curr_block is only changed in handle_ack(), which is in the same thread as us, so there
  // no worries about race conditions here on this variable.
  // In contrast sk->dof_req_latest CAN be updated in both handle_ack() and send_ctcp(), and send_ctcp() 
  // is executed in a different thread so need to take a lock when updating. We can reuse the block_mutex
  // for this as updating in send_ctp() only happens after it has taken a lock on block_mutex 
  // for sk->curr_block

  pthread_mutex_lock(   &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex) );

  if (sk->dof_req_latest == 0){
    //Done with current block.  Waiting to move onto next block.  DL
    if (sk->debug>3) printf("waiting on block ready %d ...", sk->curr_block);

    if (sk->maxblockno == sk->curr_block + NUM_BLOCKS) {
       if (sk->debug>3) printf("Risk of block_ready_condv deadlock...");
    }
    pthread_cond_wait( &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_ready_condv),
                            &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex));
    if (sk->debug>3) printf("done\n");
  }

  pthread_mutex_unlock( &(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex) );

  if (sk->status == CLOSED){
    return;
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
      CurrOnFly[j] += (sk->active_paths[j]->OnFly[k%MAX_CWND] == sk->curr_block) & (sk->active_paths[j]->tx_time[k%MAX_CWND] >= current_time);
    }

    d[j] = sk->active_paths[j]->srtt;
  }

  //////////////////////////  START CHECKING EACH BLOCK   //////////////////////////

  int blockno = sk->curr_block;
  while ( (win > 0) && (blockno <= sk->curr_block + NUM_BLOCKS) && (!sk->maxblockno || blockno <= sk->maxblockno) ){

    if (blockno == sk->curr_block){
      // Need to take a lock here as sk->dof_req_latest updated by send_ctcp() which executes in a different thread
      // (also updated by handle_ack() but that executes in the same thread as us).
      pthread_mutex_lock(   &(sk->blocks[blockno%NUM_BLOCKS].block_mutex) );
      dof_request_tmp = sk->dof_req_latest;
      pthread_mutex_unlock(   &(sk->blocks[blockno%NUM_BLOCKS].block_mutex) );
    }else{
      pthread_mutex_lock(   &(sk->blocks[blockno%NUM_BLOCKS].block_mutex) );
      dof_request_tmp = sk->blocks[blockno%NUM_BLOCKS].len;
      pthread_mutex_unlock(   &(sk->blocks[blockno%NUM_BLOCKS].block_mutex) );

      
      for (j = 0; j < sk->num_active; j++){
        CurrOnFly[j] = sk->active_paths[j]->packets_sent[blockno%NUM_BLOCKS];
      }
    }

    /*
    if (dof_request_tmp <= 0){
      printf("ERROR: dof_request_tmp = %d \t curr_block %d dof_req_latest %d blockno %d\n\n\n", dof_request_tmp, sk->curr_block, sk->dof_req_latest, blockno);
    }
    */

    mean_rate = 0;
    mean_latency = 0;
    mean_OnFly = 0;

    for (j = 0; j < sk->num_active; j++){

      // Compensate for server's over estimation of the loss rate caused by lost acks
      //p = sk->active_paths[j]->slr/(2.0 - sk->active_paths[j]->slr);
      p = sk->active_paths[j]->slr;

      delay_diff_tmp = subpath->srtt - d[j];
      if ((delay_diff_tmp > 0) && (d[j] > 0)){
	//        mean_rate    += (1-p)*sk->active_paths[j]->snd_cwnd/(2*d[j]);
	//        mean_latency += (1-p)*sk->active_paths[j]->snd_cwnd/(2.0);
        mean_rate    += (1-p)*(sk->active_paths[j]->snd_nxt - sk->active_paths[j]->snd_una)/(d[j]);
        mean_latency += (1-p)*(sk->active_paths[j]->snd_nxt - sk->active_paths[j]->snd_una);
      }
      mean_OnFly += (1-p)*(CurrOnFly[j]);
    }

    //p = subpath->slr/(2.0 - subpath->slr);
    p = subpath->slr;

    // The total number of dofs the we think we should be sending (for the current block) from now on
    dof_needed = 0;

    // Rolling back to something simpler here - why include mean_rate*subpath->srtt - mean_latency ?  DL
    //while ( (dof_needed < win) &&  ( (dof_needed)*(1-p) + (mean_rate*subpath->srtt - mean_latency + mean_OnFly)  < dof_request_tmp) ) {
    while ( (dof_needed < win) &&  ( dof_needed*(1-p) + mean_OnFly  < dof_request_tmp) ) {
      dof_needed++;
    }

    if (dof_needed == 0){
      
      if (mean_rate > 0){
        t0 = (dof_request_tmp - mean_OnFly + mean_latency)/(mean_rate);
        
        if (t0 > subpath->srtt){
          printf("current path delay %f  t0 %f \n", subpath->srtt/2.0, t0);
        }
        
      }

      for (j = 0; j < sk->num_active; j++){
        if (d[j] < subpath->srtt){
          //d[j] = t0;
          d[j] = subpath->srtt;
        }
      }

      if (sk->debug > 6 && sk->num_active == 1){
        printf("Now %f \t blockno %d mean OnFly %f dof_request tmp %d win %d CurrOnFly[0] %d srtt[0] %f \n", 
               1000*(getTime()-sk->start_time), blockno, mean_OnFly, dof_request_tmp, win, 
               CurrOnFly[0], sk->active_paths[0]->srtt*1000);
      }

      if (sk->debug > 6 && sk->num_active == 2){
        printf("Now %f \t path_id %d blockno %d \t mean OnFly %f \t dof_request tmp %d \t win %d \t CurrOnFly[0] %d CurrOnFly[1] %d srtt[0] %f srtt[1] %f\n", 
               1000*(getTime()-sk->start_time), pin, blockno, mean_OnFly, dof_request_tmp, win, 
               CurrOnFly[0], CurrOnFly[1], sk->active_paths[0]->srtt*1000, sk->active_paths[1]->srtt*1000);
      }

    }

    //printf("Time %f win %d curr_block %d  block no %d dof_needed %d meanOnFly %f dof_req %d \n", getTime(), win, sk->curr_block, blockno, dof_needed, mean_OnFly, dof_request_tmp);

    CurrWin = dof_needed;

    //printf("Current Block %d win %d  dof_needed %d dof_req_latest %d CurrOnFly[%d] %d mean Onfly %f t0 %f\n", blockno, win, dof_needed, dof_request_tmp, pin, CurrOnFly[pin], mean_OnFly, t0);  

    // Check whether we have enough coded packets for current block
    // Need to take a lock as sk->dof_remain updated by send_ctcp() which executes in a different thread
    pthread_mutex_lock(   &(sk->blocks[blockno%NUM_BLOCKS].block_mutex) );
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

    while (CurrWin>=1 && sk->dof_remain[blockno%NUM_BLOCKS]>0) {
      // Remove lock before calling send_one() since it might take a while.
      pthread_mutex_unlock(   &(sk->blocks[blockno%NUM_BLOCKS].block_mutex) );
      send_one(sk, blockno, pin);
      subpath->snd_nxt++;
      CurrWin--;
      win--;
      // And restore lock
      pthread_mutex_lock(   &(sk->blocks[blockno%NUM_BLOCKS].block_mutex) );
      sk->dof_remain[blockno%NUM_BLOCKS]--;   // Update the internal dof counter
      subpath->packets_sent[blockno%NUM_BLOCKS]++;
    }
    pthread_mutex_unlock(   &(sk->blocks[blockno%NUM_BLOCKS].block_mutex) );

    blockno++;

  } //////////////////////// END CHECKING BLOCKS ////////////////////
}


void
send_one(srvctcp_sock* sk, uint32_t blockno, int pin){

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
  Skb* skb = (Skb*) q_pop(&(sk->coded_q[blockno%NUM_BLOCKS]));
  Msgbuf* msgbuf = &(skb->msgbuf);
  Data_Pckt* msg = &(msgbuf->msg);

  // Correct the header information of the outgoing message
  msg->seqno = subpath->snd_nxt;
  msg->tstamp = getTime();

  if (sk->debug > 6){
    fprintf(sk->db,"%f %d xmt%d\n",
	    getTime()-sk->start_time,
	    blockno-sk->curr_block,
	    pin);

    printf("Sending... on blockno %d blocklen %d  seqno %d  snd_una %d snd_nxt %d  start pkt %d snd_cwnd %d   port %d \n",
           blockno,
           sk->blocks[sk->curr_block%NUM_BLOCKS].len,
           msg->seqno,
           subpath->snd_una,
           subpath->snd_nxt,
           msg->start_packet,
           (int)subpath->snd_cwnd,
           get_port(&subpath->cli_addr) );
  }

  // Marshall msg into buf
  int message_size = marshallData(msgbuf);
  socklen_t clilen = sizeof(subpath->cli_addr);

  do{
    if((numbytes = sendto(sk->sockfd, &(msgbuf->buff), message_size, 0,
                          (struct sockaddr*)&subpath->cli_addr, clilen)) == -1){
      printf("Sending... on blockno %d blocklen %d  seqno %d  snd_una %d snd_nxt %d  start pkt %d snd_cwnd %d   port %d \n",
             blockno,
             sk->blocks[sk->curr_block%NUM_BLOCKS].len,
             msg->seqno,
             subpath->snd_una,
             subpath->snd_nxt,
             msg->start_packet,
             (int)subpath->snd_cwnd,
	     get_port(&subpath->cli_addr) );
      err_sys(sk,"Error: sendto");
    }
  } while(errno == ENOBUFS && ++(sk->enobufs)); // use the while to increment enobufs if the condition is met


  if(numbytes != message_size){
    err_sys(sk,"write");
  }

  // Update the packets on the fly
  subpath->OnFly[subpath->snd_nxt%MAX_CWND] = blockno;
  subpath->tx_time[subpath->snd_nxt%MAX_CWND] = msg->tstamp + 1.5*subpath->srtt + RTO_BIAS;
  //printf("Freeing the message - blockno %d snd_nxt[path_id] %d ....", blockno, snd_nxt[path_id]);
  sk->opkts++;
  if (msg->flag != NORMAL) free_skb(skb);
  //printf("---------- Done Freeing the message\n-------------");
}

void
endSession(srvctcp_sock* sk){
  char myname[128];
  char* host = "Host"; // TODO: extract this from the packet

  gethostname(myname,sizeof(myname));
  printf("\n\n%s => %s for %f secs\n",
         myname,host, sk->total_time);


  int i; char s[INET6_ADDRSTRLEN];
  for (i=0; i < sk->num_active; i++){
    printf("******* Priniting Statistics for path %d -- %s : %d ********\n",i,
	   get_addr(&sk->active_paths[i]->cli_addr, s, INET6_ADDRSTRLEN),
           get_port(&sk->active_paths[i]->cli_addr) );
    printf("**THRU** %f Mbs\n",
           8.e-6*(sk->active_paths[i]->snd_una*PAYLOAD_SIZE)/sk->total_time);
    printf("**LOSS* %6.3f%% \n",
           100.*sk->active_paths[i]->total_loss/sk->active_paths[i]->snd_una);
    if (sk->ipkts) sk->active_paths[i]->avrgrtt /= sk->ipkts;
    printf("**RTT** minrtt  %f maxrtt %f avrgrtt %f\n",
           sk->active_paths[i]->minrtt, sk->active_paths[i]->maxrtt,sk->active_paths[i]->avrgrtt);
    printf("**RTT** rto %f  srtt %f \n", sk->active_paths[i]->rto, sk->active_paths[i]->srtt);
    printf("**VEGAS** max_delta %f vdecr %d v0 %d\n",
           sk->active_paths[i]->max_delta, sk->active_paths[i]->vdecr, sk->active_paths[i]->v0);
    printf("**CWND** snd_nxt %d snd_cwnd %d  snd_una %d ssthresh %d goodacks %d\n\n",
           sk->active_paths[i]->snd_nxt, sk->active_paths[i]->snd_cwnd, sk->active_paths[i]->snd_una,
           sk->active_paths[i]->snd_ssthresh, sk->goodacks);
  }

  printf("Total time %f Total idle time %f, Total timeouts %d\n", sk->total_time, sk->idle_total, sk->timeouts);
  printf("Total packets in: %d, out: %d, enobufs %d\n\n", sk->ipkts, sk->opkts, sk->enobufs);

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

  if (sk->debug > 4 ) printf("ack rcvd ackno=%d/blockno=%d ...", ackno, ack->blockno);

  //------------- RTT calculations --------------------------//
  double rtt;
  double m;
  rtt = subpath->last_ack_time - ack->tstamp; // this calculates the rtt for this coded packet
  subpath->rtt = rtt;
  if (rtt < subpath->basertt) subpath->basertt = rtt;
  if (rtt < subpath->minrtt) subpath->minrtt = rtt;
  if (rtt > subpath->maxrtt) subpath->maxrtt = rtt;
  subpath->avrgrtt += rtt;
  //  Following RTO calcs are similar to Linux
  m = rtt-subpath->srtt;
  if (m < 0) {
    m = -m; 
    if (m > subpath->mdev) m = m/8; // slow decrease
  } 
  subpath->mdev = 0.75*subpath->mdev + 0.25*m;
  if (subpath->mdev > subpath->mdev_max) {
    subpath->mdev_max = subpath->mdev;
    if (subpath->mdev_max > subpath->rttvar) 
       subpath->rttvar = subpath->mdev_max;
  }
  // What value should g have here when ACK every packet ?  Normal TCP value would be 1/8.  DL
  subpath->srtt = (1-g)*subpath->srtt + g*rtt;
  if (subpath->cntrtt == 0) {
     // once per RTT update
     if (subpath->mdev_max < subpath->rttvar) 
        subpath->rttvar = 0.75*subpath->rttvar + 0.25*subpath->mdev_max;
     subpath->mdev_max = RTO_MIN/4;
  }
  subpath->rto = subpath->srtt + 4*subpath->rttvar;
  
  // This keeps track of the average delay between a packet being received at the NIC and it becoming available
  // to server_worker() i.e. the extra delay associated with user-space operation
  subpath->srtt_user = (1-g)*subpath->srtt_user + g*subpath->last_ack_time_user;

  subpath->cntrtt++;

  if (sk->debug > 6) {
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

    log_srv_status(sk);

    //printf("waiting on block mutex to free block %d ...", sk->curr_block);

    // Need to take a lock here as sk->blocks can be updated by send_ctcp(), coding_job() etc
    // which run in different threads.  This is also the block of code where
    // dof_req_latest is updated, and same lock is used for both this and sk->blocks.

    pthread_mutex_lock(&(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex));

    if (sk->curr_block ==1)
       subpath->goodput = sk->blocks[sk->curr_block%NUM_BLOCKS].len;
    else
       subpath->goodput += sk->blocks[sk->curr_block%NUM_BLOCKS].len;

    freeBlock(&(sk->blocks[sk->curr_block%NUM_BLOCKS]));
    
    for (j = 0; j < sk->num_active; j++){
      sk->active_paths[j]->packets_sent[sk->curr_block%NUM_BLOCKS] = 0;
    }

    sk->dof_remain[sk->curr_block%NUM_BLOCKS] = 0;

    q_free(&(sk->coded_q[sk->curr_block%NUM_BLOCKS]), &free_skb);

    sk->curr_block++;            // Update the current block identifier

    // Usually, sk->curr_block <= sk->maxblockno.  But,
    // sk->curr_block can overun sk->maxblockno is we have sent all the available
    // data and have no new data to send. If that happens, we advance
    //sk->maxblockno to maintain sk->curr_block <= sk->maxblockno.   In this case the block
    // pointed to bu sk->curr_block will have no data and so will generate no acks,
    // so we will not advance sk->curr_block past this block (until some new data is
    // available, in which case we are back in the normal operating regime).

    sk->maxblockno = MAX(sk->curr_block, sk->maxblockno);

    pthread_mutex_unlock(&(sk->blocks[(sk->curr_block-1)%NUM_BLOCKS].block_mutex));
    pthread_cond_signal( &(sk->blocks[(sk->curr_block-1)%NUM_BLOCKS].block_free_condv));

    pthread_mutex_lock(&(sk->blocks[sk->curr_block%NUM_BLOCKS].block_mutex));
    sk->dof_req_latest =  sk->blocks[sk->curr_block%NUM_BLOCKS].len - ack->dof_rec;     // reset the dof counter for the current block

    if (sk->dof_req_latest < 0){
      printf("ERROR: dof_req_latest %d curr_block %d curr block len %d ack-blockno %d ack-dof_rec %d\n\n", sk->dof_req_latest, sk->curr_block,  sk->blocks[sk->curr_block%NUM_BLOCKS].len, ack->blockno, ack->dof_rec);
    }
    pthread_mutex_unlock(&(sk->blocks[(sk->curr_block)%NUM_BLOCKS].block_mutex));

    if (sk->debug > 2 && sk->curr_block%1==0){
      printf("Now sending block %d/%d, cwnd %d, SLR %f%%, SRTT %f ms, MINRTT %f ms, BASERTT %f ms, RATE %f Mbps (%f/%f), win %d, SRTT_user %f \n",
             sk->curr_block, sk->maxblockno, subpath->snd_cwnd, 100*subpath->slr, subpath->srtt*1000, subpath->minrtt*1000, subpath->basertt*1000, 8.e-6*(subpath->rate*PAYLOAD_SIZE), 8.e-6*(subpath->snd_una*MSS)/(getTime() - sk->start_time), 8.e-6*subpath->goodput*PAYLOAD_SIZE/(getTime() - sk->start_time), subpath->snd_cwnd - (subpath->snd_nxt - subpath->snd_una), subpath->srtt_user*1000);
    }     
  }

  if (ackno > subpath->snd_nxt || ack->blockno > sk->curr_block) {
    /* bad ack */
    if (sk->debug > 3) fprintf(stderr,

                               "Bad ack: curr block %d badack no %d snd_nxt %d snd_una %d cli.port %d\n\n",
                               sk->curr_block,
                               ackno,
                               subpath->snd_nxt,
                               subpath->snd_una,
			       get_port(&subpath->cli_addr) );

    sk->badacks++;
    if(subpath->snd_una < ackno) subpath->snd_una = ackno;

  } else {
    // Late or Good acks count towards goodput


    if (sk->debug > 6){
      fprintf(sk->db,"%f %d %d %d %f %f %f %f %f xmt\n",
	      getTime()-sk->start_time,
	      ack->blockno,
	      subpath->snd_cwnd,
	      subpath->snd_ssthresh,
	      subpath->slr,
	      subpath->slr_long,
	      subpath->srtt,
	      subpath->rto,
	      rtt);
    }

    subpath->idle = 0; // Late or good acks should stop the "idle" count for max-idle abort.

    if (ackno <= subpath->snd_una){
      //late ack
      if (sk->debug > 6) fprintf(stderr,
                                 "Late ack path %d: curr block %d ack-blockno %d badack no %d snd_nxt %d snd_una %d\n",
                                 pin, sk->curr_block, ack->blockno, ackno, subpath->snd_nxt, subpath->snd_una);
    } else {
      sk->goodacks++;
      int losses = ackno - (subpath->snd_una +1);
      subpath->losscnt += losses;
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
      pthread_mutex_lock(&(sk->blocks[(sk->curr_block)%NUM_BLOCKS].block_mutex));
      sk->dof_req_latest = MIN(sk->dof_req_latest,  sk->blocks[sk->curr_block%NUM_BLOCKS].len - ack->dof_rec);
      pthread_mutex_unlock(&(sk->blocks[(sk->curr_block)%NUM_BLOCKS].block_mutex));  
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
  //  exit(1);
}

void
ctcp_probe(srvctcp_sock* sk, int pin) {
   Substream_Path *subpath = sk->active_paths[pin];

   if (!sk->ctcp_probe) return; // logging disabled

   if (!sk->db) {
     sk->db = fopen("/tmp/ctcp-probe", "a"); // probably shouldn't be a hard-wired filename
     if(!sk->db) perror("An error ocurred while opening the /tmp/ctcp-probe log file");
   }

   if (sk->db) {
        fprintf(sk->db,"%f dest %s:%u  %d %#x %#x %u %u %u %u %u %u %u %u %f %f\n",
           getTime(), sk->clientip, (int) sk->clientport,
           (int) PAYLOAD_SIZE, (int) subpath->snd_nxt, (int) subpath->snd_una,
           (int) subpath->snd_cwnd, (int) subpath->snd_ssthresh, (int) MAX_CWND,
           (int) (subpath->srtt*1000), (int) (subpath->basertt*1000), (int) (subpath->rtt*1000), (int) (subpath->minrtt*1000),
           (int) subpath->total_loss, subpath->rate*PAYLOAD_SIZE*8e-6, subpath->goodput*PAYLOAD_SIZE*8e-6);
        fflush(sk->db);
   }
}

int loss_occurred(srvctcp_sock* sk, int pin) {
  Substream_Path *subpath = sk->active_paths[pin];
  return (subpath->losscnt>0) 
           && (  (subpath->srtt - subpath->basertt >0.005) 
              || (subpath->minrtt - subpath->basertt >0.002) );
}

void decrease_cwnd(srvctcp_sock* sk, int pin) {
  Substream_Path *subpath = sk->active_paths[pin];

  if (subpath->dec_snd_nxt >= subpath->snd_una) return; 
  subpath->dec_snd_nxt = subpath->snd_una + subpath->snd_cwnd ; // no more backoffs within the next RTT
  subpath->snd_cwnd -= subpath->toggle; // undo last increase 
  if (subpath->cntrtt > 2) {
     int decrease = (int) (0.5+subpath->snd_cwnd * (1-subpath->basertt/subpath->minrtt));
     if (decrease > (int) (0.5+subpath->snd_cwnd/2)) decrease = (int) (0.5+subpath->snd_cwnd/2);
     subpath->snd_cwnd -= decrease;
  } else
     subpath->snd_cwnd = (int) (subpath->snd_cwnd/2);
  if (subpath->snd_cwnd < 2) subpath->snd_cwnd = 2;
  subpath->snd_ssthresh=0.75*subpath->snd_cwnd; 
}

void constrain_cwnd(srvctcp_sock* sk, int pin) {
  Substream_Path *subpath = sk->active_paths[pin];

  if (subpath->snd_cwnd > MAX_CWND) subpath->snd_cwnd = MAX_CWND;
  if (subpath->snd_cwnd < 2) subpath->snd_cwnd = 2;
}

void
advance_cwnd(srvctcp_sock* sk, int pin){
  /* advance cwnd according to slow-start of congestion avoidance */
  Substream_Path *subpath = sk->active_paths[pin];
  
  if (subpath->beg_snd_nxt <= subpath->snd_una) { //need to be more careful about wrapping of sequence numbers here. DL
     subpath->rate = (subpath->snd_una - subpath->beg_snd_una) /(getTime()-subpath->time_snd_nxt);
     subpath->beg_snd_nxt = subpath->snd_una + subpath->snd_cwnd; // NB: can't use snd_nxt here as stale following receipt of ack
     subpath->beg_snd_una = subpath->snd_una;
     subpath->time_snd_nxt = getTime();
     uint32_t target_cwnd, diff;
     target_cwnd = subpath->snd_cwnd * subpath->basertt / subpath->srtt;
     diff = subpath->snd_cwnd - target_cwnd;
     if (!strcmp(sk->cong_control,"aimd")) { 
        // use AIMD cwnd update with adaptive backoff
        if (diff > 1 && subpath->snd_cwnd <= subpath->snd_ssthresh) {
           // exit slow-start using Vegas approach
           if (target_cwnd+1 < subpath->snd_cwnd) subpath->snd_cwnd=target_cwnd+1;
           if (subpath->snd_cwnd-1 < subpath->snd_ssthresh) subpath->snd_ssthresh=subpath->snd_cwnd-1;
        } else if (subpath->dec_snd_nxt < subpath->snd_una)
           subpath->snd_cwnd += subpath->toggle;
        if (loss_occurred(sk,pin)) decrease_cwnd(sk, pin);
        constrain_cwnd(sk,pin);
        if (subpath->snd_ssthresh < 0.75*subpath->snd_cwnd) subpath->snd_ssthresh=0.75*subpath->snd_cwnd;
     } else if (!strcmp(sk->cong_control,"vegas")) {
        // use Vegas cwnd update
        if (subpath->cntrtt <= 2) {
           // not enough RTT samples, do Reno increase
           // - need to be more careful about what to do here for links with small BDP
           subpath->snd_cwnd++;
        } else {
           // do Vegas
           // in linux they use minrtt here, another option could be srtt
           double rtt;
           if (subpath->snd_cwnd <= subpath->snd_ssthresh)
              //slow start - use srtt as packet trains can make minrtt an unreliable congestion indicator
              rtt = subpath->srtt;
           else {
              // congestion avoidance - use minrtt
              rtt = subpath->minrtt;
           }
           target_cwnd = subpath->snd_cwnd * subpath->basertt / rtt;
           diff = subpath->snd_cwnd - target_cwnd;
           if (diff > subpath->max_delta) subpath->max_delta = diff;  /* keep stats on vegas diff */
           if (diff > 1 && subpath->snd_cwnd <= subpath->snd_ssthresh) {
              // exit slow-start
              if (target_cwnd+1 < subpath->snd_cwnd) subpath->snd_cwnd=target_cwnd+1;
              if (subpath->snd_cwnd-1 < subpath->snd_ssthresh) subpath->snd_ssthresh=subpath->snd_cwnd-1;
           } else if (subpath->snd_cwnd <= subpath->snd_ssthresh) {
              // slow-start 
              subpath->snd_cwnd++; 
           } else {
              // congestion avoidance
              if (diff > sk->vbeta ) {
                 subpath->snd_cwnd--;
                 subpath->vdecr++; // keep statistics
                 if (subpath->snd_cwnd-1 < subpath->snd_ssthresh) subpath->snd_ssthresh=subpath->snd_cwnd-1;
              } else if (diff < sk->valpha) {
                 subpath->snd_cwnd++;
              } else {
                 // do nothing
                 subpath->v0++; // keep statistics
              }                                                                                                              }
           constrain_cwnd(sk,pin);
           if (subpath->snd_ssthresh < 0.75*subpath->snd_cwnd) subpath->snd_ssthresh=0.75*subpath->snd_cwnd;
        }
     } else
        printf("Unknown congestion control option: %s\n", sk->cong_control);
     if (sk->debug > 2) ctcp_probe(sk, pin);
     subpath->cntrtt = 0;
     //subpath->toggle = (subpath->toggle+1)%2; // for fairness with delayed acking
     subpath->toggle = 1; // for fairness with appropriate byte counting
     subpath->minrtt = 999999.0;
     subpath->losscnt=0;
  } else if (subpath->snd_cwnd <= subpath->snd_ssthresh) {
     // slow-start
     if (!strcmp(sk->cong_control,"aimd")) {
        // use AIMD cwnd update with adaptive backoff - exit slow start on loss
        subpath->snd_cwnd++;
     } else if (!strcmp(sk->cong_control,"vegas")) {
        // use vegas cwnd update
        subpath->snd_cwnd++;
     } else
        printf("Unknown congestion control option: %s\n", sk->cong_control);
     if (sk->debug > 2) ctcp_probe(sk, pin);
  }
  constrain_cwnd(sk,pin);
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
 
  // Need to a lock here as sk->curr_block might be updated by handle_ack()
  // executing in a different thread.  DL
  pthread_mutex_lock(&(sk->blocks[blockno%NUM_BLOCKS].block_mutex));
  if( blockno < sk->curr_block){
    if (sk->debug > 3){
      printf("Coding job request for old block - curr_block %d blockno %d dof_request %d \n\n", sk->curr_block,  blockno, dof_request);
    }
    pthread_mutex_unlock(&(sk->blocks[blockno%NUM_BLOCKS].block_mutex));
    return NULL;
  }  

  // Check whether the requested blockno is already read, if not, read it from the file
  // generate the first set of degrees of freedom according toa  random permutation

  // Still need to keep lock here as sk->blocks[blockno%NUM_BLOCKS].len might be updated by
  // send_ctcp() executing in a different thread.  DL
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

      // construct a data packet 
      if (sk->blocks[blockno%NUM_BLOCKS].skb[row] == NULL) {printf("WARNING: Failed to get a normal skb\n");continue;}
      Data_Pckt* msg    = &(sk->blocks[blockno%NUM_BLOCKS].skb[row]->msgbuf.msg);
      msg->flag         = NORMAL;
      msg->blockno      = blockno;
      msg->num_packets  = 1;
      msg->start_packet = row;
      msg->packet_coeff = 1;

      q_push_back(&(sk->coded_q[blockno%NUM_BLOCKS]), sk->blocks[blockno%NUM_BLOCKS].skb[row]);
    }  // Done with forming the initial set of coded packets
    dof_request = MAX(0, dof_request - (block_len - start));  // This many more to go
  }

  ///////////////////// ACTUAL RANDOM LINEAR CODING ///////////////////

  if (dof_request > 0){
    // Extra degrees of freedom are generated by picking a row randomly

    //fprintf(stdout, "coding job for %d coded packets \n", dof_request);

    int i, j;
    int dof_ix;
    uint8_t logcoeff;
    uint8_t coeff[BLOCK_SIZE];
    int nonzero;

    for (dof_ix = 0; dof_ix < dof_request; dof_ix++){

      Skb* skb=alloc_skb(); 
      if (!skb) {printf("WARNING: Failed to get a coded skb\n"); continue;} 
      Data_Pckt *msg = &(skb->msgbuf.msg);
      msg->blockno      = blockno;
      msg->start_packet = 0;
      msg->num_packets = block_len;
      msg->flag = CODED;
      memset(msg->payload, 0, PAYLOAD_SIZE);
      // generate random coefficients.  loop is to make sure that at least one is non-zero.
      nonzero=0;
      while (nonzero == 0) {
        msg->packet_coeff = (uint8_t) (random()%256); //record random seed used
        seedfastrand((uint32_t) (msg->packet_coeff+blockno));
        for(i = 0; i < block_len; i++){
          coeff[i] = (uint8_t) (fastrand()%GF);
          if (coeff[i]>0) nonzero++;
        }
      }
      for(i = 0; i < block_len; i++){
        if (coeff[i] == 0) continue;
        if (GF==256) {
           logcoeff = xFFlog(coeff[i]);
           for(j = 0; j < PAYLOAD_SIZE; j++){
             msg->payload[j] ^= fastFFmult(sk->blocks[blockno%NUM_BLOCKS].content[msg->start_packet+i][j], logcoeff);
           }
        } else {
           // GF(2)
           for(j = 0; j < PAYLOAD_SIZE; j+=4){
              msg->payload[j] ^= sk->blocks[blockno%NUM_BLOCKS].content[msg->start_packet+i][j];
              msg->payload[j+1] ^= sk->blocks[blockno%NUM_BLOCKS].content[msg->start_packet+i][j+1];
              msg->payload[j+2] ^= sk->blocks[blockno%NUM_BLOCKS].content[msg->start_packet+i][j+2];
              msg->payload[j+3] ^= sk->blocks[blockno%NUM_BLOCKS].content[msg->start_packet+i][j+3];
           }
        }
      }
      q_push_back(&(sk->coded_q[blockno%NUM_BLOCKS]), skb);

    }  // Done with forming the remaining set of coded packets

  }

  // At the moment we keep a lock on the whole block until all coded packets have been generated.   This might be quite a
  // long time, so might be worth looking into releasing and re-acquiring the lock inside the above for
  // loop so it doesn't block packet transmission/reception for so long.  DL
  pthread_mutex_unlock(&(sk->blocks[blockno%NUM_BLOCKS].block_mutex));

  return NULL;
}

//----------------END WORKER ---------------------------------------

//--------------------------------------------------------------------
uint32_t
readBlock(Block_t* blk, const void *buf, size_t buf_len){

  // starting from buf, read up to buf_len bytes into block #blockno
  // If the block is already full, do nothing
  uint16_t bytes_read; 
  uint32_t bytes_left = buf_len; 
  while(blk->len < BLOCK_SIZE && bytes_left){
    Skb* skb = alloc_skb(); 
    if (!skb) {printf("WARNING: readBlock failed to get an skb\n"); break;} 
    blk->skb[blk->len]=skb;
    char* tmp = skb->msgbuf.msg.payload;
    memset(tmp, 0, PAYLOAD_SIZE); // This is done to pad with 0's
    bytes_read = (uint16_t) MIN(PAYLOAD_SIZE-2, bytes_left);
    memcpy(tmp+2, buf+buf_len-bytes_left, bytes_read);
    bytes_read = htons(bytes_read);
    memcpy(tmp, &bytes_read, sizeof(uint16_t));

    bytes_read = ntohs(bytes_read);
    //printf("bytes_read from block %d = %d \t bytes_left %d\n", blockno, bytes_read, bytes_left);
   
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
  // reset the counters
  int i;
  for(i = 0; i < blk->len; i++){
    free_skb(blk->skb[i]);
  }
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

  if(!mkdir(sk->logdir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
    perror("Warning while making the logs directory");
  }

  if(!mkdir("/var/log/ctcp/figs", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
    perror("Warning while making the figs directory");
  }

  char* dir_name = malloc(20);

  sprintf(dir_name, "/var/log/ctcp/figs/%d-%02d-%02d",
          ptm->tm_year + 1900,
          ptm->tm_mon + 1,
          ptm->tm_mday);

  if(!mkdir(dir_name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
    perror("Warning while making the fig date directory");
  }

  sprintf(dir_name, "%s/%d-%02d-%02d", sk->logdir,
          ptm->tm_year + 1900,
          ptm->tm_mon + 1,
          ptm->tm_mday);

  if(!mkdir(dir_name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
    perror("Warning while making the log date directory");
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
      free(file);
    }

  sk->db = fopen(log_name, "w+");

  if(!sk->db){
    perror("An error ocurred while opening the log file");
  }


  if(auto_log)
    {
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
marshallData(Msgbuf* msgbuf){
  // Marshall the fields of the packet into the buffer
  htonpData(&(msgbuf->msg));
  return sizeof(msgbuf->msg);
}

bool 
unmarshallAck(Msgbuf* msgbuf){
  ntohpAck(&(msgbuf->ack));
  return TRUE;
}


// Compare the IP address and Port of two sockaddr structs

int
sockaddr_cmp(struct sockaddr_storage* addr1, struct sockaddr_storage* addr2){

  if (addr1->ss_family != addr2->ss_family)    return 1;   // No match

  if (addr1->ss_family == AF_INET){
    // IPv4 format
    // Cast to the IPv4 struct
    struct sockaddr_in *tmp1 = (struct sockaddr_in*)addr1;
    struct sockaddr_in *tmp2 = (struct sockaddr_in*)addr2;

    if (tmp1->sin_port != tmp2->sin_port) return 1;                // ports don't match
    if (tmp1->sin_addr.s_addr != tmp2->sin_addr.s_addr) return 1;  // Addresses don't match

    return 0; // We have a match
  } else if (addr1->ss_family == AF_INET6){
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
  }

  // initialize the thread pool
  thrpool_init( &(sk->workers), THREADS );
  // Initialize the block mutexes and queue of coded packets and counters
  for(i = 0; i < NUM_BLOCKS; i++){
    pthread_mutex_init( &(sk->blocks[i].block_mutex), NULL );
    pthread_cond_init( &(sk->blocks[i].block_free_condv), NULL );
    pthread_cond_init( &(sk->blocks[i].block_ready_condv), NULL );
    q_init(&(sk->coded_q[i]), 2*BLOCK_SIZE);
  }

  sk->status = ACTIVE;
  sk->error = NONE;

  //----------------- configurable variables -----------------//
  sk->rcvrwin    = 20;          /* rcvr window in mss-segments */
  sk->increment  = 1;           /* cc increment */
  sk->multiplier = 0.85;         /* cc backoff  &  fraction of rcvwind for initial ssthresh*/

  sk->initsegs   = 8;          /* slowstart initial */
  sk->ssincr     = 1;           /* slow start increment */
  sk->maxidle    = 10;       /* max idle before abort */

  sk->valpha     = 2;        /* vegas parameter */
  sk->vbeta      = 4;         /* vegas parameter */
  sk->debug      = 3;           /* Debug level */

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
  subpath->cntrtt = 0;
  subpath->toggle = 1;
  subpath->losscnt = 0;
  subpath->beg_snd_nxt = 0;
  subpath->beg_snd_una = 0;
  subpath->dec_snd_nxt = 0;

  if (sk->multiplier) {
    subpath->snd_ssthresh = sk->multiplier*MAX_CWND;
  } else {
    subpath->snd_ssthresh = 2147483647;  /* normal TCP, infinite */
  }
    // Statistics //
  subpath->idle       = 0;
  subpath->max_delta  = 0;
  subpath->vdecr      = 0;
  subpath->v0         = 0;    /* vegas decrements or no adjusts */


  subpath->minrtt     = 999999.0;
  subpath->basertt    = 999999.0;
  subpath->maxrtt     = 0;
  subpath->avrgrtt    = 0;
  subpath->srtt       = 0;
  subpath->rtt	      = 0;
  subpath->rto        = INIT_RTO;

  subpath->slr        = 0;
  subpath->slr_long   = SLR_LONG_INIT;
  subpath->slr_longstd= 0;
  subpath->total_loss = 0;
  //subpath->cli_addr   = NULL;
}


void
open_status_log(srvctcp_sock* sk, char* port){

  if(!mkdir(sk->logdir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)){
    perror("Warning while making the logs directory");
  }

  char buff[128];
  sprintf(buff,"%s/%u",sk->logdir,getpid());

  sk->status_log_fd = open(buff, O_RDWR | O_CREAT | O_TRUNC);

  if (sk->status_log_fd == -1){
    perror("Could not open status file");
    return;
  }

  
  if (flock(sk->status_log_fd, LOCK_EX) == -1){
    perror("Could not acquire the lock for status file");
    return;
  }
  

  //FILE *f_stream fdopen(csk->status_log_fd);
 
  int len;

  len = sprintf(buff, "\nport: %s\t", port);
  write(sk->status_log_fd, buff, len);

  len = sprintf(buff, "pid: %u\t", getpid());
  write(sk->status_log_fd, buff, len);

  if (flock(sk->status_log_fd, LOCK_UN) == -1){
    perror("Could not unlock the status file");
    return;
  }

  return;
}


void
log_srv_status(srvctcp_sock* sk){

  
  if (flock(sk->status_log_fd, LOCK_EX | LOCK_NB) == -1){
    perror("Could not acquire the lock for status file");
    return;
    } 

  char buff[256];
  int len;
  char sk_status_msg[3][16] = {
    "ACTIVE ",
    "CLOSED ",
    "CLOSING"
  };

  char path_status_msg[8][16] = {
    "SYN_RECV    ",
    "SYN_ACK_SENT",
    "ESTABLISHED ",
    "FIN_SENT    ", 
    "FIN_ACK_RECV", 
    "FIN_RECV    ", 
    "FIN_ACK_SENT", 
    "CLOSING     "
  } ;


  if(lseek(sk->status_log_fd, 0, SEEK_SET) == -1){
    perror("Could not seek");
  }

  char tmp;
  int count = 0;
  do{
    if (read(sk->status_log_fd, &tmp, 1) == -1){
      perror("file read");
    }
    count += (tmp == '\t');
  }while(count < 2);

  // We have reached the beginning of the new line

  len = sprintf(buff, "srv_status: %s\t", sk_status_msg[sk->status]);
  write(sk->status_log_fd, buff, len);

  int i;
  for (i = 0; i < sk->num_active; i++){
    if (  sk->active_paths[i]->slr*100 > 0.01){
    len = sprintf(buff, "Path %d (%s): %s %2.2f\t", i, 
		  inet_ntoa( ((struct sockaddr_in*)&(sk->active_paths[i]->cli_addr))->sin_addr), 
		  path_status_msg[sk->active_paths[i]->pathstate], 
		  sk->active_paths[i]->slr*100);
    } else {
    len = sprintf(buff, "Path %d (%s): %s --.--\t", i, 
		  inet_ntoa( ((struct sockaddr_in*)&(sk->active_paths[i]->cli_addr))->sin_addr), 
		  path_status_msg[sk->active_paths[i]->pathstate]);
    }
    write(sk->status_log_fd, buff, len);
  }

  if (flock(sk->status_log_fd, LOCK_UN) == -1){
    perror("Could not unlock the status file");
  }
  

  return;

}

void
log_pkt(srvctcp_sock* sk, int pkt, int pkt_size) {
    
    if (!sk->ctcp_probe) return; // logging disabled
    
    if (!sk->pkt_log) {
        sk->pkt_log = fopen("/tmp/ctcp-pkt_log", "a"); // probably shouldn't be a hard-wired filename
        if(!sk->pkt_log) perror("An error ocurred while opening the /tmp/ctcp-pkt_log log file");
    }
    
    if (sk->pkt_log) {
        fprintf(sk->pkt_log,"%u,%f,%s:%u,%u\n",
                pkt, getTime(), sk->clientip, (int) sk->clientport,
                pkt_size);
        fflush(sk->pkt_log);
    }
    return;
}


