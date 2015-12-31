#!/bin/sh

# To be run as check program by netmon
# Sébastien Millet, May, June 2013

NAGIOS_OK=0
NAGIOS_WARNING=1
NAGIOS_CRITICAL=2
NAGIOS_UNKNOWN=3

LC=$1

T=$(($LC % 20))
if [ $T -lt 5 ]; then
  R=0
else
  R=1
fi

if [ $R -eq 0 ]; then
  exit $NAGIOS_OK
else
  exit $NAGIOS_CRITICAL
fi

