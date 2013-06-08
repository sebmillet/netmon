#!/bin/sh

LOG="tmp-out.log"
echo "test.sh" > "$LOG"
../generic_simple2.sh "Alert scheduler (13)" "$LOG" "expected-output.txt" netmon.ini $1 -t 2
