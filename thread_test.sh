#!/bin/bash

threadNumb=8

echo "" > test_results.txt
for (( i=1; i <= $threadNumb; i++ ))
do
	export CRAWLER_THREADS=$i
	echo "Thread Count: $i" >> test_results.txt
	echo "Thread Count: $i"
	echo "" > $i.txt
	pwd
	for (( x=1; x<= 3; x++ ))
	do
		var1=$(  time (./dependencyDiscoverer -Itest test/*.c test/*.l test/*.y 2>temp 1>&2 ) 2>&1)
		echo $var1 >> $i.txt
	done
	echo "$(cat $i.txt | sort -n)"
	echo "$(cat $i.txt | sort -n)" >> test_results.txt
	rm $i.txt
        rm temp
	echo
done

