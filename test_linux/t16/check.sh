#!/bin/sh

# To be run as check program by netmon
# Sébastien Millet, May, June 2013

NAGIOS_OK=0
NAGIOS_WARNING=1
NAGIOS_CRITICAL=2
NAGIOS_UNKNOWN=3

if [ $1 -le 150 ]; then
  exit $NAGIOS_CRITICAL
else
  exit $NAGIOS_OK
fi

