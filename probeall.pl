#!/usr/bin/perl 
#                     thd@ornl.gov  ffowler@cs.utk.edu
# write out bw and avrg bw for probecli/atoucli db.tmp on stdin
# and all ACK stuff: rtt thresh cwnd and pkt-pair bw
#  0.275081 10  0.108058  4 4 ack
#  time    pkt   rtt    cwnd thresh
open(ACK,">ack.tmp");
open(SEQ,">seq.tmp");
open(BW,">bw.tmp");
open(ABW,">abw.tmp");
open(PP,">ppbw.tmp");
open(T,">thresh.tmp");
open(C,">cwnd.tmp");
open(R,">rtt.tmp");
$mss = 1472;
$when=$res = 0.1;
#$when=$res = 0.05;
while(<>){
	chop;
	if (/xmt/){
		($t, $seg) = split;
		$abw += $mss;
		$bw += $mss;
		if ($t > $when) {
			$b = 8.e-6*$bw/$res;
			print BW "$t $b\n";
			$a = 8.e-6*$abw/$when;
			print ABW "$t $a\n";
			$when += $res;
			$bw=0;
		}
		print SEQ "$t $seg\n";
	}
	if (/ ack/){
		($t, $seg, $rtt, $cwnd, $thresh) = split;
		print R "$t $rtt\n";
		print C "$t $cwnd\n";
		print T "$t $thresh\n";
		print ACK "$t $seg\n";
		$ppbw = int(8.e-6*$mss/($t - $last));
		print PP "$t $ppbw\n" if $last;
		$last = $t;
	}
}
