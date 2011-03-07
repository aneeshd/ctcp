#ifndef UTIL_H_
#define UTIL_H_

#define TRUE 1
#define FALSE 0

#define MSS 1472

typedef int socket_t;
typedef struct timeval timeval_t;

typedef struct{
  /*
   * Assumes that tstamp and msgno are already in network byte older
   */
  double tstamp;
  unsigned int msgno;
  int payload_size;
  // Can add an extra field for sha1 checksum
  char *payload;
} Ctcp_Pckt;

typedef struct {
  double tstamp;
  unsigned int msgno;
	unsigned int blkcnt;
  struct Sblks {
    unsigned int sblk,eblk;
  } sblks[3];
} Pr_Msg;

void vntohl(int *p, double cnt);
void vhtonl(int *p, double cnt);
double getTime(void);
int marshall(Ctcp_Pckt msg, char* buf);
int unmarshall(Ctcp_Pckt* msg, char* buf);
void htonp(Ctcp_Pckt *msg);
void ntohp(Ctcp_Pckt *msg);
#endif // UTIL_H_
