#!/bin/sh

# To be run as check program by netmon
# Sébastien Millet, May, June 2013

NAGIOS_OK=0
NAGIOS_WARNING=1
NAGIOS_CRITICAL=2
NAGIOS_UNKNOWN=3

exit $NAGIOS_CRITICAL

