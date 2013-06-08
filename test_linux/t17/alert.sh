#!/bin/sh

# To be run as alert program by netmon
# Sébastien Millet, May, June 2013

LC=$1
shift

T=$(($LC % 2))
if [ $T -eq 0 ]; then
  R=0
else
  R=1
fi

echo "alert.sh: $@ - WILL RETURN $R" >> tmp-out.log

exit $R

