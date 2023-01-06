#!/bin/bash

shopt -s dotglob
shopt -s nullglob

cd /home/dungnt/autotox

while true
do
        chmod 777 genbt.sh
        ./genbt.sh
        pkill autotox
        ./autotox &

        sleep 15m
done
