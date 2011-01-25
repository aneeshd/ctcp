/* scoreboard.h    thd@ornl.gov  ffowler@cs.utk.edu */

//#define SBSIZE 8192
#define SBSIZE 16384

int length_=0, first_=0, HighAck=0, Pipe=0, loss3s=0, NextLoss=0;
int retran_seqno=0, cumacks=0, goodacks=0, dup_acks=0;
int dup_total=0;int SACKed=0, ACKed=0;
int FastRecovery=0;

double wintrim = 0.0, winmult = 0.5;
int retran_data = 0, awnd = 0;

int IsEmpty () {return (length_ == 0);}
void ClearScoreBoard (); 
int GetNextRetran ();
void MarkRetran (int, int);
void UnMarkRetran (int);
int UpdateScoreBoard (int);
int CheckSndNxt ();
	
struct ScoreBoardNode {
	int seq_no_;		/* Packet number */
	int ack_flag_;		/* Acked by cumulative ACK */
	int sack_flag_;		/* Acked by SACK block */
	int retran_;		/* Packet retransmitted */
	int snd_nxt_;		/* snd_nxt at time of retransmission */
	int sack_cnt_;		/* number of reports for this hole */
	} SBN[SBSIZE+1];

#define SBNI SBN[i%SBSIZE]
