#!/bin/sh

SRCEN=netmon-sample.ini
PREFEN=netmonen-prefix.1
M1EN=netmonen-m1.1
M2EN=netmonen-m2.1
POSTEN=netmonen-postfix.1
TARGETEN=netmon.1

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
      all_comments="$all_comments\n  $c"
    fi
  elif [ -n "$s" ]; then
    if [ "$s" != "$lasts" ]; then
      if [ "$s" = "general" ]; then
        echo ".TP\n.B Variables of the [$s] section\n.br"
      else
        echo ".TP\n.B Variables of the [$s] sections\n.br"
      fi
      lasts=$s
    fi
  elif [ -n "$v" ]; then
    if [ -n "$all_comments" ]; then
      echo ".TP\n\\\\fI$v\\\\fP"
      echo "$all_comments\n.br\n"
      all_comments=""
    fi
  fi
done > $M2EN

cat $PREFEN $M1EN $M2EN $POSTEN > $TARGETEN

