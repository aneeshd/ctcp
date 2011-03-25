#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <arpa/inet.h>

#include "util.h"
#include "md5.h"

#define UNIT 10

#define MIN(x,y) (y)^(((x) ^ (y)) &  - ((x) < (y))) 
#define MAX(x,y) (y)^(((x) ^ (y)) &  - ((x) > (y))) 

Block_t block;
Coded_Block_t codedBlock;
int coding_wnd = 10;
FILE *src;
FILE *dst;
int maxblockno;

// --------------------- SERVER SIDE ------------------------------//

void
readBlock(uint32_t blockno){

  // TODO: Make sure that the memory in the block is released before calling this function
  block.len = 0;
  block.snd_nxt = 1;
  block.snd_una = 1;

  block.content = malloc(BLOCK_SIZE*sizeof(char*));

  while(block.len < BLOCK_SIZE && !feof(src)){
    char* tmp = malloc(PAYLOAD_SIZE);
    memset(tmp, 0, PAYLOAD_SIZE); // This is done to pad with 0's 
    uint16_t bytes_read = (uint16_t) fread(tmp + 2, 1, PAYLOAD_SIZE-2, src);
    bytes_read = htons(bytes_read);
    memcpy(tmp, &bytes_read, sizeof(uint16_t));
    
    // Insert this pointer into the blocks datastructure
    block.content[block.len] = tmp;
    block.len++;
  }

  if(feof(src)){
    maxblockno = blockno;
  }
}

/*
 * Frees a block from memory
 */
void
freeBlock(uint32_t blockno){
  int i;
  for(i = 0; i < block.len; i++){
    free(block.content[i]);
  }
    free(block.content);
}


//---------------------- CLIENT SIDE ---------------------------//

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
  codedBlock.dofs = 0;
  codedBlock.len = BLOCK_SIZE; // this may change once we get packets  
  
  int i;
  for(i = 0; i < BLOCK_SIZE; i++){
    codedBlock.rows[i] = NULL;
    codedBlock.content[i] = NULL;
  }
}

void
unwrap(uint32_t blockno){
  int row;
  int offset;
  int byte;
  prettyPrint(codedBlock.rows, coding_wnd);
  for(row = codedBlock.len-2; row >= 0; row--){
    for(offset = 1; offset < coding_wnd; offset++){
      if(codedBlock.rows[row][offset] == 0) 
        continue;
      for(byte = 0; byte < PAYLOAD_SIZE; byte++){
        codedBlock.content[row][byte] 
          ^= FFmult(codedBlock.rows[row][offset], 
                    codedBlock.content[row+offset][byte] );
      }
    }
  }
}


void 
writeAndFreeBlock(uint32_t blockno){
  uint16_t len;
  int i;
  for(i=0; i < codedBlock.len; i++){
    // Read the first two bytes containing the length of the useful data 
    memcpy(&len, codedBlock.content[i], 2);

    // Convert to host order
    len = ntohs(len);
    len = PAYLOAD_SIZE - 2;

    // Write the contents of the decode block into the file
    fwrite(codedBlock.content[i]+2, 1, len, dst);

    // XXX: Will have memory leaks
    // Free the content
    //    free(blocks[blockno%NUM_BLOCKS].content[i]);

    // Free the matrix
    //    free(blocks[blockno%NUM_BLOCKS].rows[i]);
  }
}

void codingTest(char* file_name) {

  
  char *copy = "copy";
  char *new_name = malloc(sizeof(file_name) + sizeof(copy));

  sprintf(new_name, "%s%s", file_name, copy);

  src = fopen(file_name, "rb");

  if(!src){
    fprintf(stderr, "Unable to open file %s\n", file_name);
    exit(1);
  }

  dst = fopen(new_name, "wb");
  
  if(!dst){
    fprintf(stderr, "Unable to open file %s\n", new_name);
    fclose(src);
    exit(1);
  }
  
  printf("reading a new block...\n");
  readBlock(0);

  printf("Initializing destination block...\n");
  codedBlock.rows = malloc(BLOCK_SIZE*sizeof(char*));
  codedBlock.content = malloc(BLOCK_SIZE*sizeof(char*));
  initCodedBlock(0);
  
  uint8_t block_len = block.len;
  uint8_t num_packets = MIN(coding_wnd, block.len);

  Data_Pckt* msg = dataPacket(1, 2, num_packets);
    int rnd = random();
    int i, j;
    uint8_t start;

  while(codedBlock.dofs < block_len){

    printf("Total DOFs %d \n",codedBlock.dofs);
    
    msg->start_packet = MIN(MAX(rnd%block_len - coding_wnd/2, 0), MAX(block_len - coding_wnd, 0));
    
    memset(msg->payload, 0, PAYLOAD_SIZE);

    for(i = 0; i < num_packets; i++){
      msg->packet_coeff[i] = (uint8_t)random()%256;
      for(j = 0; j < PAYLOAD_SIZE; j++){
        msg->payload[j] ^= FFmult(msg->packet_coeff[i], block.content[msg->start_packet+i][j]);
      }
    }
    
    start = msg->start_packet;

    while(!isEmpty(msg->packet_coeff, coding_wnd)){
      if(codedBlock.rows[start] == NULL){
        // Allocate the memory for the coefficients in the matrix for this block
        codedBlock.rows[start] = malloc(coding_wnd);
        
        // Allocate the memory for the content of the packet
        codedBlock.content[start] = malloc(PAYLOAD_SIZE);
        
        // Set the coefficients to be all zeroes (for padding if necessary)
        memset(codedBlock.rows[start], 0, coding_wnd);
        
        // Normalize the coefficients and the packet contents
        normalize(msg->packet_coeff, msg->payload, coding_wnd);
        
        // Put the coefficients into the matrix
        memcpy(codedBlock.rows[start], msg->packet_coeff, coding_wnd);
        
        // Put the payload into the corresponding place
        memcpy(codedBlock.content[start], msg->payload, PAYLOAD_SIZE);
        
        // We got an innovative eqn
        codedBlock.dofs++;
        break;
      }else{
        uint8_t pivot = msg->packet_coeff[0];
        int i;
        
        int ix;
        for (ix = 0; ix < coding_wnd; ix++){
          printf(" %d ", msg->packet_coeff[ix]);
        }
        printf("seqno %d start%d isEmpty %d \n Row coeff", msg->seqno, start, isEmpty(msg->packet_coeff, coding_wnd)==1);
        
        /* for (ix = 0; ix < coding_wnd; ix++){
          printf(" %d ", codedBlock.rows[start][ix]);
        }
        printf("\n");*/
        
        msg->packet_coeff[0] = 0; // TODO; check again
        // Subtract row with index strat with the row at hand (coffecients)
        for(i = 1; i < coding_wnd; i++){
          msg->packet_coeff[i] ^= FFmult(codedBlock.rows[start][i], pivot);
        }
        
        // Subtract row with index strat with the row at hand (content)
        for(i = 0; i < PAYLOAD_SIZE; i++){
          msg->payload[i] ^= FFmult(codedBlock.content[start][i], pivot);
        }
        
        // Shift the row 
        int shift = shift_row(msg->packet_coeff, coding_wnd);
        start += shift;
      }
    }
  }
}  


bool
compareBlocks(void){
  bool result = TRUE;
  int i,j;
  for(i = 0; i < BLOCK_SIZE; i++){
    for(j = 0; j < PAYLOAD_SIZE; j++){
      if(block.content[i][j] != codedBlock.content[i][j]){
        result = FALSE;
      }
    }
  }
  return result;
}

int
main(void){
  srandom(getpid());
  //marshall_test();
  //mult_test(255,1);
  codingTest("Foto.jpg");
  printf("Blocks matched?: %d", compareBlocks());
  return 0;
}

