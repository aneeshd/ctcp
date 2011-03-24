#ifndef ATOUSRV_H_
#define ATOUSRV_H_

#define MAXHO 1024
#define BUFFSIZE  65536

FILE *rcv_file;

//---------------- CTCP related variables ------------------//
#define NUM_BLOCKS 2
Coded_Block_t blocks[NUM_BLOCKS];
uint8_t coding_wnd;
uint32_t curr_block;

//char *version = "$Revision: 1.8 $";
double dbuff[BUFFSIZE/8];
char *buff = (char *)dbuff;
int sockfd, rcvspace;
int inlth,sackcnt,pkts, dups, drops,hi,maxooo;
int debug = 0,expect=1, expected, acks, acktimeouts=0, sendack=0;
/* holes has ascending order of missing pkts, shift left when fixed */
#define MAXHO 1024
int  hocnt, holes[MAXHO];
/*implementing sack & delack */
int sack=0;
socklen_t srvlen;
int ackdelay=0 /* usual is 200 ms */, ackheadr, sackinfo;
int  settime=0;
int start[3], endd[3];

/* stats */
double et,minrtt=999999., maxrtt=0, avrgrtt;
double due,rcvt,st,et;

unsigned int rtt_base=0; // Used to keep track of the rtt
unsigned int tempno;

unsigned int millisecs();


/*
 * Handler for when the user sends the signal SIGINT by pressing Ctrl-C
 */
void  ctrlc(void);
void err_sys(char *s);
void bldack(Data_Pckt *msg, bool match);

void normalize(uint8_t* coefficients, char*  payload, uint8_t size);
int shift_row(uint8_t* buf, int len);
bool isEmpty(uint8_t* coefficients, uint8_t size);
void initCodedBlock(uint32_t blockno);
void unwrap(uint32_t blockno);
void writeAndFreeBlock(uint32_t blockno);

bool unmarshallData(Data_Pckt* msg, char* buf);
int marshallAck(Ack_Pckt msg, char* buf);

double secs();
unsigned int millisecs();

#endif // ATOUSRV_H_
