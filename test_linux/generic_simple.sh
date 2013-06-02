#!/bin/sh

PRG=../../src/netmon

TNAME=$1
OUTPUT=$2
REFERENCE=$3
INI=$4

IS_BATCH=0
if [ "$5" = "--batch" ]; then
  IS_BATCH=1
  shift
fi

$PRG -pvvvt 1 -l "" -c $INI $5 $6 > $OUTPUT

REP=$(pwd | sed 's/.*\///')

if [ $IS_BATCH -eq 1 ]; then
	cmp $REFERENCE $OUTPUT 2>&1 > /dev/null
	if [ "$?" -ne "0" ]; then
		echo "$REP ** $TNAME: KO"
		exit 1;
	else
		echo "$REP    $TNAME: OK"
		exit 0;
	fi
fi

cat $OUTPUT
md5sum $REFERENCE
md5sum $OUTPUT

