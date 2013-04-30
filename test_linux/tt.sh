#!/bin/bash

PRG=../src/netmon

if [ -x "$PRG" ]; then

	VERSION=`$PRG --version 2>&1 | grep -i "^netmon [0-9.]\+"`
	VERSION=`echo $VERSION | sed 's/^netmon *//i;s/ .*$//'`
	ISDEBUG=`echo $VERSION | sed 's/.*\(.\)$/\1/'`
	ISDEBUG=`echo $ISDEBUG | sed 's/^\([^d]\)$/0/'`
	ISDEBUG=`echo $ISDEBUG | sed 's/^\([d]\)$/1/'`

	echo "Executable: $PRG"
	echo "Executable version: $VERSION"

	if [ $ISDEBUG -eq 1 ]; then
		echo "The program has been compiled with debug option (./configure --enable-debug)"
		echo "Tests need a non-debug compiled executable. Aborting."
		echo "** ERRORS ENCOUNTERED **"
		exit 1
	fi

	COUNTPASS=0
	COUNTFAILED=0

	for f in `ls -d t[0-9][0-9]`; do
		cd $f
		./test.sh --batch
		if [ $? -eq 0 ]; then
			COUNTPASS=$(($COUNTPASS + 1))
		else
			COUNTFAILED=$(($COUNTFAILED + 1))
		fi
		cd ..
	done

	echo "------------------------------------------------------------"
	echo "PASSED: $COUNTPASS"
	echo "FAILED: $COUNTFAILED"
	if [ $COUNTFAILED -eq 0 ]; then
		echo "OK"
	else
		echo "** ERRORS ENCOUNTERED **"
	fi

else

	echo "$PRG not available"
	echo "** ERRORS ENCOUNTERED **"
	exit 1

fi

