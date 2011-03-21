#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "util.h"
#include "md5.h"

#define UNIT 10

#define MIN(x,y) (y)^(((x) ^ (y)) &  - ((x) < (y))) 

void
write_lines(int lines){
  FILE *shrimp;
  shrimp = fopen("test_file", "wb+");
  int i = 0;

  for(i = 0; i < lines; i++){
     fprintf(shrimp, "%i ", i);
  }

  fprintf(shrimp, "%c\n", '\0');

  fclose(shrimp);
}

void
read_lines(int size, int times){
  FILE *shrimp;
  char *buf = (char*) malloc(sizeof(size));
  size_t bytes_read;
  shrimp = fopen("test_file", "r");

  // Get file length
  fseek(shrimp, 0, SEEK_END);
  long file_size = ftell(shrimp);
  rewind(shrimp);

  bytes_read = fread(buf, sizeof(char), file_size, shrimp);
  fprintf(stdout, "The file size is: %ld\n", file_size);
  fprintf(stdout, "Read %Zd bytes \n", bytes_read);
  fclose(shrimp);
  free(buf);
}

void
copy_file(char* file_name){
  FILE *src;
  FILE *dst;
  char *copy = "2";
  char* new_name = malloc(sizeof(file_name) + sizeof(copy));

  char *buf_1 = malloc(UNIT+1);
  if(!buf_1){ 
    fprintf(stderr, "Memory error. Exiting\n");
    exit(1);
  }

  sprintf(new_name, "%s%s", file_name, copy);
 
  src = fopen(file_name, "rb");
  
  if(!src){
    fprintf(stderr, "Unable to open file %s\n", file_name);
    exit(1);
  }

  dst = fopen(new_name, "wb");

  if(!src){
    fprintf(stderr, "Unable to open file %s\n", new_name);
    fclose(src);
    exit(1);
  }  

  while(!feof(src)){
    size_t bytes_read = fread(buf_1, 1, UNIT, src);
    
    fwrite(buf_1, 1, MIN(UNIT,bytes_read), dst);
  }

  fclose(src);
  fclose(dst);
  free(new_name);
  free(buf_1);
}

void
marshall_test(){
  Ctcp_Pckt rcv_packet;
  Ctcp_Pckt test_packet;
  bool checksum_match;
  test_packet.tstamp = getTime();
  test_packet.msgno = random();
  test_packet.payload = "this is a test message";
  test_packet.payload_size = strlen(test_packet.payload);

  char* buf = malloc(sizeof(double) + 3*sizeof(int) + test_packet.payload_size + 16);

  marshall(test_packet, buf);

  checksum_match = unmarshall(&rcv_packet, buf);
  
  printf("The value of the match is %d\n", checksum_match);

  if(checksum_match) printf("The checksum is correct");

  assert(checksum_match == TRUE);
  assert(test_packet.tstamp == rcv_packet.tstamp);
  assert(test_packet.msgno == rcv_packet.msgno);
  assert(strcmp(test_packet.payload, rcv_packet.payload) == 0);
  fprintf(stdout, "Passed all marshalling tests!\n");
}

int
main(void){
  srandom(getpid());
  marshall_test();
  return 0;
}

