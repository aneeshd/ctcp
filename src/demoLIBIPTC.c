
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// !!! NEED TO RUN THIS PROGRAM AS SUDO!!

typedef struct{
  char* interface;
  char* address;
  char* gateway;
  char* netmask;
} dhcp_lease;


void 
make_new_table(dhcp_lease* lease, int table_number, int mark_number){

  printf("/****** making table %d, mark %d *****/\n\n", table_number, mark_number);

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
  
  memset(command, '\0', sizeof(command));
  sprintf(command, "ip route show table %d", table_number);
  printf("%s\n", command);
  system(command);
  printf("\n");

  // Create and add rules//
  memset(command, '\0', sizeof(command));
  sprintf(command, "iptables -t mangle -A OUTPUT -s %s -j MARK --set-mark %d", lease->address, mark_number);
  system(command);

  memset(command, '\0', sizeof(command));
  sprintf(command, "ip rule add fwmark %d table %d", mark_number, table_number);
  system(command);
  
  system("iptables -t mangle -S");
  printf("\n");

  printf("ip rule show\n");
  system("ip rule show");
  printf("\n");
  
  printf("ip route flush cache\n");
  system("ip route flush cache");
  printf("\n");
  
  return;
}

void
delete_table(int table_number, int mark_number){
  printf("/****** deleting table %d, mark %d *****/\n\n", table_number, mark_number);
  char* command = malloc(150);

  // Flush routing table (table_number) //
  sprintf(command, "ip route flush table %d", table_number);
  system(command);

  memset(command, '\0', sizeof(command));
  sprintf(command, "ip rule delete fwmark %d table %d", mark_number, table_number);
  system(command);

  memset(command, '\0', sizeof(command));
  sprintf(command, "iptables -t mangle -F");
  system(command);

  // Printing...
  memset(command, '\0', sizeof(command));
  sprintf(command, "ip route show table %d", table_number);
  printf("%s\n", command);
  system(command);
  printf("\n");

  printf("ip rule show\n");
  system("ip rule show");
  printf("\n");
  
  printf("ip route flush cache\n");
  system("ip route flush cache");
  printf("\n");
}

int
main(void){

  dhcp_lease* lease = malloc(sizeof(dhcp_lease));
  lease->interface = "eth1";
  lease->address = "18.62.30.159";
  lease->gateway = "18.62.0.1";
  lease->netmask = "255.255.0.0";
  
  printf("ip route show table local\n");
  system("ip route show table local");
  printf("\n");

  make_new_table(lease, 3, 3);
  delete_table(3, 3);

  return 0;
}

/* demoLIBIPTC.c ends here */
