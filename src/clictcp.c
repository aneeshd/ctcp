#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "clictcp.h"

void
usage(void) {
    fprintf(stderr, "Usage: clictcp [-options]             \n\
\t -p   port number to receive on. Defauls to 9999                \n\
\t -b   set socket receive buffer size (default 8192)               \n\
\t -D   enable debug level                                                \n\
\t -i   specify the interface (IP address) to bind to               \n\
\t -s   number of additional substreams (established over the main interface)\n");
    exit(0);
}


uint8_t inv_vec[256]={
  0x00, 0x01 ,0x8d ,0xf6 ,0xcb ,0x52 ,0x7b ,0xd1 ,0xe8 ,0x4f ,0x29 ,0xc0 ,0xb0 ,0xe1 ,0xe5 ,0xc7,
  0x74 ,0xb4 ,0xaa ,0x4b ,0x99 ,0x2b ,0x60 ,0x5f ,0x58 ,0x3f ,0xfd ,0xcc ,0xff ,0x40 ,0xee ,0xb2 ,
  0x3a ,0x6e ,0x5a ,0xf1 ,0x55 ,0x4d ,0xa8 ,0xc9 ,0xc1 ,0x0a ,0x98 ,0x15 ,0x30 ,0x44 ,0xa2 ,0xc2 ,
  0x2c ,0x45 ,0x92 ,0x6c ,0xf3 ,0x39 ,0x66 ,0x42 ,0xf2 ,0x35 ,0x20 ,0x6f ,0x77 ,0xbb ,0x59 ,0x19 ,
  0x1d ,0xfe ,0x37 ,0x67 ,0x2d ,0x31 ,0xf5 ,0x69 ,0xa7 ,0x64 ,0xab ,0x13 ,0x54 ,0x25 ,0xe9 ,0x09 ,
  0xed ,0x5c ,0x05 ,0xca ,0x4c ,0x24 ,0x87 ,0xbf ,0x18 ,0x3e ,0x22 ,0xf0 ,0x51 ,0xec ,0x61 ,0x17 ,
  0x16 ,0x5e ,0xaf ,0xd3 ,0x49 ,0xa6 ,0x36 ,0x43 ,0xf4 ,0x47 ,0x91 ,0xdf ,0x33 ,0x93 ,0x21 ,0x3b ,
  0x79 ,0xb7 ,0x97 ,0x85 ,0x10 ,0xb5 ,0xba ,0x3c ,0xb6 ,0x70 ,0xd0 ,0x06 ,0xa1 ,0xfa ,0x81 ,0x82 ,
  0x83 ,0x7e ,0x7f ,0x80 ,0x96 ,0x73 ,0xbe ,0x56 ,0x9b ,0x9e ,0x95 ,0xd9 ,0xf7 ,0x02 ,0xb9 ,0xa4 ,
  0xde ,0x6a ,0x32 ,0x6d ,0xd8 ,0x8a ,0x84 ,0x72 ,0x2a ,0x14 ,0x9f ,0x88 ,0xf9 ,0xdc ,0x89 ,0x9a ,
  0xfb ,0x7c ,0x2e ,0xc3 ,0x8f ,0xb8 ,0x65 ,0x48 ,0x26 ,0xc8 ,0x12 ,0x4a ,0xce ,0xe7 ,0xd2 ,0x62 ,
  0x0c ,0xe0 ,0x1f ,0xef ,0x11 ,0x75 ,0x78 ,0x71 ,0xa5 ,0x8e ,0x76 ,0x3d ,0xbd ,0xbc ,0x86 ,0x57 ,
  0x0b ,0x28 ,0x2f ,0xa3 ,0xda ,0xd4 ,0xe4 ,0x0f ,0xa9 ,0x27 ,0x53 ,0x04 ,0x1b ,0xfc ,0xac ,0xe6 ,
  0x7a ,0x07 ,0xae ,0x63 ,0xc5 ,0xdb ,0xe2 ,0xea ,0x94 ,0x8b ,0xc4 ,0xd5 ,0x9d ,0xf8 ,0x90 ,0x6b ,
  0xb1 ,0x0d ,0xd6 ,0xeb ,0xc6 ,0x0e ,0xcf ,0xad ,0x08 ,0x4e ,0xd7 ,0xe3 ,0x5d ,0x50 ,0x1e ,0xb3 ,
  0x5b ,0x23 ,0x38 ,0x34 ,0x68 ,0x46 ,0x03 ,0x8c ,0xdd ,0x9c ,0x7d ,0xa0 ,0xcd ,0x1a ,0x41 ,0x1c
};




/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void
ctrlc(clictcp_sock *csk){
  csk->end_time = csk->end_time - csk->start_time;  /* elapsed time */

  if (csk->end_time==0) csk->end_time = 0.1;

  /* don't include first pkt in data/pkt rate */
  printf("\n \n**Packets** %d pkts  %d acks  %d bytes\n**THRU** %f KBs %f Mbs %f secs \n",
         csk->pkts,csk->acks,PAYLOAD_SIZE*csk->pkts,1.e-3*PAYLOAD_SIZE*(csk->pkts)/csk->end_time,
         8.e-6*PAYLOAD_SIZE*(csk->pkts)/csk->end_time,csk->end_time);

  //printf("PAYLOAD_SIZE %d\n",PAYLOAD_SIZE);
  printf("**Ndofs** %d  coding loss rate %f\n", csk->ndofs, (double)csk->ndofs/(double)csk->pkts);
  printf("**Old packets** %d  old pkt loss rate %f\n", csk->old_blk_pkts, (double)csk->old_blk_pkts/(double)csk->pkts);
  printf("**Next Block packets** %d  nxt pkt loss rate %f\n", csk->nxt_blk_pkts, (double)csk->nxt_blk_pkts/(double)csk->pkts);
  printf("Total Channel loss rate %f\n", (double)csk->total_loss/(double)csk->last_seqno);
  printf("Total idle time %f, Gaussian Elimination delay %f, Decoding delay %f\n", csk->idle_total, csk->elimination_delay, csk->decoding_delay);


  // Flush the routing tables and iptables
  
  /* TODO TODO add this later for multi-interface
  int k;
  for (k=0; k < csk->substreams; k++){
    delete_table(k+1, k+1);
  }
  */

  exit(0);
}


/*
int
main(int argc, char** argv){
    char *file_name = FILE_NAME;
    char *lease_file = NULL;
    char *port = PORT;
    char *host = HOST;

    int c;
    while((c = getopt(argc, argv, "h:p:b:D:f:s:l:")) != -1) {
        switch (c) {
        case 'h':
          host = optarg;
          break;
        case 'p':
          port = optarg;
          break;
          //case 'b':
          //rcvspace = atoi(optarg);
          //break;
         
          //case 'D':
          //csk->debug = atoi(optarg);
          //break;
        case 'f':
          file_name = optarg;
          break;
        case 'l':
          lease_file = optarg;
          break;
        default:
          usage();
        }
    }

    clictcp_sock* csk = connect_ctcp(host, port, lease_file);


    if (csk == NULL){
      printf("Could not create CTCP socket\n");
      return 1;
    } else{
      
      char dst_file_name[100] = "Rcv_";
      strcat(dst_file_name, file_name);
      rcv_file = fopen(dst_file_name,  "wb");

      uint32_t f_buf_size = NUM_BLOCKS*BLOCK_SIZE*PAYLOAD_SIZE;
      uint32_t bytes_read;

      char *f_buffer = malloc(f_buf_size);

      printf("Calling read ctcp... \n");

      uint32_t total_bytes = 0;
      while(total_bytes < 11492499){
      
        bytes_read = read_ctcp(csk, f_buffer, f_buf_size);  
        fwrite(f_buffer, 1, bytes_read, rcv_file);
        total_bytes += bytes_read;
        //printf(" %d bytes receieved\n", bytes_read);
      }

      fclose(rcv_file);

      printf("Closed file successfully\n");

      fifo_free(&(csk->usr_cache));    // TODO should go to the close function
    }

    return 0;
}

*/



clictcp_sock*
connect_ctcp(char *host, char *port, char *lease_file){
    int optlen,rlth;
    struct addrinfo *result;
    int k; // for loop counter
    int rv;
    int numbytes;//[MAX_SUBSTREAMS];
    int rcvspace = 0;


    // Create the ctcp socket
    clictcp_sock* csk = create_clictcp_sock();

    dhcp_lease leases[MAX_SUBSTREAMS];
    if (lease_file != NULL){
      // Need to make sure there is at least one lease in the lease file
      csk->substreams = readLease(lease_file, leases);
    }

    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // This works for buth IPv4 and IPv6
    hints.ai_socktype = SOCK_DGRAM;

    if((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return NULL;
    }
    // TODO need to loop through to find interfaces number of connections

    for(result = servinfo; result != NULL; result = result->ai_next) {
      
      if((csk->sockfd[0] = socket(result->ai_family,
                                  result->ai_socktype,
                                  result->ai_protocol)) == -1){
        perror("Failed to initialize socket");
        continue;
      }
      for (k = 1; k < csk->substreams; k++){
        csk->sockfd[k] = socket(result->ai_family,
                                result->ai_socktype,
                                result->ai_protocol);
      }

      break;
    }

    // If we got here, it means that we couldn't initialize the socket.
    if(result  == NULL){
      perror("Failed to create socket");
      return NULL;
    }
    //freeaddrinfo(servinfo);


    //-------------- BIND to proper local address ----------------------
    // Only bind if the interface IP address is specified through the command line
    struct addrinfo *result_cli, *cli_info;

    if (lease_file != NULL){
      for (k=0; k < csk->substreams; k++){
      
        make_new_table(&leases[k], k+1, k+1);

        if((rv = getaddrinfo(leases[k].address, "9999", &hints, &cli_info)) != 0) {
          fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
          return NULL;
        }

        for(result_cli = cli_info; result_cli != NULL; result_cli = result_cli->ai_next) {
          printf("IP address trying to bind to %s \n\n", inet_ntoa(((struct sockaddr_in*)result_cli->ai_addr)->sin_addr));
     
          if (bind(csk->sockfd[k], result_cli->ai_addr, result_cli->ai_addrlen) == -1) {
            close(csk->sockfd[k]);
            perror("can't bind to local address");
            return NULL;
          }
        }

      }
    }

    //--------------DONE Binding-----------------------------------------
    
    //signal(SIGINT, (__sighandler_t) ctrlc);

    if (!rcvspace) rcvspace = MSS*MAX_CWND;

    optlen = sizeof(rcvspace);
    for(k=0; k < csk->substreams; k++){
      setsockopt(csk->sockfd[k],SOL_SOCKET,SO_RCVBUF, (char *) &rcvspace, optlen);
      getsockopt(csk->sockfd[k], SOL_SOCKET, SO_RCVBUF, (char *) &rlth, (socklen_t*)&optlen);
    }
    printf("ctcpcli using port %s rcvspace %d\n", port,rlth);

    char* buff = malloc(BUFFSIZE);
    memset(buff,0,BUFFSIZE);        /* pretouch */

    // ------------  Send a SYN packet for any new connection ----------------
    for (k = 0; k < csk->substreams; k++){
      Ack_Pckt* SYN_pkt = ackPacket(0, 0, 0);
      SYN_pkt->tstamp = 0;
      SYN_pkt->flag = SYN;
      int size = marshallAck(*SYN_pkt, buff);

      if((numbytes = sendto(csk->sockfd[k], buff, size, 0,
                            result->ai_addr, result->ai_addrlen)) == -1){
        perror("Failed to send SYN packet");
        free(buff);
        return NULL;
      }
      printf("New connection request sent to server on socket %d\n", k+1);
    }

    if (poll_SYN_ACK(csk) == -1){
      printf("Did not receive SYN ACK\n");
      free(buff);
      return NULL;
    }

    ///////////////////////   CONNECTION SETUP UP TO HERE  ///////////////

    // Let another thread do the job and return the socket

    pthread_t daemon_thread;

    rv = pthread_create( &daemon_thread, NULL, handle_connection, (void *) csk);

    // TODO TODO We need to have a close function to join the threads and gracefully close the connection
    free(buff);
    printf("Done with connecting...\n");
    return csk;
}

uint32_t 
read_ctcp(clictcp_sock* csk, void *usr_buf, size_t count){
  size_t res = fifo_pop(&(csk->usr_cache), usr_buf, count);
  printf("read_ctcp: pop %d bytes, csk->usr_cache size %d\n", res, csk->usr_cache.size);
  return res;

}


void
*handle_connection(void* arg){

  clictcp_sock* csk = (clictcp_sock*) arg;
  socklen_t srvlen;
  int k, numbytes;//[MAX_SUBSTREAMS];
  char *buff = malloc(BUFFSIZE);

  // READING FROM MULTIPLE SOCKET
  struct pollfd read_set[csk->substreams];
  for(k=0; k < csk->substreams; k++){
    read_set[k].fd = csk->sockfd[k];
    read_set[k].events = POLLIN;
  }

  Data_Pckt *msg = malloc(sizeof(Data_Pckt));
  double idle_timer;
  int curr_substream=0;
  int ready;
  do{
    idle_timer = getTime();
    // value -1 blocks until something is ready to read
    ready = poll(read_set, csk->substreams, -1);

    if(ready == -1){
      perror("poll");
    }else if (ready == 0){
      printf("Timeout occurred during poll! Should not happen with -1\n");
      ready = POLL_TO_FLG;
    }else{
      srvlen = sizeof(csk->srv_addr); // TODO: this is not necessary -> remove
      //printf("ready! %d\n", ready);

      do{
        if(read_set[curr_substream].revents & POLLIN){
          //printf("reading substream %d\n", curr_substream);
          if((numbytes = recvfrom(csk->sockfd[curr_substream], buff, MSS, 0,
                                  &(csk->srv_addr), &srvlen)) == -1){
            err_sys("recvfrom",csk);
          }
          if(numbytes <= 0) break;

          csk->idle_total += getTime() - idle_timer;

          csk->pkts++;
          csk->end_time = getTime();  /* last read */
          if (csk->start_time == 0) csk->start_time = csk->end_time;  /* first pkt time */

          // Unmarshall the packet
          bool match = unmarshallData(msg, buff, csk);

          switch (msg->flag){

          case FIN:
            ctrlc(csk);
            break;

          case SYN_ACK:
            // ACK the SYN-ACK
            break;

          case SYN:
            printf("ERROR: received SYN on the client side\n");
            ctrlc(csk);
            break;

          default:
            if (csk->debug > 6){
              printf("seqno %d num pkts %d start pkt %d curr_block %d dofs %d\n",
                     msg->seqno,  msg->num_packets, msg->start_packet, 
                     csk->curr_block, csk->blocks[csk->curr_block%NUM_BLOCKS].dofs);
            }
            if (csk->debug > 6 && msg->blockno != csk->curr_block ){
              printf("exp %d got %d\n", csk->curr_block, msg->blockno);
            }

            bldack(csk, msg, match, curr_substream);            
          }
           
          ready--;    // decrease the number of ready sockets
        }
        curr_substream++;
        if(curr_substream == csk->substreams) curr_substream = 0;

      }while(ready>0);
    }
    // TODO Should this be such that all sockfd are not -1? or should it be just the maximum..?
    // NOTE that the ones that are not active can be zero, or any value.
  }while(numbytes > 0); // TODO doesn't ever seem to exit the loop! Need to ctrlc

  free(buff);
  ctrlc(csk);

  pthread_exit(NULL);
  
}




void
err_sys(char *s, clictcp_sock *csk){
    perror(s);
    ctrlc(csk);     /* do stats on error */
}

void
bldack(clictcp_sock* csk, Data_Pckt *msg, bool match, int curr_substream){
  socklen_t srvlen;
  char *buff = malloc(BUFFSIZE);
  double elimination_timer = getTime();
  uint32_t blockno = msg->blockno;    //The block number of incoming packet
  uint8_t start;


  if (msg->seqno > csk->last_seqno+1){
    //printf("Loss report blockno %d Number of losses %d\n", msg->blockno, msg->seqno - last_seqno - 1);
    csk->total_loss += msg->seqno - (csk->last_seqno+1);
  }

    csk->last_seqno = MAX(msg->seqno, csk->last_seqno) ;  // ignore out of order packets

    if (blockno < csk->curr_block){
        // Discard the packet if it is coming from a decoded block or it is too far ahead
        // Send an appropriate ack to return the token
      if (csk->debug > 5){
        printf("Old packet  curr block %d packet blockno %d seqno %d substream %d \n", csk->curr_block, blockno, msg->seqno, curr_substream);
      }
      csk->old_blk_pkts++;
    }else if (blockno >= csk->curr_block + NUM_BLOCKS){
        printf("BAD packet: The block does not exist yet.\n");
    }else{
        int prev_dofs = csk->blocks[blockno%NUM_BLOCKS].dofs;

        // Otherwise, the packet should go to one of the blocks in the memory
        // perform the Gaussian elimination on the packet and put in proper block

        // If the block is already full, skip Gaussian elimination
        if(csk->blocks[blockno%NUM_BLOCKS].dofs < csk->blocks[blockno%NUM_BLOCKS].len){

          start = msg->start_packet;

            // Shift the row to make SHURE the leading coefficient is not zero
            int shift = shift_row(msg->packet_coeff, msg->num_packets);
            start += shift;

            // THE while loop!
            while(!isEmpty(msg->packet_coeff, msg->num_packets)){
                if(csk->blocks[blockno%NUM_BLOCKS].rows[start] == NULL){

                    msg->num_packets = MIN(msg->num_packets, BLOCK_SIZE - start);

                    // Allocate the memory for the coefficients in the matrix for this block
                    csk->blocks[blockno%NUM_BLOCKS].rows[start] = malloc(msg->num_packets);
                    csk->blocks[blockno%NUM_BLOCKS].row_len[start] = msg->num_packets;

                    // Allocate the memory for the content of the packet
                    csk->blocks[blockno%NUM_BLOCKS].content[start] = malloc(PAYLOAD_SIZE);

                    // Set the coefficients to be all zeroes (for padding if necessary)
                    memset(csk->blocks[blockno%NUM_BLOCKS].rows[start], 0, msg->num_packets);

                    // Normalize the coefficients and the packet contents
                    normalize(msg->packet_coeff, msg->payload, msg->num_packets);

                    // Put the coefficients into the matrix
                    memcpy(csk->blocks[blockno%NUM_BLOCKS].rows[start], msg->packet_coeff, msg->num_packets);

                    // Put the payload into the corresponding place
                    memcpy(csk->blocks[blockno%NUM_BLOCKS].content[start], msg->payload, PAYLOAD_SIZE);


                    if (csk->blocks[blockno%NUM_BLOCKS].rows[start][0] != 1){
                      printf("blockno %d\n", blockno);
                    }

                    // We got an innovative eqn
                    csk->blocks[blockno%NUM_BLOCKS].max_coding_wnd = msg->num_packets;
                    csk->blocks[blockno%NUM_BLOCKS].dofs++;
                    break;
                }else{
                    uint8_t pivot = msg->packet_coeff[0];
                    int i;

                    if (csk->debug > 9){
                        int ix;
                        for (ix = 0; ix < msg->num_packets; ix++){
                            printf(" %d ", msg->packet_coeff[ix]);
                        }
                        printf("seqno %d start%d isEmpty %d \n Row coeff", msg->seqno, start, isEmpty(msg->packet_coeff, msg->num_packets)==1);

                        for (ix = 0; ix < msg->num_packets; ix++){
                            printf(" %d ",csk->blocks[blockno%NUM_BLOCKS].rows[start][ix]);
                        }
                        printf("\n");
                    }

                    msg->packet_coeff[0] = 0; // TODO; check again
                    // Subtract row with index start with the row at hand (coffecients)
                    for(i = 1; i < csk->blocks[blockno%NUM_BLOCKS].row_len[start]; i++){
                        msg->packet_coeff[i] ^= FFmult(csk->blocks[blockno%NUM_BLOCKS].rows[start][i], pivot);
                    }

                    // Subtract row with index start with the row at hand (content)
                    for(i = 0; i < PAYLOAD_SIZE; i++){
                        msg->payload[i] ^= FFmult(csk->blocks[blockno%NUM_BLOCKS].content[start][i], pivot);
                    }

                    // Shift the row
                    shift = shift_row(msg->packet_coeff, msg->num_packets);
                    start += shift;
                }
            } // end while

            if(csk->blocks[blockno%NUM_BLOCKS].dofs == prev_dofs){
              csk->ndofs++;
            }
        } else {  // end if block.dof < block.len
          csk->nxt_blk_pkts++;   // If the block is full rank but not yet decoded, anything arriving is old
          if (csk->debug > 5){
            printf("NEXT packet  curr block %d packet blockno %d seqno %d substream %d\n", csk->curr_block, blockno, msg->seqno, curr_substream);
          }
        }

        csk->elimination_delay += getTime() - elimination_timer;

        //printf("current blk %d\t dofs %d \n ", curr_block, blocks[curr_block%NUM_BLOCKS].dofs);

    } // end else (if   curr_block <= blockno <= curr_block + NUM_BLOCKS -1 )

    //------------------------------------------------------------------------------------------------------

    // Build the ack packet according to the new information
    Ack_Pckt* ack = ackPacket(msg->seqno+1, csk->curr_block,
                              csk->blocks[csk->curr_block%NUM_BLOCKS].dofs);

    while(ack->dof_rec == csk->blocks[(ack->blockno)%NUM_BLOCKS].len){
        // The current block is decodable, so need to request for the next block
        // XXX make sure that NUM_BLOCKS is not 1, or this will break
        ack->blockno++;
        ack->dof_rec = csk->blocks[(ack->blockno)%NUM_BLOCKS].dofs;
    }

    ack->tstamp = msg->tstamp;

    // Marshall the ack into buff
    int size = marshallAck(*ack, buff);
    srvlen = sizeof(csk->srv_addr);

    if(sendto(csk->sockfd[curr_substream],buff, size, 0, &(csk->srv_addr), srvlen) == -1){
      err_sys("bldack: sendto",csk);
    }
    csk->acks++;

    //free(msg->packet_coeff);
    //free(msg->payload);

    if (csk->debug > 6){
        printf("Sent an ACK: ackno %d blockno %d\n", ack->ackno, ack->blockno);
    }

    //----------------------------------------------------------------
    //      CHECK IF ANYTHING CAN BE PUSHED TO THE APPLICATION     //

    if (csk->blocks[csk->curr_block%NUM_BLOCKS].dofs < csk->blocks[csk->curr_block%NUM_BLOCKS].len){
      partial_write(csk);
    }
    
    // Always try decoding the curr_block first, even if the next block is decodable, it is not useful
    while(csk->blocks[csk->curr_block%NUM_BLOCKS].dofs == csk->blocks[csk->curr_block%NUM_BLOCKS].len){
        // We have enough dofs to decode, DECODE!

        double decoding_timer = getTime();
        if (csk->debug > 4){
            printf("Starting to decode block %d ... ", csk->curr_block);
        }

        unwrap(&(csk->blocks[csk->curr_block%NUM_BLOCKS]));

        // Write the decoded packets into the file
        writeAndFreeBlock(&(csk->blocks[csk->curr_block%NUM_BLOCKS]), &(csk->usr_cache));

        // Initialize the block for next time
        initCodedBlock(&(csk->blocks[csk->curr_block%NUM_BLOCKS]));

        // Increment the current block number
        csk->curr_block++;

        csk->decoding_delay += getTime() - decoding_timer;
        if (csk->debug > 4){
          printf("Done within %f secs   blockno %d max_pkt_ix %d \n", getTime()-decoding_timer, csk->curr_block, csk->blocks[(csk->curr_block)%NUM_BLOCKS].max_packet_index);
        }
    } // end if the block is done

    free(buff);
}
//========================== END Build Ack ===============================================================


void
partial_write(clictcp_sock* csk){

  int blockno = csk->curr_block;
  uint8_t start = csk->blocks[blockno%NUM_BLOCKS].dofs_pushed;
  bool push_ready = TRUE; 
  uint16_t payload_len;
  int i;
  size_t bytes_pushed;

  if(csk->blocks[blockno%NUM_BLOCKS].dofs == csk->blocks[blockno%NUM_BLOCKS].max_packet_index){
    // We have enough dofs to decode, DECODE!
    unwrap(&(csk->blocks[blockno%NUM_BLOCKS]));
 }



  do {
    if ( csk->blocks[blockno%NUM_BLOCKS].rows[start] == NULL){
      push_ready = FALSE;
    } else {
      for (i = 1; i < csk->blocks[blockno%NUM_BLOCKS].row_len[start]; i++){
        if (csk->blocks[blockno%NUM_BLOCKS].rows[start][i]){
          push_ready = FALSE;
          break;
        }
      }
    }

    if (push_ready){
      // check the queue size
      // if enough room, push, otherwise, exit the push process
        // Read the first two bytes containing the length of the useful data
        memcpy(&payload_len, csk->blocks[blockno%NUM_BLOCKS].content[start], 2);

        // Convert to host order
        payload_len = ntohs(payload_len);
      if (fifo_getspace(&(csk->usr_cache)) >= payload_len){
        // push the packet to user cache


        // Write the contents of the decode block into the file
        //fwrite(blocks[blockno%NUM_BLOCKS].content[i]+2, 1, len, rcv_file);

        bytes_pushed = 0;
        while (bytes_pushed < payload_len){
          bytes_pushed += fifo_push(&(csk->usr_cache), csk->blocks[blockno%NUM_BLOCKS].content[start]+2+bytes_pushed, payload_len - bytes_pushed);
        }

        printf("write_ctcp: pushed %d bytes blockno %d max_pkt_ix %d start %d\n", 
               bytes_pushed, blockno, csk->blocks[blockno%NUM_BLOCKS].max_packet_index, start);
        //printf("blockno %d dofs %d max_pkt_ix %d dofs_pushed %d payload_len %d\n", blockno, csk->blocks[blockno%NUM_BLOCKS].dofs, csk->blocks[blockno%NUM_BLOCKS].max_packet_index, start, payload_len);
        start++;

      }else{
        push_ready = FALSE;
      }
    }

  } while (push_ready);

  csk->blocks[blockno%NUM_BLOCKS].dofs_pushed = start;

  return;
}


// TODO: TEST!
void
normalize(uint8_t* coefficients, char*  payload, uint8_t size){
  if (coefficients[0] != 1){
    uint8_t pivot = inv_vec[coefficients[0]];
    int i;
    
    for(i = 0; i < size; i++){
      coefficients[i] = FFmult(pivot,  coefficients[i]);
    }
    
    for(i = 0; i < PAYLOAD_SIZE; i++){
      payload[i] = FFmult(pivot,  payload[i]);
    }
  }
}


int
shift_row(uint8_t* buf, int len){
    if(len == 0) return 0;

    int shift;
    for(shift=0; !buf[shift]; shift++); // Get to the first nonzero element

    if(shift == 0) return shift;

    int i;
    for(i = 0; i < len-shift; i++){
        buf[i] = buf[shift+i];
    }

    memset(buf+len-shift, 0, shift);
    return shift;
}

bool
isEmpty(uint8_t* coefficients, uint8_t size){
    uint8_t result = 0;
    int i;
    for(i = 0; i < size; i++) result |= coefficients[i];
    return (result == 0);
}

/*
 * Initialize a new block struct
 */
void
initCodedBlock(Coded_Block_t *blk){
    blk->dofs = 0;
    blk->len  = BLOCK_SIZE; // this may change once we get packets
    blk->max_coding_wnd = 0;
    blk->dofs_pushed = 0;
    blk->max_packet_index = 0;

    int i;
    for(i = 0; i < BLOCK_SIZE; i++){
        blk->rows[i]    = NULL;
        blk->row_len[i]    = 0;
        blk->content[i] = NULL;
    }
}

void
unwrap(Coded_Block_t *blk){
    int row;
    int offset;
    int byte;
    //prettyPrint(blocks[blockno%NUM_BLOCKS].rows, MAX_CODING_WND);
    for(row = blk->max_packet_index-2; row >= blk->dofs_pushed; row--){
      
      /*
        int k;
        printf("row[%d] = ", row);
        for (k = 0; k < blk->row_len[row]; k++){
          printf("%d, ", blk->rows[row][k]);
        }
        printf("\n");
      */

        for(offset = 1; offset <  blk->row_len[row]; offset++){
            if(blk->rows[row][offset] == 0)
                continue;
            for(byte = 0; byte < PAYLOAD_SIZE; byte++){
                blk->content[row][byte]
                    ^= FFmult(blk->rows[row][offset],
                              blk->content[row+offset][byte] );
            }
        }

        blk->row_len[row] = 1;   // Now this row has only the diagonal entry
    }
}


void
writeAndFreeBlock(Coded_Block_t *blk, fifo_t *buffer){
    uint16_t len;
    int bytes_pushed, i;
    //printf("Writing a block of length %d\n", blocks[blockno%NUM_BLOCKS].len);

    for(i = blk->dofs_pushed; i < blk->len; i++){
        // Read the first two bytes containing the length of the useful data
        memcpy(&len, blk->content[i], 2);

        // Convert to host order
        len = ntohs(len);
        // Write the contents of the decode block into the file
        //fwrite(blocks[blockno%NUM_BLOCKS].content[i]+2, 1, len, rcv_file);

        bytes_pushed = 0;
        while (bytes_pushed < len){
          bytes_pushed += fifo_push(buffer, blk->content[i]+2+bytes_pushed, len - bytes_pushed);
        }

        // TODO remove the if condition (This is to avoid seg fault for the last block)
        //if (blocks[blockno%NUM_BLOCKS].len == BLOCK_SIZE){
            // Free the content
            free(blk->content[i]);

            // Free the matrix
            free(blk->rows[i]);
            //}
    }
}

bool
unmarshallData(Data_Pckt* msg, char* buf, clictcp_sock *csk){
    int index = 0;
    int part = 0;

    memcpy(&msg->tstamp, buf+index, (part = sizeof(msg->tstamp)));
    index += part;
    memcpy(&msg->flag, buf+index, (part = sizeof(msg->flag)));
    index += part;
    memcpy(&msg->seqno, buf+index, (part = sizeof(msg->seqno)));
    index += part;
    memcpy(&msg->blockno, buf+index, (part = sizeof(msg->blockno)));
    index += part;

    ntohpData(msg);

    memcpy(&msg->start_packet, buf+index, (part = sizeof(msg->start_packet)));
    index += part;

    memcpy(&msg->num_packets, buf+index, (part = sizeof(msg->num_packets)));
    index += part;

    // Need to make sure all the incoming packet_coeff are non-zero
    if (msg->blockno >= csk->curr_block){
      csk->blocks[msg->blockno%NUM_BLOCKS].max_packet_index = MAX(msg->start_packet + msg->num_packets , csk->blocks[msg->blockno%NUM_BLOCKS].max_packet_index);
    }

    int coding_wnd = MAX(msg->num_packets, csk->blocks[msg->blockno%NUM_BLOCKS].max_coding_wnd);
    msg->packet_coeff = malloc(coding_wnd);

    // Padding with zeroes
    memset(msg->packet_coeff, 0, coding_wnd);

    int i;
    for(i = 0; i < msg->num_packets; i++){
        memcpy(&msg->packet_coeff[i], buf+index, (part = sizeof(msg->packet_coeff[i])));
        index += part;
    }

    msg->num_packets = coding_wnd;


    msg->payload = malloc(PAYLOAD_SIZE);
    memcpy(msg->payload, buf+index, (part = PAYLOAD_SIZE));
    index += part;

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


int
marshallAck(Ack_Pckt msg, char* buf){
    int index = 0;
    int part = 0;
    int ack_size = ACK_SIZE;

    //Set to zeroes before starting
    memset(buf, 0, ack_size);

    // Marshall the fields of the packet into the buffer
    htonpAck(&msg);
    memcpy(buf + index, &msg.tstamp, (part = sizeof(msg.tstamp)));
    index += part;
    memcpy(buf + index, &msg.flag, (part = sizeof(msg.flag)));
    index += part;
    memcpy(buf + index, &msg.ackno, (part = sizeof(msg.ackno)));
    index += part;
    memcpy(buf + index, &msg.blockno, (part = sizeof(msg.blockno)));
    index += part;
    memcpy(buf + index, &msg.dof_rec, (part = sizeof(msg.dof_rec)));
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

int
readLease(char *leasefile, dhcp_lease *leases){
  
  /* read lease file if there, keyword value */
  FILE *fp;
  char line[128], type[64], val1[64], val2[64];
  int substreams = -1;

  fp = fopen(leasefile,"r");

  if (fp == NULL) {
    printf("ctcp unable to open %s\n",leasefile);
    return -1;
  }

  while (fgets(line, sizeof (line), fp) != NULL) {
    sscanf(line,"%s %s %s",type, val1, val2);
    if (*type == '#') continue;
    else if (strcmp(type,"lease")==0){
      substreams++;
      
      printf("lease %d\n", substreams);
    }
    else if (strcmp(type,"fixed-address")==0){
      leases[substreams].address = strndup(val1, strlen(val1)-1);
      printf("Address: %s\n", leases[substreams].address);
    }    
    else if (strcmp(type,"interface")==0){
      leases[substreams].interface = strndup(val1+sizeof(char), strlen(val1)-3); 
      printf("Iface: %s\n", leases[substreams].interface);
    }    
    else if (strcmp(type,"option")==0){
      if (strcmp(val1,"subnet-mask")==0){
        leases[substreams].netmask = strndup(val2, strlen(val2)-1);
        printf("Netmask: %s\n", leases[substreams].netmask);
      }    
      else if (strcmp(val1,"routers")==0){
        leases[substreams].gateway = strndup(val2, strlen(val2)-1);
        printf("Gateway:%s\n\n", leases[substreams].gateway);
      }    
    }
    
  }

  return substreams+1; 
}



void 
make_new_table(dhcp_lease* lease, int table_number, int mark_number){

  printf("/****** making table %d, mark %d *****/\n", table_number, mark_number);

  // command to be used for system()
  char* command = malloc(150);

  // Flush routing table (table_number) //
  sprintf(command, "ip route flush table %d", table_number);
  //printf("%s\n", command);
  system(command);
  
  // Figure out the Network Address, Netmask Number, etc.//
  struct in_addr* address = malloc(sizeof(struct in_addr));
  struct in_addr* netmask = malloc(sizeof(struct in_addr));
  struct in_addr* network_addr = malloc(sizeof(struct in_addr)); //Network address
  uint32_t mask, network;
  int maskbits; //Netmask number
  if(!inet_aton(lease->address, address)){
    printf("%s is not a good IP address\n", lease->address);
    exit(1);
  }
  if(!inet_aton(lease->netmask, netmask)){
    printf("%s is not a good netmask\n", lease->netmask);
    exit(1);
  }
  /* compute netmask number*/
  mask = ntohl(netmask->s_addr);
  for(maskbits=32; (mask & 1L<<(32-maskbits))==0; maskbits--);
  

  // printf("IP address %s\n", inet_ntoa(*address));
  // printf("Netmask %s\n", inet_ntoa(*netmask));
  // printf("Netmask bits %d\n", maskbits);
  
  /* compute network address -- AND netmask and IP addr */
  network = ntohl(address->s_addr) & ntohl(netmask->s_addr);
  network_addr->s_addr = htonl(network);
  // printf("Network %s\n", inet_ntoa(*network_addr));

  // Add routes to the routing table (table_number)//
  memset(command, '\0', sizeof(command));
  sprintf(command, "ip route add table %d %s/%d dev %s proto static src %s", table_number, inet_ntoa(*network_addr), maskbits, lease->interface, lease->address);
  system(command);

  memset(command, '\0', sizeof(command));
  sprintf(command, "ip route add table %d default via %s dev %s proto static", table_number, lease->gateway, lease->interface);
  system(command);
  
  //memset(command, '\0', sizeof(command));
  //sprintf(command, "ip route show table %d", table_number);
  //printf("%s\n", command);
  //system(command);
  //printf("\n");

  // Create and add rules//
  memset(command, '\0', sizeof(command));
  sprintf(command, "iptables -t mangle -A OUTPUT -s %s -j MARK --set-mark %d", lease->address, mark_number);
  system(command);

  memset(command, '\0', sizeof(command));
  sprintf(command, "ip rule add fwmark %d table %d", mark_number, table_number);
  system(command);
  
  //system("iptables -t mangle -S");
  //printf("\n");

  //printf("ip rule show\n");
  //system("ip rule show");
  //printf("\n");
  
  //printf("ip route flush cache\n");
  system("ip route flush cache");
  //printf("\n");
  
  return;
}

void
delete_table(int table_number, int mark_number){
  printf("/****** deleting table %d, mark %d *****/\n", table_number, mark_number);
  char* command = malloc(150);

  // Flush routing table (table_number) //
  sprintf(command, "ip route flush table %d", table_number);
  system(command);

  memset(command, '\0', sizeof(command));
  sprintf(command, "ip rule delete fwmark %d table %d", mark_number, table_number);
  if (system(command) == -1){
    printf("No ip rule to delete\n");
  }

  memset(command, '\0', sizeof(command));
  sprintf(command, "iptables -t mangle -F");
  system(command);

  // Printing...
  //memset(command, '\0', sizeof(command));
  //sprintf(command, "ip route show table %d", table_number);
  //printf("%s\n", command);
  //system(command);
  //printf("\n");

  //printf("ip rule show\n");
  //system("ip rule show");
  //printf("\n");
  
  //printf("ip route flush cache\n");
  system("ip route flush cache");
  //printf("\n");
}

clictcp_sock* 
create_clictcp_sock(void){
  int k;

  clictcp_sock* sk = malloc(sizeof(clictcp_sock));

  sk-> curr_block = 1;
    
  // Initialize the blocks
  for(k = 0; k < NUM_BLOCKS; k++){
    sk->blocks[k].rows = malloc(BLOCK_SIZE*sizeof(char*));
    sk->blocks[k].content = malloc(BLOCK_SIZE*sizeof(char*));
    sk->blocks[k].row_len = malloc(BLOCK_SIZE*sizeof(int));
    initCodedBlock(&(sk->blocks[k]));
  }

  // MULTIPLE SUBSTREAMS
  sk->substreams = 1;
 
  fifo_init(&(sk->usr_cache), NUM_BLOCKS*BLOCK_SIZE*PAYLOAD_SIZE);

  //---------------- STATISTICS & ACCOUTING ------------------//
  sk->pkts = 0;
  sk->acks = 0;
  sk->debug = 0;
  sk->ndofs = 0;
  sk->old_blk_pkts = 0;
  sk->nxt_blk_pkts = 0;
  sk->total_loss = 0;
  sk->idle_total = 0; // The total time the client has spent waiting for a packet
  sk->decoding_delay = 0;
  sk->elimination_delay = 0;
  sk->last_seqno = 0;
  sk->start_time = 0;

  return sk;
}

int 
poll_SYN_ACK(clictcp_sock *csk){
  char *buff = malloc(BUFFSIZE);
  Data_Pckt *msg = malloc(sizeof(Data_Pckt));
  int k, ready, numbytes;
  int curr_substream = 0;
  socklen_t srvlen;

  // READING FROM MULTIPLE SOCKET
  struct pollfd read_set[csk->substreams];
  for(k=0; k < csk->substreams; k++){
    read_set[k].fd = csk->sockfd[k];
    read_set[k].events = POLLIN;
  }

  // value -1 blocks until something is ready to read
  ready = poll(read_set, csk->substreams, SYN_ACK_TO);

  if(ready == -1){
    perror("poll");
    free(buff);
    return -1;
  }else if (ready == 0){
    printf("Timeout occurred during poll!\n");
    free(buff);
    return -1;
  }else{
    srvlen = sizeof(csk->srv_addr);
    while (curr_substream < csk->substreams){

      if(read_set[curr_substream].revents & POLLIN){
        if((numbytes = recvfrom(csk->sockfd[curr_substream], buff, MSS, 0,
                                &(csk->srv_addr), &srvlen)) == -1){
          err_sys("recvfrom",csk);
        }
        if(numbytes <= 0) {
          free(buff);
          return -1;
        }
          
        // Unmarshall the packet
        bool match = unmarshallData(msg, buff, csk);
        if (msg->flag == SYN_ACK){
          free(buff);
          return 0;
        }else{
          printf("Expected SYN ACK, received something else!\n");
          free(buff);
          return -1;
        }
      }

      curr_substream++;
    }  /* end while */
  }
  free(buff);
  return -1;

}
