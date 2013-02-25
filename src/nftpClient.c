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

      printf("Calling read ctcp... \n");

      uint32_t total_bytes = 0;
      while(total_bytes < 500000000)  // limit the received file size to 500MB
        {  
      
          bytes_read = read_ctcp(csk, f_buffer, f_buf_size); 
        
          if (bytes_read == -1){
            printf("read_ctcp is done!\n");
            break;
          }

          fwrite(f_buffer, 1, bytes_read, rcv_file);
          total_bytes += bytes_read;
        }

      printf("%d Total bytes receieved\n", total_bytes);
      ctrlc(csk);

      close_clictcp(csk);

      fclose(rcv_file);
      printf("Closed file successfully\n");

    }

    return 0;
}

