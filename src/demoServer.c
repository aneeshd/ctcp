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


#define PORT0 "8887"
#define PORT1 "8888"
#define PORT2 "8889"
#define PORT3 "8890"

#define BUFSIZE 1400
#define TCP_BUFFSIZE 50000



char*
get_ip_str(const struct sockaddr *sa, char *s, size_t maxlen)
{
  switch(sa->sa_family) {
  case AF_INET:
    inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
              s, maxlen);
    break;

  case AF_INET6:
    inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
              s, maxlen);
    break;

  default:
    strncpy(s, "Unknown AF", maxlen);
    return NULL;
  }

  return s;
}

int
main (int argc, char** argv){
    struct addrinfo hints, *servinfo;
    struct addrinfo *result; //This is where the info about the server is stored
    struct sockaddr cliAddr;
    socklen_t cliAddrLen = sizeof cliAddr;
    int       sockfd[4];
    int       rv;
    char      ip[INET6_ADDRSTRLEN] = {0};
    char      buff[BUFSIZE];
    int       numbytes;
    char* port[4];
    port[0] = PORT0;
    port[1] = PORT1;
    port[2] = PORT2;
    port[3] = PORT3;

    printf("Starting Demo Server\n");

    int k;
    for (k = 0; k < 4; k++){

      // Setup the hints struct
      memset(&hints, 0, sizeof hints);
      hints.ai_family   = AF_UNSPEC;
      if (k == 3){
	hints.ai_socktype = SOCK_STREAM;
      }else{
	hints.ai_socktype = SOCK_DGRAM;
      }
      hints.ai_flags    = AI_PASSIVE;

      // Get the server's info
      if((rv = getaddrinfo(NULL, port[k], &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
      }

      // Loop through all the results and connect to the first possible
      for(result = servinfo; result != NULL; result = result->ai_next) {
        if((sockfd[k] = socket(result->ai_family,
                            result->ai_socktype,
                            result->ai_protocol)) == -1){
	  perror("demoServer: error during socket initialization");
	  continue;
        }
        if (bind(sockfd[k], result->ai_addr, result->ai_addrlen) == -1) {
	  close(sockfd[k]);
	  perror("demoServer: can't bind local address");
	  continue;
        }
        break;
      }

      if (result == NULL) { // If we are here, we failed to initialize the socket
	perror("atousrv: failed to initialize socket");
	return 2;
      }
      freeaddrinfo(servinfo);
    }


    printf("***Demo Server Ready***\nWaiting for requests...\n");

    pid_t pid;
    pid = fork();

    if (pid ==0){
      // child process
      // echo server 

      while(1)    {
	// First call is blocking
	if((numbytes = recvfrom(sockfd[0], buff, BUFSIZE, 0,
				&cliAddr, &cliAddrLen)) == -1)
	  {
	    perror("DemoServer: recvfrom failed\n");
	  }

	/*
	char* tmp = malloc(numbytes);

	snprintf(tmp, numbytes+1, "%s", buff);

	printf("Got \t %s \t from %s port %d\n", tmp,
	       get_ip_str(&cliAddr, ip, INET6_ADDRSTRLEN), ((struct sockaddr_in*)&cliAddr)->sin_port  );

	printf("Echoing...");

	int len = strlen(tmp) + 7;
	char* echoString = malloc(len);

	snprintf(echoString, len, "Echo: %s", tmp);

	//        snprintf(echoString, len, "Echo: %s from %s", buff, get_ip_str(&cliAddr, ip, INET6_ADDRSTRLEN));
	//        printf("The echoString is %s\n", echoString);
	*/

	if((numbytes = sendto(sockfd[0], buff, BUFSIZE, 0,
			      &cliAddr, cliAddrLen)) == -1)
	  {
	    perror("DemoServer: sento failed\n");
	  }

	printf("Echo sent\n");
	//free(echoString);
	//free(tmp);
      }

      return 0;
      //===========================================================================
    }else if (pid == -1){
      perror("echo-server fork");
      return -1;
    }

    // only parent process will reach here

    pid = fork();
    if (pid ==0){

      // child process
      // flush server 
      while (1){

	// wait for flush request which is 0xffff
	if((numbytes = recvfrom(sockfd[1], buff, BUFSIZE, 0,
				&cliAddr, &cliAddrLen)) == -1)
	  {
	    perror("DemoServer: recvfrom failed\n");
	  }

	if (buff[0] == (char) 255 && buff[1] == (char) 255){

	  struct timespec t_sleep;
	  t_sleep.tv_sec = 0;
	  t_sleep.tv_nsec = 1000000;
	  int flush_tries = 1000;
	  int i;
	  int seqno = 1;
	  for (i = 0; i < flush_tries; i++){

	    //	printf("Sending %d \t", i);

	    memcpy(buff, &i, sizeof(int));

	    if((numbytes = sendto(sockfd[1], buff, BUFSIZE, 0,
				  &cliAddr, cliAddrLen)) == -1)
	      {
		perror("DemoServer: sento failed\n");
	      }

	    nanosleep(&t_sleep, NULL);

	  } // end for loop
	  printf("Done with the flush part.\n");
	}

      } // end infinite while loop

      return 0;
      //===========================================================================
    }else if (pid == -1){
      perror("flush-server fork");
      return -1;
    }

    // only parent process will reach here
     // probe server 

    pid = fork();
    if (pid ==0){

      if (listen(sockfd[3], 10) < 0){
	perror("TCP listen failed");
	return -1;
      }


      int tcp_sk;

      while (1){
      // wait for TCP connections

	tcp_sk = accept(sockfd[3], &cliAddr, &cliAddrLen);

	if (tcp_sk == -1){
	  perror("accept");
	  break;
	}

	if (!fork()){
	  // this is the grandchild process
	  close(sockfd[3]);
	  int total_bytes_sent = 0;
	  char tcp_buff[TCP_BUFFSIZE];
	  memset(&tcp_buff, 0, TCP_BUFFSIZE);

	  while (total_bytes_sent < 2000000){
	    if( (numbytes = send(tcp_sk, tcp_buff, TCP_BUFFSIZE, 0)) == -1) {
		perror("DemoServer: recvfrom failed\n");
		break;
	      }	    
	      total_bytes_sent += numbytes;
	  }

	  close(tcp_sk);
	  return 0;
	}  // end fork

      } // end infinite while loop

      return 0;
      //===========================================================================
    }else if (pid == -1){
      perror("flush-server fork");
      return -1;
    }



    // only parent process will reach here
     // probe server 


    while (1) {

      // wait for the probe rate req packets to come

      if((numbytes = recvfrom(sockfd[2], buff, BUFSIZE, 0,
			      &cliAddr, &cliAddrLen)) == -1)
	{
	  perror("DemoServer: recvfrom failed\n");
	}
	
      uint8_t probe_rate = buff[0];

      if (buff[1] == (char) 255 && probe_rate > 0){

	printf("requested probe rate: %d kbps\n", (int)probe_rate*10);

	struct timespec t_sleep;
	t_sleep.tv_sec = 0;
	t_sleep.tv_nsec = (long) 1000000/(probe_rate*10)*8*1400;


	int probe_tries = (int) 5000/250*probe_rate;
	if (probe_tries < 200){
	  probe_tries = 200;
	}
	int i;
	for (i = 0; i < probe_tries; i++){

	  //	printf("Sending %d \t", i);

	  memcpy(buff, &i, sizeof(int));

	  if((numbytes = sendto(sockfd[2], buff, BUFSIZE, 0,
				&cliAddr, cliAddrLen)) == -1)
	    {
	      perror("DemoServer: sento failed\n");
	    }

	  nanosleep(&t_sleep, NULL);

	} // end for loop

	printf("Done with the slow part.\n");
      } else{
	printf("Did not receive the correct message - buf[1] = %u \n", buff[1]);
      }


    } // end infinite while loop



    return 0;
      //===========================================================================



}
