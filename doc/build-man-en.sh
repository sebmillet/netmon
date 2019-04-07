#!/usr/bin/bash

#set -euo pipefail

SRCE0=netmon-sample.ini
SRCEN=netmonen-m0.txt
PREFEN=netmonen-prefix.1
M1EN=netmonen-m1.1
M2EN=netmonen-m2.1
POSTEN=netmonen-postfix.1
TARGETEN=netmon.1

dos2unix -n $SRCE0 $SRCEN
sed -n '/^;-$/,/^;--$/p' $SRCEN | egrep -v "^;-$" | egrep -v "^;--$" | sed 's/^; \?//' | sed 's/^\*\(.*\)\*$/.B \1/' > $M1EN

all_comments=""

sed -n '/^;--$/,$p' $SRCEN | while read l; do

    # Comment line
  c=`echo $l | egrep "^; " | sed 's/^; //'`
    # Variable line
  v=`echo $l | egrep "^\S.*=" | sed 's/=.*$//'`
    # Section line
  s=`echo $l | egrep "^\[.*\]$" | sed 's/^\[//;s/\]$//'`

  if [ -n "$c" ]; then
    if [ -z "$all_comments" ]; then
      all_comments="  $c"
    else
      all_comments="$all_comments
  $c"
    fi
  elif [ -n "$s" ]; then
    if [ "$s" != "$lasts" ]; then
      if [ "$s" = "general" ]; then
        echo ".TP
.B Variables of the [$s] section
.br"
      else
        echo ".TP
.B Variables of the [$s] sections
.br"
      fi
      lasts=$s
    fi
  elif [ -n "$v" ]; then
    if [ -n "$all_comments" ]; then
      echo ".TP
\\fI$v\\fP"
      echo "$all_comments
.br
"
      all_comments=""
    fi
  fi
done > $M2EN

cat $PREFEN $M1EN $M2EN $POSTEN > $TARGETEN

