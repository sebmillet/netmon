#!/bin/sh

../generic_simple.sh "Email loop test (takes 20s)" "tmp-output.txt" "expected-output.txt" netmon.ini $1 -t 3
