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

/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void
ctrlc(void){
  end_time = end_time-start_time;  /* elapsed time */
  if (end_time==0)end_time=.1;
  /* don't include first pkt in data/pkt rate */
  printf("\n \n%d pkts  %d acks  %d bytes\n %f KBs %f Mbs %f secs \n",
         pkts,acks,PAYLOAD_SIZE*pkts,1.e-3*PAYLOAD_SIZE*(pkts-1)/end_time,
         8.e-6*PAYLOAD_SIZE*(pkts-1)/end_time,end_time);
  printf("PAYLOAD_SIZE %d\n",PAYLOAD_SIZE);
  printf("**Ndofs** %d  coding loss rate %f\n", ndofs, (double)ndofs/(double)pkts);
  printf("**Old packets** %d  old pkt loss rate %f\n", old_blk_pkts, (double)old_blk_pkts/(double)pkts);
  printf("Total Channel loss rate %f\n", (double)total_loss/(double)last_seqno);
  printf("Total idle time %f, Gaussian Elimination delay %f, Decoding delay %f\n", idle_total, elimination_delay, decoding_delay);
  fclose(rcv_file);
  exit(0);
}

int
main(int argc, char** argv){
    int optlen,rlth;
    char *file_name = FILE_NAME;
    char *port = PORT;
    char *host = HOST;
    int numbytes;//[MAX_SUBSTREAMS];
    int k; // for loop counter
    int rv;

    int substreams = 0;
    char* local_addr[MAX_SUBSTREAMS];
    local_addr[0] = NULL;

    int c;
    while((c = getopt(argc, argv, "h:p:b:D:f:i:s:")) != -1) {
        switch (c) {
        case 'h':
          host = optarg;
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
        case 'f':
          file_name = optarg;
          break;
        case 'i':
          if (substreams < MAX_SUBSTREAMS){
            local_addr[substreams] = optarg;
            substreams++;
          }
          break;
        case 's':
          substreams = MIN(substreams + atoi(optarg), MAX_SUBSTREAMS);
          break;
        default:
          usage();
        }
    }

    if (substreams == 0){
      substreams = 1;   // The default number of substreams
    }

    // Open the file where the contents of the file transfer will be stored
    char dst_file_name[100] = "Rcv_";
    strcat(dst_file_name, file_name);
    rcv_file = fopen(dst_file_name,  "wb");



    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // This works for buth IPv4 and IPv6
    hints.ai_socktype = SOCK_DGRAM;

    if((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    // TODO need to loop through to find interfaces number of connections

    for(result = servinfo; result != NULL; result = result->ai_next) {
      
      if((sockfd[0] = socket(result->ai_family,
                             result->ai_socktype,
                             result->ai_protocol)) == -1){
        perror("atoucli: failed to initialize socket");
        continue;
      }
      for (k=1; k<substreams; k++){
        sockfd[k] = socket(result->ai_family,
                           result->ai_socktype,
                           result->ai_protocol);
      }
      
      break;        
    }  

    // If we got here, it means that we couldn't initialize the socket.
    if(result  == NULL){
      err_sys("atoucli: failed to bind to socket");
    }
    freeaddrinfo(servinfo);



    //-------------- BIND to proper local address ----------------------
    // Only bind if the interface IP address is specified through the command line
    struct addrinfo *result_cli, *cli_info;
    k = 0;
    while (local_addr[k] != NULL){
      
      if((rv = getaddrinfo(local_addr[k], "9999", &hints, &cli_info)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
      }

      for(result_cli = cli_info; result_cli != NULL; result_cli = result_cli->ai_next) {
        printf("IP address trying to bind to %s \n", inet_ntoa(((struct sockaddr_in*)result_cli->ai_addr)->sin_addr));
     
        if (bind(sockfd[k], result_cli->ai_addr, result_cli->ai_addrlen) == -1) {
          close(sockfd[k]);
          err_sys("atousrv: can't bind local address");
          continue;
        }
      }
        
    }

    //--------------DONE Binding-----------------------------------------
    

    signal(SIGINT, (__sighandler_t) ctrlc);


    // Send request to the server.
    // We only send request (with the file name) through the first substream
    if((numbytes = sendto(sockfd[0], file_name, (strlen(file_name)+1)*sizeof(char), 0,
                          result->ai_addr, result->ai_addrlen)) == -1){
      err_sys("sendto: Request failed");
    }
    fprintf(stdout, "Request sent for %s on the first socket\n", file_name);

    // Send a SYN packet for any new connection
    for (k = 1; k < substreams; k++){
      Ack_Pckt* SYN_pkt = ackPacket(0, 0, 0);
      SYN_pkt->tstamp = 0;
      SYN_pkt->flag = SYN;
      int size = marshallAck(*SYN_pkt, buff);
     
      if((numbytes = sendto(sockfd[k], buff, size, 0,
                            result->ai_addr, result->ai_addrlen)) == -1){
        err_sys("sendto: Request failed");
      }
      printf("New connection request sent to server on socket %d\n", k+1);
    }


    if (!rcvspace) rcvspace = MSS*MAX_CWND;

    optlen = sizeof(rcvspace);
    for(k=0; k<substreams; k++){
      setsockopt(sockfd[k],SOL_SOCKET,SO_RCVBUF, (char *) &rcvspace, optlen);
      getsockopt(sockfd[k], SOL_SOCKET, SO_RCVBUF, (char *) &rlth, (socklen_t*)&optlen);
    }
    printf("ctcpcli using port %s rcvspace %d\n", port,rlth);

    memset(buff,0,BUFFSIZE);        /* pretouch */

    curr_block = 1;
    

    // Initialize the blocks
    for(k = 0; k < NUM_BLOCKS; k++){
        blocks[k].rows = malloc(BLOCK_SIZE*sizeof(char*));
        blocks[k].content = malloc(BLOCK_SIZE*sizeof(char*));
        blocks[k].row_len = malloc(BLOCK_SIZE*sizeof(int));
        initCodedBlock(k);
    }

    // READING FROM MULTIPLE SOCKET
    struct pollfd read_set[substreams];
    for(k=0; k<substreams; k++){
      read_set[k].fd = sockfd[k];
      read_set[k].events = POLLIN;
    }


    Data_Pckt *msg = malloc(sizeof(Data_Pckt));
    double idle_timer;
    int curr_substream=0;
    int ready;
    do{
      idle_timer = getTime();
      // value -1 blocks until something is ready to read
      ready = poll(read_set, substreams, -1); 
      
      if(ready == -1){
        perror("poll"); 
      }else if (ready == 0){
        printf("Timeout occurred during poll! Should not happen with -1\n");
      }else{
        srvlen = sizeof srv_addr; // TODO: this is not necessary -> remove
        //printf("ready! %d\n", ready);

        do{
          if(read_set[curr_substream].revents & POLLIN){
            //printf("reading substream %d\n", curr_substream);
            if((numbytes = recvfrom(sockfd[curr_substream], buff, MSS, 0,
                                    &srv_addr, &srvlen)) == -1){
              err_sys("recvfrom");
            }
            if(numbytes <= 0) break;
            
            idle_total += getTime() - idle_timer;

            pkts++;
            end_time = secs();  /* last read */
            if (start_time == 0) start_time = end_time;  /* first pkt time */
            
            // Unmarshall the packet
            bool match = unmarshallData(msg, buff);
            if(msg->flag == FIN_CLI){
              ctrlc();
            }
            if (debug > 6){
              printf("seqno %d blklen %d num pkts %d start pkt %d curr_block %d dofs %d\n",msg->seqno, msg->blk_len, msg->num_packets, msg->start_packet, curr_block, blocks[curr_block%NUM_BLOCKS].dofs);
            }
            if (debug > 6 && msg->blockno != curr_block ) printf("exp %d got %d\n", curr_block, msg->blockno);
            
            bldack(msg, match, curr_substream);
            ready -= 1;
          }
          curr_substream++;
          if(curr_substream == substreams) curr_substream = 0;
        }while(ready>0);
      }
        // TODO Should this be such that all sockfd are not -1? or should it be just the maximum..? 
        // NOTE that the ones that are not active can be zero, or any value. 
    }while(numbytes > 0); // TODO doesn't ever seem to exit the loop! Need to ctrlc 

    ctrlc();

    return 0;
}


void
err_sys(char *s){
    perror(s);
    ctrlc();     /* do stats on error */
}

void
bldack(Data_Pckt *msg, bool match, int curr_substream){
    double elimination_timer = getTime();
    uint32_t blockno = msg->blockno;    //The block number of incoming packet

    // Update the incoming block lenght
    blocks[blockno%NUM_BLOCKS].len = msg->blk_len;

    if (msg->seqno > last_seqno+1){
        //printf("Loss report blockno %d Number of losses %d\n", msg->blockno, msg->seqno - last_seqno - 1);
        total_loss += msg->seqno - (last_seqno+1);
    }

    last_seqno = MAX(msg->seqno, last_seqno) ;  // ignore out of order packets

    if (blockno < curr_block){
        // Discard the packet if it is coming from a decoded block or it is too far ahead
        // Send an appropriate ack to return the token
        //printf("Old packet.\n");
        old_blk_pkts++;
    }else if (blockno >= curr_block + NUM_BLOCKS){
        //printf("BAD packet: The block does not exist yet.\n");
    }else{
        int prev_dofs = blocks[blockno%NUM_BLOCKS].dofs;

        // Otherwise, the packet should go to one of the blocks in the memory
        // perform the Gaussian elimination on the packet and put in proper block

        // If the block is already full, skip Gaussian elimination
        if(blocks[blockno%NUM_BLOCKS].dofs < blocks[blockno%NUM_BLOCKS].len){

            uint8_t start = msg->start_packet;

            // Shift the row to make SHURE the leading coefficient is not zero
            int shift = shift_row(msg->packet_coeff, msg->num_packets);
            start += shift;

            // THE while loop!
            while(!isEmpty(msg->packet_coeff, msg->num_packets)){
                if(blocks[blockno%NUM_BLOCKS].rows[start] == NULL){
                    // Allocate the memory for the coefficients in the matrix for this block
                    blocks[blockno%NUM_BLOCKS].rows[start] = malloc(msg->num_packets);
                    blocks[blockno%NUM_BLOCKS].row_len[start] = msg->num_packets;

                    // Allocate the memory for the content of the packet
                    blocks[blockno%NUM_BLOCKS].content[start] = malloc(PAYLOAD_SIZE);

                    // Set the coefficients to be all zeroes (for padding if necessary)
                    memset(blocks[blockno%NUM_BLOCKS].rows[start], 0, msg->num_packets);

                    // Normalize the coefficients and the packet contents
                    normalize(msg->packet_coeff, msg->payload, msg->num_packets);

                    // Put the coefficients into the matrix
                    memcpy(blocks[blockno%NUM_BLOCKS].rows[start], msg->packet_coeff, msg->num_packets);

                    // Put the payload into the corresponding place
                    memcpy(blocks[blockno%NUM_BLOCKS].content[start], msg->payload, PAYLOAD_SIZE);

                    // We got an innovative eqn
                    blocks[blockno%NUM_BLOCKS].max_coding_wnd = msg->num_packets;
                    blocks[blockno%NUM_BLOCKS].dofs++;
                    break;
                }else{
                    uint8_t pivot = msg->packet_coeff[0];
                    int i;

                    if (debug > 9){
                        int ix;
                        for (ix = 0; ix < msg->num_packets; ix++){
                            printf(" %d ", msg->packet_coeff[ix]);
                        }
                        printf("seqno %d start%d isEmpty %d \n Row coeff", msg->seqno, start, isEmpty(msg->packet_coeff, msg->num_packets)==1);

                        for (ix = 0; ix < msg->num_packets; ix++){
                            printf(" %d ",blocks[blockno%NUM_BLOCKS].rows[start][ix]);
                        }
                        printf("\n");
                    }

                    msg->packet_coeff[0] = 0; // TODO; check again
                    // Subtract row with index start with the row at hand (coffecients)
                    for(i = 1; i < blocks[blockno%NUM_BLOCKS].row_len[start]; i++){
                        msg->packet_coeff[i] ^= FFmult(blocks[blockno%NUM_BLOCKS].rows[start][i], pivot);
                    }

                    // Subtract row with index start with the row at hand (content)
                    for(i = 0; i < PAYLOAD_SIZE; i++){
                        msg->payload[i] ^= FFmult(blocks[blockno%NUM_BLOCKS].content[start][i], pivot);
                    }

                    // Shift the row
                    shift = shift_row(msg->packet_coeff, msg->num_packets);
                    start += shift;
                }
            } // end while
        } // end if block.dof < block.len

        if(blocks[blockno%NUM_BLOCKS].dofs == prev_dofs){
            ndofs++;
        }

        elimination_delay += getTime() - elimination_timer;

        //printf("current blk %d\t dofs %d \n ", curr_block, blocks[curr_block%NUM_BLOCKS].dofs);

    } // end else (if   curr_block <= blockno <= curr_block + NUM_BLOCKS -1 )

    //------------------------------------------------------------------------------------------------------

    // Build the ack packet according to the new information
    Ack_Pckt* ack = ackPacket(msg->seqno+1, curr_block,
                              blocks[curr_block%NUM_BLOCKS].len - blocks[curr_block%NUM_BLOCKS].dofs);

    if(blocks[curr_block%NUM_BLOCKS].dofs == blocks[curr_block%NUM_BLOCKS].len){
        // The current block is decodable, so need to request for the next block
        // XXX make sure that NUM_BLOCKS is not 1, or this will break
        ack->blockno = curr_block+1;
        ack->dof_req =   blocks[(curr_block+1)%NUM_BLOCKS].len - blocks[(curr_block+1)%NUM_BLOCKS].dofs;
    }

    ack->tstamp = msg->tstamp;

    // Marshall the ack into buff
    int size = marshallAck(*ack, buff);
    srvlen = sizeof(srv_addr);
    if(sendto(sockfd[curr_substream],buff, size, 0, &srv_addr, srvlen) == -1){
        err_sys("bldack: sendto");
    }
    acks++;

    //free(msg->packet_coeff);
    //free(msg->payload);

    if (debug > 6){
        printf("Sent an ACK: ackno %d blockno %d\n", ack->ackno, ack->blockno);
    }

    //------------------------------------------------------------------------------------------------------

    // Always try decoding the curr_block first, even if the next block is decodable, it is not useful
    if(blocks[curr_block%NUM_BLOCKS].dofs == blocks[curr_block%NUM_BLOCKS].len){
        // We have enough dofs to decode, DECODE!

        double decoding_timer = getTime();
        if (debug > 4){
            printf("Starting to decode block %d ... ", curr_block);
        }

        unwrap(curr_block);

        // Write the decoded packets into the file
        writeAndFreeBlock(curr_block);

        // Initialize the block for next time
        initCodedBlock(curr_block);

        // Increment the current block number
        curr_block++;

        decoding_delay += getTime() - decoding_timer;
        if (debug > 4){
            printf("Done within %f secs\n", getTime()-decoding_timer);
        }
    } // end if the block is done

}
//========================== END Build Ack ===============================================================


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
initCodedBlock(uint32_t blockno){
    blocks[blockno%NUM_BLOCKS].dofs = 0;
    blocks[blockno%NUM_BLOCKS].len  = BLOCK_SIZE; // this may change once we get packets
    blocks[blockno%NUM_BLOCKS].max_coding_wnd = 0;

    int i;
    for(i = 0; i < BLOCK_SIZE; i++){
        blocks[blockno%NUM_BLOCKS].rows[i]    = NULL;
        blocks[blockno%NUM_BLOCKS].row_len[i]    = 0;
        blocks[blockno%NUM_BLOCKS].content[i] = NULL;
    }
}

void
unwrap(uint32_t blockno){
    int row;
    int offset;
    int byte;
    //prettyPrint(blocks[blockno%NUM_BLOCKS].rows, MAX_CODING_WND);
    for(row = blocks[blockno%NUM_BLOCKS].len-2; row >= 0; row--){
        for(offset = 1; offset <  blocks[blockno%NUM_BLOCKS].row_len[row]; offset++){
            if(blocks[blockno%NUM_BLOCKS].rows[row][offset] == 0)
                continue;
            for(byte = 0; byte < PAYLOAD_SIZE; byte++){
                blocks[blockno%NUM_BLOCKS].content[row][byte]
                    ^= FFmult(blocks[blockno%NUM_BLOCKS].rows[row][offset],
                              blocks[blockno%NUM_BLOCKS].content[row+offset][byte] );
            }
        }
    }
}


void
writeAndFreeBlock(uint32_t blockno){
    uint16_t len;
    int i;
    //printf("Writing a block of length %d\n", blocks[blockno%NUM_BLOCKS].len);

    for(i=0; i < blocks[blockno%NUM_BLOCKS].len; i++){
        // Read the first two bytes containing the length of the useful data
        memcpy(&len, blocks[blockno%NUM_BLOCKS].content[i], 2);

        // Convert to host order
        len = ntohs(len);
        // Write the contents of the decode block into the file
        fwrite(blocks[blockno%NUM_BLOCKS].content[i]+2, 1, len, rcv_file);

        // TODO remove the if condition (This is to avoid seg fault for the last block)
        //if (blocks[blockno%NUM_BLOCKS].len == BLOCK_SIZE){
            // Free the content
            //    free(blocks[blockno%NUM_BLOCKS].content[i]);

            // Free the matrix
            free(blocks[blockno%NUM_BLOCKS].rows[i]);
            //}
    }
}

bool
unmarshallData(Data_Pckt* msg, char* buf){
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

    if (msg->flag == PARTIAL_BLK){
        memcpy(&msg->blk_len, buf+index, (part = sizeof(msg->blk_len)));
        index += part;
    } else {
        msg->blk_len = BLOCK_SIZE;
    }

    memcpy(&msg->start_packet, buf+index, (part = sizeof(msg->start_packet)));
    index += part;

    memcpy(&msg->num_packets, buf+index, (part = sizeof(msg->num_packets)));
    index += part;

    //  msg->packet_coeff = malloc(MIN(coding_wnd, blocks[msg->blockno%NUM_BLOCKS].len - msg->start_packet));

    int coding_wnd = MAX(msg->num_packets, blocks[msg->blockno%NUM_BLOCKS].max_coding_wnd);
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
    memcpy(buf + index, &msg.dof_req, (part = sizeof(msg.dof_req)));
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



