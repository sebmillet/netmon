#!/bin/sh

# To be run as an alert program by netmon
# Sébastien Millet, May 2013

for i in 1 2 3 4 5; do
  echo "\$$i = $1"
  shift
done

