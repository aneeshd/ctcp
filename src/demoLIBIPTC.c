
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/errno.h>

#include <getopt.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

//#include <libiptc/libiptc.h>
//#include "iptables.h"

typedef struct{
  char* interface;
  char* address;
  char* gateway;
  char* netmask;
} dhcp_lease;


void 
make_new_table(dhcp_lease* lease, int table_number, int mark_number){

  char* command = malloc(20);

  sprintf(command, "ip route flush table %d", table_number);
  printf("%s\n", command);
  system(command);
  
  // clear out the string
  memset(command, '\0', sizeof(command));
  //springf(command, "ip route add table %d ", table_number);
  
}


int
main(void){

  dhcp_lease* lease = malloc(sizeof(dhcp_lease));
  lease->interface = "wlan0";
  lease->address = "18.62.30.236";
  lease->gateway = "18.62.0.1";
  lease->netmask = "255.255.0.0";
  
  system("ip route show table local");

  make_new_table(lease, 2, 2);
  /*
  struct iptc_handle* h = malloc(sizeof(struct iptc_handle));
  const char *chain = NULL;
  const char *tablename = NULL;
  
  h = iptc_init(tablename);
  if (!h)
    printf("blah!");
  return 0;
  */
  return 0;

}



/* demoLIBIPTC.c ends here */
