#!/bin/bash

shopt -s dotglob
shopt -s nullglob

while true
do
	chmod 777 genbt.sh
	./genbt.sh
	
	while true
	do
		pkill autotox
		rm ol_ok.tox
		./autotox &
		sleep 7m
		if [ -f ol_ok.tox ] ; then 
			break
		fi
	done

	sleep 20m
done

