/*  
 * Filename: ftpServer.c
 * Description: This program uses CTCP API's to send a local file over CTCP to ftpClient software
 * Created: Sun Oct 14 15:06:21 2012 (-0400)
 * Author: Ali Parandeh
 * 
 */

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

#define PORT "8777"


int
main (int argc, char** argv){

  char *file_name = "Honda";
  FILE *snd_file; // The file to be sent
  char *configfile = "config/vegas";
  char *port = PORT;  // This is the port that the server is listening to
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
  } 

  // Wait for the SYN packet to come
  if (listen_srvctcp(sk) == -1){
    printf("Could not establish the connection \n");
    return 1;
  }

  // read from the file and send over ctcp socket

  size_t buf_size = 1000000;
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
     
  close_srvctcp(sk);

  fclose(snd_file);

  return 0;
}

