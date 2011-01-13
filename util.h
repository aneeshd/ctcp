#ifndef UTIL_H_
#define UTIL_H_

typedef int socket_t;

void done(void);
void usage(void);
void rdconfig(void);
int doit(char* host);
void err_sys(char* s);
void send_segs(socket_t fd);
void vntohl(int *p, int cnt);
socket_t timedread(socket_t fd, double t);
void handle_ack(socket_t fd);
void handle_sack(socket_t fd);
void floyd_aimd(int cevent);
void send_one(socket_t fd, unsigned int n);


#endif // 
