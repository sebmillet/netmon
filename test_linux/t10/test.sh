#!/bin/sh

LOG="tmp-alertlog-log.log"
echo "test.sh" > "$LOG"
../generic_simple2.sh "Alert scheduler (1)" "$LOG" "expected-output.txt" netmon.ini $1 -t 2
