#!/bin/sh

# To be run as an alert program by netmon
# Sébastien Millet, May, June 2013

echo "\$0='$0'"

I=1
while [ -n "$1" ]; do
  echo "\$$I='$1'"
  I=$(($I+1))
  shift
done

