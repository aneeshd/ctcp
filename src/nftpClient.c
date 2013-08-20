/*  
 * Filename: ftpClient.c
 * Description: This program uses CTCP API's to receive a file over CTCP from ftpServer software
 * Created: Sun Oct 14
 * Author: Ali Parandeh
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <math.h>
#include "clictcp.h"

#define PORT "8777"
#define HOST "127.0.0.1"
#define FILE_NAME "NftpTestFile"

void
usage(void) {
    fprintf(stderr, "Usage: nftpClient [-options]             \n\
\t -h   IP address of the host (nftpServer). Default host: 127.0.0.1               \n\
\t -p   port number of the nftpServer. Default port 8777                \n");
    exit(0);
}


int
main(int argc, char** argv){
    char *file_name = FILE_NAME;
    char *lease_file = NULL;
    char *port = PORT;
    char *host = HOST;
    struct child_local_cfg cfg = {.logdir="/var/log/ctcp"};

    timeval_t time,last_time;
    double start_time,end_time;
    int c;
    
    while((c = getopt(argc, argv, "h:p:f:")) != -1) {
        switch (c) {
        case 'h':
          host = optarg;
          break;
        case 'p':
          port = optarg;
          break;
        default:
          usage();
        }
    }

    clictcp_sock* csk = connect_ctcp(host, port, lease_file, &cfg);

    if (csk == NULL){
      printf("Could not create CTCP socket\n");
      return 1;
    } else{
      
      char dst_file_name[100] = "Recvd";
      strcat(dst_file_name, file_name);
      rcv_file = fopen(dst_file_name,  "wb");

      uint32_t f_buf_size = NUM_BLOCKS*BLOCK_SIZE*PAYLOAD_SIZE;
      uint32_t bytes_read;

      char *f_buffer = malloc(f_buf_size);

      printf("Downloading file... \n");

      gettimeofday(&last_time,NULL);
      start_time = last_time.tv_sec+last_time.tv_usec*1.e-6;
        
      uint32_t total_bytes = 0;
      while(total_bytes < 500000000)  // limit the received file size to 500MB
        {  
      
          bytes_read = read_ctcp(csk, f_buffer, f_buf_size); 
        
          if (bytes_read == -1){
            printf("\n\n");
            break;
          }

          fwrite(f_buffer, 1, bytes_read, rcv_file);
          total_bytes += bytes_read;
            
          gettimeofday(&time,NULL);
          if (time.tv_sec > last_time.tv_sec+1) {
              printf(".");
              fflush(stdout);
              last_time = time;
          }
        }
        
      gettimeofday(&time,NULL);
      end_time = time.tv_sec+time.tv_usec*1.e-6;

      printf("Total bytes receieved: %d\n", total_bytes);
      ctrlc(csk);

      printf("Total transfer time: %6.2f \n",end_time-start_time);
      printf("Throughput: %6.2f Mbps\n\n",(double)total_bytes*8/1.e+6/(end_time-start_time));
        
      close_clictcp(csk);

      fclose(rcv_file);
      //printf("Closed file successfully\n");

    }

    return 0;
}

