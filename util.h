#ifndef UTIL_H_
#define UTIL_H_

#define TRUE 1
#define FALSE 0

// flags for Ctcp_Pckt
#define NORMAL 0
#define FIN_CLI 1

#define MSS 1472
#define HDR_SIZE (sizeof(double) +3*sizeof(int))

typedef int socket_t;
typedef struct timeval timeval_t;

typedef struct{
  /*
   * Assumes that tstamp and msgno are already in network byte older
   */
  double tstamp;
  unsigned int flag;
  /*
   * 0 = normal
   * 1 = terminate_client
   */
  unsigned int msgno;
  int payload_size;
  // Can add an extra field for sha1 checksum
  char *payload;
} Ctcp_Pckt;

void vntohl(int *p, double cnt);
void vhtonl(int *p, double cnt);
double getTime(void);
Ctcp_Pckt* Packet(unsigned int msgno, int payload_size);
int marshall(Ctcp_Pckt msg, char* buf);
int unmarshall(Ctcp_Pckt* msg, char* buf);
void htonp(Ctcp_Pckt *msg);
void ntohp(Ctcp_Pckt *msg);
#endif // UTIL_H_
