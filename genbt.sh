#!/bin/bash
shopt -s dotglob
shopt -s nullglob

rs=$(curl https://nodes.tox.chat/ | grep 'ONLINE' | wc -l)

if [ $rs -gt 0 ] ; then   

   max=3

   if [ $rs -eq 1 ] ; then
	max=1
   elif [ $rs -eq 2 ] ; then
	max=2
   fi

   echo "max=""$max"

   l=$(curl https://nodes.tox.chat/ | grep -B 8 'ONLINE' | grep -o '<td>.*</td>' | sed 's/\(<td>\|<\/td>\)//g')
   echo $l
     
   if [ -f ./bt.tox ] ; then
	truncate --size=0 ./bt.tox
   else
	touch ./bt.tox
   fi

   num=0  
   count=-1
   ok=0
   for word in $l
   do
    count=$(( $count + 1 ))
    t=$(( $count % 5 ))
    if [ $t -eq 0 ] ; then
	if [ "$word" != "-" ] ; then
		echo "$word" >> ./bt.tox
		ok=1
	else
		ok=0
	fi
    elif [ $t -eq 1 ] ; then
	if [ $ok -eq 0 ] && [ "$word" != "-" ]  ; then
		echo "$word" >> ./bt.tox
	fi
    else
        if [ $t -eq 4 ] ; then
		num=$(( $num + 1 ))
		if [ $num -eq 3 ] ; then
			break
		fi
	else
		echo "$word" >> ./bt.tox
	fi
    fi
   done

   echo "count:""$count"

fi
