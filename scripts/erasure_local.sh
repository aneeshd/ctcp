#!/bin/bash
looping=1
LOSSRATE=0

while [ $looping -eq 1 ]
do
    echo -n "Select a loss rate (in %). If you would like to exit, enter 0: "
    read -e LOSSRATE

    if [ $LOSSPROB ]
    then
        #echo In the middle...
        iptables -D INPUT -m statistic --mode random --probability $LOSSPROB -j DROP
    #else
        #echo First time around...
    fi

    if [ $LOSSRATE -eq 0 ]
    then
        looping=0
        echo Exiting...
    else
        echo Setting loss rate to $LOSSRATE %...
        LOSSPROB=`echo "$LOSSRATE/100" | bc -l`
        #echo loss probability $LOSSPROB
        iptables -A INPUT -m statistic --mode random --probability $LOSSPROB -j DROP
    fi
done
