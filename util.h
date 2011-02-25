#ifndef UTIL_H_
#define UTIL_H_

#define TRUE 1
#define FALSE 0

typedef int socket_t;
typedef struct timeval timeval_t;

typedef struct {
  double tstamp;
  unsigned int msgno;
	unsigned int blkcnt;
  struct Sblks {
    unsigned int sblk,eblk;
  } sblks[3];
} Pr_Msg;

void vntohl(int *p, int cnt);
void vhtonl(int *p, int cnt);
double getTime(void);
#endif // UTIL_H_
