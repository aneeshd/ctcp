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
#include "clictcp.h"

#define PORT "7890"
#define HOST "127.0.0.1"
#define FILE_NAME "Avatar.mov"

struct sockaddr srv_addr;
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
  fclose(rcv_file);
	exit(0);
}


int
main(int argc, char** argv){
	int optlen,rlth;
  char *file_name = FILE_NAME;
  char *port = PORT;
  char *host = HOST;
  int numbytes;
  struct addrinfo hints, *servinfo;
  int rv;
	int c;
  
	while((c = getopt(argc, argv, "h:sd:p:b:D:f:")) != -1) { 
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
    case 'f':
      file_name = optarg;
      break;
    default:
      usage();
	  }
	}
  
  // Open the file where the contents of the file transfer will be stored
  char dst_file_name[100] = "Rcv_";
  strcat(dst_file_name, file_name);
  printf("dest %s", dst_file_name);
  rcv_file = fopen(dst_file_name,  "wb");
  
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
  if((numbytes = sendto(sockfd, file_name, (strlen(file_name)+1)*sizeof(char), 0,
                        result->ai_addr, result->ai_addrlen)) == -1){
    err_sys("sendto: Request failed");
  }
  fprintf(stdout, "Request sent\n");

    
	if (rcvspace) {
    setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF, (char *) &rcvspace, optlen);
	}
	getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char *) &rlth, (socklen_t*)&optlen);

	printf("ctcpcli using port %s rcvspace %d\n", port,rlth);
  
	memset(buff,0,BUFFSIZE);        /* pretouch */

  curr_block = 1;


  // Initialize the blocks
  int k;
  for(k = 0; k < NUM_BLOCKS; k++){
    blocks[k].rows = malloc(BLOCK_SIZE*sizeof(char*));
    blocks[k].content = malloc(BLOCK_SIZE*sizeof(char*));
    initCodedBlock(k);
  }

  Data_Pckt *msg = malloc(sizeof(Data_Pckt));

  do{
    srvlen = sizeof srv_addr; // TODO: this is not necessary -> remove
    // TODO: should be reading only a packet or multiple packets at a time, need to know the packet size in advance...
    if((numbytes = recvfrom(sockfd, buff, MSS, 0, 
                            &srv_addr, &srvlen)) == -1){
      err_sys("recvfrom");
    }
    
    if(numbytes <= 0) break;

	  pkts++;
	  et = secs();  /* last read */
	  if (st == 0) st = et;  /* first pkt time */

    // Unmarshall the packet 
    bool match = unmarshallData(msg, buff);
    if(msg->flag == FIN_CLI){
      break;
    }

    //printf("seqno %d blklen %d num pkts %d start pkt %d curr_block %d dofs %d\n",msg->seqno, msg->blk_len, msg->num_packets, msg->start_packet, curr_block, blocks[curr_block%NUM_BLOCKS].dofs);


    if (debug && msg->blockno != curr_block ) printf("exp %d got %d\n", curr_block, msg->blockno); 
    
    bldack(msg, match);

	  inlth = numbytes;

  }while(numbytes > 0);

  ctrlc();

  return 0;
}


void
err_sys(char *s){
	perror(s);
	ctrlc();     /* do stats on error */
}

void
bldack(Data_Pckt *msg, bool match){
  Ack_Pckt* ack = ackPacket(msg->seqno+1, msg->blockno); // request for next packet
  ack->tstamp = msg->tstamp;

  blocks[curr_block%NUM_BLOCKS].len = msg->blk_len;
  
  uint8_t start = msg->start_packet;

  if(msg->blockno == curr_block){
    while(!isEmpty(msg->packet_coeff, coding_wnd)){
      if(blocks[curr_block%NUM_BLOCKS].rows[start] == NULL){
        // Allocate the memory for the coefficients in the matrix for this block
        blocks[curr_block%NUM_BLOCKS].rows[start] = malloc(coding_wnd);

        // Allocate the memory for the content of the packet
        blocks[curr_block%NUM_BLOCKS].content[start] = malloc(PAYLOAD_SIZE);

        // Set the coefficients to be all zeroes (for padding if necessary)
        memset(blocks[curr_block%NUM_BLOCKS].rows[start], 0, coding_wnd);

        // Normalize the coefficients and the packet contents
        normalize(msg->packet_coeff, msg->payload, coding_wnd);

        // Put the coefficients into the matrix
        memcpy(blocks[curr_block%NUM_BLOCKS].rows[start], msg->packet_coeff, coding_wnd);
        
        // Put the payload into the corresponding place
        memcpy(blocks[curr_block%NUM_BLOCKS].content[start], msg->payload, PAYLOAD_SIZE);
        
        // We got an innovative eqn
        blocks[curr_block%NUM_BLOCKS].dofs++;
        break;
      }else{
        uint8_t pivot = msg->packet_coeff[0];
        int i;
       
        int ix;
        for (ix = 0; ix < coding_wnd; ix++){
          printf(" %d ", msg->packet_coeff[ix]);
        }
        printf("seqno %d start%d isEmpty %d \n Row coeff", msg->seqno, start, isEmpty(msg->packet_coeff, coding_wnd)==1);
        
        for (ix = 0; ix < coding_wnd; ix++){
          printf(" %d ",blocks[curr_block%NUM_BLOCKS].rows[start][ix]);
        }
        printf("\n");

        msg->packet_coeff[0] = 0; // TODO; check again
        // Subtract row with index strat with the row at hand (coffecients)
        for(i = 1; i < coding_wnd; i++){
          msg->packet_coeff[i] ^= FFmult(blocks[curr_block%NUM_BLOCKS].rows[start][i], pivot);
        }
        
        // Subtract row with index strat with the row at hand (content)
        for(i = 0; i < PAYLOAD_SIZE; i++){
          msg->payload[i] ^= FFmult(blocks[curr_block%NUM_BLOCKS].content[start][i], pivot);
        }

        // Shift the row 
        int shift = shift_row(msg->packet_coeff, coding_wnd);
        start += shift;
      }
    }
    
    if(blocks[curr_block%NUM_BLOCKS].dofs == blocks[curr_block%NUM_BLOCKS].len){
      // We have enough dofs to decode
      // Decode!
      
      double dec_time = getTime();
      printf("Starting to decode  ... ");
    
      unwrap(curr_block);

      // Write the decoded packets into the file 
      writeAndFreeBlock(curr_block);

      // Initialize the block for next time
      initCodedBlock(curr_block);

      // Increment the current block number
      curr_block++;

      // Reset the ack number
      ack->ackno = 1;
      last_ackno = 1;

      printf("Done within %f secs\n", getTime()-dec_time);
    }
  
  } else if(msg->blockno < curr_block){
    last_ackno++;
    ack->ackno = last_ackno;
  }

  ack->blockno = curr_block;
  
  // Marshall the ack into buff
  int size = marshallAck(*ack, buff);
  srvlen = sizeof(srv_addr);
  if(sendto(sockfd,buff, size, 0, &srv_addr, srvlen) == -1){
    err_sys("bldack: sendto");
  }
  acks++;
  //printf("Sent an ACK: ackno %d blockno %d\n", ack->ackno, ack->blockno);

}


// TODO: TEST!
void
normalize(uint8_t* coefficients, char*  payload, uint8_t size){
  uint8_t pivot = FFinv(coefficients[0]);
  int i;

  for(i = 0; i < size; i++){
     coefficients[i] = FFmult(pivot,  coefficients[i]);
  }

  for(i = 0; i < PAYLOAD_SIZE; i++){
     payload[i] = FFmult(pivot,  payload[i]);
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
  blocks[blockno%NUM_BLOCKS].len = BLOCK_SIZE; // this may change once we get packets  
  
  int i;
  for(i = 0; i < BLOCK_SIZE; i++){
    blocks[blockno%NUM_BLOCKS].rows[i] = NULL;
    blocks[blockno%NUM_BLOCKS].content[i] = NULL;
  }
}

void
unwrap(uint32_t blockno){
  int row;
  int offset;
  int byte;
  for(row = blocks[blockno%NUM_BLOCKS].len-2; row >= 0; row--){
    for(offset = 1; offset < coding_wnd; offset++){
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
  for(i=0; i < blocks[blockno%NUM_BLOCKS].len; i++){
    // Read the first two bytes containing the length of the useful data 
    memcpy(&len, blocks[blockno%NUM_BLOCKS].content[i], 2);

    // Convert to host order
    len = ntohs(len);

    // Write the contents of the decode block into the file
    fwrite(blocks[blockno%NUM_BLOCKS].content[i]+2, 1, len, rcv_file);

    // Free the content
    free(blocks[blockno%NUM_BLOCKS].content[i]);

    // Free the matrix
    free(blocks[blockno%NUM_BLOCKS].rows[i]);
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

  msg->packet_coeff = malloc(coding_wnd);

  // Padding with zeroes
  memset(msg->packet_coeff, 0, coding_wnd);

  int i;
  for(i = 0; i < msg->num_packets; i++){
    memcpy(&msg->packet_coeff[i], buf+index, (part = sizeof(msg->packet_coeff[i])));
    index += part;
  }


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
  int ack_size = sizeof(double) + sizeof(int) + 2 + 4;

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
